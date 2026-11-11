#pragma once

#include "test/benchmark/performance.h"

namespace test::cases {

inline void lowRiskPerformance() {
    const mss::ShapeSolver::Config shapeConfig{
        .graphThreshold = 24,
        .order = mss::ShapeSolverConfig::Order::Auto,
        .window3Threshold = 24,
        .sa = {
            .rounds = 4,
            .iterations = 1000,
            .startTemperature = 4.0,
            .endTemperature = 0.01
        }
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
    const mss::MovePolicy::Config::Brute bruteConfig{.limit = 0, .config = nullptr};
    const mss::MovePolicy::Config lowRiskConfig{
        .algorithm = mss::MovePolicy::Algorithm::LowRisk,
        .selection = nullptr,
        .fiftyFifty = nullptr,
        .javaEval = nullptr,
        .anytime = nullptr,
        .brute = bruteConfig
    };
    benchmark::Performance().run(benchmarkConfig, lowRiskConfig, [](const mss::Game &) {});
}

} // 命名空间 test::cases
