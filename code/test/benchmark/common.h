#pragma once

#include <algorithm>
#include <chrono>
#include <utility>

#include "test/common.h"

namespace test::benchmark {

enum class Algo { LowRisk, JavaEval, JavaBrute, JavaSearch };

struct Config {
    int cols, rows, mines;
    long double time;
    int games;
    Algo algorithm;
    long long nodes;

    constexpr Config(int cols, int rows, int mines, long double time, int games, Algo algorithm, long long nodes) : cols(cols), rows(rows), mines(mines), time(time), games(games), algorithm(algorithm), nodes(nodes) {}
};

// time 单位为秒；JavaSearch 的 nodes 包含第零层，保底完成第零层；残局沿用 JavaBrute。
inline constexpr Config config{
    /* 列数 */ 30,
    /* 行数 */ 16,
    /* 雷数 */ 99,
    /* 时限，秒 */ 900.0L,
    /* 对局数上限 */ 200000,
    /* 算法 */ Algo::JavaSearch,
    /* 搜索节点预算 */ 256,
};
static_assert(config.time > 0.0L && config.games > 0 && config.nodes > 0, "测试时限、对局数和搜索预算必须均为正数");

// 配置须合法，time/games 均为有限正数；限制在整局之间检查，首点固定右上角，seed 决定对局种子序列。
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
        callback(std::as_const(game));
        game.play({config.cols - 1});
        for (;;) {
            callback(std::as_const(game));
            if (game.state() != Game::State::Running)
                break;
            if (config.algorithm == Algo::JavaSearch) {
                mss::GameControl::Position &position = game.position();
                const std::vector<mss::ObservedBoard::CellId> &safe = position.safeMoves(algo);
                if (!safe.empty()) {
                    game.play(safe);
                    continue;
                }
                const mss::winrate::Input input = position.input(algo);
                if (input.probability.candidates() >= engineConfig.bruteForce.candidateThreshold) {
                    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.longTermRisk, workspace.longTermRisk);
                    mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, risk, {}, engineConfig.javaEval, engineConfig.moveEval, engineConfig.longTermRisk, engineConfig.pseudo, workspace.javaEval, false);
                    mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, std::move(evaluated), engineConfig, workspace.anytimeSearch, 0, {});
                    mss::AnytimeSearch::update(result, engineConfig, workspace.anytimeSearch, (std::max)(0LL, config.nodes - result.node), {});
                    game.play({result.recommended()});
                    continue;
                }
            }
            const Game::SuggestAlgo strategy = config.algorithm == Algo::LowRisk ? Game::SuggestAlgo::LowRisk : config.algorithm == Algo::JavaEval ? Game::SuggestAlgo::JavaEval : Game::SuggestAlgo::JavaBrute;
            game.play(game.suggest(algo, strategy, engineConfig).suggest);
        }
    }
}

}
