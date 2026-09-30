#pragma once

#include <iomanip>
#include <iostream>

#include "test/common.h"

namespace test {

inline void graphSolverFrequency() {
    constexpr int kRows = 30;
    constexpr int kCols = 30;
    constexpr int kMines = 225;
    constexpr double kDurationSeconds = 20.0;
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

    // 每次建议后按 Shape 的 Box 数统计 GraphSolver 阈值命中；同一组件跨决策会重复计入。
    auto collectGraphSolverFrequency = [&](const mss::GameControl::Game &game) {
        ++decisions;
        const mss::Structure::Result &structure = game.position.structure();
        const mss::Structure::Pool &shapes = game.shapePool();
        for (int i = 0; i < (int)structure.components.size(); ++i) {
            const mss::Structure::Instance &instance = shapes.getInstance(structure.components[i]);
            const mss::Structure::Shape &shape = shapes.getShape(instance.shape);
            ++components;
            if ((int)shape.boxes.size >= mss::ShapeSolver::graphThreshold) {
                ++graphComponents;
                if (!boardCaptured) {
                    printBoardJson(game.position.observedBoard);
                    boardCaptured = true;
                }
            }
        }
    };
    const RunSummary summary = runGames(kRows, kCols, kMines, kDurationSeconds, collectGraphSolverFrequency, [](const RunSummary &) {});
    mss::assert_(components > 0, "test::graphSolverFrequency: 没有采集到组件");
    mss::assert_(boardCaptured, "test::graphSolverFrequency: 没有捕获到 GraphSolver 组件");
    std::cout << "test/graphSolverFrequency: board=" << kRows << 'x' << kCols << '/' << kMines << ", mode=LowRisk, target_seconds=" << kDurationSeconds << ", games=" << summary.games << ", wins=" << summary.wins << ", decisions=" << decisions << ", components=" << components << ", graph_components=" << graphComponents << ", graph_ratio=" << std::fixed << std::setprecision(2) << 100.0 * graphComponents / components << "%, elapsed=" << summary.elapsedSeconds << "s\n";
}

}

