#pragma once

#include "algo/probability_engine/bruteforce/bruteforce_multimask.h"

namespace mss {

// 根并行调度与递归求解分开编译；递归仍复用 MultiMaskSolver 的掩码实现。
template <typename Mask> struct BruteForce::MultiMaskParallelSolver {
    template <bool CheckAllMoves>
    static int solveCandidates(const Workspace::CommonSession &common, Workspace::MultiMask &multiMask,
                               Workspace::MultiMask::Session<Mask> &session, const std::vector<ConfigId> &configs,
                               int start, int count, int need, Result &result,
                               Workspace::MultiMask::Worker &workspace);
};

} // 命名空间 mss
