#pragma once

#include "test/common.h"
#include "test/cases.h"
#include "core/assert.h"

namespace test::correctness {

inline void monteCarlo() {
    mss::Workspace workspace;
    mss::GameControl::Position position(beginnerCorners(), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(input.probability.candidates() == 6690509280.0L, "MonteCarlo 固定根盘面的布局数变化");
    std::vector<mss::ObservedBoard::CellId> targets;
    for (int row = 0; row <= 4; ++row)
        for (int col = row; col <= 4; ++col)
            if (input.board.board[row][col] == mss::ObservedBoard::CellState::Hidden) targets.push_back(input.board.id(row, col));
    mss::MonteCarlo::Result result = mss::MonteCarlo::analyze(input, mss::winrate::SuggestAlgo::JavaEval, engineConfig.monteCarlo, workspace.monteCarlo, targets);
    mss::assert_(!result.winrate && result.candidates.size() == 14, "MonteCarlo 应比较 14 个首招");
    std::stop_source stopped;
    stopped.request_stop();
    mss::assert_(!mss::MonteCarlo::update(result, input, engineConfig.monteCarlo, workspace.monteCarlo, stopped.get_token()) && result.samples() == 0, "MonteCarlo 取消请求计入了样本");
    for (int sample = 1; sample <= 16; ++sample) {
        mss::assert_(mss::MonteCarlo::update(result, input, engineConfig.monteCarlo, workspace.monteCarlo), "MonteCarlo 未取消的采样没有完成");
        long long samples = 0, wins = 0;
        for (const mss::MonteCarlo::Candidate &candidate : result.candidates) {
            mss::assert_(candidate.wins >= 0 && candidate.wins <= candidate.samples, "MonteCarlo 候选胜负计数非法");
            samples += candidate.samples;
            wins += candidate.wins;
        }
        mss::assert_(samples == sample && samples == result.samples() && wins == result.wins(), "MonteCarlo 完整样本统计不一致");
        mss::assert_(mss::Basic::checkValid(input.board, input.basic) && position.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 6690509280.0L, "MonteCarlo 根盘面未恢复");
    }
    mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, engineConfig.game.javaEval, workspace.game.javaEval);
    std::stable_sort(evaluated.begin(), evaluated.end(), [](const mss::JavaEval::Analysis &lhs, const mss::JavaEval::Analysis &rhs) { return lhs.value > rhs.value; });
    targets.clear();
    for (const mss::JavaEval::Analysis &candidate : evaluated) targets.push_back(candidate.cellId);
    mss::MonteCarlo::Result automatic = mss::MonteCarlo::analyze(input, mss::winrate::SuggestAlgo::JavaEval, engineConfig.monteCarlo, workspace.monteCarlo);
    mss::MonteCarlo::Result explicitTargets = mss::MonteCarlo::analyze(input, mss::winrate::SuggestAlgo::JavaEval, engineConfig.monteCarlo, workspace.monteCarlo, targets);
    for (int sample = 1; sample <= 16; ++sample) {
        mss::MonteCarlo::Workspace fresh;
        mss::MonteCarlo::update(automatic, input, engineConfig.monteCarlo, workspace.monteCarlo);
        mss::MonteCarlo::update(explicitTargets, input, engineConfig.monteCarlo, fresh);
        mss::assert_(automatic.samples() == sample && automatic.wins() == explicitTargets.wins() && automatic.recommended() == explicitTargets.recommended(), "MonteCarlo 自动候选或工作区历史改变采样");
        if (sample == 1) mss::assert_(automatic.candidates.size() == 1, "MonteCarlo 首局尚未保留推荐首招");
        else {
            mss::assert_(automatic.candidates.size() == targets.size(), "MonteCarlo 没有发布完整候选");
            for (int i = 0; i < (int)targets.size(); ++i) mss::assert_(automatic.candidates[i].cellId == targets[i] && automatic.candidates[i].samples == explicitTargets.candidates[i].samples && automatic.candidates[i].wins == explicitTargets.candidates[i].wins, "MonteCarlo 自动与显式首招序列不一致");
        }
    }
    std::cout << "test/monte_carlo: explicit=14 samples=16 automatic=" << targets.size() << " replay_samples=16 fresh_workspace=true\n";
}

}
