// The rules of Hanka8/Nu-pogodi's game A (app.js and its CSS animations), on one clock: plain C++, tested on the
// host (test/test_game.cpp).
#pragma once

#include <cstdint>
#include <deque>
#include <random>

namespace nupogodi {

// The wolf's four poses and the four ramps: bit 0 the bottom, bit 1 the right.
enum Lane : uint8_t { kTopLeft, kBottomLeft, kTopRight, kBottomRight };
constexpr bool is_right(int lane) { return lane & 2; }

// Sounds to start, as a mask.
enum Sound : uint8_t { kCollect = 1, kLifeDown = 2, kGameOver = 4 };

constexpr int kMaxEggs = 4; // on the ramps at once: a 4 s fall, one every 2 s

// What the screen shows at one moment.
struct View {
    uint8_t wolf = kTopLeft;
    uint8_t eggs = 0;
    struct Egg {
        uint8_t lane = 0, step = 0; // step 0..4 down the ramp
        bool operator==(const Egg &) const = default;
    } egg[kMaxEggs];
    int8_t crash = -1;   // the lane whose egg lies broken
    int8_t chicken = -1; // the lane whose chick runs off, in frames 0..3
    int8_t chicken_frame = 0;
    uint8_t lives_lit = 0; // lost lives shown, a bit each (they blink as they come)
    int score = 0;
    bool operator==(const View &) const = default;
};

class Game {
public:
    explicit Game(uint32_t seed) : rng_(seed) {}
    void tap(Lane lane, uint32_t now); // starts a game when none runs (a second after the last ended), moves the wolf
    uint8_t update(uint32_t now);      // runs the clock to now; the sounds to start
    View view(uint32_t now) const;

private:
    struct Egg {
        uint8_t lane;
        uint32_t born, speed; // speed: app.js's eggSpeed when it came, a quarter of its fall
    };
    void tick(uint32_t t);
    uint8_t land(uint32_t t);

    std::minstd_rand rng_;
    bool started_ = false, ended_ = false, ticking_ = false;
    uint32_t next_tick_ = 0, ended_at_ = 0;
    int score_ = 0, misses_ = 0, previous_lane_ = -1;
    uint32_t speed_ = 0, frequency_ = 0; // frequency only paces the speed-ups: app.js's interval never changes
    uint8_t wolf_ = kTopLeft;
    std::deque<Egg> eggs_;
    int crash_lane_ = -1, chicken_lane_ = -1;
    uint32_t crash_at_ = 0, chicken_at_ = 0, life_at_[3] = {};
};

} // namespace nupogodi
