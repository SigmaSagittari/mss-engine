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

struct PerformanceWorkspace {
    mss::Workspace engine;
    mss::ObservedBoard::Delta updates;
};

inline bool runLowRiskGame(mss::GameControl::Game &game, mss::Random &rng, PerformanceWorkspace &workspace) {
    std::vector<mss::ObservedBoard::CellId> moves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
    const mss::ObservedBoard::CellId firstCell = game.position.observedBoard.id(0, game.position.observedBoard.board.cols() - 1);
    mss::ObservedBoard::CellId safeCell = firstCell;
    if (game.number(0, game.position.observedBoard.board.cols() - 1) == 9) {
        const int cellCount = game.position.observedBoard.board.rows() * game.position.observedBoard.board.cols();
        safeCell = -1;
        while (safeCell < 0) {
            const mss::ObservedBoard::CellId candidate = (mss::ObservedBoard::CellId)(rng.next() % (std::uint64_t)cellCount);
            if (candidate != firstCell && game.number(candidate / game.position.observedBoard.board.cols(), candidate % game.position.observedBoard.board.cols()) != 9)
                safeCell = candidate;
        }
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
    }
}

// 固定时长运行高级盘面，统计 Game 的 LowRisk 胜率和整体吞吐量。
inline void performance() {
    constexpr int kRows = 30;
    constexpr int kCols = 16;
    constexpr int kMines = 99;
    constexpr double kDurationSeconds = 20.0;
    mss::Random rng(0xC0FFEE12345ULL, 0xD1B54A32D192ED03ULL);
    PerformanceWorkspace workspace;
    long long games = 0;
    int wins = 0;
    mss::GameControl::Game game(kRows, kCols, kMines, {rng.next(), rng.next()}, workspace.engine);
    const std::vector<mss::ObservedBoard::CellId> initialMoves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
    const mss::ObservedBoard::CellId firstCell = game.position.observedBoard.id(0, game.position.observedBoard.board.cols() - 1);
    mss::assert_(initialMoves.size() == 1 && initialMoves[0] == firstCell, "test::performance: 初始平局没有选择右上角");
    const TimeBox timebox(kDurationSeconds);
    while (!timebox.expired()) {
        if (games != 0)
            game.reset({rng.next(), rng.next()});
        if (runLowRiskGame(game, rng, workspace))
            ++wins;
        ++games;
        if (games % 1000 == 0)
            std::cout << "test/performance: completed=" << games << ", wins=" << wins << '\n' << std::flush;
    }
    const double elapsedSeconds = timebox.elapsedSeconds();
    std::cout << "test/performance: difficulty=Advanced, mode=LowRisk, board=" << kRows << 'x' << kCols << '/' << kMines << ", target_seconds=" << kDurationSeconds << ", games=" << games << ", wins=" << wins << ", losses=" << games - wins << ", win_rate=" << std::fixed << std::setprecision(2) << 100.0 * wins / games << "%, elapsed=" << elapsedSeconds << "s, games_per_second=" << games / elapsedSeconds << '\n';
}

}
