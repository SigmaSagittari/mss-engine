#pragma once

#include "test/cases.h"
#include "test/benchmark/performance.h"

namespace test {

inline void harness() {
    benchmark::performance(config, policy);
}

}
