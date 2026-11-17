#pragma once

#include <cstdio>

#include "algo/ioe_engine/bruteforce/bruteforce.h"
#include "core/assert.h"

namespace test::benchmark {

struct IoeRoot {
    void run() {
        using Solver = mss::IoeBruteForce;
        constexpr int side = 9;
        constexpr int mines = 10;
        constexpr int clicks = 1;
        constexpr char cells[] =
            "?20000000"
            "?21110111"
            "????101??"
            "????211??"
            "?????????"
            "?????????"
            "?????????"
            "?????????"
            "?????????";
        static_assert(sizeof(cells) == side * side + 1);

        mss::ObservedBoard::Result board(side, side, mines);
        for (int i = 0; i < side * side; ++i)
            if (cells[i] != '?') board.cells[i / side][i % side] = (mss::ObservedBoard::CellState)(cells[i] - '0');
        const mss::Grid<bool> flags(side, side, false);

        const Solver::RootResult root = Solver().analyzeRoot(board, flags, clicks, 1086008);
        mss::assert_(root.status == Solver::Status::Exact, "IOE 根部未完成");
        std::printf("候选摆法：%.0Lf\n可达 200%% 的摆法：%zu\n根部 DOM 调用：%zu\n", root.candidates, root.eligible, root.domCalls);
    }
};

} // 命名空间 test::benchmark
