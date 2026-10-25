#pragma once

#include <chrono>
#include <cmath>
#include <utility>

#include "core/algorithm/rng.h"
#include "core/assert.h"
#include "game/game.h"

namespace test {

struct Config {
    int cols, rows, mines;
    long double time;
    int games, threads;

    Config(int cols, int rows, int mines, long double time, int games, int threads) : cols(cols), rows(rows), mines(mines), time(time), games(games), threads(threads) {
        mss::assert_(cols > 0 && rows > 0 && mines >= 0 && mines < rows * cols, "test::Config: 棋盘参数无效");
        mss::assert_(std::isfinite(time) && time > 0.0L && games > 0 && threads > 0 && threads <= games, "test::Config: 时限、局数和线程数无效");
    }
};

inline const Config config{
    /* cols 列数 */ 30,
    /* rows 行数 */ 16,
    /* mines 雷数 */ 99,
    /* time 时限，秒 */ 900.0L,
    /* games 局数上限 */ 200000,
    /* threads 对局线程数 */ 5
};
inline const mss::GameControl::Config gameConfig{
    /* shapeSolver 概率求解器 */ {
        /* graphThreshold 切换 Graph 的 Box 数阈值 */ 24,
        /* order 消元顺序 */ mss::ShapeSolverConfig::Order::Auto,
        /* window3Threshold 尝试优化顺序的前沿阈值 */ 24,
        /* saRounds 退火轮数 */ 4,
        /* saIterations 每轮迭代次数 */ 1000,
        /* saStartTemperature 起始温度 */ 4.0,
        /* saEndTemperature 结束温度 */ 0.01
    }
};
inline const mss::JavaEval::Config javaEvalConfig{
    /* selection 候选筛选 */ {
        /* selectionThreshold1 第一筛选阈值 */ 0.10L,
        /* selectionThreshold2 第二筛选阈值 */ 0.20L,
        /* spaceThreshold 空间阈值 */ 8,
        /* interchangeable 对换去重 */ false,
        /* equivalence 等价类去重 */ false,
        /* dominance 支配去重 */ true
    },
    /* progressContribution 推进加分 */ 0.001L,
    /* influenceScale 长期风险系数 */ 0.9L
};
inline const mss::AnytimeSearch::Config anytimeConfig{
    /* javaEval 单步评分与候选配置 */ {
        /* selection 候选筛选与评分前剪枝 */ {
            /* selectionThreshold1 第一筛选阈值 */ javaEvalConfig.selection.selectionThreshold1,
            /* selectionThreshold2 第二筛选阈值 */ javaEvalConfig.selection.selectionThreshold2,
            /* spaceThreshold 空间阈值 */ javaEvalConfig.selection.spaceThreshold,
            /* interchangeable 对换去重 */ true,
            /* equivalence 等价类去重 */ true,
            /* dominance 支配去重 */ true
        },
        /* progressContribution 推进加分 */ javaEvalConfig.progressContribution,
        /* influenceScale 长期风险系数 */ javaEvalConfig.influenceScale
    },
    /* nodeLimit 全盘概率分析与 Observe 次数预算，约等于旧版 256 节点 */ 1280,
    /* width[0] 第一层宽度 */ 6,
    /* width[1] 第二层宽度 */ 3,
    /* width[2] 后续层宽度 */ 1
};
inline const mss::FiftyFifty::TemplateConfig templateConfig{
    /* prefix 模板前缀 */ 20
};
inline const mss::LongTermRisk::Config riskConfig{
    /* influenceThreshold 影响量阈值 */ 0.025L
};
inline const mss::BruteForce::Config bruteConfig{
    /* checkAllMoves 返回全部候选 */ false,
    /* parallel 根节点并行 */ false,
    /* threadCount 暴力线程数 */ 1
};
inline const mss::MovePolicy policy{
    /* basic 普通算法 */ {
        /* algorithm 算法 */ mss::MovePolicy::Basic::Algorithm::Anytime,
        /* javaEval 单步算法配置 */ nullptr,
        /* anytime 搜索算法配置 */ &anytimeConfig
    },
    /* fiftyFifty 50-50 算法 */ {
        /* algorithm 算法 */ mss::MovePolicy::FiftyFifty::Algorithm::OldJava,
        /* templates 模板检测配置 */ nullptr,
        /* risk 长期风险配置 */ &riskConfig
    },
    /* brute 暴力接管 */ {
        /* limit 摆法数阈值，0 关闭 */ 1000,
        /* config 关闭时可传 nullptr */ &bruteConfig
    }
};

// time/games 为有限正数，在整局之间检查；首点固定右上角，每次调用拥有独立的池和工作区。
// callback(const Game &) 在空盘和每次 play 后调用，包括终局；引用只在本次回调中有效。
template <typename Callback>
void runGames(const Config &config, const mss::MovePolicy &policy, mss::U128 seed, Callback &&callback) {
    using Game = mss::GameControl::Game;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    mss::GameControl::Workspace workspace;
    mss::GameControl::SuggestPool suggestions;
    mss::FiftyFifty::Pool fiftyPool;
    mss::AnytimeSearch::Pool searchPool;
    mss::AnytimeSearch::Workspace searchWorkspace(workspace.basic, workspace.structure, workspace.probability);
    mss::BruteForce::Workspace bruteForce(workspace.probability.shapeSolver);
    mss::vector<mss::ObservedBoard::CellId> moves;
    mss::Random random(seed.lo, seed.hi);
    Game game(config.rows, config.cols, config.mines, {random.next(), random.next()}, workspace, gameConfig, suggestions);
    const mss::ObservedBoard::CellId first = config.cols - 1;
    for (int games = 0; games < config.games && std::chrono::duration<long double>(std::chrono::steady_clock::now() - start).count() < config.time; ++games) {
        if (games != 0) game.reset({random.next(), random.next()});
        callback(std::as_const(game));
        game.play({&first, 1});
        for (;;) {
            callback(std::as_const(game));
            if (game.state() != Game::State::Running) break;
            game.suggest(policy, fiftyPool, searchPool, searchWorkspace, bruteForce, moves);
            game.play({moves.data, (int)moves.size});
        }
    }
}

}
