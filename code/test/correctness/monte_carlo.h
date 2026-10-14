#pragma once

#include "test/common.h"
#include "test/cases.h"
#include "core/assert.h"

namespace test::correctness {

inline void monteCarlo() {
    mss::Workspace workspace;
    mss::GameControl::Position position(beginnerCorners(), workspace);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(input.probability.candidates() == 6690509280.0L, "MonteCarlo 固定根盘面的布局数变化");
    std::vector<mss::ObservedBoard::CellId> targets;
    for (int row = 0; row <= 4; ++row)
        for (int col = row; col <= 4; ++col)
            if (input.board.board[row][col] == mss::ObservedBoard::CellState::Hidden) targets.push_back(input.board.id(row, col));
    mss::MonteCarlo::Result result = mss::MonteCarlo::analyze(input, mss::winrate::SuggestAlgo::JavaEval, engineConfig, workspace.monteCarlo, targets);
    mss::assert_(!result.winrate && result.candidates.size() == 14, "MonteCarlo 应比较 14 个首招");
    std::stop_source stopped;
    stopped.request_stop();
    mss::assert_(!mss::MonteCarlo::update(result, input, engineConfig, workspace.monteCarlo, stopped.get_token()) && result.samples() == 0, "MonteCarlo 取消请求计入了样本");
    for (int sample = 1; sample <= 16; ++sample) {
        mss::assert_(mss::MonteCarlo::update(result, input, engineConfig, workspace.monteCarlo), "MonteCarlo 未取消的采样没有完成");
        long long samples = 0, wins = 0;
        for (const mss::MonteCarlo::Candidate &candidate : result.candidates) {
            mss::assert_(candidate.wins >= 0 && candidate.wins <= candidate.samples, "MonteCarlo 候选胜负计数非法");
            samples += candidate.samples;
            wins += candidate.wins;
        }
        mss::assert_(samples == sample && samples == result.samples() && wins == result.wins(), "MonteCarlo 完整样本统计不一致");
        mss::assert_(mss::Basic::checkValid(input.board, input.basic) && position.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 6690509280.0L, "MonteCarlo 根盘面未恢复");
    }
}

}
