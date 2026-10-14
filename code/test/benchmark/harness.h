#pragma once

#include "performance.h"
#include "graph_solver_frequency.h"
#include "monte_carlo.h"
#include "bruteforce.h"

namespace test::benchmark {

inline void harness() {
    performance(config, true);
    // graphSolverFrequency(config);
    // monteCarlo();
    // bruteforce();
}

}
