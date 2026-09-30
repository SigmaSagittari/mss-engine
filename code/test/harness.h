#pragma once

#include "test/bruteforce.h"
#include "test/graph_solver_frequency.h"
#include "test/performance.h"
#include "test/probability_case.h"

namespace test {

inline void harness() {
    // Bruteforce：固定极端残局的单线程和多线程搜索。
    // bruteforce();
    // GraphSolverFrequency：统计随机高级局中 GraphSolver 组件的比例。
    // graphSolverFrequency();
    // Performance：固定时长测量高级 LowRisk 实战吞吐量。
    performance();
    // ProbabilityCase：固定概率盘面的逐步回归。
    // probabilityCase();
}

}
