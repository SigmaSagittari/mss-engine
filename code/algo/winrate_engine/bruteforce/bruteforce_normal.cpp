#include "algo/winrate_engine/bruteforce/bruteforce_normal.h"

#include <algorithm>

namespace mss {

// 按揭示值分桶时，一个桶在 groups 里的下标和方案数。
struct BruteForceNormal::RevealGroup {
    int reveal;
    int count;
};

struct BruteForceNormal::Layer {
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

struct BruteForceNormal::Workspace {
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

__declspec(noinline) BruteForceNormal::Workspace &BruteForceNormal::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

struct NormalSolver {
    U128 hashConfigs(const vector<std::uint64_t> &hashes, const BruteForceType::ConfigId *configs, int start, int count) {
        U128 hash{(std::uint64_t)count, (std::uint64_t)count};
        for (int i = start; i < start + count; ++i) {
            const std::uint64_t value = hashes.data[configs[i]];
            hash.lo += value;
            hash.hi ^= value;
        }
        hash.lo = splitmix64(hash.lo);
        return hash;
    }

    void saveFail(const U128 &key, int upper, int count, FlatHashTable<U128, int, U128Hash> &table) {
        // 负值编码“最多可能赢 upper 局”；等于方案总数的上界没有剪枝价值。
        if (upper <= 0 || upper >= count)
            return;
        int *old = table.find(key);
        if (old == nullptr) {
            table[key] = -upper;
            return;
        }
        if (*old < 0)
            *old = (std::max)(*old, -upper);
    }

    struct State {
        Grid<std::uint8_t> &mineByConfig;
        Grid<std::uint8_t> &revealByConfig;
        DynamicBitset &unopenedCandidates;
        FlatHashTable<U128, int, U128Hash> &cache;
        long long nodes;

        State(Grid<std::uint8_t> &mineByConfig, Grid<std::uint8_t> &revealByConfig, DynamicBitset &unopenedCandidates, FlatHashTable<U128, int, U128Hash> &cache) : mineByConfig(mineByConfig), revealByConfig(revealByConfig), unopenedCandidates(unopenedCandidates), cache(cache), nodes(0) {}
    };

    template <bool CheckAllMoves, bool IsRoot>
    int solve(const BruteForceType::Session &session, State &state, const BruteForceType::ConfigId *configs, int start, int count, int need, int depth, BruteForceType::Result &result);
};

template <bool CheckAllMoves, bool IsRoot>
int NormalSolver::solve(const BruteForceType::Session &session, State &state, const BruteForceType::ConfigId *configs, int start, int count, int need, int depth, BruteForceType::Result &result) {
    BruteForceNormal::Workspace &workspace = BruteForceNormal().getWorkspace();
    ++state.nodes;
    if constexpr (CheckAllMoves && IsRoot) {
        if (need > count)
            return -count;
        BruteForceNormal::Layer &layer = workspace.layer(depth);
        vector<int> &deaths = layer.deaths;
        deaths.resize(session.candidateCount);
        std::fill(deaths.data, deaths.data + deaths.size, 0);
        for (int i = start; i < start + count; ++i)
            for (int mine = 0; mine < session.minesPerConfig; ++mine)
                ++deaths.data[session.mineCandidateIds[configs[i]][mine]];
        int best = 0;
        state.unopenedCandidates.forEachSetBit([&](int candidate) {
            for (int reveal = 0; reveal < 9; ++reveal)
                layer.groups[reveal].resize(0);
            for (int i = start; i < start + count; ++i) {
                const BruteForceType::ConfigId config = configs[i];
                if (!state.mineByConfig[config][candidate])
                    layer.groups[state.revealByConfig[config][candidate]].push_back(config);
            }
            state.unopenedCandidates.reset(candidate);
            int wins = 0;
            for (int reveal = 0; reveal < 9; ++reveal)
                if (layer.groups[reveal].size != 0) {
                    const int value = solve<false, false>(session, state, layer.groups[reveal].data, 0, (int)layer.groups[reveal].size, 1, depth + 1, result);
                    if (value > 0)
                        wins += value;
                }
            state.unopenedCandidates.set(candidate);
            result.moves.data[candidate] = BruteForceType::Result::Move(session.candidates.data[candidate].cell, wins);
            best = (std::max)(best, wins);
        });
        return best;
    } else {
        if (count <= 1) {
            if (need > count)
                return -count;
            if constexpr (IsRoot)
                if (count == 1)
                    for (int candidate = 0; candidate < session.candidateCount; ++candidate)
                        if (!state.mineByConfig[configs[start]][candidate]) {
                            result.moves.data[0].cell = session.candidates.data[candidate].cell;
                            break;
                        }
            return count;
        }
        if (need > count)
            return -count;
        const U128 key = hashConfigs(session.configHashes, configs, start, count);
        // 缓存键不含 unopened：递归中被移除的格子都已证明对当前方案集合共同安全。
        if (const int *cached = state.cache.find(key)) {
            if (*cached >= 0)
                return *cached >= need ? *cached : -*cached;
            if (-*cached < need)
                return *cached;
        }

        BruteForceNormal::Layer &layer = workspace.layer(depth);
        vector<int> &deaths = layer.deaths;
        deaths.resize(session.candidateCount);
        std::fill(deaths.data, deaths.data + deaths.size, 0);
        for (int i = start; i < start + count; ++i)
            for (int mine = 0; mine < session.minesPerConfig; ++mine)
                ++deaths.data[session.mineCandidateIds[configs[i]][mine]];
        vector<int> &safeCells = layer.safeCells;
        safeCells.resize(0);
        state.unopenedCandidates.forEachSetBit([&](int candidate) {
            if (deaths.data[candidate] == 0)
                safeCells.push_back(candidate);
        });
        if (safeCells.size != 0) {
            // 全体方案共同安全的格子可以一次打开；按揭示向量分组，只保留互异子局面。
            if constexpr (IsRoot)
                result.moves.data[0].cell = session.candidates.data[safeCells.data[0]].cell;
            for (int i = 0; i < (int)safeCells.size; ++i)
                state.unopenedCandidates.reset(safeCells.data[i]);
            vector<U128> &hashes = layer.safeHashes;
            hashes.resize(0);
            const int keyLength = (int)safeCells.size;
            for (int i = start; i < start + count; ++i) {
                const BruteForceType::ConfigId config = configs[i];
                U128Hasher hasher;
                for (int j = 0; j < keyLength; ++j)
                    hasher.mix((std::uint64_t)state.revealByConfig[config][safeCells.data[j]] * (keyLength + 1) + j);
                hashes.push_back(hasher.finalize());
            }
            vector<int> &groupIds = layer.safeGroupIds;
            vector<int> &groupSizes = layer.safeGroupSizes;
            vectorPool<BruteForceType::ConfigId> &groupedConfigs = layer.safeGroupConfigs;
            vector<BruteForceNormal::SafeGroup> &groupList = layer.safeGroupList;
            FlatHashTable<U128, int, U128Hash> &groupTable = workspace.safeGroupTable;
            groupTable.clear();
            groupTable.reserve(hashes.size);
            groupIds.resize(count);
            groupSizes.resize(0);
            for (int i = 0; i < count; ++i) {
                int &slot = groupTable[hashes.data[i]];
                if (slot == 0) {
                    slot = (int)groupSizes.size + 1;
                    groupSizes.push_back(0);
                }
                groupIds.data[i] = slot - 1;
                ++groupSizes.data[groupIds.data[i]];
            }
            groupedConfigs.data.resize(0);
            groupList.resize(0);
            for (int i = 0; i < (int)groupSizes.size; ++i) {
                vectorPool<BruteForceType::ConfigId>::vector slice = groupedConfigs.push_back();
                for (int j = 0; j < groupSizes.data[i]; ++j)
                    groupedConfigs.push_back(slice, 0);
                groupList.push_back(slice);
                groupSizes.data[i] = (int)slice.offset;
            }
            for (int i = 0; i < count; ++i)
                groupedConfigs.data.data[groupSizes.data[groupIds.data[i]]++] = configs[start + i];
            std::sort(groupList.data, groupList.data + groupList.size, [](const BruteForceNormal::SafeGroup &left, const BruteForceNormal::SafeGroup &right) {
                if (left.count != right.count)
                    return left.count > right.count;
                return left.offset < right.offset;
            });
            int wins = 0;
            bool bailed = false;
            int upper = 0;
            int remaining = count;
            for (int i = 0; i < (int)groupList.size; ++i) {
                const int size = (int)groupList.data[i].count;
                remaining -= size;
                if (wins + size + remaining < need) {
                    upper = wins + size + remaining;
                    bailed = true;
                    break;
                }
                const int value = solve<false, false>(session, state, groupedConfigs.data.data + groupList.data[i].offset, 0, size, (std::max)(1, need - wins - remaining), depth + 1, result);
                if (value <= 0) {
                    upper = wins - value + remaining;
                    bailed = true;
                    break;
                }
                wins += value;
            }
            for (int i = 0; i < (int)safeCells.size; ++i)
                state.unopenedCandidates.set(safeCells.data[i]);
            if (bailed) {
                saveFail(key, upper, count, state.cache);
                return -upper;
            }
            state.cache[key] = wins;
            return wins;
        }

        vector<int> &order = layer.order;
        order.resize(0);
        state.unopenedCandidates.forEachSetBit([&](int candidate) { order.push_back(candidate); });
        std::sort(order.data, order.data + order.size, [&](int left, int right) {
            if (deaths.data[left] != deaths.data[right])
                return deaths.data[left] < deaths.data[right];
            return left < right;
        });
        int best = 0;
        int upper = 0;
        for (int index = 0; index < (int)order.size; ++index) {
            const int candidate = order.data[index];
            const int target = (std::max)(best + 1, need);
            if (count - deaths.data[candidate] < target) {
                upper = (std::max)(upper, count - deaths.data[candidate]);
                break;
            }
            vector<BruteForceType::ConfigId> *groups = layer.groups;
            for (int reveal = 0; reveal < 9; ++reveal)
                groups[reveal].resize(0);
            int groupCount = 0;
            for (int i = start; i < start + count; ++i) {
                const BruteForceType::ConfigId config = configs[i];
                if (!state.mineByConfig[config][candidate]) {
                    const int reveal = state.revealByConfig[config][candidate];
                    if (groups[reveal].size == 0)
                        ++groupCount;
                    groups[reveal].push_back(config);
                }
            }
            // 只产生一个安全揭示组的点击会丢掉雷位方案，却没有区分存活方案。
            if (groupCount <= 1)
                continue;
            vector<BruteForceNormal::RevealGroup> &groupList = layer.groupList;
            groupList.resize(0);
            for (int reveal = 0; reveal < 9; ++reveal)
                if (groups[reveal].size != 0)
                    groupList.push_back({reveal, (int)groups[reveal].size});
            std::sort(groupList.data, groupList.data + groupList.size, [](const BruteForceNormal::RevealGroup &left, const BruteForceNormal::RevealGroup &right) {
                if (left.count != right.count)
                    return left.count > right.count;
                return left.reveal < right.reveal;
            });
            state.unopenedCandidates.reset(candidate);
            int wins = 0;
            bool bailed = false;
            int moveUpper = 0;
            int remaining = count - deaths.data[candidate];
            for (int i = 0; i < (int)groupList.size; ++i) {
                vector<BruteForceType::ConfigId> &group = groups[groupList.data[i].reveal];
                const int size = groupList.data[i].count;
                remaining -= size;
                if (wins + size + remaining < target) {
                    moveUpper = wins + size + remaining;
                    bailed = true;
                    break;
                }
                const int value = solve<false, false>(session, state, group.data, 0, size, (std::max)(1, target - wins - remaining), depth + 1, result);
                if (value <= 0) {
                    moveUpper = wins - value + remaining;
                    bailed = true;
                    break;
                }
                wins += value;
            }
            state.unopenedCandidates.set(candidate);
            if (bailed)
                upper = (std::max)(upper, moveUpper);
            if (!bailed && wins > best) {
                best = wins;
                if constexpr (IsRoot)
                    result.moves.data[0].cell = session.candidates.data[candidate].cell;
            }
        }
        if (best >= need) {
            state.cache[key] = best;
            return best;
        }
        if (best == 0 && upper == 0) {
            if constexpr (IsRoot)
                for (int candidate = 0; candidate < session.candidateCount; ++candidate)
                    if (!state.mineByConfig[configs[start]][candidate]) {
                        result.moves.data[0].cell = session.candidates.data[candidate].cell;
                        break;
                    }
            return 1;
        }
        saveFail(key, (std::max)(best, upper), count, state.cache);
        return -(std::max)(best, upper);
    }
}

BruteForceNormal::Layer &BruteForceNormal::Workspace::layer(int depth) {
    if ((int)layers.size() <= depth)
        layers.emplace_back();
    return layers[depth];
}

void BruteForceNormal::Workspace::reset() {
    for (std::size_t i = 0; i < layers.size(); ++i) {
        Layer &layer = layers[i];
        layer.deaths.resize(0);
        layer.safeCells.resize(0);
        layer.order.resize(0);
        for (int reveal = 0; reveal < 9; ++reveal)
            layer.groups[reveal].resize(0);
        layer.groupList.resize(0);
        layer.safeHashes.resize(0);
        layer.safeGroupIds.resize(0);
        layer.safeGroupSizes.resize(0);
        layer.safeGroupConfigs.data.resize(0);
        layer.safeGroupList.resize(0);
    }
    safeGroupTable.clear();
}

void BruteForceNormal::solve(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result) {
    Workspace &workspace = getWorkspace();
    Grid<std::uint8_t> &mineByConfig = workspace.mineByConfig;
    Grid<std::uint8_t> &revealByConfig = workspace.revealByConfig;
    DynamicBitset &unopenedCandidates = workspace.unopenedStorage;
    FlatHashTable<U128, int, U128Hash> &cache = workspace.cacheStorage;
    NormalSolver::State state(mineByConfig, revealByConfig, unopenedCandidates, cache);
    state.mineByConfig.resize(session.possibilityCount, session.candidateCount, 0);
    for (int configIndex = 0; configIndex < session.possibilityCount; ++configIndex)
        for (int i = 0; i < session.minesPerConfig; ++i)
            state.mineByConfig[configIndex][session.mineCandidateIds[configIndex][i]] = 1;
    state.revealByConfig.resize(session.possibilityCount, session.candidateCount, 0);
    for (int configIndex = 0; configIndex < session.possibilityCount; ++configIndex)
        for (int candidate = 0; candidate < session.candidateCount; ++candidate) {
            int value = session.candidates.data[candidate].fixedMines;
            const BruteForceType::Session::Candidate &current = session.candidates.data[candidate];
            for (std::uint32_t i = 0; i < current.linksCount; ++i)
                value += state.mineByConfig[configIndex][session.links.data[current.linksOffset + i]];
            state.revealByConfig[configIndex][candidate] = (std::uint8_t)value;
        }

    workspace.reset();
    workspace.configIds.resize(session.possibilityCount);
    for (int configIndex = 0; configIndex < session.possibilityCount; ++configIndex)
        workspace.configIds.data[configIndex] = configIndex;
    state.unopenedCandidates.resize(session.candidateCount);
    state.unopenedCandidates.setAll();

    if (config.checkAllMoves) {
        result.moves.resize(session.candidateCount);
        NormalSolver().solve<true, true>(session, state, workspace.configIds.data, 0, session.possibilityCount, 1, 0, result);
    } else {
        result.moves.resize(1);
        result.moves.data[0] = BruteForceType::Result::Move(-1, 0);
        const int wins = NormalSolver().solve<false, true>(session, state, workspace.configIds.data, 0, session.possibilityCount, 1, 0, result);
        if (wins < 1)
            result.moves.data[0] = BruteForceType::Result::Move(-1, 0);
        else
            result.moves.data[0].wins = wins;
    }
    result.nodes = state.nodes;
    state.cache.clear();
}

} // 命名空间 mss
