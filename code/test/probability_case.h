#pragma once

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "core/assert.h"
#include "core/utility/neighbors.h"
#include "core/workspace.h"
#include "test/common.h"

namespace test {

// 按固定安全点序列逐点打开，并检查增量更新后仍存在合法概率解。
inline void probabilityCase() {
    constexpr int kRows = 10;
    constexpr int kCols = 21;
    constexpr int kMines = 78;
    constexpr double kTimeoutSeconds = 100.0;
    constexpr std::array<std::string_view, kRows> kRowsData{
        {"HMHMHMHMHMHMHMHMHMHHH", "HMHHHMHHHMHHHMHHHMHMH", "HMHMHMHMHMHMHMHMHMHMH", "HMHHHMHHHMHHHMHHHMHHH", "HMHMHMHMHMHMHMHMHMHMH",
         "HMHHHMHHHMHHHMHHHMHMH", "HMHMHMHMHMHMHMHMHMHMH", "HMHHHMHHHMHHHMHHHMHMH", "HMHMHMHMHMHMHMHMHMHMH", "HMHHHMHHHMHHHMHHHMHMH"}};

    const TimeBox timebox(kTimeoutSeconds);
    mss::ObservedBoard::Result board = mss::ObservedBoard::analyze(kRows, kCols, kMines);
    mss::RawGrid<char> mines(kRows, kCols, 0);
    int mineCount = 0;
    for (int row = 0; row < kRows; ++row) {
        mss::assert_(kRowsData[row].size() == kCols, "probability case row width is incorrect");
        for (int col = 0; col < kCols; ++col) {
            const char cell = kRowsData[row][col];
            mss::assert_(cell == 'H' || cell == 'M', "probability case contains an invalid cell");
            if (cell == 'M') {
                mines[row][col] = 1;
                ++mineCount;
            }
        }
    }
    mss::assert_(mineCount == kMines, "probability case mine count is incorrect");

    mss::Workspace workspace;
    mss::Basic::Result basic = mss::Basic::analyze(board, workspace.basic);
    mss::Structure::Pool shapes;
    mss::Structure::Result structure = mss::Structure::analyze(board, basic, shapes, workspace.structure);
    mss::ShapeSolver::DistributionPool distributions;
    mss::Probability::Result probability;
    mss::Probability::analyze(board, basic, structure, shapes, distributions, probability, workspace.probability, mss::ShapeSolver::OrderAlgo::AutoSA);
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
            mss::assert_(mines[row][col] == 0, "probability case opened a mine");
            const mss::ObservedBoard::CellId cell = board.id(row, col);
            const long double clickProbability = probability.mineProbability(cell, board, basic, structure);
            const std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
            int adjacentMines = 0;
            mss::forEachAdjacent(row, col, kRows, kCols, [&](int neighborRow, int neighborCol) {
                adjacentMines += mines[neighborRow][neighborCol];
            });
            updates.clear();
            updates.changes.push_back({cell, (mss::ObservedBoard::CellState)(adjacentMines)});
            mss::ObservedBoard::update(board, updates);
            mss::Basic::update(basic, basicDelta, board, updates, workspace.basic);
            mss::Structure::update(structure, structureDelta, board, basic, shapes, updates, workspace.structure);
            mss::Probability::analyze(board, basic, structure, shapes, distributions, probability, workspace.probability, mss::ShapeSolver::OrderAlgo::AutoSA);
            const double calculationMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stepStart).count();
            mss::assert_(mss::Basic::checkValid(board, basic), "probability case basic result is invalid");
            mss::assert_(probability.candidates() > 0.0L, "probability case has no consistent mine layouts");
            ++totalSteps;
            totalCalculationMilliseconds += calculationMilliseconds;
            timedOut = timebox.expired();
            std::cout << "  step=" << totalSteps << " open=(" << row + 1 << ',' << col + 1 << ") number=" << adjacentMines
                      << " click-risk=" << clickProbability * 100.0L << "% candidates=" << std::setprecision(18)
                      << probability.candidates() << std::setprecision(6)
                      << " t-cell=" << probability.tCellProbability() * 100.0L << "% calc_ms=" << calculationMilliseconds << '\n' << std::flush;
        }
    std::cout << "  steps=" << totalSteps << ", calc_total_ms=" << totalCalculationMilliseconds
              << ", elapsed_ms=" << timebox.elapsedSeconds() * 1000.0
              << ", timeout_s=" << kTimeoutSeconds << ", timed_out=" << std::boolalpha << timedOut << '\n';
    mss::assert_(!timedOut && totalSteps == 55, "probability case did not complete within its time limit");
}

}
