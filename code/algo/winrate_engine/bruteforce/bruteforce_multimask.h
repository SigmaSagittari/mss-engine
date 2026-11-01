#pragma once

#include <cstdint>
#include <deque>

#include "algo/winrate_engine/bruteforce/bruteforce_type.h"
#include "core/datastructure/bit_mask.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/vector_pool.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"

namespace mss {

// 定宽掩码后端：候选格和雷位压进 bitMask，候选数放不下时由上层的普通后端处理。
struct BruteForceMultimask {
    struct Workspace;
    // 一组方案是 safeGroupConfigs 池里的切片。
    using SafeGroup = vectorPool<BruteForceType::ConfigId>::vector;

    struct RevealGroup;

    struct Layer;

    struct Scratch;

    struct Worker;
    struct CacheShard;
    struct ParallelCache;

    // 与普通后端输入输出相同；根据候选格数选择可容纳它们的固定掩码位宽。
    void solve(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result);

  private:
    Workspace &getWorkspace();
    template <typename Mask> vector<Mask> &maskStorage();
    template <typename Mask> void solveWidth(const BruteForceType::Session &common, const BruteForceType::Config &config, BruteForceType::Result &result, bool allowParallel);
    template <typename Mask> void solveWidthParallel(const BruteForceType::Session &common, const BruteForceType::Config &config, BruteForceType::Result &result);
};

} // 命名空间 mss
