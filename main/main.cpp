// Nu, pogodi! (Hanka8/Nu-pogodi's game A) on the board: its pictures where its CSS puts them, its 255 x 189 px
// game board scaled to the screen's 480 px width, its sounds, and a tap on the left or right half moving the wolf
// up (the middle) or down (the bottom). A tap starts a game when none runs. The side keys: "+" and "–" the volume,
// PWR in the middle locks it (dark, deaf to taps, the game paused) until pressed again.
#include <algorithm>

#include "board.h"
#include "esp_random.h"
#include "game.h"
#include "lvgl.h"

using namespace nupogodi;

extern "C" const lv_font_t digits; // digits.c: the score's Digital Numbers

namespace {

// An embedded file (main/CMakeLists.txt): a PNG LVGL decodes on first use, or a sound's PCM.
#define EMBEDDED(file)                                                                                         \
    extern const uint8_t file##_start[] asm("_binary_" #file "_start");                                         \
    extern const uint8_t file##_end[] asm("_binary_" #file "_end")

lv_image_dsc_t png(const uint8_t *start, const uint8_t *end) {
    lv_image_dsc_t image = {};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    image.data = start;
    image.data_size = uint32_t(end - start);
    return image;
}

#define PNG(name)                                                                                              \
    EMBEDDED(name##_png);                                                                                      \
    const lv_image_dsc_t name = png(name##_png_start, name##_png_end)
#define PCM(name) EMBEDDED(name##_pcm)

PNG(background);
PNG(wolf_LeftUp);
PNG(wolf_LeftDown);
PNG(wolf_RightUp);
PNG(wolf_RightDown);
PNG(egg);
PNG(crashed_egg);
PNG(life);
PNG(chicken_left1);
PNG(chicken_left2);
PNG(chicken_left3);
PNG(chicken_left4);
PNG(chicken_right1);
PNG(chicken_right2);
PNG(chicken_right3);
PNG(chicken_right4);
PCM(collect);
PCM(lifeDown);
PCM(gameOver);

const lv_image_dsc_t *const kWolf[] = {&wolf_LeftUp, &wolf_LeftDown, &wolf_RightUp, &wolf_RightDown}; // by Lane
const lv_image_dsc_t *const kChicken[2][4] = {
    {&chicken_left1, &chicken_left2, &chicken_left3, &chicken_left4},
    {&chicken_right1, &chicken_right2, &chicken_right3, &chicken_right4},
};

constexpr float kScale = 480.0f / 255; // the CSS board's px to the screen's
constexpr int kTop = (480 - 356) / 2;  // the board (background.png's height) in the middle
constexpr int kDownFrom = 320;         // taps below this line move the wolf down
constexpr int kStartVolume = 8;        // of board::kMaxVolume

constexpr int32_t px(float css) { return int32_t(css * kScale + 0.5f); }
void place(lv_obj_t *obj, float css_x, float css_y) { lv_obj_set_pos(obj, px(css_x), kTop + px(css_y)); }

Game game(0);
View shown;
int volume = kStartVolume;
bool locked = false;
uint32_t locked_at = 0, paused = 0; // the game's clock stops while locked

uint32_t game_clock() { return (locked ? locked_at : lv_tick_get()) - paused; }
struct {
    lv_obj_t *wolf, *egg[kMaxEggs], *crash, *chicken, *life[3], *score;
} ui;

lv_obj_t *image(const lv_image_dsc_t *src) {
    lv_obj_t *obj = lv_image_create(lv_screen_active());
    lv_image_set_src(obj, src);
    return obj;
}

// The positions as styles.css's (in its board's px) and its keyframes' steps.
void show(const View &v) {
    lv_image_set_src(ui.wolf, kWolf[v.wolf]);
    place(ui.wolf, is_right(v.wolf) ? 125 : 65, 80);
    for (int i = 0; i < kMaxEggs; i++) {
        lv_obj_set_hidden(ui.egg[i], i >= v.eggs);
        if (i >= v.eggs) continue;
        auto [lane, step] = v.egg[i];
        int dir = is_right(lane) ? -1 : 1; // moveEggLeft rolls right, moveEggRight left
        place(ui.egg[i], (is_right(lane) ? 223 : 24) + dir * 9 * step, (lane & 1 ? 94 : 52) + 5 * step);
        lv_image_set_rotation(ui.egg[i], dir * 300 * step); // 30 degrees a step
    }
    lv_obj_set_hidden(ui.crash, v.crash < 0);
    if (v.crash >= 0) place(ui.crash, is_right(v.crash) ? 179 : 49, 159);
    lv_obj_set_hidden(ui.chicken, v.chicken < 0);
    if (v.chicken >= 0) {
        bool right = is_right(v.chicken);
        float run = 255 * 0.03f * (v.chicken_frame + 1); // 3 % of the board a frame
        lv_image_set_src(ui.chicken, kChicken[right][v.chicken_frame]);
        place(ui.chicken, right ? 193 + run : 45 - run, 147);
    }
    for (int i = 0; i < 3; i++) lv_obj_set_hidden(ui.life[i], !(v.lives_lit >> i & 1));
    if (v.score) lv_label_set_text_fmt(ui.score, "%d", v.score);
    else lv_label_set_text(ui.score, "0000"); // as the page shows it until the first catch
}

void play(const uint8_t *start, const uint8_t *end) { board::play(start, size_t(end - start)); }

void frame(lv_timer_t *) {
    if (locked) return;
    uint32_t now = game_clock();
    uint8_t sounds = game.update(now);
    if (sounds & kCollect) play(collect_pcm_start, collect_pcm_end);
    if (sounds & kLifeDown) play(lifeDown_pcm_start, lifeDown_pcm_end);
    if (sounds & kGameOver) play(gameOver_pcm_start, gameOver_pcm_end);
    View v = game.view(now);
    if (v == shown) return;
    show(v);
    shown = v;
}

void pressed(lv_event_t *) {
    if (locked) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    game.tap(Lane((p.x >= 240) << 1 | (p.y >= kDownFrom)), game_clock());
    frame(nullptr); // the wolf moves now, not on the next frame
}

void buttons(lv_timer_t *) {
    uint8_t pressed = board::buttons_pressed();
    if (pressed & board::kPower) {
        if (locked) paused += lv_tick_get() - locked_at;
        else locked_at = lv_tick_get();
        locked = !locked;
        board::display(!locked);
    }
    if (locked || !(pressed & (board::kVolumeUp | board::kVolumeDown))) return;
    volume = std::clamp(volume + bool(pressed & board::kVolumeUp) - bool(pressed & board::kVolumeDown), 0, board::kMaxVolume);
    board::volume(volume);
    play(collect_pcm_start, collect_pcm_end); // to hear how loud
}

// In the page's order, later on top: the board, the score, the lives, the wolf, the eggs, the broken egg, the chick.
void build() {
    game = Game(esp_random());
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xb6bfb2), 0); // #gameBoard's
    lv_obj_add_event_cb(screen, pressed, LV_EVENT_PRESSED, nullptr);
    place(image(&background), 0, 0);
    ui.score = lv_label_create(screen);
    lv_obj_set_style_text_font(ui.score, &digits, 0);
    lv_obj_set_style_text_color(ui.score, lv_color_black(), 0);
    place(ui.score, 142, 11);
    // #livesBox (54 x 19 at 126, 32) fills from the right, 30 % of it a life with 3 % between.
    for (int i = 0; i < 3; i++) place(ui.life[i] = image(&life), 126 + 54 - 16.2f * (i + 1) - 1.62f * i, 32);
    ui.wolf = image(kWolf[kTopLeft]);
    for (lv_obj_t *&egg_obj : ui.egg) egg_obj = image(&egg);
    ui.crash = image(&crashed_egg);
    ui.chicken = image(kChicken[0][0]);
    show(shown);
    lv_timer_create(frame, 10, nullptr);
    lv_timer_create(buttons, 30, nullptr); // slower than the keys bounce
}

} // namespace

extern "C" void app_main() {
    board::display_start();
    board::audio_start();
    board::volume(volume);
    board::buttons_start();
    board::display_lock();
    build();
    board::display_unlock();
    board::display(true);
}
