#pragma once

#include <cstddef>
#include <cstdint>

namespace board {

// The panel with touch, and LVGL on its own task (board.cpp); build the screen under the display lock, then
// turn the panel on.
void display_start();
void display_lock();
void display_unlock();
void display(bool on);

// The speaker: 44.1 kHz mono, 16-bit little-endian PCM, mixed over whatever already plays.
void audio_start();
void play(const uint8_t *pcm, size_t bytes);
constexpr int kMaxVolume = 10;
void volume(int level); // 0 (silent) .. kMaxVolume, 4 dB a step

// The three keys on the side: "+" (IO18), PWR in the middle and "–" (BOOT).
enum Button : uint8_t { kVolumeUp = 1, kPower = 2, kVolumeDown = 4 };
void buttons_start();
uint8_t buttons_pressed(); // presses since the last call, a mask; on the LVGL task, a few times a second or more
} // namespace board
