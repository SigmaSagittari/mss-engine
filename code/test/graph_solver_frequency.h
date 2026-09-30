#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "core/utility/rng.h"
#include "core/workspace.h"
#include "game/game.h"
#include "test/common.h"

namespace test {

struct GraphSolverFrequencyWorkspace {
    mss::Workspace engine;
    mss::ObservedBoard::Delta updates;
};

struct GraphSolverFrequency {
    long long decisions = 0;
    long long components = 0;
    long long graphComponents = 0;
    bool boardCaptured = false;
};

inline void printBoardJson(const mss::ObservedBoard::Result &board) {
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
}

// 每次建议后按 Shape 的 Box 数统计 GraphSolver 阈值命中；同一组件跨决策会重复计入。
inline void collectGraphSolverFrequency(const mss::GameControl::Game &game, GraphSolverFrequency &frequency) {
    ++frequency.decisions;
    const mss::Structure::Result &structure = game.position.structure();
    const mss::Structure::Pool &shapes = game.shapePool();
    for (int i = 0; i < (int)structure.components.size(); ++i) {
        const mss::Structure::Instance &instance = shapes.getInstance(structure.components[i]);
        const mss::Structure::Shape &shape = shapes.getShape(instance.shape);
        ++frequency.components;
        if ((int)shape.boxes.size >= mss::ShapeSolver::graphThreshold) {
            ++frequency.graphComponents;
            if (!frequency.boardCaptured) {
                printBoardJson(game.position.observedBoard);
                frequency.boardCaptured = true;
            }
        }
    }
}

inline bool runLowRiskGame(mss::GameControl::Game &game, mss::Random &rng, GraphSolverFrequencyWorkspace &workspace, GraphSolverFrequency &frequency) {
    std::vector<mss::ObservedBoard::CellId> moves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
    collectGraphSolverFrequency(game, frequency);
    const mss::ObservedBoard::CellId firstCell = game.position.observedBoard.id(0, game.position.observedBoard.board.cols() - 1);
    mss::ObservedBoard::CellId safeCell = -1;
    const int cellCount = game.position.observedBoard.board.rows() * game.position.observedBoard.board.cols();
    while (safeCell < 0) {
        const mss::ObservedBoard::CellId candidate = (mss::ObservedBoard::CellId)(rng.next() % (std::uint64_t)cellCount);
        if (candidate != firstCell && game.number(candidate / game.position.observedBoard.board.cols(), candidate % game.position.observedBoard.board.cols()) != 9)
            safeCell = candidate;
    }
    game.makeFirstMoveSafe(firstCell, safeCell);
    for (;;) {
        for (int i = 0; i < (int)moves.size(); ++i) {
            const mss::ObservedBoard::CellId cell = moves[i];
            const std::pair<int, int> position = game.position.observedBoard.pos(cell);
            const int number = game.number(position.first, position.second);
            if (number == 9)
                return false;
            workspace.updates.clear();
            workspace.updates.changes.push_back({cell, (mss::ObservedBoard::CellState)(number)});
            game.update(workspace.updates);
            if (game.won())
                return true;
        }
        moves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
        collectGraphSolverFrequency(game, frequency);
    }
}

inline void graphSolverFrequency() {
    constexpr int kRows = 30;
    constexpr int kCols = 30;
    constexpr int kMines = 225;
    constexpr double kDurationSeconds = 20.0;
    mss::Random rng(0xC0FFEE12345ULL, 0xD1B54A32D192ED03ULL);
    GraphSolverFrequencyWorkspace workspace;
    GraphSolverFrequency frequency;
    long long games = 0;
    int wins = 0;
    mss::GameControl::Game game(kRows, kCols, kMines, {rng.next(), rng.next()}, workspace.engine);
    const std::vector<mss::ObservedBoard::CellId> initialMoves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
    const mss::ObservedBoard::CellId firstCell = game.position.observedBoard.id(0, game.position.observedBoard.board.cols() - 1);
    mss::assert_(initialMoves.size() == 1 && initialMoves[0] == firstCell, "test::graphSolverFrequency: 初始平局没有选择右上角");
    const TimeBox timebox(kDurationSeconds);
    while (!timebox.expired()) {
        if (games != 0)
            game.reset({rng.next(), rng.next()});
        if (runLowRiskGame(game, rng, workspace, frequency))
            ++wins;
        ++games;
    }
    const double elapsedSeconds = timebox.elapsedSeconds();
    mss::assert_(frequency.components > 0, "test::graphSolverFrequency: 没有采集到组件");
    mss::assert_(frequency.boardCaptured, "test::graphSolverFrequency: 没有捕获到 GraphSolver 组件");
    std::cout << "test/graphSolverFrequency: board=" << kRows << 'x' << kCols << '/' << kMines << ", mode=LowRisk, target_seconds=" << kDurationSeconds << ", games=" << games << ", wins=" << wins << ", decisions=" << frequency.decisions << ", components=" << frequency.components << ", graph_components=" << frequency.graphComponents << ", graph_ratio=" << std::fixed << std::setprecision(2) << 100.0 * frequency.graphComponents / frequency.components << "%, elapsed=" << elapsedSeconds << "s\n";
}

}

