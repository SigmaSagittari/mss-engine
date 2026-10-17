#pragma once

#include "correctness/anytime_search.h"
#include "correctness/bruteforce.h"
#include "correctness/probability_case.h"
#include "correctness/monte_carlo.h"
#include "benchmark/anytime_search.h"
#include "benchmark/performance.h"
#include "benchmark/graph_solver_frequency.h"
#include "benchmark/monte_carlo.h"
#include "benchmark/bruteforce.h"

namespace test {

inline void harness() {
    // 正确性检查
    correctness::probabilityCase();
    correctness::anytimeSearch();
    correctness::bruteforce();
    correctness::monteCarlo();

    // 基准测试
    benchmark::anytimeSearch();
    // benchmark::performance(config, true);
    // benchmark::graphSolverFrequency(config);
    // benchmark::monteCarlo();
    // benchmark::bruteforce();
}

}
