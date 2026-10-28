#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

#include "algo/winrate_engine/bruteforce/bruteforce_type.h"
#include "core/datastructure/bit_mask.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/vector_pool.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"

namespace mss {

// 定宽掩码后端：候选格和雷位压进 bitMask，候选数放不下时由上层的普通后端处理。
struct BruteForceMultimask {
    // 一组方案是 safeGroupConfigs 池里的切片。
    using SafeGroup = vectorPool<BruteForceType::ConfigId>::vector;

    // 按揭示值分桶时，一个桶在 groups 里的下标和方案数。
    struct RevealGroup {
        int reveal;
        int count;
    };

    struct Layer {
        // 当前候选的踩雷布局数、尝试顺序，以及按揭出数字分桶后的布局列表。
        vector<int> deaths;
        vector<int> order;
        vector<BruteForceType::ConfigId> groups[9];
        vector<RevealGroup> groupList;
        vector<int> safeGroupIds;
        vector<int> safeGroupSizes;
        // 当前递归帧所有安全组的 ConfigId 拼接；每次分组前清空，切片只在本帧内有效。
        vectorPool<BruteForceType::ConfigId> safeGroupConfigs;
        // 每个安全组是 safeGroupConfigs 的切片，按组大小降序处理。
        vector<SafeGroup> safeGroupList;
    };

    // 一个线程的递归临时内存；每个深度一份 Layer，缓存内容只在本次 solve 有效。
    struct Scratch {
        Scratch();
        Layer &layer(int depth);
        void reset();

        // 递归时持有 Layer&，用 deque 保证新增层不搬动已有层。
        std::deque<Layer> layers;
        FlatHashTable<U128, int, U128Hash> safeGroupTable;
        FlatHashTable<U128, int, U128Hash> cacheStorage;
        // Java-lite 排序的固定容量计数，不随递归深度分配。
        int javaLiteMineCounts[9][bitMask<8>::kBitCount];
        int javaLiteGroupSizes[9];
        int smallGroupCounts[9 * 9 * 9];
        vector<int> smallGroupKeys;
        vector<int> smallGroupConfigKeys;
    };

    struct ParallelCache;

    // 本次 solve 的工作状态；cache 引用 Scratch，根并行时可接入共享缓存。
    struct Worker {
        Scratch &scratch;
        // 引用的缓存由 Scratch 持有，Worker 不接管其生命周期。
        FlatHashTable<U128, int, U128Hash> &cache;
        // 非空表示当前参与根并行，count 足够大的子问题走共享缓存。
        ParallelCache *sharedCache;

        Worker(Scratch &scratch, ParallelCache *sharedCache);
        ~Worker();
    };

    struct CacheShard {
        std::mutex mutex;
    };

    // 根并行线程共享的缓存；按键分片加锁，避免所有子问题争用同一把锁。
    struct ParallelCache {
        CacheShard shards[256];
        FlatHashTable<U128, int, U128Hash> (&storage)[256];

        explicit ParallelCache(FlatHashTable<U128, int, U128Hash> (&storage)[256]) : storage(storage) {}
        void reset();
        int find(const U128 &key);
        void store(const U128 &key, int value);
    };

    struct Workspace {
        // 工作线程临时区只增不减，保留各线程的分组容量；Worker 的状态只在 solve 内存活。
        std::deque<Scratch> workers;
        FlatHashTable<U128, int, U128Hash> sharedStorage[256];
        vector<bitMask<1>> mineMasks1;
        vector<bitMask<2>> mineMasks2;
        vector<bitMask<4>> mineMasks4;
        vector<bitMask<8>> mineMasks8;
        // 不同掩码位宽共用二维表的存储容量。
        Grid<std::uint64_t> mineByteWords;
        Grid<std::uint8_t> reveals;
        vector<BruteForceType::ConfigId> configIds;
        // 根并行时的候选顺序和各 worker 的上界、节点数。
        vector<int> order;
        vector<int> deaths;
        vector<int> uppers;
        vector<long long> nodes;
    };

    // 与普通后端输入输出相同；根据候选格数选择可容纳它们的固定掩码位宽。
    static void solve(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result, Workspace &workspace);
};

} // 命名空间 mss
