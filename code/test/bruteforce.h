#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#include "common.h"
#include "core/assert.h"
#include "core/config.h"
#include "algo/probability_engine/bruteforce/bruteforce.h"
#include "core/workspace.h"

namespace test {

inline void bruteforce() {
    auto runBruteForceFixedEndgame = [&](const char *mode, const mss::BruteForce::Config &config, bool independent) {
        constexpr int kRows = 30;
        constexpr int kCols = 16;
        constexpr int kMines = 99;
        // 固定残局沿用 1.4；合法布局数应为 134550，最佳单步胜局数应为 83553。
        constexpr std::array<const char *, kRows> kBoardRows{
            {"00012?211111012?", "0001?33?11?101??", "112222?33321013?", "2?2?1123??11223?", "?332101?4321??3?", "12?100112?11222?",
             "0111000011100011", "111000000011112?", "1?210011113?33??", "12?2122?22???6??", "0112?3?33?45????", "00024?5?32?224??",
             "0001??4?32222332", "110122213?32??10", "?11110113??22210", "223?201?22221111", "1?3?20112112?21?", "112110001?12?211",
             "0122111122322111", "12??11?22?4?201?", "?333112?44??2011", "23?21012??332211", "?23?21122211?2?1", "222?32?211232211",
             "?223??3?11??2111", "?3?????22233?11?", "???????22?211111", "???????33?311011", "????????323?101?", "????????21?21011"}};

        mss::ObservedBoard::Result board(kRows + (independent ? 4 : 0), kCols, kMines + independent);
        if (independent) {
            // 新区域以 ForcedSafe 隔开；新增两格恰有一雷，点击任一安全格只揭示 1，不提供原残局的信息。
            for (int row = kRows; row < kRows + 4; ++row)
                for (int col = 0; col < kCols; ++col)
                    board.board[row][col] = mss::ObservedBoard::CellState::ForcedSafe;
            board.board[kRows + 2][0] = mss::ObservedBoard::CellState::Hidden;
            board.board[kRows + 2][1] = mss::ObservedBoard::CellState::Hidden;
            board.board[kRows + 3][0] = mss::ObservedBoard::CellState::Num1;
        }
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col) {
                const char cell = kBoardRows[row][col];
                board.board[row][col] = cell == '?' ? mss::ObservedBoard::CellState::Hidden : (mss::ObservedBoard::CellState)(cell - '0');
            }

        mss::Workspace analysisWorkspace;
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
        const std::chrono::steady_clock::time_point logicStarted = std::chrono::steady_clock::now();
        const long double logicWays = mss::Probability::analyzeLogicComponents(logicComponents, board.totalMines - basic.mineSum, analysisWorkspace.probability, [&](int component, int, long double, long double componentProbability) { componentProbabilities[component] += componentProbability; });
        const double logicMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - logicStarted).count();
        mss::assert_(std::fabs(logicWays - probability.candidates()) < 1e-12L, "test::runBruteForceFixedEndgame: logicComponent API 布局数不一致");
        for (int component = 0; component < (int)componentProbabilities.size(); ++component)
            mss::assert_(std::fabs(componentProbabilities[component] - 1.0L) < 1e-12L, "test::runBruteForceFixedEndgame: logicComponent API 概率未归一化");

        mss::Workspace solverWorkspace;
        std::cout << "test/bruteforce: mode=" << mode << ", thread_limit=" << config.threadLimit << ", phase=固定盘面搜索\n" << std::flush;
        const std::chrono::steady_clock::time_point searchStarted = std::chrono::steady_clock::now();
        const mss::BruteForce::Result result = mss::BruteForce::solve(board, basic, structure, shapes, config, solverWorkspace.bruteForce);
        const double searchMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - searchStarted).count();
        mss::assert_(!result.moves.empty() && result.moves[0].wins == 83553, "test::runBruteForceFixedEndgame: 全盘搜索胜局数变化");
        std::cout << "test/bruteforce: mode=" << mode << ", thread_limit=" << config.threadLimit << ", independent=" << independent << ", layouts=" << probability.candidates() << ", wins=" << result.moves[0].wins << ", nodes=" << result.nodes << ", search_ms=" << std::fixed << std::setprecision(3) << searchMilliseconds << ", logic_component_api_ms=" << logicMilliseconds << '\n';
    };
    const mss::BruteForce::Config single(false, mss::BruteForce::Config::Solver::Bitwise, engineConfig.bruteForce.candidateThreshold, engineConfig.bruteForce.threadLimit);
    const mss::BruteForce::Config multi(false, mss::BruteForce::Config::Solver::BitwiseRootParallel, engineConfig.bruteForce.candidateThreshold, engineConfig.bruteForce.threadLimit);
    runBruteForceFixedEndgame("single", single, false);
    runBruteForceFixedEndgame("multi", multi, false);
    runBruteForceFixedEndgame("single", single, true);
    runBruteForceFixedEndgame("multi", multi, true);
}

}
