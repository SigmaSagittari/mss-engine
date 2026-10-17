#include "algo/probability_engine/bruteforce/bruteforce_normal.h"

#include <algorithm>

namespace mss {

BruteForce::Result BruteForce::runNormal(const CommonSession &common, const BruteForce::Config &config, Workspace::Normal &workspace) {
    // 普通后端预先展开方案×候选格表，使递归热路径只查表和分组，不再遍历邻居链接。
    workspace.mineByConfig.resize(common.possibilityCount, common.candidateCount, 0);
    for (int config = 0; config < common.possibilityCount; ++config)
        for (int i = 0; i < common.minesPerConfig; ++i)
            workspace.mineByConfig[config][common.mineCandidateIds[config][i]] = 1;
    workspace.revealByConfig.resize(common.possibilityCount, common.candidateCount, 0);
    for (int config = 0; config < common.possibilityCount; ++config)
        for (int candidate = 0; candidate < common.candidateCount; ++candidate) {
            int value = common.candidates[candidate].fixedMines;
            const CommonSession::Candidate &current = common.candidates[candidate];
            for (std::uint32_t i = 0; i < current.linksCount; ++i)
                value += workspace.mineByConfig[config][common.links[current.linksOffset + i]];
            workspace.revealByConfig[config][candidate] = value;
        }

    workspace.configIds.resize(common.possibilityCount);
    for (int config = 0; config < common.possibilityCount; ++config)
        workspace.configIds[config] = config;
    workspace.unopenedCandidates.resize(common.candidateCount);
    workspace.unopenedCandidates.setAll();
    Result result;
    if (config.checkAllMoves) {
        result.moves.resize(common.candidateCount);
        NormalSolver::solve<true, true>(common, workspace, workspace.configIds, 0, common.possibilityCount, 1, 0, result);
    } else {
        result.moves.resize(1);
        const int wins = NormalSolver::solve<false, true>(common, workspace, workspace.configIds, 0, common.possibilityCount,
                                                 1, 0, result);
        if (wins < 1)
            result.moves.clear();
        else
            result.moves[0].wins = wins;
    }
    result.nodes = workspace.nodes;
    return result;
}

template <bool CheckAllMoves, bool IsRoot>
int BruteForce::NormalSolver::solve(const CommonSession &common, Workspace::Normal &workspace,
                            const std::vector<ConfigId> &configs, int start, int count, int need, int depth, Result &result) {
    // 返回正数表示精确胜局数；负数表示未达到 need，绝对值是当前可证明的胜局上界。
    ++workspace.nodes;
    if constexpr (CheckAllMoves && IsRoot) {
        if (need > count)
            return -count;
        Workspace::Normal::Layer &layer = workspace.layer(depth);
        std::vector<int> &deaths = layer.deaths;
        deaths.assign(common.candidateCount, 0);
        for (int i = start; i < start + count; ++i)
            for (int mine = 0; mine < common.minesPerConfig; ++mine)
                ++deaths[common.mineCandidateIds[configs[i]][mine]];
        int best = 0;
        std::array<std::vector<ConfigId>, 9> &groups = layer.groups;
        workspace.unopenedCandidates.for_each([&](int candidate) {
            for (std::vector<ConfigId> &group : groups)
                group.clear();
            for (int i = start; i < start + count; ++i) {
                const ConfigId config = configs[i];
                if (!workspace.mineByConfig[config][candidate])
                    groups[workspace.revealByConfig[config][candidate]].push_back(config);
            }
            workspace.unopenedCandidates.reset(candidate);
            int wins = 0;
            for (int reveal = 0; reveal < 9; ++reveal)
                if (!groups[reveal].empty()) {
                    const int value = solve<false, false>(common, workspace, groups[reveal], 0, groups[reveal].size(), 1, depth + 1, result);
                    if (value > 0)
                        wins += value;
                }
            workspace.unopenedCandidates.set(candidate);
            result.moves[candidate] = {common.candidates[candidate].cell, wins};
            best = (std::max)(best, wins);
        });
        return best;
    } else {
        if (count <= 1) {
            if (need > count)
                return -count;
            if constexpr (IsRoot)
                if (count == 1)
                    for (int candidate = 0; candidate < common.candidateCount; ++candidate)
                        if (!workspace.mineByConfig[configs[start]][candidate]) {
                            result.moves[0].cell = common.candidates[candidate].cell;
                            break;
                        }
            return count;
        }
        if (need > count)
            return -count;
        const U128 key = hashConfigs(configs, start, count);
        // 缓存键不含 unopened：递归中被移除的格子都已证明对当前方案集合共同安全，重复点击不会增加信息。
        if (const int *cached = workspace.cache.find(key)) {
            if (*cached >= 0)
                return *cached >= need ? *cached : -*cached;
            if (-*cached < need)
                return *cached;
        }

        Workspace::Normal::Layer &layer = workspace.layer(depth);
        std::vector<int> &deaths = layer.deaths;
        deaths.assign(common.candidateCount, 0);
        for (int i = start; i < start + count; ++i)
            for (int mine = 0; mine < common.minesPerConfig; ++mine)
                ++deaths[common.mineCandidateIds[configs[i]][mine]];
        std::vector<int> &safeCells = layer.safeCells;
        safeCells.clear();
        workspace.unopenedCandidates.for_each([&](int candidate) {
            if (deaths[candidate] == 0)
                safeCells.push_back(candidate);
        });
        if (!safeCells.empty()) {
            // 全体方案共同安全的格子可以一次打开；只按整组揭示向量分桶，递归只保留互异子局面。
            if constexpr (IsRoot)
                result.moves[0].cell = common.candidates[safeCells[0]].cell;
            for (int candidate : safeCells)
                workspace.unopenedCandidates.reset(candidate);
            std::vector<U128> &hashes = layer.safeHashes;
            hashes.clear();
            const int keyLength = safeCells.size();
            for (int i = start; i < start + count; ++i) {
                const ConfigId config = configs[i];
                U128Hasher hasher;
                for (int j = 0; j < keyLength; ++j)
                    hasher.mix((std::uint64_t)workspace.revealByConfig[config][safeCells[j]] * (keyLength + 1) + j);
                hashes.push_back(hasher.finalize());
            }
            std::vector<int> &groupIds = layer.safeGroupIds;
            std::vector<int> &groupSizes = layer.safeGroupSizes;
            std::vector<int> &groupOffsets = layer.safeGroupOffsets;
            std::vector<ConfigId> &groupedConfigs = layer.safeGroupedConfigs;
            std::vector<std::pair<int, int>> &groupList = layer.safeGroupList;
            FlatHashTable<U128, int, U128Hash> &groupTable = workspace.safeGroupTable;
            groupTable.clear();
            groupTable.reserve(hashes.size());
            groupIds.resize(count);
            groupSizes.clear();
            for (int i = 0; i < count; ++i) {
                int &slot = groupTable[hashes[i]];
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
            std::sort(groupList.begin(), groupList.end(), [](const std::pair<int, int> &left, const std::pair<int, int> &right) {
                if (left.second != right.second)
                    return left.second > right.second;
                return left.first < right.first;
            });
            int wins = 0;
            bool bailed = false;
            int upper = 0;
            int remaining = count;
            for (int i = 0; i < (int)groupList.size(); ++i) {
                const int size = groupList[i].second;
                remaining -= size;
                if (wins + size + remaining < need) {
                    upper = wins + size + remaining;
                    bailed = true;
                    break;
                }
                const int value = solve<false, false>(common, workspace, groupedConfigs, groupList[i].first, size,
                                                             (std::max)(1, need - wins - remaining), depth + 1, result);
                if (value <= 0) {
                    upper = wins - value + remaining;
                    bailed = true;
                    break;
                }
                wins += value;
            }
            for (int candidate : safeCells)
                workspace.unopenedCandidates.set(candidate);
            if (bailed) {
                saveFail(key, upper, count, workspace.cache);
                return -upper;
            }
            workspace.cache[key] = wins;
            return wins;
        }

        std::vector<int> &order = layer.order;
        order.clear();
        workspace.unopenedCandidates.for_each([&](int candidate) { order.push_back(candidate); });
        std::sort(order.begin(), order.end(), [&](int left, int right) {
            if (deaths[left] != deaths[right])
                return deaths[left] < deaths[right];
            return left < right;
        });
        int best = 0;
        int upper = 0;
        for (int candidate : order) {
            const int target = (std::max)(best + 1, need);
            if (count - deaths[candidate] < target) {
                upper = (std::max)(upper, count - deaths[candidate]);
                break;
            }
            std::array<std::vector<ConfigId>, 9> &groups = layer.groups;
            for (std::vector<ConfigId> &group : groups)
                group.clear();
            int groupCount = 0;
            for (int i = start; i < start + count; ++i) {
                const ConfigId config = configs[i];
                if (!workspace.mineByConfig[config][candidate]) {
                    const int reveal = workspace.revealByConfig[config][candidate];
                    if (groups[reveal].empty())
                        ++groupCount;
                    groups[reveal].push_back(config);
                }
            }
            // 只产生一个安全揭示组的点击会丢掉雷位方案，却没有区分存活方案，无法优于信息更强的候选。
            if (groupCount <= 1)
                continue;
            std::vector<std::pair<int, int>> &groupList = layer.groupList;
            groupList.clear();
            for (int reveal = 0; reveal < 9; ++reveal)
                if (!groups[reveal].empty()) {
                    const int groupSize = groups[reveal].size();
                    groupList.push_back({reveal, groupSize});
                }
            std::sort(groupList.begin(), groupList.end(), [](const std::pair<int, int> &left, const std::pair<int, int> &right) {
                if (left.second != right.second)
                    return left.second > right.second;
                return left.first < right.first;
            });
            workspace.unopenedCandidates.reset(candidate);
            int wins = 0;
            bool bailed = false;
            int moveUpper = 0;
            int remaining = count - deaths[candidate];
            for (int i = 0; i < (int)groupList.size(); ++i) {
                std::vector<ConfigId> &group = groups[groupList[i].first];
                const int size = groupList[i].second;
                remaining -= size;
                if (wins + size + remaining < target) {
                    moveUpper = wins + size + remaining;
                    bailed = true;
                    break;
                }
                const int value = solve<false, false>(common, workspace, group, 0, size,
                                                             (std::max)(1, target - wins - remaining), depth + 1, result);
                if (value <= 0) {
                    moveUpper = wins - value + remaining;
                    bailed = true;
                    break;
                }
                wins += value;
            }
            workspace.unopenedCandidates.set(candidate);
            if (bailed)
                upper = (std::max)(upper, moveUpper);
            if (!bailed && wins > best) {
                best = wins;
                if constexpr (IsRoot)
                    result.moves[0].cell = common.candidates[candidate].cell;
            }
        }
        if (best >= need) {
            workspace.cache[key] = best;
            return best;
        }
        if (best == 0 && upper == 0) {
            if constexpr (IsRoot)
                for (int candidate = 0; candidate < common.candidateCount; ++candidate)
                    if (!workspace.mineByConfig[configs[start]][candidate]) {
                        result.moves[0].cell = common.candidates[candidate].cell;
                        break;
                    }
            return 1;
        }
        saveFail(key, (std::max)(best, upper), count, workspace.cache);
        return -(std::max)(best, upper);
    }
}


} // 命名空间 mss
