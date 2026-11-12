#pragma once

#include "test/benchmark/performance.h"

namespace test::cases {

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
