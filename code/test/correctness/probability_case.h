#pragma once

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "core/assert.h"
#include "core/utility/neighbors.h"
#include "core/workspace.h"

namespace test::correctness {

// 按固定安全点序列逐点打开，并检查增量更新后仍存在合法概率解。
inline void probabilityCase() {
    constexpr int kRows = 10;
    constexpr int kCols = 21;
    constexpr int kMines = 78;
    constexpr double kTimeoutSeconds = 100.0;
    constexpr std::array<std::string_view, kRows> kRowsData{
        {"HMHMHMHMHMHMHMHMHMHHH", "HMHHHMHHHMHHHMHHHMHMH", "HMHMHMHMHMHMHMHMHMHMH", "HMHHHMHHHMHHHMHHHMHHH", "HMHMHMHMHMHMHMHMHMHMH",
         "HMHHHMHHHMHHHMHHHMHMH", "HMHMHMHMHMHMHMHMHMHMH", "HMHHHMHHHMHHHMHHHMHMH", "HMHMHMHMHMHMHMHMHMHMH", "HMHHHMHHHMHHHMHHHMHMH"}};

    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    mss::ObservedBoard::Result board = mss::ObservedBoard::analyze(kRows, kCols, kMines);
    mss::RawGrid<char> mines(kRows, kCols, 0);
    for (int row = 0; row < kRows; ++row)
        for (int col = 0; col < kCols; ++col)
            mines[row][col] = kRowsData[row][col] == 'M';

    mss::GameControl::Workspace workspace;
    mss::Basic::Result basic = mss::Basic::analyze(board, workspace.basic);
    mss::Structure::Pool shapes;
    mss::Structure::Result structure = mss::Structure::analyze(board, basic, shapes, workspace.structure);
    mss::ShapeSolver::DistributionPool distributions;
    mss::Probability::Result probability = mss::Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability, mss::ShapeSolver::OrderAlgo::SA);
    mss::ObservedBoard::Delta updates;
    mss::Basic::Delta basicDelta;
    mss::Structure::Delta structureDelta;
    long long totalSteps = 0;
    double totalCalculationMilliseconds = 0.0;
    bool timedOut = false;
    std::cout << "test/probability-case: " << kCols << 'x' << kRows << 'x' << kMines << ", sequence=(1,1),(1,3),...,(9,21)\n";
    std::cout << std::fixed << std::setprecision(6);
    for (int row = 0; row < kRows && !timedOut; row += 2)
        for (int col = 0; col < kCols && !timedOut; col += 2) {
            const mss::ObservedBoard::CellId cell = board.id(row, col);
            const long double clickProbability = probability.mineProbability(cell, board, basic, structure);
            const std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
            int adjacentMines = 0;
            mss::forEachAdjacent(row, col, kRows, kCols, [&](int neighborRow, int neighborCol) { adjacentMines += mines[neighborRow][neighborCol]; });
            updates.clear();
            updates.changes.push_back({cell, board.board[row][col], (mss::ObservedBoard::CellState)adjacentMines});
            mss::ObservedBoard::applyDelta(board, updates.changes);
            mss::Basic::update(basic, basicDelta, board, updates, workspace.basic);
            mss::Structure::update(structure, structureDelta, board, basic, shapes, {&cell, 1}, workspace.structure);
            mss::Probability::analyze(board, basic, structure, shapes, distributions, probability, workspace.probability, mss::ShapeSolver::OrderAlgo::SA);
            const double calculationMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stepStart).count();
            mss::assert_(mss::Basic::checkValid(board, basic), "test::probabilityCase: 增量 Basic 结果不合法");
            mss::assert_(probability.candidates() > 0.0L, "test::probabilityCase: 没有合法布局");
            ++totalSteps;
            totalCalculationMilliseconds += calculationMilliseconds;
            timedOut = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >= kTimeoutSeconds;
            std::cout << "  step=" << totalSteps << " open=(" << row + 1 << ',' << col + 1 << ") number=" << adjacentMines << " click-risk=" << clickProbability * 100.0L << "% candidates=" << std::setprecision(18) << probability.candidates() << std::setprecision(6) << " t-cell=" << probability.tCellProbability() * 100.0L << "% calc_ms=" << calculationMilliseconds << '\n' << std::flush;
        }
    std::cout << "  steps=" << totalSteps << ", calc_total_ms=" << totalCalculationMilliseconds << ", elapsed_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() << ", timeout_s=" << kTimeoutSeconds << ", timed_out=" << std::boolalpha << timedOut << '\n';
    mss::assert_(!timedOut && totalSteps == 55, "test::probabilityCase: 未在时限内完成 55 步");
}

}
