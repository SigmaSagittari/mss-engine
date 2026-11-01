#pragma once

#include <deque>

#include "algo/winrate_engine/bruteforce/bruteforce_type.h"
#include "core/datastructure/dynamic_bitset.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/vector_pool.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"

namespace mss {

// 普通递归后端：预展开方案×候选表，按揭示值分组递归。
struct BruteForceNormal {
    struct Workspace;
    // 一组方案是 safeGroupConfigs 池里的切片。
    using SafeGroup = vectorPool<BruteForceType::ConfigId>::vector;

    struct RevealGroup;

    struct Layer;

    // session 是 BruteForce 构建的完整布局表；返回原始赢局数，由上层归一化为胜率。
    void solve(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result);

  private:
    friend struct NormalSolver;
    Workspace &getWorkspace();
};

} // 命名空间 mss
