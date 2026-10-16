#pragma once

#include <iomanip>
#include <iostream>

#include "test/common.h"
#include "test/cases.h"

namespace test::benchmark {

inline void anytimeSearch() {
    constexpr long long nodeBudget = 10000000;
    mss::AnytimeSearch::Config cfg = engineConfig.anytimeSearch; cfg.fullAnalysis = true;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    mss::Workspace workspace;
    mss::GameControl::Position position(beginnerCorners(), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    // 沿主对角线取左上角的 1/8 区域，跳过已揭开的角；完整分析 14 个代表格。
    std::vector<mss::winrate::CellId> cells;
    for (int row = 0; row <= 4; ++row)
        for (int col = row; col <= 4; ++col)
            if (input.board.board[row][col] == mss::ObservedBoard::CellState::Hidden) cells.push_back(input.board.id(row, col));
    mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, cfg, workspace.anytimeSearch, 0, {}, cells);
    const std::chrono::steady_clock::time_point prepared = std::chrono::steady_clock::now();
    // 与 UI 节点统计一致：1000 万节点包括第零层，余下预算一次交给续算。
    mss::AnytimeSearch::update(result, cfg, workspace.anytimeSearch, nodeBudget - result.node, {});
    const std::chrono::steady_clock::time_point finished = std::chrono::steady_clock::now();
    const double preparationSeconds = std::chrono::duration<double>(prepared - start).count();
    const double searchSeconds = std::chrono::duration<double>(finished - prepared).count();
    const double elapsed = std::chrono::duration<double>(finished - start).count();
    const std::pair<int, int> best = input.board.pos(result.recommended());
    std::cout << std::fixed << std::setprecision(6) << "benchmark/anytime_search: board=9x9/10 corners=1 roots=" << result.candidates.size() << " widths=" << cfg.width[0] << ',' << cfg.width[1] << ',' << cfg.width[2] << " budget=" << nodeBudget << " nodes=" << result.node << " depth=" << result.completedDepth << " finished=" << result.finished << " recommended=(" << best.first + 1 << ',' << best.second + 1 << ") prepare_seconds=" << preparationSeconds << " search_seconds=" << searchSeconds << " total_seconds=" << elapsed << " nodes/s=" << result.node / elapsed << '\n';
    for (const mss::AnytimeSearch::Candidate &candidate : result.candidates) {
        const std::pair<int, int> cell = input.board.pos(candidate.cellId);
        std::cout << "  cell=(" << cell.first + 1 << ',' << cell.second + 1 << ")";
        for (int depth = 0; depth < (int)candidate.value.size(); ++depth) std::cout << " depth" << depth << '=' << std::setprecision(12) << candidate.value[depth];
        std::cout << '\n';
    }
}

}
