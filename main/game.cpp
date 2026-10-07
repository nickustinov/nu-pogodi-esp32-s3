#include "game.h"

#include <algorithm>

namespace nupogodi {

namespace {

constexpr uint32_t kTickMs = 2000;   // a new egg (app.js's eggFrequency when the game starts)
constexpr uint32_t kStartSpeed = 1000;
constexpr uint32_t kSpeedStep = 40, kFrequencyStep = 450; // EGG_SPEED_CHANGE_A, EGG_FREQUENCY_CHANGE_A
constexpr uint32_t kRestartMs = 1000; // taps right after the end still meant for the wolf

uint32_t due(uint32_t born, uint32_t speed) { return born + 4 * speed; }

} // namespace

void Game::tap(Lane lane, uint32_t now) {
    if (!started_ || ended_) {
        if (ended_ && now - ended_at_ < kRestartMs) return;
        *this = Game(rng_());
        started_ = ticking_ = true;
        next_tick_ = now + kTickMs;
        speed_ = kStartSpeed;
        frequency_ = kTickMs;
    }
    wolf_ = lane;
}

uint8_t Game::update(uint32_t now) {
    uint8_t sounds = 0;
    while (true) { // the ticks and the landings in order; a tick first when both fall due together
        uint32_t landing = eggs_.empty() || ended_ ? UINT32_MAX : due(eggs_.front().born, eggs_.front().speed);
        if (ticking_ && next_tick_ <= now && next_tick_ <= landing) {
            tick(next_tick_);
            next_tick_ += kTickMs;
        } else if (landing <= now) {
            sounds |= land(landing);
        } else {
            return sounds;
        }
    }
}

void Game::tick(uint32_t t) {
    if (ended_) { // the game is over: what still rolls goes
        eggs_.clear();
        ticking_ = false;
        return;
    }
    int lane;
    do lane = int(rng_() % 4); while (lane == previous_lane_); // never the same ramp twice running
    previous_lane_ = lane;
    eggs_.push_back({uint8_t(lane), t, speed_});
    if (score_ % 100 == 0) frequency_ = 4000;
    if (score_ == 200 || score_ == 500 || score_ == 1000) misses_ = 0;
    if (score_ % 10 == 0 && frequency_ >= 1000 && speed_ >= 500) {
        frequency_ -= kFrequencyStep;
        speed_ -= kSpeedStep;
    }
}

uint8_t Game::land(uint32_t t) {
    int lane = eggs_.front().lane;
    eggs_.pop_front();
    if (lane == wolf_) {
        score_++;
        return kCollect;
    }
    life_at_[misses_++] = t;
    if (misses_ == 3) {
        ended_ = true;
        ended_at_ = t;
        return kLifeDown | kGameOver; // and no broken egg
    }
    crash_lane_ = chicken_lane_ = lane;
    crash_at_ = chicken_at_ = t;
    return kLifeDown;
}

View Game::view(uint32_t now) const {
    View v;
    v.wolf = wolf_;
    v.score = score_;
    for (const Egg &e : eggs_) {
        if (v.eggs == kMaxEggs) break;
        // Its CSS animation: 4.1 falls' quarters in five step-start keyframes.
        v.egg[v.eggs++] = {e.lane, uint8_t(std::min<uint32_t>(4, (now - e.born) * 50 / (41 * e.speed)))};
    }
    if (crash_lane_ >= 0 && now - crash_at_ < 1000) v.crash = int8_t(crash_lane_);
    if (chicken_lane_ >= 0 && now - chicken_at_ < 2000) {
        v.chicken = int8_t(chicken_lane_);
        v.chicken_frame = int8_t((now - chicken_at_) / 500);
    }
    for (int i = 0; i < misses_; i++) {
        uint32_t age = now - life_at_[i]; // lifeAppears: dark 400 ms, lit 400 ms, three times
        if (age >= 2400 || age % 800 >= 400) v.lives_lit |= 1 << i;
    }
    return v;
}

} // namespace nupogodi
