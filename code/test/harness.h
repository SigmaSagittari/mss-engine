#pragma once

#include "performance.h"
#include "bruteforce.h"
#include "probability_case.h"
#include "graph_solver_frequency.h"
#include "monte_carlo.h"

namespace test {

// time 单位为秒；时限与对局数上限均须为有限正数，先达到任一限制就停止。
inline constexpr Config config{30, 16, 99, 20.0L, 1000000, mss::GameControl::Game::SuggestAlgo::LowRisk};
static_assert(config.time > 0.0L && config.games > 0, "测试时限与对局数上限必须均为正数");

inline void harness() {
    bruteforce();
    // performance(config);
    // performance(config, true);
    // graphSolverFrequency(config);
    // probabilityCase();
    // monteCarlo();
}

}
