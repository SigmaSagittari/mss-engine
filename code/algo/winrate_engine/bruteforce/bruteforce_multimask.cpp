#include "algo/winrate_engine/bruteforce/bruteforce_multimask.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

#include "core/datastructure/grid.h"

namespace mss {

// 同 death 的候选达到此数量才值得做 Java-lite 重排；沿用 1.5。
constexpr int javaLiteConfigThreshold = 10000;
// 方案数低于此值不启动根并行，避免线程开销；沿用 1.5。
constexpr int parallelConfigThreshold = 4096;
// 子问题方案数达到此值才用共享缓存，避免小子问题付锁开销；沿用 1.5。
constexpr int sharedConfigThreshold = 32;

static U128 hashConfigs(const vector<std::uint64_t> &hashes, const BruteForceType::ConfigId *configs, int start, int count) {
    U128 hash{(std::uint64_t)count, (std::uint64_t)count};
    for (int i = start; i < start + count; ++i) {
        const std::uint64_t value = hashes.data[configs[i]];
        hash.lo += value;
        hash.hi ^= value;
    }
    hash.lo = splitmix64(hash.lo);
    return hash;
}

static void saveFail(const U128 &key, int upper, int count, FlatHashTable<U128, int, U128Hash> &table) {
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

template <typename Mask> static vector<Mask> &maskStorage(BruteForceMultimask::Workspace &workspace) {
    if constexpr (Mask::kWordCount == 1) return workspace.mineMasks1;
    else if constexpr (Mask::kWordCount == 2) return workspace.mineMasks2;
    else if constexpr (Mask::kWordCount == 4) return workspace.mineMasks4;
    else return workspace.mineMasks8;
}

template <typename Mask> struct MaskLayout {
    vector<Mask> &mineMaskByConfig;
    Grid<std::uint64_t> &mineByteWordsByConfig;
    Grid<std::uint8_t> &revealByConfig;

    MaskLayout(vector<Mask> &mineMaskByConfig, Grid<std::uint64_t> &mineByteWordsByConfig, Grid<std::uint8_t> &revealByConfig) : mineMaskByConfig(mineMaskByConfig), mineByteWordsByConfig(mineByteWordsByConfig), revealByConfig(revealByConfig) {}
};

// 一次根并行直接读写工作区中的候选顺序、汇总数组和共享缓存。
struct ParallelState {
    vector<int> &order, &deaths, &uppers;
    vector<long long> &nodes;
    BruteForceMultimask::ParallelCache cache;

    ParallelState(vector<int> &order, vector<int> &deaths, vector<int> &uppers, vector<long long> &nodes, FlatHashTable<U128, int, U128Hash> (&sharedStorage)[256]) : order(order), deaths(deaths), uppers(uppers), nodes(nodes), cache(sharedStorage) {}
    ~ParallelState() { cache.reset(); }
};

BruteForceMultimask::Worker::Worker(Scratch &scratch, ParallelCache *sharedCache) : scratch(scratch), cache(scratch.cacheStorage), sharedCache(sharedCache) {
    scratch.reset();
    cache.clear();
}

BruteForceMultimask::Worker::~Worker() {
    cache.clear();
}

template <typename Mask> struct MaskSession {
    const BruteForceType::Session &common;
    const MaskLayout<Mask> &layout;
    Mask unopenedCandidates;
    long long nodes;

    MaskSession(const BruteForceType::Session &common, const MaskLayout<Mask> &layout, const Mask &unopened) : common(common), layout(layout), unopenedCandidates(unopened), nodes(0) {}
};

template <typename Mask> struct MaskSolver {
    static void buildLayout(const BruteForceType::Session &common, MaskLayout<Mask> &layout);
    static int groupByReveal(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int candidate, vector<BruteForceType::ConfigId> *groups);
    static int javaLiteScore(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int candidate, BruteForceMultimask::Worker &workspace);
    static vector<int> &orderCandidates(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int depth, BruteForceMultimask::Worker &workspace);
    static void writeFirstSafeMove(const MaskSession<Mask> &session, BruteForceType::ConfigId config, BruteForceType::Result &result);
    template <bool Small>
    static void groupSafeConfigs(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, const Mask &safeMask, BruteForceMultimask::Layer &layer, BruteForceMultimask::Worker &workspace);
    template <bool CheckAllMoves, bool IsRoot>
    static int solve(MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int need, int depth, BruteForceMultimask::Worker &workspace, BruteForceType::Result &result);
};

template <typename Mask> void MaskSolver<Mask>::buildLayout(const BruteForceType::Session &common, MaskLayout<Mask> &layout) {
    layout.mineMaskByConfig.resize(0);
    for (int config = 0; config < common.possibilityCount; ++config)
        layout.mineMaskByConfig.push_back(Mask());
    layout.mineByteWordsByConfig.resize(common.possibilityCount, (common.candidateCount + 7) / 8, 0);
    for (int config = 0; config < common.possibilityCount; ++config)
        for (int i = 0; i < common.minesPerConfig; ++i) {
            const BruteForceType::CandidateId candidate = common.mineCandidateIds[config][i];
            layout.mineMaskByConfig.data[config].set(candidate);
            layout.mineByteWordsByConfig[config][candidate / 8] |= std::uint64_t{1} << (8 * (candidate % 8));
        }
    layout.revealByConfig.resize(common.possibilityCount, common.candidateCount, 0);
    for (int config = 0; config < common.possibilityCount; ++config)
        for (int candidate = 0; candidate < common.candidateCount; ++candidate) {
            int value = common.candidates.data[candidate].fixedMines;
            const BruteForceType::Session::Candidate &current = common.candidates.data[candidate];
            for (std::uint32_t i = 0; i < current.linksCount; ++i)
                value += layout.mineMaskByConfig.data[config].test(common.links.data[current.linksOffset + i]);
            layout.revealByConfig[config][candidate] = layout.mineMaskByConfig.data[config].test(candidate) ? 9 : value;
        }
}

template <typename Mask> int MaskSolver<Mask>::groupByReveal(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int candidate, vector<BruteForceType::ConfigId> *groups) {
    int groupCount = 0;
    for (int i = start; i < start + count; ++i) {
        const BruteForceType::ConfigId config = configs[i];
        const int reveal = session.layout.revealByConfig[config][candidate];
        if (reveal == 9)
            continue;
        if (groups[reveal].size == 0)
            ++groupCount;
        groups[reveal].push_back(config);
    }
    return groupCount;
}

template <typename Mask> int MaskSolver<Mask>::javaLiteScore(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int candidate, BruteForceMultimask::Worker &workspace) {
    int (&mineCounts)[9][bitMask<8>::kBitCount] = workspace.scratch.javaLiteMineCounts;
    int (&groupSizes)[9] = workspace.scratch.javaLiteGroupSizes;
    for (int reveal = 0; reveal < 9; ++reveal)
        std::fill(mineCounts[reveal], mineCounts[reveal] + session.common.candidateCount, 0);
    std::fill(groupSizes, groupSizes + 9, 0);
    Mask remaining = session.unopenedCandidates;
    remaining.reset(candidate);
    for (int i = start; i < start + count; ++i) {
        const BruteForceType::ConfigId config = configs[i];
        const int reveal = session.layout.revealByConfig[config][candidate];
        if (reveal == 9)
            continue;
        const Mask &mines = session.layout.mineMaskByConfig.data[config];
        ++groupSizes[reveal];
        Mask nextMines = mines;
        nextMines &= remaining;
        nextMines.forEachSetBit([&](int next) {
            ++mineCounts[reveal][next];
        });
    }
    int score = 0;
    for (int reveal = 0; reveal < 9; ++reveal) {
        const int groupSize = groupSizes[reveal];
        int bestMine = groupSize;
        int secondMine = groupSize;
        for (int next = 0; next < session.common.candidateCount; ++next) {
            if (!remaining.test(next))
                continue;
            const int mines = mineCounts[reveal][next];
            if (mines < bestMine) {
                secondMine = bestMine;
                bestMine = mines;
            } else if (mines < secondMine)
                secondMine = mines;
        }
        score += 4 * (groupSize - bestMine) + (groupSize - secondMine);
    }
    return score;
}

template <typename Mask> vector<int> &MaskSolver<Mask>::orderCandidates(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int depth, BruteForceMultimask::Worker &workspace) {
    BruteForceMultimask::Layer &layer = workspace.scratch.layer(depth);
    vector<int> &deaths = layer.deaths;
    deaths.resize(session.common.candidateCount);
    std::fill(deaths.data, deaths.data + deaths.size, 0);
    const int wordCount = session.layout.mineByteWordsByConfig.cols();
    std::uint64_t sums[Mask::kBitCount / 8];
    for (int begin = 0; begin < count; begin += (std::numeric_limits<std::uint8_t>::max)()) {
        std::fill(sums, sums + Mask::kBitCount / 8, 0);
        const int end = (std::min)(begin + (std::numeric_limits<std::uint8_t>::max)(), count);
        for (int i = begin; i < end; ++i) {
            const std::uint64_t *row = session.layout.mineByteWordsByConfig[configs[start + i]];
            for (int word = 0; word < wordCount; ++word)
                sums[word] += row[word];
        }
        for (int word = 0; word < wordCount; ++word) {
            std::uint64_t value = sums[word];
            const int endCandidate = (std::min)(word * 8 + 8, session.common.candidateCount);
            for (int candidate = word * 8; candidate < endCandidate; ++candidate) {
                deaths.data[candidate] += (int)(value & (std::numeric_limits<std::uint8_t>::max)());
                value >>= 8;
            }
        }
    }
    vector<int> &order = layer.order;
    order.resize(0);
    session.unopenedCandidates.forEachSetBit([&](int candidate) {
        order.push_back(candidate);
    });
    std::sort(order.data, order.data + order.size, [&](int left, int right) {
        if (deaths.data[left] != deaths.data[right])
            return deaths.data[left] < deaths.data[right];
        return left < right;
    });
    if (count >= javaLiteConfigThreshold && order.size > 1) {
        std::size_t tieEnd = 1;
        while (tieEnd < order.size && deaths.data[order.data[tieEnd]] == deaths.data[order.data[0]])
            ++tieEnd;
        if (tieEnd > 1) {
            int javaLiteScores[Mask::kBitCount];
            std::fill(javaLiteScores, javaLiteScores + Mask::kBitCount, 0);
            for (int i = 0; i < (int)tieEnd; ++i)
                javaLiteScores[order.data[i]] = javaLiteScore(session, configs, start, count, order.data[i], workspace);
            std::sort(order.data, order.data + tieEnd, [&](int left, int right) {
                if (javaLiteScores[left] != javaLiteScores[right])
                    return javaLiteScores[left] > javaLiteScores[right];
                return left < right;
            });
        }
    }
    return order;
}

template <typename Mask> void MaskSolver<Mask>::writeFirstSafeMove(const MaskSession<Mask> &session, BruteForceType::ConfigId config, BruteForceType::Result &result) {
    for (int candidate = 0; candidate < session.common.candidateCount; ++candidate)
        if (session.layout.revealByConfig[config][candidate] != 9) {
            result.moves.data[0].cell = session.common.candidates.data[candidate].cell;
            return;
        }
}

template <typename Mask> template <bool Small> void MaskSolver<Mask>::groupSafeConfigs(const MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, const Mask &safeMask, BruteForceMultimask::Layer &layer, BruteForceMultimask::Worker &workspace) {
    vectorPool<BruteForceType::ConfigId> &pool = layer.safeGroupConfigs;
    vector<BruteForceMultimask::SafeGroup> &groupList = layer.safeGroupList;
    pool.data.resize(0);
    groupList.resize(0);
    int safeCandidates[Small ? 3 : Mask::kBitCount];
    int safeCount = 0;
    safeMask.forEachSetBit([&](int candidate) {
        safeCandidates[safeCount++] = candidate;
    });
    if constexpr (Small) {
        int *counts = workspace.scratch.smallGroupCounts;
        vector<int> &keys = workspace.scratch.smallGroupKeys;
        vector<int> &configKeys = workspace.scratch.smallGroupConfigKeys;
        keys.resize(0);
        configKeys.resize(count);
        for (int i = 0; i < count; ++i) {
            const std::uint8_t *row = session.layout.revealByConfig[configs[start + i]];
            int key = 0;
            int factor = 1;
            for (int j = 0; j < safeCount; ++j) {
                key += row[safeCandidates[j]] * factor;
                factor *= 9;
            }
            configKeys.data[i] = key;
            if (counts[key]++ == 0)
                keys.push_back(key);
        }
        for (int i = 0; i < (int)keys.size; ++i) {
            const int key = keys.data[i];
            vectorPool<BruteForceType::ConfigId>::vector slice = pool.push_back();
            for (int j = 0; j < counts[key]; ++j)
                pool.push_back(slice, 0);
            groupList.push_back(slice);
            counts[key] = (int)slice.offset;
        }
        for (int i = 0; i < count; ++i)
            pool.data.data[counts[configKeys.data[i]]++] = configs[start + i];
        for (int i = 0; i < (int)keys.size; ++i)
            counts[keys.data[i]] = 0;
    } else {
        FlatHashTable<U128, int, U128Hash> &groupTable = workspace.scratch.safeGroupTable;
        vector<int> &groupIds = layer.safeGroupIds;
        vector<int> &groupSizes = layer.safeGroupSizes;
        groupTable.clear();
        groupTable.reserve(count);
        groupIds.resize(count);
        groupSizes.resize(0);
        for (int i = 0; i < count; ++i) {
            const std::uint8_t *row = session.layout.revealByConfig[configs[start + i]];
            U128 key;
            if (safeCount <= 16) {
                std::uint64_t packed = 0;
                for (int j = 0; j < safeCount; ++j)
                    packed |= (std::uint64_t)row[safeCandidates[j]] << (4 * j);
                key = U128{packed, splitmix64(packed)};
            } else {
                U128Hasher hasher;
                for (int j = 0; j < safeCount; j += 16) {
                    std::uint64_t chunk = 0;
                    for (int k = 0; k < 16 && j + k < safeCount; ++k)
                        chunk |= (std::uint64_t)row[safeCandidates[j + k]] << (4 * k);
                    hasher.mix(chunk);
                }
                key = hasher.finalize();
            }
            int &slot = groupTable[key];
            if (slot == 0) {
                slot = (int)groupSizes.size + 1;
                groupSizes.push_back(0);
            }
            groupIds.data[i] = slot - 1;
            ++groupSizes.data[groupIds.data[i]];
        }
        for (int i = 0; i < (int)groupSizes.size; ++i) {
            vectorPool<BruteForceType::ConfigId>::vector slice = pool.push_back();
            for (int j = 0; j < groupSizes.data[i]; ++j)
                pool.push_back(slice, 0);
            groupList.push_back(slice);
            groupSizes.data[i] = (int)slice.offset;
        }
        for (int i = 0; i < count; ++i)
            pool.data.data[groupSizes.data[groupIds.data[i]]++] = configs[start + i];
    }
}

template <typename Mask> template <bool CheckAllMoves, bool IsRoot> int MaskSolver<Mask>::solve(MaskSession<Mask> &session, const BruteForceType::ConfigId *configs, int start, int count, int need, int depth, BruteForceMultimask::Worker &workspace, BruteForceType::Result &result) {
    ++session.nodes;
    if constexpr (CheckAllMoves && IsRoot) {
        if (need > count)
            return -count;
        BruteForceMultimask::Layer &layer = workspace.scratch.layer(depth);
        result.moves.resize(session.common.candidateCount);
        vector<int> &order = orderCandidates(session, configs, start, count, depth, workspace);
        int best = 0;
        for (int i = 0; i < (int)order.size; ++i) {
            const int candidate = order.data[i];
            for (int reveal = 0; reveal < 9; ++reveal)
                layer.groups[reveal].resize(0);
            groupByReveal(session, configs, start, count, candidate, layer.groups);
            session.unopenedCandidates.reset(candidate);
            int wins = 0;
            for (int reveal = 0; reveal < 9; ++reveal)
                if (layer.groups[reveal].size != 0) {
                    const int value = solve<false, false>(session, layer.groups[reveal].data, 0, (int)layer.groups[reveal].size, 1, depth + 1, workspace, result);
                    if (value > 0)
                        wins += value;
                }
            session.unopenedCandidates.set(candidate);
            result.moves.data[candidate] = BruteForceType::Result::Move(session.common.candidates.data[candidate].cell, wins);
            best = (std::max)(best, wins);
        }
        return best;
    } else {
        if (count <= 1) {
            if (need > count)
                return -count;
            if constexpr (IsRoot)
                if (count == 1)
                    writeFirstSafeMove(session, configs[start], result);
            return count;
        }
        if (need > count)
            return -count;
        if constexpr (!IsRoot)
            if (count == 2) {
                Mask safe = session.unopenedCandidates;
                safe &= ~session.layout.mineMaskByConfig.data[configs[start]];
                safe &= ~session.layout.mineMaskByConfig.data[configs[start + 1]];
                while (safe.any()) {
                    const int candidate = safe.firstSetBit();
                    if (session.layout.revealByConfig[configs[start]][candidate] != session.layout.revealByConfig[configs[start + 1]][candidate])
                        return 2;
                    safe.reset(candidate);
                }
                return need <= 1 ? 1 : -1;
            }
        const bool shared = workspace.sharedCache != nullptr && count >= sharedConfigThreshold;
        const U128 key = hashConfigs(session.common.configHashes, configs, start, count);
        int cached = 0;
        if (shared)
            cached = workspace.sharedCache->find(key);
        else if (const int *value = workspace.cache.find(key))
            cached = *value;
        if (cached != 0) {
            if (cached >= 0)
                return cached >= need ? cached : -cached;
            if (-cached < need)
                return cached;
        }

        BruteForceMultimask::Layer &layer = workspace.scratch.layer(depth);
        Mask safeMask = session.unopenedCandidates;
        for (int i = start; i < start + count; ++i) {
            safeMask &= ~session.layout.mineMaskByConfig.data[configs[i]];
            if (!safeMask.any())
                break;
        }
        if (safeMask.any()) {
            if constexpr (IsRoot && !CheckAllMoves)
                result.moves.data[0].cell = session.common.candidates.data[safeMask.firstSetBit()].cell;
            session.unopenedCandidates &= ~safeMask;
            if (safeMask.popcount() <= 3)
                groupSafeConfigs<true>(session, configs, start, count, safeMask, layer, workspace);
            else
                groupSafeConfigs<false>(session, configs, start, count, safeMask, layer, workspace);
            std::sort(layer.safeGroupList.data, layer.safeGroupList.data + layer.safeGroupList.size, [](const BruteForceMultimask::SafeGroup &left, const BruteForceMultimask::SafeGroup &right) {
                if (left.count != right.count)
                    return left.count > right.count;
                return left.offset < right.offset;
            });
            int wins = 0;
            int remaining = count;
            bool reached = true;
            int upper = 0;
            for (int i = 0; i < (int)layer.safeGroupList.size; ++i) {
                const BruteForceMultimask::SafeGroup &group = layer.safeGroupList.data[i];
                const int size = (int)group.count;
                remaining -= size;
                if (wins + size + remaining < need) {
                    upper = wins + size + remaining;
                    reached = false;
                    break;
                }
                const int value = solve<false, false>(session, layer.safeGroupConfigs.data.data + group.offset, 0, size, (std::max)(1, need - wins - remaining), depth + 1, workspace, result);
                if (value <= 0) {
                    upper = wins - value + remaining;
                    reached = false;
                    break;
                }
                wins += value;
            }
            session.unopenedCandidates |= safeMask;
            if (!reached) {
                if (shared) {
                    if (upper > 0 && upper < count)
                        workspace.sharedCache->store(key, -upper);
                } else
                    saveFail(key, upper, count, workspace.cache);
                return -upper;
            }
            if (shared)
                workspace.sharedCache->store(key, wins);
            else
                workspace.cache[key] = wins;
            return wins;
        }

        vector<int> &order = orderCandidates(session, configs, start, count, depth, workspace);
        vector<int> &deaths = layer.deaths;
        int best = 0;
        int upper = 0;
        for (int i = 0; i < (int)order.size; ++i) {
            const int candidate = order.data[i];
            const int target = (std::max)(best + 1, need);
            if (count - deaths.data[candidate] < target) {
                upper = (std::max)(upper, count - deaths.data[candidate]);
                break;
            }
            int reveals = 0;
            for (int j = start; j < start + count; ++j) {
                const int reveal = session.layout.revealByConfig[configs[j]][candidate];
                if (reveal != 9)
                    reveals |= 1 << reveal;
                if (reveals & (reveals - 1))
                    break;
            }
            if ((reveals & (reveals - 1)) == 0)
                continue;
            for (int reveal = 0; reveal < 9; ++reveal)
                layer.groups[reveal].resize(0);
            groupByReveal(session, configs, start, count, candidate, layer.groups);
            vector<BruteForceMultimask::RevealGroup> &groupList = layer.groupList;
            groupList.resize(0);
            for (int reveal = 0; reveal < 9; ++reveal)
                if (layer.groups[reveal].size != 0)
                    groupList.push_back({reveal, (int)layer.groups[reveal].size});
            std::sort(groupList.data, groupList.data + groupList.size, [](const BruteForceMultimask::RevealGroup &left, const BruteForceMultimask::RevealGroup &right) {
                if (left.count != right.count)
                    return left.count > right.count;
                return left.reveal < right.reveal;
            });
            session.unopenedCandidates.reset(candidate);
            int wins = 0;
            int remaining = count - deaths.data[candidate];
            bool reached = true;
            int moveUpper = 0;
            for (int k = 0; k < (int)groupList.size; ++k) {
                const BruteForceMultimask::RevealGroup &group = groupList.data[k];
                const int size = group.count;
                remaining -= size;
                if (wins + size + remaining < target) {
                    moveUpper = wins + size + remaining;
                    reached = false;
                    break;
                }
                const int value = solve<false, false>(session, layer.groups[group.reveal].data, 0, size, (std::max)(1, target - wins - remaining), depth + 1, workspace, result);
                if (value <= 0) {
                    moveUpper = wins - value + remaining;
                    reached = false;
                    break;
                }
                wins += value;
            }
            session.unopenedCandidates.set(candidate);
            if (!reached)
                upper = (std::max)(upper, moveUpper);
            if (reached && wins > best) {
                best = wins;
                if constexpr (IsRoot)
                    result.moves.data[0].cell = session.common.candidates.data[candidate].cell;
            }
        }
        if (best >= need) {
            if (shared)
                workspace.sharedCache->store(key, best);
            else
                workspace.cache[key] = best;
            return best;
        }
        if (best == 0 && upper == 0) {
            if (shared)
                workspace.sharedCache->store(key, 1);
            else
                workspace.cache[key] = 1;
            if constexpr (IsRoot)
                writeFirstSafeMove(session, configs[start], result);
            return 1;
        }
        const int failUpper = (std::max)(best, upper);
        if (shared) {
            if (failUpper > 0 && failUpper < count)
                workspace.sharedCache->store(key, -failUpper);
        } else
            saveFail(key, failUpper, count, workspace.cache);
        return -failUpper;
    }
}

template <typename Mask> static void solveWidthParallel(const BruteForceType::Session &common, const BruteForceType::Config &config, BruteForceType::Result &result, BruteForceMultimask::Workspace &workspace);

template <typename Mask> static void solveWidth(const BruteForceType::Session &common, const BruteForceType::Config &config, BruteForceType::Result &result, BruteForceMultimask::Workspace &workspace, bool allowParallel) {
    if (allowParallel && config.parallel && config.threadCount > 1 && common.possibilityCount >= parallelConfigThreshold) {
        solveWidthParallel<Mask>(common, config, result, workspace);
        return;
    }
    vector<Mask> &mineMaskByConfig = maskStorage<Mask>(workspace);
    Grid<std::uint64_t> &mineByteWordsByConfig = workspace.mineByteWords;
    Grid<std::uint8_t> &revealByConfig = workspace.reveals;
    MaskLayout<Mask> layout(mineMaskByConfig, mineByteWordsByConfig, revealByConfig);
    MaskSolver<Mask>::buildLayout(common, layout);
    workspace.configIds.resize(common.possibilityCount);
    for (int configIndex = 0; configIndex < common.possibilityCount; ++configIndex)
        workspace.configIds.data[configIndex] = configIndex;
    MaskSession<Mask> session(common, layout, Mask::all(common.candidateCount));
    if (workspace.workers.empty()) workspace.workers.emplace_back();
    BruteForceMultimask::Worker worker(workspace.workers[0], nullptr);
    if (config.checkAllMoves) {
        result.moves.resize(common.candidateCount);
        MaskSolver<Mask>::template solve<true, true>(session, workspace.configIds.data, 0, common.possibilityCount, 1, 0, worker, result);
    } else {
        result.moves.resize(1);
        result.moves.data[0] = BruteForceType::Result::Move(-1, 0);
        const int wins = MaskSolver<Mask>::template solve<false, true>(session, workspace.configIds.data, 0, common.possibilityCount, 1, 0, worker, result);
        if (wins < 1)
            result.moves.data[0] = BruteForceType::Result::Move(-1, 0);
        else {
            result.moves.data[0].wins = wins;
            if (result.moves.data[0].cell < 0)
                result.moves.data[0].cell = common.candidates.data[0].cell;
        }
    }
    result.nodes = session.nodes;
}

template <typename Mask> static void solveWidthParallel(const BruteForceType::Session &common, const BruteForceType::Config &config, BruteForceType::Result &result, BruteForceMultimask::Workspace &workspace) {
    vector<Mask> &mineMaskByConfig = maskStorage<Mask>(workspace);
    Grid<std::uint64_t> &mineByteWordsByConfig = workspace.mineByteWords;
    Grid<std::uint8_t> &revealByConfig = workspace.reveals;
    MaskLayout<Mask> layout(mineMaskByConfig, mineByteWordsByConfig, revealByConfig);
    MaskSolver<Mask>::buildLayout(common, layout);
    workspace.configIds.resize(common.possibilityCount);
    for (int configIndex = 0; configIndex < common.possibilityCount; ++configIndex)
        workspace.configIds.data[configIndex] = configIndex;
    MaskSession<Mask> rootSession(common, layout, Mask::all(common.candidateCount));
    vector<int> &order = workspace.order;
    vector<int> &deaths = workspace.deaths;
    vector<int> &uppers = workspace.uppers;
    vector<long long> &nodes = workspace.nodes;
    FlatHashTable<U128, int, U128Hash> (&sharedStorage)[256] = workspace.sharedStorage;
    ParallelState parallel(order, deaths, uppers, nodes, sharedStorage);
    if (workspace.workers.empty()) workspace.workers.emplace_back();
    {
        BruteForceMultimask::Worker root(workspace.workers[0], nullptr);
        vector<int> &rootOrder = MaskSolver<Mask>::orderCandidates(rootSession, workspace.configIds.data, 0, common.possibilityCount, 0, root);
        parallel.order = rootOrder;
        parallel.deaths = root.scratch.layer(0).deaths;
    }
    const int candidateCount = (int)parallel.order.size;
    if (candidateCount == 0) {
        solveWidth<Mask>(common, config, result, workspace, false);
        return;
    }
    int threadCount = (std::min)(config.threadCount, candidateCount);
    if (threadCount < 1)
        threadCount = 1;
    if (threadCount == 1) {
        solveWidth<Mask>(common, config, result, workspace, false);
        return;
    }
    parallel.uppers.resize(candidateCount);
    std::fill(parallel.uppers.data, parallel.uppers.data + parallel.uppers.size, 0);
    parallel.nodes.resize(threadCount);
    std::fill(parallel.nodes.data, parallel.nodes.data + parallel.nodes.size, 0);
    if ((int)workspace.workers.size() < threadCount) workspace.workers.resize(threadCount);
    result.moves.resize(config.checkAllMoves ? common.candidateCount : 1);
    if (!config.checkAllMoves)
        result.moves.data[0] = BruteForceType::Result::Move(-1, 0);
    std::atomic<int> next{0};
    std::atomic<long long> best{0};
    auto work = [&](int workerIndex) {
        BruteForceMultimask::Worker worker(workspace.workers[workerIndex], &parallel.cache);
        MaskSession<Mask> session(common, layout, Mask::all(common.candidateCount));
        BruteForceMultimask::Layer &layer = worker.scratch.layer(0);
        for (;;) {
            const int index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= candidateCount)
                break;
            const int candidate = parallel.order.data[index];
            const long long previous = best.load(std::memory_order_relaxed);
            const int previousWins = (int)(previous / (candidateCount + 1));
            const int previousIndex = candidateCount - (int)(previous % (candidateCount + 1));
            const int target = config.checkAllMoves ? 1 : (std::max)(1, previousWins + (index > previousIndex));
            if (!config.checkAllMoves && common.possibilityCount - parallel.deaths.data[candidate] < target) {
                parallel.uppers.data[index] = common.possibilityCount - parallel.deaths.data[candidate];
                continue;
            }
            for (int reveal = 0; reveal < 9; ++reveal)
                layer.groups[reveal].resize(0);
            const int groupCount = MaskSolver<Mask>::groupByReveal(session, workspace.configIds.data, 0, common.possibilityCount, candidate, layer.groups);
            if (!config.checkAllMoves && groupCount <= 1)
                continue;
            vector<BruteForceMultimask::RevealGroup> &groupList = layer.groupList;
            groupList.resize(0);
            for (int reveal = 0; reveal < 9; ++reveal)
                if (layer.groups[reveal].size != 0)
                    groupList.push_back({reveal, (int)layer.groups[reveal].size});
            std::sort(groupList.data, groupList.data + groupList.size, [](const BruteForceMultimask::RevealGroup &left, const BruteForceMultimask::RevealGroup &right) {
                if (left.count != right.count)
                    return left.count > right.count;
                return left.reveal < right.reveal;
            });
            session.unopenedCandidates.reset(candidate);
            bool reached = true;
            int wins = 0;
            int upper = 0;
            int remaining = common.possibilityCount - parallel.deaths.data[candidate];
            for (int k = 0; k < (int)groupList.size; ++k) {
                const BruteForceMultimask::RevealGroup &group = groupList.data[k];
                const long long current = best.load(std::memory_order_relaxed);
                const int currentWins = (int)(current / (candidateCount + 1));
                const int currentIndex = candidateCount - (int)(current % (candidateCount + 1));
                const int required = config.checkAllMoves ? 1 : (std::max)(1, currentWins + (index > currentIndex));
                const int size = group.count;
                remaining -= size;
                upper = wins + size + remaining;
                if (upper < required) {
                    reached = false;
                    break;
                }
                const int value = MaskSolver<Mask>::template solve<false, false>(session, layer.groups[group.reveal].data, 0, size, (std::max)(1, required - wins - remaining), 1, worker, result);
                if (value <= 0) {
                    upper = wins - value + remaining;
                    reached = false;
                    break;
                }
                wins += value;
            }
            session.unopenedCandidates.set(candidate);
            if (config.checkAllMoves)
                result.moves.data[candidate] = BruteForceType::Result::Move(common.candidates.data[candidate].cell, wins);
            if (!reached) {
                parallel.uppers.data[index] = upper;
                continue;
            }
            const long long value = (long long)wins * (candidateCount + 1) + candidateCount - index;
            long long old = best.load(std::memory_order_relaxed);
            while (old < value && !best.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
        }
        parallel.nodes.data[workerIndex] = session.nodes;
    };
    std::thread *threads = new std::thread[threadCount - 1];
    for (int i = 0; i < threadCount - 1; ++i)
        threads[i] = std::thread(work, i + 1);
    work(0);
    for (int i = 0; i < threadCount - 1; ++i)
        threads[i].join();
    delete[] threads;
    long long totalNodes = 0;
    for (int i = 0; i < (int)parallel.nodes.size; ++i)
        totalNodes += parallel.nodes.data[i];
    result.nodes = totalNodes;
    const long long packed = best.load(std::memory_order_relaxed);
    const int wins = (int)(packed / (candidateCount + 1));
    if (!config.checkAllMoves && wins >= 1) {
        const int candidate = parallel.order.data[candidateCount - (int)(packed % (candidateCount + 1))];
        result.moves.data[0] = BruteForceType::Result::Move(common.candidates.data[candidate].cell, wins);
    } else if (!config.checkAllMoves) {
        int upper = 0;
        for (int i = 0; i < (int)parallel.uppers.size; ++i)
            upper = (std::max)(upper, parallel.uppers.data[i]);
        if (wins == 0 && upper == 0) {
            MaskSolver<Mask>::writeFirstSafeMove(rootSession, workspace.configIds.data[0], result);
            result.moves.data[0].wins = 1;
        } else
            result.moves.data[0] = BruteForceType::Result::Move(-1, 0);
    }
}

void BruteForceMultimask::ParallelCache::reset() {
    for (int i = 0; i < 256; ++i)
        storage[i].clear();
}

int BruteForceMultimask::ParallelCache::find(const U128 &key) {
    const int shardIndex = (int)(key.lo % 256);
    CacheShard &shard = shards[shardIndex];
    std::lock_guard<std::mutex> lock(shard.mutex);
    const int *value = storage[shardIndex].find(key);
    return value == nullptr ? 0 : *value;
}

void BruteForceMultimask::ParallelCache::store(const U128 &key, int value) {
    const int shardIndex = (int)(key.lo % 256);
    CacheShard &shard = shards[shardIndex];
    std::lock_guard<std::mutex> lock(shard.mutex);
    int &old = storage[shardIndex][key];
    if (old == 0 || value > 0 || (old < 0 && value > old))
        old = value;
}

BruteForceMultimask::Scratch::Scratch() {
    std::memset(javaLiteMineCounts, 0, sizeof(javaLiteMineCounts));
    std::memset(javaLiteGroupSizes, 0, sizeof(javaLiteGroupSizes));
    std::memset(smallGroupCounts, 0, sizeof(smallGroupCounts));
}

BruteForceMultimask::Layer &BruteForceMultimask::Scratch::layer(int depth) {
    if ((int)layers.size() <= depth)
        layers.emplace_back();
    return layers[depth];
}

void BruteForceMultimask::Scratch::reset() {
    for (std::size_t i = 0; i < layers.size(); ++i) {
        Layer &layer = layers[i];
        layer.deaths.resize(0);
        layer.order.resize(0);
        for (int reveal = 0; reveal < 9; ++reveal)
            layer.groups[reveal].resize(0);
        layer.groupList.resize(0);
        layer.safeGroupIds.resize(0);
        layer.safeGroupSizes.resize(0);
        layer.safeGroupConfigs.data.resize(0);
        layer.safeGroupList.resize(0);
    }
    safeGroupTable.clear();
    smallGroupKeys.resize(0);
    smallGroupConfigKeys.resize(0);
}

void BruteForceMultimask::solve(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result, Workspace &workspace) {
    if (session.candidateCount <= bitMask<1>::kBitCount)
        solveWidth<bitMask<1>>(session, config, result, workspace, true);
    else if (session.candidateCount <= bitMask<2>::kBitCount)
        solveWidth<bitMask<2>>(session, config, result, workspace, true);
    else if (session.candidateCount <= bitMask<4>::kBitCount)
        solveWidth<bitMask<4>>(session, config, result, workspace, true);
    else
        solveWidth<bitMask<8>>(session, config, result, workspace, true);
}

} // 命名空间 mss
