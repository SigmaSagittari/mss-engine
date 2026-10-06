#include "algo/probability_engine/bruteforce/bruteforce_multimask.h"

#include <algorithm>
#include <limits>

#include "algo/probability_engine/bruteforce/bruteforce_parallel.h"

namespace mss {

template <typename Mask>
void BruteForce::MultiMaskSolver<Mask>::buildSession(const CommonSession &common,
                                                    Workspace::MultiMask::Layout<Mask> &layout) {
    layout.mineMaskByConfig.assign(common.possibilityCount, Mask{});
    layout.mineByteWordsByConfig.resize(common.possibilityCount, (common.candidateCount + 7) / 8, 0);
    for (int config = 0; config < common.possibilityCount; ++config)
        for (int i = 0; i < common.minesPerConfig; ++i) {
            const CandidateId candidate = common.mineCandidateIds[config][i];
            layout.mineMaskByConfig[config].set(candidate);
            layout.mineByteWordsByConfig[config][candidate / 8] |= std::uint64_t{1} << (8 * (candidate % 8));
        }
    layout.revealByConfig.resize(common.possibilityCount, common.candidateCount, 0);
    for (int config = 0; config < common.possibilityCount; ++config)
        for (int candidate = 0; candidate < common.candidateCount; ++candidate) {
            int value = common.candidates[candidate].fixedMines;
            const CommonSession::Candidate &current = common.candidates[candidate];
            for (std::uint32_t i = 0; i < current.linksCount; ++i)
                value += layout.mineMaskByConfig[config].test(common.links[current.linksOffset + i]);
            layout.revealByConfig[config][candidate] = layout.mineMaskByConfig[config].test(candidate) ? 9 : value;
        }
}

template <typename Mask>
int BruteForce::MultiMaskSolver<Mask>::groupByReveal(const Workspace::MultiMask::Session<Mask> &session,
                                                     const std::vector<ConfigId> &configs, int start, int count,
                                                     int candidate, std::array<std::vector<ConfigId>, 9> &groups) {
    int groupCount = 0;
    for (int i = start; i < start + count; ++i) {
        const ConfigId config = configs[i];
        const int reveal = session.layout.revealByConfig[config][candidate];
        if (reveal == 9)
            continue;
        if (groups[reveal].empty())
            ++groupCount;
        groups[reveal].push_back(config);
    }
    return groupCount;
}

template <typename Mask>
int BruteForce::MultiMaskSolver<Mask>::javaLiteScore(const CommonSession &common,
                                                    const Workspace::MultiMask::Session<Mask> &session,
                                                    const std::vector<ConfigId> &configs, int start, int count,
                                                    int candidate, Workspace::MultiMask::Worker &workspace) {
    // 只给同死亡数的首层候选排序；分数不参与剪枝或返回结果，避免支付全候选打分成本。
    std::array<std::array<int, bitMask<8>::kBitCount>, 9> &mineCounts = workspace.scratch.javaLiteMineCounts;
    std::array<int, 9> &groupSizes = workspace.scratch.javaLiteGroupSizes;
    for (std::array<int, bitMask<8>::kBitCount> &counts : mineCounts)
        std::fill_n(counts.begin(), common.candidateCount, 0);
    groupSizes.fill(0);
    Mask remaining = session.unopenedCandidates;
    remaining.reset(candidate);
    for (int i = start; i < start + count; ++i) {
        const ConfigId config = configs[i];
        const int reveal = session.layout.revealByConfig[config][candidate];
        if (reveal == 9)
            continue;
        const Mask &mines = session.layout.mineMaskByConfig[config];
        ++groupSizes[reveal];
        Mask nextMines = mines;
        nextMines &= remaining;
        nextMines.forEachSetBit([&](int next) { ++mineCounts[reveal][next]; });
    }
    int score = 0;
    for (int reveal = 0; reveal < 9; ++reveal) {
        const int groupSize = groupSizes[reveal];
        int bestMine = groupSize;
        int secondMine = groupSize;
        for (int next = 0; next < common.candidateCount; ++next) {
            if (!remaining.test(next))
                continue;
            const int mines = mineCounts[reveal][next];
            if (mines < bestMine) {
                secondMine = bestMine;
                bestMine = mines;
            } else if (mines < secondMine) {
                secondMine = mines;
            }
        }
        score += 4 * (groupSize - bestMine) + (groupSize - secondMine);
    }
    return score;
}

template <typename Mask>
std::vector<int> &BruteForce::MultiMaskSolver<Mask>::orderCandidates(
    const CommonSession &common, const Workspace::MultiMask::Session<Mask> &session,
    const std::vector<ConfigId> &configs, int start, int count, int depth,
    Workspace::MultiMask::Worker &workspace) {
    Workspace::MultiMask::Layer &layer = workspace.scratch.layer(depth);
    std::vector<int> &deaths = layer.deaths;
    deaths.assign(common.candidateCount, 0);
    // 每行每个字节表示一个候选的雷位；每批最多累加一个字节可表示的方案数，避免溢出串到相邻候选。
    const int wordCount = session.layout.mineByteWordsByConfig.cols();
    std::array<std::uint64_t, Mask::kBitCount / 8> sums{};
    for (int begin = 0; begin < count; begin += (std::numeric_limits<std::uint8_t>::max)()) {
        sums.fill(0);
        const int end = (std::min)(begin + (std::numeric_limits<std::uint8_t>::max)(), count);
        for (int i = begin; i < end; ++i) {
            const std::uint64_t *row = session.layout.mineByteWordsByConfig[configs[start + i]];
            for (int word = 0; word < wordCount; ++word)
                sums[word] += row[word];
        }
        for (int word = 0; word < wordCount; ++word) {
            std::uint64_t value = sums[word];
            const int endCandidate = (std::min)(word * 8 + 8, common.candidateCount);
            for (int candidate = word * 8; candidate < endCandidate; ++candidate) {
                deaths[candidate] += (int)(value & (std::numeric_limits<std::uint8_t>::max)());
                value >>= 8;
            }
        }
    }
    std::vector<int> &order = layer.order;
    order.clear();
    session.unopenedCandidates.forEachSetBit([&](int candidate) { order.push_back(candidate); });
    std::sort(order.begin(), order.end(), [&](int left, int right) {
        if (deaths[left] != deaths[right])
            return deaths[left] < deaths[right];
        return left < right;
    });
    // death 升序保证后续上界可提前停止；Java-lite 只重排队首同 death 的候选。
    if (count >= BruteForce::javaLiteConfigThreshold && order.size() > 1) {
        std::size_t tieEnd = 1;
        while (tieEnd < order.size() && deaths[order[tieEnd]] == deaths[order[0]])
            ++tieEnd;
        if (tieEnd > 1) {
            std::array<int, Mask::kBitCount> javaLiteScores{};
            for (int i = 0; i < (int)tieEnd; ++i)
                javaLiteScores[order[i]] = javaLiteScore(common, session, configs, start, count, order[i], workspace);
            std::sort(order.begin(), order.begin() + tieEnd, [&](int left, int right) {
                if (javaLiteScores[left] != javaLiteScores[right])
                    return javaLiteScores[left] > javaLiteScores[right];
                return left < right;
            });
        }
    }
    return order;
}

template <typename Mask>
void BruteForce::MultiMaskSolver<Mask>::writeFirstSafeMove(const CommonSession &common,
                                                           const Workspace::MultiMask::Session<Mask> &session,
                                                           ConfigId config, Result &result) {
    for (int candidate = 0; candidate < common.candidateCount; ++candidate)
        if (session.layout.revealByConfig[config][candidate] != 9) {
            result.moves[0].cell = common.candidates[candidate].cell;
            return;
        }
}

template <typename Mask>
template <bool Small>
void BruteForce::MultiMaskSolver<Mask>::groupSafeConfigs(
    const Workspace::MultiMask::Session<Mask> &session, const std::vector<ConfigId> &configs, int start, int count,
    const Mask &safeMask, Workspace::MultiMask::Layer &layer, Workspace::MultiMask::Worker &workspace) {
    // safeMask 的揭示向量决定同一批共同安全点击后的子问题；少于等于 3 格用 9 进制直查，其余用哈希。
    std::vector<int> &groupOffsets = layer.safeGroupOffsets;
    std::vector<ConfigId> &groupedConfigs = layer.safeGroupedConfigs;
    std::vector<std::pair<int, int>> &groupList = layer.safeGroupList;
    std::array<int, Small ? 3 : Mask::kBitCount> safeCandidates{};
    int safeCount = 0;
    safeMask.forEachSetBit([&](int candidate) { safeCandidates[safeCount++] = candidate; });
    if constexpr (Small) {
        std::array<int, 9 * 9 * 9> &counts = workspace.scratch.smallGroupCounts;
        std::vector<int> &keys = workspace.scratch.smallGroupKeys;
        std::vector<int> &configKeys = workspace.scratch.smallGroupConfigKeys;
        keys.clear();
        configKeys.resize(count);
        for (int i = 0; i < count; ++i) {
            const std::uint8_t *row = session.layout.revealByConfig[configs[start + i]];
            int key = 0;
            int factor = 1;
            for (int j = 0; j < safeCount; ++j) {
                key += row[safeCandidates[j]] * factor;
                factor *= 9;
            }
            configKeys[i] = key;
            if (counts[key]++ == 0)
                keys.push_back(key);
        }
        groupOffsets.resize(keys.size() + 1);
        groupOffsets[0] = 0;
        for (int i = 0; i < (int)keys.size(); ++i) {
            groupOffsets[i + 1] = groupOffsets[i] + counts[keys[i]];
            counts[keys[i]] = groupOffsets[i];
        }
        groupedConfigs.resize(count);
        for (int i = 0; i < count; ++i)
            groupedConfigs[counts[configKeys[i]]++] = configs[start + i];
        groupList.clear();
        for (int i = 0; i < (int)keys.size(); ++i) {
            groupList.push_back({groupOffsets[i], groupOffsets[i + 1] - groupOffsets[i]});
            counts[keys[i]] = 0;
        }
    } else {
        FlatHashTable<U128, int, U128Hash> &groupTable = workspace.scratch.safeGroupTable;
        std::vector<int> &groupIds = layer.safeGroupIds;
        std::vector<int> &groupSizes = layer.safeGroupSizes;
        groupTable.clear();
        groupTable.reserve(count);
        groupIds.resize(count);
        groupSizes.clear();
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
                slot = groupSizes.size() + 1;
                groupSizes.push_back(0);
            }
            groupIds[i] = slot - 1;
            ++groupSizes[groupIds[i]];
        }
        groupOffsets.resize(groupSizes.size() + 1);
        groupOffsets[0] = 0;
        for (int i = 0; i < (int)groupSizes.size(); ++i)
            groupOffsets[i + 1] = groupOffsets[i] + groupSizes[i];
        groupedConfigs.resize(count);
        for (int i = 0; i < (int)groupSizes.size(); ++i)
            groupSizes[i] = groupOffsets[i];
        for (int i = 0; i < count; ++i)
            groupedConfigs[groupSizes[groupIds[i]]++] = configs[start + i];
        groupList.clear();
        for (int i = 0; i < (int)groupSizes.size(); ++i)
            groupList.push_back({groupOffsets[i], groupOffsets[i + 1] - groupOffsets[i]});
    }
}

template <typename Mask>
template <bool CheckAllMoves, bool IsRoot>
int BruteForce::MultiMaskSolver<Mask>::solve(
    const CommonSession &common, Workspace::MultiMask &multiMask, Workspace::MultiMask::Session<Mask> &session,
    const std::vector<ConfigId> &configs, int start, int count, int need, int depth,
    FlatHashTable<U128, int, U128Hash> &table, Result &result, Workspace::MultiMask::Worker &workspace, const BruteForce::Config &config) {
    // 位掩码后端与 Common 使用相同的阈值协议和动作语义，掩码只替换雷位集合的表示。
    ++session.nodes;
    if constexpr (CheckAllMoves && IsRoot) {
        // 全候选模式逐个精确计算首步；此分支只由 BruteForce::Config::checkAllMoves 启用。
        if (need > count)
            return -count;
        Workspace::MultiMask::Layer &layer = workspace.scratch.layer(depth);
        result.moves.resize(common.candidateCount);
        std::vector<int> &order = orderCandidates(common, session, configs, start, count, depth, workspace);
        if (config.solver == BruteForce::Config::Solver::BitwiseRootParallel && config.threadLimit > 1 && count >= BruteForce::parallelConfigThreshold)
            return MultiMaskParallelSolver<Mask>::template solveCandidates<true>(common, multiMask, session, configs,
                start, count, need, result, workspace, config);
        int best = 0;
        for (int candidate : order) {
            for (std::vector<ConfigId> &group : layer.groups)
                group.clear();
            groupByReveal(session, configs, start, count, candidate, layer.groups);
            session.unopenedCandidates.reset(candidate);
            int wins = 0;
            for (int reveal = 0; reveal < 9; ++reveal)
                if (!layer.groups[reveal].empty()) {
                    const int value = solve<false, false>(common, multiMask, session, layer.groups[reveal], 0,
                                                          layer.groups[reveal].size(), 1, depth + 1,
                                                          table, result, workspace, config);
                    if (value > 0)
                        wins += value;
                }
            session.unopenedCandidates.set(candidate);
            result.moves[candidate] = {common.candidates[candidate].cell, wins};
            best = (std::max)(best, wins);
        }
        return best;
    } else {
        if (count <= 1) {
            // 方案数不超过一时可直接返回；恰有一个方案的根节点展示其下标最小的安全候选。
            if (need > count)
                return -count;
            if constexpr (IsRoot)
                if (count == 1)
                    writeFirstSafeMove(common, session, configs[start], result);
            return count;
        }
        if (need > count)
            return -count;
        if constexpr (!IsRoot)
            if (count == 2) {
                // 两种布局只需找到一个共同安全且揭示值不同的格子即可全部分开；否则最多保证赢一种。
                Mask safe = session.unopenedCandidates;
                safe &= ~session.layout.mineMaskByConfig[configs[start]];
                safe &= ~session.layout.mineMaskByConfig[configs[start + 1]];
                while (safe.any()) {
                    const int candidate = safe.firstSetBit();
                    if (session.layout.revealByConfig[configs[start]][candidate] !=
                        session.layout.revealByConfig[configs[start + 1]][candidate])
                        return 2;
                    safe.reset(candidate);
                }
                return need <= 1 ? 1 : -1;
            }
        const U128 key = workspace.useSharedCache ? multiMask.parallel.sharedCache.hashConfigs(configs, start, count) :
            BruteForce::hashConfigs(configs, start, count);
        const bool shared = workspace.useSharedCache && count >= BruteForce::sharedConfigThreshold;
        // 只有根并行的较大子问题走分片缓存；较小子问题用各线程私有表，避免锁开销。
        int cached = 0;
        if (shared)
            cached = multiMask.parallel.sharedCache.find(key);
        else if (const int *value = table.find(key))
            cached = *value;
        if (cached != 0) {
            if (cached > 0)
                return cached >= need ? cached : -cached;
            if (-cached < need)
                return cached;
        }

        Workspace::MultiMask::Layer &layer = workspace.scratch.layer(depth);
        Mask safeMask = session.unopenedCandidates;
        for (int i = start; i < start + count; ++i) {
            safeMask &= ~session.layout.mineMaskByConfig[configs[i]];
            if (!safeMask.any())
                break;
        }
        if (safeMask.any()) {
            // 将共同安全格全部从后续候选中移除，恢复掩码在所有递归返回路径之后执行。
            if constexpr (IsRoot) {
                const int candidate = safeMask.firstSetBit();
                result.moves[0].cell = common.candidates[candidate].cell;
            }
            session.unopenedCandidates &= ~safeMask;
            if (safeMask.popcount() <= 3)
                groupSafeConfigs<true>(session, configs, start, count, safeMask, layer, workspace);
            else
                groupSafeConfigs<false>(session, configs, start, count, safeMask, layer, workspace);
            std::vector<std::pair<int, int>> &groupList = layer.safeGroupList;
            std::sort(groupList.begin(), groupList.end(), [](const std::pair<int, int> &left,
                                                             const std::pair<int, int> &right) {
                if (left.second != right.second)
                    return left.second > right.second;
                return left.first < right.first;
            });
            int wins = 0;
            int remaining = count;
            bool reached = true;
            int upper = 0;
            for (const std::pair<int, int> &group : groupList) {
                remaining -= group.second;
                if (wins + group.second + remaining < need) {
                    upper = wins + group.second + remaining;
                    reached = false;
                    break;
                }
                const int value = solve<false, false>(common, multiMask, session, layer.safeGroupedConfigs, group.first,
                                                      group.second, (std::max)(1, need - wins - remaining),
                                                      depth + 1, table, result, workspace, config);
                if (value <= 0) {
                    upper = wins - value + remaining;
                    reached = false;
                    break;
                }
                wins += value;
            }
            session.unopenedCandidates |= safeMask;
            if (!reached) {
                if (shared)
                    multiMask.parallel.sharedCache.store(key, -upper);
                else
                    BruteForce::saveFail(key, upper, count, table);
                return -upper;
            }
            if (shared)
                multiMask.parallel.sharedCache.store(key, wins);
            else
                table[key] = wins;
            return wins;
        }

        std::vector<int> &order = orderCandidates(common, session, configs, start, count, depth, workspace);
        if constexpr (IsRoot)
            if (config.solver == BruteForce::Config::Solver::BitwiseRootParallel && config.threadLimit > 1 && count >= BruteForce::parallelConfigThreshold)
                return MultiMaskParallelSolver<Mask>::template solveCandidates<false>(common, multiMask, session, configs,
                    start, count, need, result, workspace, config);
        std::vector<int> &deaths = layer.deaths;
        std::array<std::vector<ConfigId>, 9> &groups = layer.groups;
        int best = 0;
        int upper = 0;
        for (int candidate : order) {
            const int target = (std::max)(best + 1, need);
            if (count - deaths[candidate] < target) {
                upper = (std::max)(upper, count - deaths[candidate]);
                break;
            }
            int reveals = 0;
            // reveal==9 表示该方案中点击会踩雷；其余揭示值用 9 位集合判断是否至少分成两组。
            for (int i = start; i < start + count; ++i) {
                const int reveal = session.layout.revealByConfig[configs[i]][candidate];
                if (reveal != 9)
                    reveals |= 1 << reveal;
                if (reveals & (reveals - 1))
                    break;
            }
            // 没有两个不同的安全揭示结果时，点击只会丢掉部分布局而不增加信息，不能改善后续可赢数。
            if ((reveals & (reveals - 1)) == 0)
                continue;
            for (std::vector<ConfigId> &group : groups)
                group.clear();
            groupByReveal(session, configs, start, count, candidate, groups);
            std::vector<std::pair<int, int>> &groupList = layer.groupList;
            groupList.clear();
            for (int reveal = 0; reveal < 9; ++reveal)
                if (!groups[reveal].empty()) {
                    const int groupSize = groups[reveal].size();
                    groupList.push_back({reveal, groupSize});
                }
            std::sort(groupList.begin(), groupList.end(), [](const std::pair<int, int> &left,
                                                             const std::pair<int, int> &right) {
                if (left.second != right.second)
                    return left.second > right.second;
                return left.first < right.first;
            });
            session.unopenedCandidates.reset(candidate);
            int wins = 0;
            int remaining = count - deaths[candidate];
            bool reached = true;
            int moveUpper = 0;
            for (const std::pair<int, int> &group : groupList) {
                const int size = group.second;
                remaining -= size;
                if (wins + size + remaining < target) {
                    moveUpper = wins + size + remaining;
                    reached = false;
                    break;
                }
                const int value = solve<false, false>(common, multiMask, session, groups[group.first], 0, size,
                                                      (std::max)(1, target - wins - remaining), depth + 1,
                                                      table, result, workspace, config);
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
                    result.moves[0].cell = common.candidates[candidate].cell;
            }
        }
        if (best >= need) {
            if (shared)
                multiMask.parallel.sharedCache.store(key, best);
            else
                table[key] = best;
            return best;
        }
        if (best == 0 && upper == 0) {
            if (shared)
                multiMask.parallel.sharedCache.store(key, 1);
            else
                table[key] = 1;
            if constexpr (IsRoot)
                writeFirstSafeMove(common, session, configs[start], result);
            return 1;
        }
        const int failUpper = (std::max)(best, upper);
        if (shared)
            multiMask.parallel.sharedCache.store(key, -failUpper);
        else
            BruteForce::saveFail(key, failUpper, count, table);
        return -failUpper;
    }
}

template <typename Mask>
BruteForce::Result BruteForce::solveWithMask(const CommonSession &common, const BruteForce::Config &config, Workspace &workspace) {
    Workspace::MultiMask &multiMask = workspace.multiMask;
    if (!std::holds_alternative<Workspace::MultiMask::Layout<Mask>>(multiMask.layouts))
        multiMask.layouts.emplace<Workspace::MultiMask::Layout<Mask>>();
    Workspace::MultiMask::Layout<Mask> &layout = std::get<Workspace::MultiMask::Layout<Mask>>(multiMask.layouts);
    MultiMaskSolver<Mask>::buildSession(common, layout);
    multiMask.configIds.resize(common.possibilityCount);
    for (int config = 0; config < common.possibilityCount; ++config)
        multiMask.configIds[config] = config;
    Workspace::MultiMask::Session<Mask> session{layout, Mask::all(common.candidateCount)};
    Workspace::MultiMask::Worker &worker = multiMask.main;
    worker.scratch.reset();
    worker.cache.clear();
    worker.useSharedCache = false;
    Result result;
    if (config.checkAllMoves) {
        MultiMaskSolver<Mask>::template solve<true, true>(common, multiMask, session, multiMask.configIds, 0,
            common.possibilityCount, 1, 0, worker.cache, result, worker, config);
    } else {
        result.moves.resize(1);
        const int wins = MultiMaskSolver<Mask>::template solve<false, true>(common, multiMask, session, multiMask.configIds, 0,
            common.possibilityCount, 1, 0, worker.cache, result, worker, config);
        if (wins < 1)
            result.moves.clear();
        else
            result.moves[0].wins = wins;
    }
    result.nodes = session.nodes;
    return result;
}

BruteForce::Result BruteForce::solveCommonSession(const CommonSession &common, const BruteForce::Config &config, Workspace &workspace) {
    if (common.possibilityCount == 0 || common.candidateCount == 0)
        return {};
    Result result;
    if (config.solver != BruteForce::Config::Solver::Common && common.possibilityCount > 1 &&
        common.candidateCount <= bitMask<8>::kBitCount) {
        if (common.candidateCount <= bitMask<1>::kBitCount)
            result = solveWithMask<bitMask<1>>(common, config, workspace);
        else if (common.candidateCount <= bitMask<2>::kBitCount)
            result = solveWithMask<bitMask<2>>(common, config, workspace);
        else if (common.candidateCount <= bitMask<4>::kBitCount)
            result = solveWithMask<bitMask<4>>(common, config, workspace);
        else
            result = solveWithMask<bitMask<8>>(common, config, workspace);
    } else {
        workspace.normal.reset();
        result = runNormal(common, config, workspace.normal);
    }
    // 单步模式必须把实际候选格交给调用方。两种情况都会让 cell 失效，且都与胜局数无关：
    // 未取得正胜局数时 moves 被清空；writeFirstSafeMove 只在 configs[start] 一个布局里找非雷
    // 候选，全部为雷时不会写入，cell 停在 -1。candidateCount 非零由上方的提前返回保证。
    if (!config.checkAllMoves && (result.moves.empty() || result.moves[0].cell < 0)) {
        if (result.moves.empty())
            result.moves.push_back({common.candidates[0].cell, 0});
        else
            result.moves[0].cell = common.candidates[0].cell;
    }
    return result;
}

template struct BruteForce::MultiMaskSolver<bitMask<1>>;
template struct BruteForce::MultiMaskSolver<bitMask<2>>;
template struct BruteForce::MultiMaskSolver<bitMask<4>>;
template struct BruteForce::MultiMaskSolver<bitMask<8>>;

// 并行实现跨编译单元调用非根递归；显式实例化使求解模板仍留在本文件。
template int BruteForce::MultiMaskSolver<bitMask<1>>::solve<false, false>(const Workspace::CommonSession &, Workspace::MultiMask &, Workspace::MultiMask::Session<bitMask<1>> &, const std::vector<std::uint32_t> &, int, int, int, int, FlatHashTable<U128, int, U128Hash> &, Result &, Workspace::MultiMask::Worker &, const BruteForce::Config &);
template int BruteForce::MultiMaskSolver<bitMask<2>>::solve<false, false>(const Workspace::CommonSession &, Workspace::MultiMask &, Workspace::MultiMask::Session<bitMask<2>> &, const std::vector<std::uint32_t> &, int, int, int, int, FlatHashTable<U128, int, U128Hash> &, Result &, Workspace::MultiMask::Worker &, const BruteForce::Config &);
template int BruteForce::MultiMaskSolver<bitMask<4>>::solve<false, false>(const Workspace::CommonSession &, Workspace::MultiMask &, Workspace::MultiMask::Session<bitMask<4>> &, const std::vector<std::uint32_t> &, int, int, int, int, FlatHashTable<U128, int, U128Hash> &, Result &, Workspace::MultiMask::Worker &, const BruteForce::Config &);
template int BruteForce::MultiMaskSolver<bitMask<8>>::solve<false, false>(const Workspace::CommonSession &, Workspace::MultiMask &, Workspace::MultiMask::Session<bitMask<8>> &, const std::vector<std::uint32_t> &, int, int, int, int, FlatHashTable<U128, int, U128Hash> &, Result &, Workspace::MultiMask::Worker &, const BruteForce::Config &);

} // 命名空间 mss
