#pragma once

#include <chrono>
#include <utility>

#include "core/utility/rng.h"
#include "core/config.h"
#include "core/workspace.h"
#include "game/game.h"

namespace test {

// 本组用例明确选择引擎参数；线程数引用由主配置绑定到自己的 Global。
inline const mss::Config engineConfig{mss::Config::Global(5), false, mss::BruteForce::Config::Solver::Bitwise, 1000, {0.025L, 0.9L, 1e-9L}, mss::Pseudo::Config(1e-9L), {0.001L, 0.10L, 0.20L, 0.9L, 0.0001L, 4, 1, 8}, {0.05L, 1.0L}};

struct Config {
    int cols, rows, mines;
    long double time;
    int games;
    mss::GameControl::Game::SuggestAlgo algorithm;

    constexpr Config(int cols, int rows, int mines, long double time, int games, mss::GameControl::Game::SuggestAlgo algorithm) : cols(cols), rows(rows), mines(mines), time(time), games(games), algorithm(algorithm) {}
};

// 配置须合法，time/games 均为有限正数；限制在整局之间检查，所有落子均由 suggest 选择，seed 决定对局种子序列。
// callback(const Game &) 在空盘和每次 play 后调用，包括终局；借用的对象与局面内容仅在本次回调期间有效。
template <typename Callback>
void runGames(const Config &config, const mss::ShapeSolver::OrderAlgo &algo, mss::U128 seed, mss::GameControl::SuggestPool &suggestions, Callback &&callback) {
    using Game = mss::GameControl::Game;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    mss::Workspace workspace;
    mss::Random random(seed.lo, seed.hi);
    Game game(config.rows, config.cols, config.mines, {random.next(), random.next()}, workspace, suggestions);
    for (int games = 0; games < config.games && std::chrono::duration<long double>(std::chrono::steady_clock::now() - start).count() < config.time; ++games) {
        if (games != 0)
            game.reset({random.next(), random.next()});
        for (;;) {
            callback(std::as_const(game));
            if (game.state() != Game::State::Running)
                break;
            game.play(game.suggest(algo, config.algorithm, engineConfig).suggest);
        }
    }
}

}
