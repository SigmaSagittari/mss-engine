#pragma once

#include "anytime_search.h"
#include "bruteforce.h"
#include "probability_case.h"
#include "monte_carlo.h"

namespace test::correctness {

inline void harness() {
    anytimeSearch();
    bruteforce();
    probabilityCase();
    monteCarlo();
}

}
