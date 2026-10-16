#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "test/common.h"
#include "test/cases.h"
#include "core/assert.h"
#include "core/config.h"
#include "algo/probability_engine/bruteforce/bruteforce.h"
#include "core/workspace.h"

namespace test::correctness {

inline void bruteforce() {
    auto runBruteForceFixedEndgame = [&](const mss::BruteForce::Config &config, bool independent) {
        mss::ObservedBoard::Result board = endgame134550(independent);

        mss::GameControl::Workspace analysisWorkspace;
        const mss::Basic::Result basic = mss::Basic::analyze(board, analysisWorkspace.basic);
        mss::Structure::Pool shapes;
        const mss::Structure::Result structure = mss::Structure::analyze(board, basic, shapes, analysisWorkspace.structure);
        mss::ShapeSolver::DistributionPool distributions;
        const mss::Probability::Result probability = mss::Probability::analyze(board, basic, structure, shapes, distributions, analysisWorkspace.probability);
        mss::assert_(probability.candidates() == 134550.0L * (independent ? 2 : 1), "test::runBruteForceFixedEndgame: 固定盘面的布局数变化");

        std::vector<mss::ShapeSolver::DistributionId> distributionIds;
        for (int component = 0; component < (int)structure.components.size(); ++component) {
            const mss::Structure::Instance &instance = shapes.getInstance(structure.components[component]);
            distributionIds.push_back(mss::ShapeSolver::analyze(shapes.getShape(instance.shape), shapes, distributions, analysisWorkspace.shapeSolver));
        }
        std::vector<long double> unitWays{1.0L};
        std::vector<mss::Probability::LogicComponentInput> logicComponents;
        // Structure 组件分别作为前沿因子，全部 T 格作为一个无前沿因子，覆盖全盘候选布局。
        for (int component = 0; component < (int)distributionIds.size(); ++component) {
            const mss::ShapeSolver::DistributionResult &distribution = distributions.get(distributionIds[component]);
            logicComponents.push_back({mss::Poly(distribution.start(), distribution.ways()), 0});
        }
        if (basic.unknownSum > 0)
            logicComponents.push_back({mss::Poly(0, unitWays), basic.unknownSum});
        std::vector<long double> componentProbabilities(logicComponents.size(), 0.0L);
        const long double logicWays = mss::Probability::analyzeLogicComponents(logicComponents, board.totalMines - basic.mineSum, analysisWorkspace.probability, [&](int component, int, long double, long double componentProbability) { componentProbabilities[component] += componentProbability; });
        mss::assert_(std::fabs(logicWays - probability.candidates()) < 1e-12L, "test::runBruteForceFixedEndgame: logicComponent API 布局数不一致");
        for (int component = 0; component < (int)componentProbabilities.size(); ++component)
            mss::assert_(std::fabs(componentProbabilities[component] - 1.0L) < 1e-12L, "test::runBruteForceFixedEndgame: logicComponent API 概率未归一化");

        mss::GameControl::Workspace solverWorkspace;
        const mss::BruteForce::Result result = mss::BruteForce::solve(board, basic, structure, shapes, config, solverWorkspace.bruteForce);
        mss::assert_(!result.moves.empty() && result.moves[0].wins == 83553, "test::runBruteForceFixedEndgame: 全盘搜索胜局数变化");
    };
    const mss::BruteForce::Config single(false, mss::BruteForce::Config::Solver::Bitwise, engineConfig.game.bruteForce.candidateThreshold, engineConfig.game.bruteForce.threadLimit);
    const mss::BruteForce::Config multi(false, mss::BruteForce::Config::Solver::BitwiseRootParallel, engineConfig.game.bruteForce.candidateThreshold, engineConfig.game.bruteForce.threadLimit);
    runBruteForceFixedEndgame(single, false);
    runBruteForceFixedEndgame(multi, false);
    runBruteForceFixedEndgame(single, true);
    runBruteForceFixedEndgame(multi, true);
}

}
