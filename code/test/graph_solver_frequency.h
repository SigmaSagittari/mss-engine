#pragma once

#include <iomanip>
#include <iostream>

#include "test/common.h"
#include "core/assert.h"

namespace test {

inline void graphSolverFrequency(const Config &config) {
    const mss::U128 seed{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    long long games = 0;
    long long wins = 0;
    long long decisions = 0;
    long long components = 0;
    long long graphComponents = 0;
    bool boardCaptured = false;
    auto printBoardJson = [](const mss::ObservedBoard::Result &board) {
        const int rows = board.board.rows();
        const int cols = board.board.cols();
        std::cout << "test/graphSolverFrequency: 首个 GraphSolver 组件盘面（UI JSON）\n";
        std::cout << "{\"rows\":" << rows << ",\"cols\":" << cols << ",\"mines\":" << board.totalMines << ",\"cells\":\"";
        for (int cell = 0; cell < rows * cols; ++cell) {
            const mss::ObservedBoard::CellState state = board.board[cell / cols][cell % cols];
            if (state == mss::ObservedBoard::CellState::Hidden)
                std::cout.put('?');
            else if (state == mss::ObservedBoard::CellState::ForcedMine)
                std::cout.put('M');
            else if (state == mss::ObservedBoard::CellState::ForcedSafe)
                std::cout.put('S');
            else
                std::cout.put((char)('0' + (int)state));
        }
        std::cout << "\"}\n" << std::flush;
    };

    // 按每个待决策局面的 Shape Box 数统计阈值命中，不统计终局，也不等同于实际求解次数；同一组件跨决策会重复计入。
    auto collectGraphSolverFrequency = [&](const mss::GameControl::Game &game) {
        if (game.state() != mss::GameControl::Game::State::Running) {
            ++games;
            wins += game.state() == mss::GameControl::Game::State::Won;
            return;
        }
        ++decisions;
        const mss::Structure::Result &structure = game.position().structure();
        const mss::Structure::Pool &shapes = game.position().shapePool();
        for (int i = 0; i < (int)structure.components.size(); ++i) {
            const mss::Structure::Instance &instance = shapes.getInstance(structure.components[i]);
            const mss::Structure::Shape &shape = shapes.getShape(instance.shape);
            ++components;
            if ((int)shape.boxes.size >= mss::ShapeSolver::graphThreshold) {
                ++graphComponents;
                if (!boardCaptured) {
                    printBoardJson(game.position().observedBoard());
                    boardCaptured = true;
                }
            }
        }
    };
    mss::GameControl::SuggestPool suggestions;
    runGames(config, mss::ShapeSolver::OrderAlgo::Auto, seed, suggestions, collectGraphSolverFrequency);
    const double elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const char *algorithm = config.algorithm == mss::GameControl::Game::SuggestAlgo::LowRisk ? "LowRisk" : config.algorithm == mss::GameControl::Game::SuggestAlgo::JavaEval ? "JavaEval" : "JavaBrute";
    mss::assert_(components > 0, "test::graphSolverFrequency: 没有采集到组件");
    std::cout << "test/graphSolverFrequency: board=" << config.cols << 'x' << config.rows << '/' << config.mines << ", mode=" << algorithm << ", target_seconds=" << config.time << ", target_games=" << config.games << ", seed=(" << seed.lo << ',' << seed.hi << "), games=" << games << ", wins=" << wins << ", losses=" << games - wins << ", decisions=" << decisions << ", components=" << components << ", graph_components=" << graphComponents << ", graph_ratio=" << std::fixed << std::setprecision(2) << 100.0 * graphComponents / components << "%, elapsed=" << elapsedSeconds << "s\n";
}

}

