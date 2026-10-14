#pragma once

#include "performance.h"
#include "bruteforce.h"
#include "probability_case.h"
#include "graph_solver_frequency.h"
#include "monte_carlo.h"
#include "anytime_search.h"

namespace test {

inline void harness() {
    // anytimeSearch();
    // bruteforce();
    performance(config, true);
    // graphSolverFrequency(config);
    // probabilityCase();
    // monteCarlo();
}

}
