#pragma once

#include <cstdio>

#include "algo/ioe_engine/bruteforce/moves.h"
#include "test/benchmark/performance.h"

namespace test::cases {

inline void ioeGroupOrder() {
    using namespace mss;
    // WR3 实盘根筛的 7 张雷图有 9 个共同雷位。第 10 个雷位给出下面的雷图编号。
    constexpr char visible[] =
        "00000001?" "110111011" "?311?1000" "?????2110" "???????10"
        "??????110" "??????100" "??????200" "??????200";
    constexpr int common[] = {8, 18, 22, 27, 28, 42, 48, 68, 77};
    constexpr int last[] = {36, 45, 55, 56, 65, 75, 76};
    constexpr int order[] = {4, 1, 6, 0, 2, 5, 3};
    IoeView view{};
    for (int cell = 0; cell < IoeBoard::cells; ++cell)
        if (visible[cell] != '?') view.revealed.set(cell);
    vector<IoeStrategyBoard> boards;
    for (int mine : last) {
        IoeStrategyBoard board{};
        for (int cell : common) board.mines.set(cell);
        board.mines.set(mine);
        for (int cell = 0; cell < IoeBoard::cells; ++cell) {
            board.digits[cell] = board.mines.test(cell) ? 9 : (board.mines & IoeBoard().neighbors()[cell]).popcount();
            if (!board.digits[cell]) board.zeros.set(cell);
        }
        boards.push_back(board);
    }

    IoeMoves moves;
    IoeMoves::Result result;
    for (int count : {3, 7}) {
        // 开 36 有踩雷和同格不同数字，开 46 还会遇到几种不同的零区展开。
        for (int cell : {36, 46}) {
            const IoeMove move{IoeMove::Kind::Open, cell};
            IoeCells shown[7];
            bool safe[7]{}, seen[7]{};
            for (int i = 0; i < count; ++i) safe[order[i]] = moves.play(move, view, boards[order[i]], shown[order[i]]);
            moves.group(move, view, boards, {order, count}, result);
            auto check = [&](bool ok, int expected, int actual) {
                if (ok) return;
                std::fprintf(stderr, "WR3 %s, prefix=%d, open=%d, expected=%d, actual=%d\n", visible, count, cell, expected, actual);
                assert_(false, "IOE 扩批后的观测分组或组内顺序错误");
            };
            for (std::size_t g = 0; g < result.groups.size; ++g) {
                const IoeMoves::Group &group = result.groups[g];
                check(group.layouts.count > 0, 1, 0);
                const auto ids = result.layouts.span(group.layouts);
                const int first = ids[0];
                check(first >= 0 && first < 7 && safe[first], 1, first);
                check(group.revealed == shown[first], 1, 0);
                std::size_t n = 0;
                for (int i = 0; i < count; ++i) {
                    const int id = order[i];
                    if (!safe[id] || shown[id] != group.revealed) continue;
                    bool same = true;
                    shown[id].forEachSetBit([&](int c) { same &= boards[id].digits[c] == boards[first].digits[c]; });
                    if (!same) continue;
                    const int actual = n < group.layouts.count ? ids[n] : -1;
                    check(actual == id && !seen[id], id, actual);
                    seen[id] = true;
                    ++n;
                }
                check(n == group.layouts.count, (int)n, (int)group.layouts.count);
            }
            for (int id : order) check(safe[id] == seen[id], safe[id], seen[id]);
        }
    }
    std::puts("IOE WR3: group order passed (3/7 layouts, open 36/46)");
}

inline void lowRiskPerformance() {
    const mss::ShapeSolver::Config shapeConfig{
        .graphThreshold = 24,
        .order = mss::ShapeSolverConfig::Order::Auto,
        .window3Threshold = 24,
        .sa = nullptr
    };
    const Config benchmarkConfig{
        .cols = 30,
        .rows = 16,
        .mines = 99,
        .maxGames = 10000,
        .seconds = 45.0L,
        .seed = {0x123456789abcdef0ULL, 0xfedcba9876543210ULL},
        .position = {shapeConfig}
    };
    const mss::MovePolicy::Config lowRiskConfig{
        .algorithm = mss::MovePolicy::Algorithm::LowRisk,
        .guess = nullptr,
        .brute = nullptr
    };
    benchmark::Performance().run(benchmarkConfig, lowRiskConfig, [](const mss::Game &) {});
}

} // 命名空间 test::cases
