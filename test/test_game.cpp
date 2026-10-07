// The rules, on the host: c++ -std=c++20 -Imain test/test_game.cpp main/game.cpp -o build/test_game && build/test_game
#include <cassert>
#include <cstdio>

#include "game.h"

using namespace nupogodi;

namespace {

// Runs the clock in 10 ms steps from `from` to `to`, the wolf under the next egg to land when `catching`.
void run(Game &game, uint32_t from, uint32_t to, bool catching) {
    for (uint32_t t = from; t <= to; t += 10) {
        View v = game.view(t);
        if (catching && v.eggs) game.tap(Lane(v.egg[0].lane), t);
        game.update(t);
    }
}

// Just before the egg due at `due` lands, the wolf goes to the other side.
void miss_at(Game &game, uint32_t due) {
    game.update(due - 1);
    game.tap(Lane(game.view(due - 1).egg[0].lane ^ 2), due - 1);
}

void idle_until_tapped() {
    Game game(1);
    assert(game.update(5000) == 0);
    View v = game.view(5000);
    assert(v.wolf == kTopLeft && v.eggs == 0 && v.score == 0 && v.lives_lit == 0 && v.crash < 0 && v.chicken < 0);
}

void tap_starts_and_moves() {
    Game game(1);
    game.tap(kBottomRight, 0);
    assert(game.view(0).wolf == kBottomRight);
    game.update(1990);
    assert(game.view(1990).eggs == 0);
    game.update(2000); // the first egg comes with the first 2 s tick
    View v = game.view(2000);
    assert(v.eggs == 1 && v.egg[0].step == 0);
}

void egg_rolls_in_five_steps() {
    Game game(1);
    game.tap(kTopLeft, 0);
    game.update(2000);
    // The CSS animation: 4.1 x 1000 ms in five step-start keyframes, 820 ms each, the last until it lands at 4 s.
    assert(game.view(2000 + 819).egg[0].step == 0);
    assert(game.view(2000 + 820).egg[0].step == 1);
    assert(game.view(2000 + 3279).egg[0].step == 3);
    assert(game.view(2000 + 3280).egg[0].step == 4);
    assert(game.view(2000 + 3999).egg[0].step == 4);
}

void catch_scores() {
    Game game(1);
    game.tap(kTopLeft, 0);
    game.update(2000);
    game.tap(Lane(game.view(2000).egg[0].lane), 2000);
    assert(game.update(5999) == 0);
    assert(game.update(6000) == kCollect); // 4 x 1000 ms after it came
    View v = game.view(6000);
    assert(v.score == 1 && v.lives_lit == 0 && v.crash < 0 && v.chicken < 0);
    assert(v.eggs == 2); // the 4 s and 6 s ones
}

void miss_breaks_egg_and_chicken_runs() {
    Game game(1);
    game.tap(kTopLeft, 0);
    game.update(2000);
    int lane = game.view(2000).egg[0].lane;
    miss_at(game, 6000);
    assert(game.update(6000) == kLifeDown);
    View v = game.view(6000);
    assert(v.score == 0 && v.crash == lane && v.chicken == lane && v.chicken_frame == 0);
    assert(game.view(6499).chicken_frame == 0 && game.view(6500).chicken_frame == 1);
    assert(game.view(7999).chicken_frame == 3 && game.view(8000).chicken < 0);
    assert(game.view(6999).crash == lane && game.view(7000).crash < 0);
    // The life blinks three times (800 ms each, dark first), then stays.
    assert(game.view(6000).lives_lit == 0 && game.view(6400).lives_lit == 1 && game.view(6800).lives_lit == 0);
    assert(game.view(7600).lives_lit == 0 && game.view(8399).lives_lit == 1 && game.view(9200).lives_lit == 1);
}

void lanes_never_repeat() {
    Game game(7);
    game.tap(kTopLeft, 0);
    int previous = -1;
    for (uint32_t t = 2000; t < 2000 * 300; t += 2000) {
        run(game, t - 1990, t, true);
        View v = game.view(t);
        assert(v.eggs > 0 && v.egg[v.eggs - 1].lane != previous);
        previous = v.egg[v.eggs - 1].lane;
    }
    assert(game.view(2000 * 300).lives_lit == 0);
}

void eggs_speed_up() {
    Game game(1);
    game.tap(kTopLeft, 0);
    // At 0 points every tick takes 40 ms off a step: the 2 s egg lands at 6 s, the 4 s one at 4 + 4 x 0.96 s.
    game.update(4000);
    game.tap(Lane(game.view(4000).egg[0].lane), 4000);
    assert(game.update(6000) == kCollect);
    game.tap(Lane(game.view(6000).egg[0].lane), 6000);
    assert(game.update(7839) == 0);
    assert(game.update(7840) == kCollect);
}

// Eggs come every 2 s, each a step 40 ms quicker than the last (0 points): they land 1840 ms apart from 6 s.
constexpr uint32_t kThirdMiss = 6000 + 2 * 1840;

void miss_three(Game &game) {
    game.tap(kTopLeft, 0);
    for (uint32_t miss = 0, due = 6000; miss < 3; miss++, due += 1840) {
        miss_at(game, due);
        assert(game.update(due) == (miss < 2 ? kLifeDown : (kLifeDown | kGameOver)));
    }
}

void three_misses_end_it() {
    Game game(1);
    miss_three(game);
    View v = game.view(kThirdMiss);
    assert(v.crash < 0); // no broken egg for the last one
    assert(v.eggs == 1); // the 8 s egg rolls on until the next tick...
    assert(game.update(kThirdMiss + 4000) == 0 && game.view(kThirdMiss + 4000).eggs == 0); // ...lands nowhere; none come
    assert(game.view(kThirdMiss + 2400).lives_lit == 7);
}

void restart_after_a_pause() {
    Game game(1);
    miss_three(game);
    int wolf = game.view(kThirdMiss).wolf;
    game.tap(Lane(wolf ^ 1), kThirdMiss + 999); // a tap meant for the wolf, not a new game
    assert(game.view(kThirdMiss + 999).wolf == wolf);
    assert(game.update(kThirdMiss + 3999) == 0 && game.view(kThirdMiss + 3999).eggs == 0);
    game.tap(kBottomLeft, kThirdMiss + 5000);
    View v = game.view(kThirdMiss + 5000);
    assert(v.wolf == kBottomLeft && v.lives_lit == 0 && v.score == 0 && v.eggs == 0);
    game.update(kThirdMiss + 7000);
    assert(game.view(kThirdMiss + 7000).eggs == 1);
}

void misses_forgiven_at_200() {
    Game game(3);
    game.tap(kTopLeft, 0);
    miss_at(game, 6000);
    game.update(6000);
    assert(game.view(9000).lives_lit == 1);
    uint32_t t = 6010;
    for (; game.view(t).score < 200; t += 10) run(game, t, t, true);
    assert(game.view(t).lives_lit == 1);
    run(game, t, t + 2000, true); // the next tick at 200 points clears them
    assert(game.view(t + 2000).lives_lit == 0 && game.view(t + 2000).score >= 200);
}

} // namespace

int main() {
    idle_until_tapped();
    tap_starts_and_moves();
    egg_rolls_in_five_steps();
    catch_scores();
    miss_breaks_egg_and_chicken_runs();
    lanes_never_repeat();
    eggs_speed_up();
    three_misses_end_it();
    restart_after_a_pause();
    misses_forgiven_at_200();
    std::puts("game: ok");
}
