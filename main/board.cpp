// A Waveshare ESP32-S3-Touch-AMOLED-2.16, as ../puce sets it up: Waveshare's BSP for the I2C bus, the CST9220
// touch and the ES8311, LVGL on its own task (esp_lvgl_adapter), and the CO5300 AMOLED over QSPI set up here (the
// BSP's panel copies every strip LVGL draws in PSRAM into internal RAM to send it, which runs out; ESP-IDF 6 can
// send from PSRAM directly). Pins as the BSP's (BSP_LCD_*). The keys aren't the BSP's: see buttons_start().
#include <algorithm>
#include <iterator>
#include <mutex>

#include "board.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_codec_dev.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr uint16_t kLines = 50; // LVGL draws in strips of this many lines, two in PSRAM (as the BSP)

esp_lcd_panel_handle_t panel = nullptr;

// Waveshare's sequence for this panel (the BSP's): sleep out, its vendor page, SPI mode, RGB565, the tearing
// line, brightness control at full, the 480 x 480 window, the picture turned (MADCTL 0xA0), display on.
const uint8_t kNone[] = {0x00}, kPage20[] = {0x20}, kReg19[] = {0x10}, kReg1C[] = {0xA0}, kPage0[] = {0x00},
              kSpiMode[] = {0x80}, kRgb565[] = {0x55}, kTearing[] = {0x00}, kBrightnessOn[] = {0x20},
              kFull[] = {0xFF}, kWindow[] = {0x00, 0x00, 0x01, 0xDF}, kTurned[] = {0xA0};
const co5300_lcd_init_cmd_t kInit[] = {
    {0x11, kNone, 0, 600},   {0xFE, kPage20, 1, 0},  {0x19, kReg19, 1, 0},        {0x1C, kReg1C, 1, 0},
    {0xFE, kPage0, 1, 0},    {0xC4, kSpiMode, 1, 0}, {0x3A, kRgb565, 1, 0},       {0x35, kTearing, 1, 0},
    {0x53, kBrightnessOn, 1, 0}, {0x51, kFull, 1, 0}, {0x63, kFull, 1, 0},        {0x2A, kWindow, 4, 0},
    {0x2B, kWindow, 4, 0},   {0x36, kTurned, 1, 0},  {0x29, kNone, 0, 600},
};

esp_lcd_panel_io_handle_t start_panel() {
    spi_bus_config_t spi = {};
    spi.sclk_io_num = BSP_LCD_PCLK;
    spi.data0_io_num = BSP_LCD_DATA0;
    spi.data1_io_num = BSP_LCD_DATA1;
    spi.data2_io_num = BSP_LCD_DATA2;
    spi.data3_io_num = BSP_LCD_DATA3;
    spi.max_transfer_sz = BSP_LCD_H_RES * kLines * 2;
    spi.dma_burst_size = 64; // the most PSRAM takes (the driver's 32 let the panel's SPI outrun PSRAM)
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &spi, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_config = {}; // CO5300_PANEL_IO_QSPI_CONFIG's, which is C only
    io_config.cs_gpio_num = BSP_LCD_CS;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 3; // as the BSP
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    io_config.flags.psram_dma_direct = true;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(BSP_LCD_SPI_NUM), &io_config, &io));
    static co5300_vendor_config_t vendor = {};
    vendor.init_cmds = kInit;
    vendor.init_cmds_size = std::size(kInit);
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = BSP_LCD_RST;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    config.bits_per_pixel = 16;
    config.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io, &config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, false)); // its memory is noise until the first screen
    return io;
}

// The CO5300 takes only areas that start on an even pixel and are an even number wide and high.
void even_area(lv_event_t *e) {
    auto *area = static_cast<lv_area_t *>(lv_event_get_param(e));
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

// The speaker: sounds mixed 10 ms at a time while any plays, as the web page's overlapping Audio()s. Between
// them the I2S DMA sends silence (the BSP's auto_clear).
constexpr int kRate = 44100;
constexpr size_t kFrame = kRate / 100;

struct Voice {
    const uint8_t *pcm = nullptr; // embedded files aren't aligned: samples are read a byte at a time
    size_t left = 0;              // samples
};
Voice voices[4];
std::mutex voices_mu;
esp_codec_dev_handle_t speaker = nullptr;
TaskHandle_t speaker_handle = nullptr;

void speaker_task(void *) {
    static int16_t stereo[2 * kFrame];
    while (true) {
        size_t longest = 0;
        {
            std::lock_guard<std::mutex> lock(voices_mu);
            for (size_t i = 0; i < kFrame; i++) {
                int sum = 0;
                for (const Voice &v : voices)
                    if (i < v.left) sum += int16_t(v.pcm[2 * i] | v.pcm[2 * i + 1] << 8);
                stereo[2 * i] = stereo[2 * i + 1] = int16_t(std::clamp(sum, -32768, 32767));
            }
            for (Voice &v : voices) {
                size_t n = std::min(v.left, kFrame);
                longest = std::max(longest, n);
                v.pcm += 2 * n;
                v.left -= n;
            }
        }
        if (!longest) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // play() wakes it
            continue;
        }
        if (esp_codec_dev_write(speaker, stereo, sizeof stereo) != ESP_CODEC_DEV_OK)
            vTaskDelay(pdMS_TO_TICKS(10)); // a failed codec must not spin this task
    }
}

// The keys, as marked on the board: "+" (IO18) and "–" (BOOT, GPIO0) pull their pins low; PWR is the AXP2101's,
// its short press a bit in the chip's IRQ status. Registers as in XPowersLib (and ../puce): 0x41 IRQ enable 2,
// 0x49 IRQ status 2 (write 1 to clear), bit 3 a short press, bits 6 and 7 USB removed and plugged in.
constexpr gpio_num_t kUpPin = GPIO_NUM_18, kDownPin = GPIO_NUM_0;
constexpr uint8_t kAxp2101 = 0x34, kIrqEnable2 = 0x41, kIrqStatus2 = 0x49;
constexpr uint8_t kShortPress = 1 << 3, kUsbRemoved = 1 << 6, kUsbInserted = 1 << 7;
i2c_master_dev_handle_t pmu = nullptr;

bool pmu_read(uint8_t reg, uint8_t &value) { return pmu && i2c_master_transmit_receive(pmu, &reg, 1, &value, 1, 50) == ESP_OK; }

void pmu_write(uint8_t reg, uint8_t value) {
    const uint8_t out[] = {reg, value};
    if (pmu) i2c_master_transmit(pmu, out, sizeof out, 50);
}

} // namespace

namespace board {

void display_start() {
    esp_lcd_panel_io_handle_t io = start_panel();

    // ESP_LV_ADAPTER_DEFAULT_CONFIG() leaves a member out, which C++ doesn't allow; its values, but:
    esp_lv_adapter_config_t lvgl = {};
    lvgl.task_stack_size = 16 * 1024;
    lvgl.task_core_id = 1;
    lvgl.task_priority = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY;
    lvgl.tick_period_ms = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS;
    lvgl.task_min_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS;
    lvgl.task_max_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&lvgl));

    esp_lv_adapter_display_config_t display = {};
    display.panel = panel;
    display.panel_io = io;
    display.profile.interface = ESP_LV_ADAPTER_PANEL_IF_OTHER;
    display.profile.rotation = ESP_LV_ADAPTER_ROTATE_0;
    display.profile.hor_res = BSP_LCD_H_RES;
    display.profile.ver_res = BSP_LCD_V_RES;
    display.profile.buffer_height = kLines;
    display.profile.use_psram = true;
    display.profile.require_double_buffer = true;
    display.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
    lv_display_t *lv_display = esp_lv_adapter_register_display(&display);
    lv_display_add_event_cb(lv_display, even_area, LV_EVENT_INVALIDATE_AREA, nullptr);

    bsp_display_cfg_t touch_turn = {};
    touch_turn.touch_flags = {.swap_xy = 1, .mirror_x = 0, .mirror_y = 1}; // as bsp_display_start(), for the panel's turn
    esp_lcd_touch_handle_t touch = nullptr;
    ESP_ERROR_CHECK(bsp_touch_new(&touch_turn, &touch));
    esp_lv_adapter_touch_config_t pointer = {};
    pointer.disp = lv_display;
    pointer.handle = touch;
    pointer.scale.x = pointer.scale.y = 1.0f;
    pointer.multi_touch.mode = ESP_LV_ADAPTER_TOUCH_MODE_SINGLE;
    pointer.multi_touch.pointers = 1;
    esp_lv_adapter_register_touch(&pointer);

    ESP_ERROR_CHECK(esp_lv_adapter_start());
}

void display_lock() { esp_lv_adapter_lock(-1); } // a negative timeout waits; 0 would not
void display_unlock() { esp_lv_adapter_unlock(); }
void display(bool on) { esp_lcd_panel_disp_on_off(panel, on); } // an AMOLED: its pixels go dark

void audio_start() {
    speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate = kRate;
    fs.channel = 2; // as Waveshare's firmware: the same sample on both
    fs.bits_per_sample = 16;
    if (!speaker || esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE("audio", "no speaker");
        speaker = nullptr;
        return;
    }
    xTaskCreatePinnedToCore(speaker_task, "speaker", 4096, nullptr, 7, &speaker_handle, 0);
}

void play(const uint8_t *pcm, size_t bytes) {
    if (!speaker_handle) return;
    {
        std::lock_guard<std::mutex> lock(voices_mu);
        // A free voice, or else the one nearest its end.
        Voice &v = *std::min_element(std::begin(voices), std::end(voices),
                                     [](const Voice &a, const Voice &b) { return a.left < b.left; });
        v = {pcm, bytes / 2};
    }
    xTaskNotifyGive(speaker_handle);
}

// The codec's default curve: 0..100 for -50..0 dB.
void volume(int level) {
    if (speaker) esp_codec_dev_set_out_vol(speaker, level > 0 ? 100 - 8 * (kMaxVolume - level) : 0);
}

void buttons_start() {
    gpio_config_t keys = {};
    keys.pin_bit_mask = 1ULL << kUpPin | 1ULL << kDownPin;
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE; // the board pulls them up too
    ESP_ERROR_CHECK(gpio_config(&keys));
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = kAxp2101;
    device.scl_speed_hz = 400000;
    if (bsp_i2c_init() != ESP_OK || i2c_master_bus_add_device(bsp_i2c_get_handle(), &device, &pmu) != ESP_OK) {
        ESP_LOGE("buttons", "no AXP2101: no PWR key");
        pmu = nullptr;
        return;
    }
    uint8_t irqs = 0;
    if (pmu_read(kIrqEnable2, irqs)) pmu_write(kIrqEnable2, irqs | kShortPress);
    pmu_write(kIrqStatus2, kShortPress); // a press from before the start doesn't count
}

uint8_t buttons_pressed() {
    static uint8_t held = 0; // sampled no faster than the keys bounce
    uint8_t down = (gpio_get_level(kUpPin) ? 0 : kVolumeUp) | (gpio_get_level(kDownPin) ? 0 : kVolumeDown);
    uint8_t pressed = down & ~held;
    held = down;
    uint8_t status = 0;
    if (pmu_read(kIrqStatus2, status) && status) {
        pmu_write(kIrqStatus2, status);
        // Unplugging USB can set the short-press bit too; it is not a press.
        if (status & kShortPress && !(status & (kUsbRemoved | kUsbInserted))) pressed |= kPower;
    }
    return pressed;
}

} // namespace board
