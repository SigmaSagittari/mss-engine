#pragma once

#include "algo/probability_engine/bruteforce/bruteforce.h"

namespace mss {

struct BruteForce::NormalSolver {
    template <bool CheckAllMoves, bool IsRoot>
    static int solve(const CommonSession &common, Workspace::Normal &workspace, const std::vector<ConfigId> &configs,
                     int start, int count, int need, int depth, Result &result);
};

} // 命名空间 mss
