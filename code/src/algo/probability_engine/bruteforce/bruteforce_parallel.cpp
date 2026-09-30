#include "algo/probability_engine/bruteforce/bruteforce_parallel.h"

#include <algorithm>
#include <atomic>

#include "core/config.h"

namespace mss {

void BruteForce::Workspace::MultiMask::Parallel::SharedCache::reset(int configCount) {
    hashes.resize(configCount);
    for (int config = 0; config < configCount; ++config)
        hashes[config] = splitmix64(config);
    for (Shard &shard : shards)
        shard.table.clear();
}

U128 BruteForce::Workspace::MultiMask::Parallel::SharedCache::hashConfigs(
    const std::vector<ConfigId> &configs, int start, int count) const {
    // hashes 预先按 ConfigId 计算；求和与异或不依赖排列，和单线程缓存使用同一键定义。
    U128 hash{(std::uint64_t)count, (std::uint64_t)count};
    for (int i = start; i < start + count; ++i) {
        const std::uint64_t value = hashes[configs[i]];
        hash.lo += value;
        hash.hi ^= value;
    }
    hash.lo = splitmix64(hash.lo);
    return hash;
}

int BruteForce::Workspace::MultiMask::Parallel::SharedCache::find(const U128 &key) {
    Shard &shard = shards[key.lo % shards.size()];
    std::lock_guard<std::mutex> lock(shard.mutex);
    const int *value = shard.table.find(key);
    return value == nullptr ? 0 : *value;
}

void BruteForce::Workspace::MultiMask::Parallel::SharedCache::store(const U128 &key, int value) {
    Shard &shard = shards[key.lo % shards.size()];
    std::lock_guard<std::mutex> lock(shard.mutex);
    int &old = shard.table[key];
    if (old == 0 || value > 0 || (old < 0 && value > old))
        old = value;
}

template <typename Mask>
template <bool CheckAllMoves>
int BruteForce::MultiMaskParallelSolver<Mask>::solveCandidates(
    const Workspace::CommonSession &common, Workspace::MultiMask &multiMask,
    Workspace::MultiMask::Session<Mask> &session, const std::vector<ConfigId> &configs,
    int start, int count, int need, Result &result, Workspace::MultiMask::Worker &workspace) {
    // 根排序和并行临时数组放在工作区；各线程独占递归缓存，布局表通过 Session 只读共享。
    Workspace::MultiMask::Parallel &parallel = multiMask.parallel;
    const Workspace::MultiMask::Layer &root = workspace.scratch.layer(0);
    parallel.order = root.order;
    parallel.deaths = root.deaths;
    const int candidateCount = (int)parallel.order.size();
    const int possibilityCount = count;
    const int threadCount = (std::min)(maxThread, candidateCount);
    parallel.uppers.assign(candidateCount, 0);
    parallel.nodes.resize(threadCount);
    parallel.sharedCache.reset(common.possibilityCount);
    if ((int)parallel.workers.size() < threadCount - 1)
        parallel.workers.resize(threadCount - 1);
    parallel.threads.clear();
    parallel.threads.reserve(threadCount - 1);
    std::atomic<int> next{0};
    std::atomic<long long> best{0};
    // 每个 worker 只写自己的节点数、候选上界；全候选模式下各线程写不同的结果槽。
    auto work = [&](int workerIndex) {
        Workspace::MultiMask::Session<Mask> workerSession = session;
        workerSession.nodes = 0;
        Result localResult;
        Workspace::MultiMask::Worker &workerWorkspace = workerIndex == 0 ? multiMask.main : parallel.workers[workerIndex - 1];
        workerWorkspace.scratch.reset();
        workerWorkspace.cache.clear();
        workerWorkspace.useSharedCache = true;
        Workspace::MultiMask::Layer &layer = workerWorkspace.scratch.layer(0);
        for (;;) {
            const int index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= candidateCount)
                break;
            const int candidate = parallel.order[index];
            const long long previous = best.load(std::memory_order_relaxed);
            const int previousWins = (int)(previous / (candidateCount + 1));
            const int previousIndex = candidateCount - (int)(previous % (candidateCount + 1));
            const int target = CheckAllMoves ? 1 : (std::max)(need, previousWins + (index > previousIndex));
            if (!CheckAllMoves && possibilityCount - parallel.deaths[candidate] < target) {
                parallel.uppers[index] = possibilityCount - parallel.deaths[candidate];
                continue;
            }
            for (std::vector<ConfigId> &group : layer.groups)
                group.clear();
            const int groupCount = MultiMaskSolver<Mask>::groupByReveal(workerSession, configs, start, count, candidate, layer.groups);
            if (!CheckAllMoves && groupCount <= 1)
                continue;
            layer.groupList.clear();
            for (int reveal = 0; reveal < 9; ++reveal)
                if (!layer.groups[reveal].empty()) {
                    const int groupSize = (int)layer.groups[reveal].size();
                    layer.groupList.push_back({reveal, groupSize});
                }
            std::sort(layer.groupList.begin(), layer.groupList.end(), [](const std::pair<int, int> &left,
                                                                         const std::pair<int, int> &right) {
                if (left.second != right.second)
                    return left.second > right.second;
                return left.first < right.first;
            });
            workerSession.unopenedCandidates.reset(candidate);
            bool reached = true;
            int wins = 0;
            int upper = 0;
            int remaining = possibilityCount - parallel.deaths[candidate];
            for (const std::pair<int, int> &groupEntry : layer.groupList) {
                const long long current = best.load(std::memory_order_relaxed);
                const int required = CheckAllMoves ? 1 : (std::max)(need, (int)(current / (candidateCount + 1)) +
                    (index > candidateCount - (int)(current % (candidateCount + 1))));
                remaining -= groupEntry.second;
                upper = wins + groupEntry.second + remaining;
                if (upper < required) {
                    reached = false;
                    break;
                }
                const int value = MultiMaskSolver<Mask>::template solve<false, false>(common, multiMask, workerSession,
                    layer.groups[groupEntry.first], 0, layer.groups[groupEntry.first].size(),
                    (std::max)(1, required - wins - remaining), 1, workerWorkspace.cache, localResult, workerWorkspace);
                if (value <= 0) {
                    upper = wins - value + remaining;
                    reached = false;
                    break;
                }
                wins += value;
            }
            workerSession.unopenedCandidates.set(candidate);
            if constexpr (CheckAllMoves)
                result.moves[candidate] = {common.candidates[candidate].cell, wins};
            if (!reached) {
                parallel.uppers[index] = upper;
                continue;
            }
            const long long value = (long long)wins * (candidateCount + 1) + candidateCount - index;
            long long old = best.load(std::memory_order_relaxed);
            while (old < value && !best.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
        }
        parallel.nodes[workerIndex] = workerSession.nodes;
        workerWorkspace.useSharedCache = false;
    };
    for (int worker = 1; worker < threadCount; ++worker)
        parallel.threads.emplace_back(work, worker);
    work(0);
    for (std::jthread &thread : parallel.threads)
        thread.join();
    parallel.threads.clear();
    for (long long nodes : parallel.nodes)
        session.nodes += nodes;
    const long long value = best.load(std::memory_order_relaxed);
    const int wins = (int)(value / (candidateCount + 1));
    if constexpr (CheckAllMoves)
        return wins;
    // need 至少为 1（Config::minWins 的默认值，并在 solveCommonSession 中断言），因此
    // value 为 0 时 wins >= need 必不成立，下面的解码用不到"从未搜到候选"的哨兵值。
    if (wins >= need) {
        const int candidate = parallel.order[candidateCount - (int)(value % (candidateCount + 1))];
        result.moves[0] = {common.candidates[candidate].cell, wins};
        return wins;
    }
    const int upper = *std::max_element(parallel.uppers.begin(), parallel.uppers.end());
    if (wins == 0 && upper == 0) {
        MultiMaskSolver<Mask>::writeFirstSafeMove(common, session, configs[start], result);
        return 1;
    }
    return -(std::max)(wins, upper);
}

// 只导出四种掩码与两种结果模式，根节点按对应模式调用，递归子节点使用 MultiMaskSolver。
template int BruteForce::MultiMaskParallelSolver<bitMask<1>>::solveCandidates<true>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<1>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<1>>::solveCandidates<false>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<1>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<2>>::solveCandidates<true>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<2>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<2>>::solveCandidates<false>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<2>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<4>>::solveCandidates<true>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<4>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<4>>::solveCandidates<false>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<4>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<8>>::solveCandidates<true>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<8>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);
template int BruteForce::MultiMaskParallelSolver<bitMask<8>>::solveCandidates<false>(const BruteForce::Workspace::CommonSession &, BruteForce::Workspace::MultiMask &, BruteForce::Workspace::MultiMask::Session<bitMask<8>> &, const std::vector<std::uint32_t> &, int, int, int, BruteForce::Result &, BruteForce::Workspace::MultiMask::Worker &);

} // 命名空间 mss
