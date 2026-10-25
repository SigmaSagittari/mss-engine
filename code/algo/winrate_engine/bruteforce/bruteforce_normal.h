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
    // 一组方案是 safeGroupConfigs 池里的切片。
    using SafeGroup = vectorPool<BruteForceType::ConfigId>::vector;

    // 按揭示值分桶时，一个桶在 groups 里的下标和方案数。
    struct RevealGroup {
        int reveal;
        int count;
    };

    struct Layer {
        // 当前局面各候选的踩雷布局数、必安候选及尝试顺序。
        vector<int> deaths;
        vector<int> safeCells;
        vector<int> order;
        vector<BruteForceType::ConfigId> groups[9]; // 按揭出的数字 0..8 分桶，每项是完整雷盘下标。
        vector<RevealGroup> groupList;
        // 同时揭开必安格时，按它们显示的数字组合给布局分组。
        vector<U128> safeHashes;
        vector<int> safeGroupIds;
        vector<int> safeGroupSizes;
        // 当前递归帧所有安全组的 ConfigId 拼接；每次分组前清空，切片只在本帧内有效。
        vectorPool<BruteForceType::ConfigId> safeGroupConfigs;
        // 每个安全组是 safeGroupConfigs 的切片，按组大小降序处理。
        vector<SafeGroup> safeGroupList;
    };

    struct Workspace {
        Layer &layer(int depth);
        void reset();

        Grid<std::uint8_t> mineByConfig; // [完整布局][候选] 是否为雷。
        Grid<std::uint8_t> revealByConfig; // [完整布局][候选] 揭开后显示的数字。
        DynamicBitset unopenedStorage;
        vector<BruteForceType::ConfigId> configIds;
        FlatHashTable<U128, int, U128Hash> safeGroupTable;
        // 仅保存空表的存储容量；有效缓存由一次 solve 的状态对象持有。
        FlatHashTable<U128, int, U128Hash> cacheStorage;
        // 递归时持有 Layer&，用 deque 保证新增层不搬动已有层。
        std::deque<Layer> layers;
    };

    // session 是 BruteForce 构建的完整布局表；返回原始赢局数，由上层归一化为胜率。
    static void solve(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result, Workspace &workspace);
};

} // 命名空间 mss
