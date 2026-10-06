#pragma once

#include "algo/probability_engine/bruteforce/bruteforce.h"

namespace mss {

// 掩码后端的声明只服务对应实现文件；求解实现保留在 bruteforce_multimask.cpp。
template <typename Mask> struct BruteForce::MultiMaskSolver {
    static void buildSession(const CommonSession &common, Workspace::MultiMask::Layout<Mask> &layout);
    static int javaLiteScore(const CommonSession &common, const Workspace::MultiMask::Session<Mask> &session,
                             const std::vector<ConfigId> &configs, int start, int count, int candidate,
                             Workspace::MultiMask::Worker &workspace);
    static std::vector<int> &orderCandidates(const CommonSession &common, const Workspace::MultiMask::Session<Mask> &session,
                                             const std::vector<ConfigId> &configs, int start, int count, int depth,
                                             Workspace::MultiMask::Worker &workspace);
    template <bool Small>
    static void groupSafeConfigs(const Workspace::MultiMask::Session<Mask> &session,
                                 const std::vector<ConfigId> &configs, int start, int count, const Mask &safeMask,
                                 Workspace::MultiMask::Layer &layer, Workspace::MultiMask::Worker &workspace);
    static int groupByReveal(const Workspace::MultiMask::Session<Mask> &session, const std::vector<ConfigId> &configs,
                             int start, int count, int candidate, std::array<std::vector<ConfigId>, 9> &groups);
    static void writeFirstSafeMove(const CommonSession &common, const Workspace::MultiMask::Session<Mask> &session,
                                   ConfigId config, Result &result);
    template <bool CheckAllMoves, bool IsRoot>
    static int solve(const CommonSession &common, Workspace::MultiMask &multiMask,
                     Workspace::MultiMask::Session<Mask> &session, const std::vector<ConfigId> &configs,
                     int start, int count, int need, int depth, FlatHashTable<U128, int, U128Hash> &table, Result &result,
                     Workspace::MultiMask::Worker &workspace, const BruteForce::Config &config);
};

} // 命名空间 mss
