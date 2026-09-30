#pragma once

#include <iomanip>
#include <iostream>

#include "test/common.h"

namespace test {

// 固定时长运行高级盘面，统计 Game 的 LowRisk 胜率和整体吞吐量。
inline void performance() {
    constexpr int kRows = 30;
    constexpr int kCols = 16;
    constexpr int kMines = 99;
    constexpr double kDurationSeconds = 20.0;
    const RunSummary summary = runGames(kRows, kCols, kMines, kDurationSeconds, [](const mss::GameControl::Game &) {}, [](const RunSummary &summary) {
        if (summary.games % 1000 == 0)
            std::cout << "test/performance: completed=" << summary.games << ", wins=" << summary.wins << '\n' << std::flush;
    });
    std::cout << "test/performance: difficulty=Advanced, mode=LowRisk, board=" << kRows << 'x' << kCols << '/' << kMines << ", target_seconds=" << kDurationSeconds << ", games=" << summary.games << ", wins=" << summary.wins << ", losses=" << summary.games - summary.wins << ", win_rate=" << std::fixed << std::setprecision(2) << 100.0 * summary.wins / summary.games << "%, elapsed=" << summary.elapsedSeconds << "s, games_per_second=" << summary.games / summary.elapsedSeconds << '\n';
}

}
