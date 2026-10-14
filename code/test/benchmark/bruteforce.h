#pragma once

#include <iomanip>
#include <iostream>
#include "common.h"
#include "test/boards.h"

namespace test::benchmark {

inline void bruteforce() {
    for (int independent = 0; independent < 2; ++independent)
        for (int parallel = 0; parallel < 2; ++parallel) {
            mss::Workspace workspace;
            mss::GameControl::Position position(bruteForceBoard(independent), workspace);
            const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
            const mss::BruteForce::Config cfg(false, parallel ? mss::BruteForce::Config::Solver::BitwiseRootParallel : mss::BruteForce::Config::Solver::Bitwise, engineConfig.bruteForce.candidateThreshold, engineConfig.bruteForce.threadLimit);
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const mss::BruteForce::Result result = mss::BruteForce::solve(input.board, input.basic, input.structure, input.shapes, cfg, workspace.bruteForce);
            const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "benchmark/bruteforce: parallel=" << parallel << " independent=" << independent << " thread_limit=" << cfg.threadLimit << " layouts=" << input.probability.candidates() << " nodes=" << result.nodes << " search_ms=" << std::fixed << std::setprecision(3) << milliseconds << '\n';
        }
}

}
