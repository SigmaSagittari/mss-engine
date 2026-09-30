#include "algo/probability_engine/shape_solver/graph_solver/graph_solver.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"
#include "core/utility/combination.h"

namespace mss {
namespace ShapeSolver {

std::uint64_t Workspace::GraphSolverWorkspace::Layer::frontierValue(const State &state, int slot) const {
    const std::uint64_t word = frontierWords[state.frontierOffset + slot / 16];
    return (word >> ((slot & 15) * 4)) & 0xf;
}

void Workspace::GraphSolverWorkspace::Layer::reset() {
    states.clear();
    counts.clear();
    momentBoxes.clear();
    momentValues.clear();
    frontierWords.clear();
    index.clear();
    states.push_back({0, 0, 0});
    counts.push_back({0, 1.0L, 0, -1});
}

template <typename Plan> void Workspace::GraphSolverWorkspace::Layer::advance(const Plan &plan, Layer &nextLayer) const {
    // 按 StepPlan 限定当前 Box 的雷数；合并相同 frontier 与组件总雷数，并传播已闭合 Box 的加权雷数。
    nextLayer.states.clear();
    nextLayer.counts.clear();
    nextLayer.momentValues.clear();
    nextLayer.frontierWords.clear();
    nextLayer.index.clear();
    nextLayer.momentBoxes = momentBoxes;
    for (const typename Plan::Closing &closing : plan.closings)
        nextLayer.momentBoxes.push_back(closing.box);

    for (const State &state : states) {
        int minMine = 0;
        int maxMine = plan.boxSize;
        for (const typename Plan::Check &check : plan.checks) {
            int partial = 0;
            for (int i = 0; i < check.readCount; ++i)
                partial += (int)(frontierValue(state, check.readSlots[i]));
            minMine = (std::max)(minMine, check.sum - partial - check.remainingSize);
            maxMine = (std::min)(maxMine, check.sum - partial);
        }
        for (int mine = minMine; mine <= maxMine; ++mine) {
            const std::size_t packedOffset = nextLayer.frontierWords.size();
            for (std::size_t slot = 0; slot < plan.gather.size(); ++slot) {
                if ((slot & 15) == 0)
                    nextLayer.frontierWords.push_back(0);
                const int source = plan.gather[slot];
                const std::uint64_t value = source < 0 ? mine : frontierValue(state, source);
                nextLayer.frontierWords.back() |= value << ((slot & 15) * 4);
            }
            U128Hasher hasher;
            for (std::size_t i = packedOffset; i < nextLayer.frontierWords.size(); ++i)
                hasher.mix(nextLayer.frontierWords[i]);
            const U128 hash = hasher.finalize();
            State *target;
            if (const std::size_t *found = nextLayer.index.find(hash)) {
                nextLayer.frontierWords.resize(packedOffset);
                target = &nextLayer.states[*found];
            } else {
                const std::size_t id = nextLayer.states.size();
                nextLayer.states.push_back({packedOffset, -1, -1});
                nextLayer.index.emplace(hash, id);
                target = &nextLayer.states.back();
            }
            const long double factor = Combination::chooseSmall(plan.boxSize, mine);
            const bool factorIsOne = factor == 1.0L;
            for (int sourceIndex = state.firstCount; sourceIndex >= 0; sourceIndex = counts[sourceIndex].next) {
                const Count &source = counts[sourceIndex];
                const int mineCount = source.mineCount + mine;
                int targetIndex = target->firstCount;
                while (targetIndex >= 0 && nextLayer.counts[targetIndex].mineCount != mineCount)
                    targetIndex = nextLayer.counts[targetIndex].next;
                const bool firstContribution = targetIndex < 0;
                if (firstContribution) {
                    targetIndex = nextLayer.counts.size();
                    nextLayer.counts.push_back({mineCount, 0.0L, nextLayer.momentValues.size(), -1});
                    if (target->lastCount >= 0)
                        nextLayer.counts[target->lastCount].next = targetIndex;
                    else
                        target->firstCount = targetIndex;
                    target->lastCount = targetIndex;
                    const std::size_t momentSize = nextLayer.momentValues.size() + nextLayer.momentBoxes.size();
                    // 先显式按倍数扩容，再增加逻辑大小，避免小块追加时反复搬迁整段矩数据。
                    if (momentSize > nextLayer.momentValues.capacity())
                        nextLayer.momentValues.reserve((std::max)(momentSize, nextLayer.momentValues.capacity() * 2));
                    nextLayer.momentValues.resize(momentSize);
                }
                Count &targetCount = nextLayer.counts[targetIndex];
                const long double ways = factorIsOne ? source.ways : source.ways * factor;
                targetCount.ways += ways;
                for (int slot = 0; slot < (int)(momentBoxes.size()); ++slot) {
                    const long double contribution = momentValues[source.momentOffset + slot].value * (factorIsOne ? 1.0L : factor);
                    MomentValue &targetValue = nextLayer.momentValues[targetCount.momentOffset + slot];
                    targetValue.value = firstContribution ? contribution : targetValue.value + contribution;
                }
                for (int i = 0; i < (int)(plan.closings.size()); ++i) {
                    const typename Plan::Closing &closing = plan.closings[i];
                    const long double boxMine = closing.oldSlot < 0 ? mine : frontierValue(state, closing.oldSlot);
                    const long double contribution = boxMine * ways;
                    MomentValue &targetValue = nextLayer.momentValues[targetCount.momentOffset + momentBoxes.size() + i];
                    targetValue.value = firstContribution ? contribution : targetValue.value + contribution;
                }
            }
        }
    }
}

DistributionResult GraphSolver::analyze(const Structure::Shape &shape, const Structure::Pool &shapes, const OrderAlgo &algo, Workspace &workspace) {
    const GraphStepGenerator::Graph graph = GraphStepGenerator::Graph::fromShape(shape, shapes, workspace);
    const std::vector<Structure::BoxId> order = GraphStepGenerator::makeOrder(graph, algo, workspace);
    Workspace::GraphSolverWorkspace::Layer *current = &workspace.graph_solver.current;
    Workspace::GraphSolverWorkspace::Layer *next = &workspace.graph_solver.next;
    current->reset();
    next->reset();
    const GraphStepGenerator::Callback callback = [&](const GraphStepGenerator::StepPlan &plan) {
        current->advance(plan, *next);
        Workspace::GraphSolverWorkspace::Layer *previous = current;
        current = next;
        next = previous;
    };
    GraphStepGenerator::forEach(shape, shapes, order, workspace, callback);

    if (current->states.empty())
        return {0, shape.boxes.size, {}, {}};
    int start = (std::numeric_limits<int>::max)();
    int end = -1;
    for (const Workspace::GraphSolverWorkspace::Layer::State &state : current->states)
        for (int index = state.firstCount; index >= 0; index = current->counts[index].next) {
            const Workspace::GraphSolverWorkspace::Layer::Count &count = current->counts[index];
            start = (std::min)(start, count.mineCount);
            end = (std::max)(end, count.mineCount);
        }

    const int boxCount = shape.boxes.size;
    const int distributionSize = end - start + 1;
    std::vector<int> &momentSlot = workspace.graph_solver.momentSlot;
    momentSlot.assign(boxCount, -1);
    for (int slot = 0; slot < (int)(current->momentBoxes.size()); ++slot)
        momentSlot[current->momentBoxes[slot]] = slot;
    std::vector<long double> ways(distributionSize, 0.0L);
    std::vector<long double> &moments = workspace.graph_solver.moments;
    moments.assign((std::size_t)distributionSize * boxCount, 0.0L);
    for (const Workspace::GraphSolverWorkspace::Layer::State &state : current->states)
        for (int index = state.firstCount; index >= 0; index = current->counts[index].next) {
            const Workspace::GraphSolverWorkspace::Layer::Count &count = current->counts[index];
            const int mineIndex = count.mineCount - start;
            ways[mineIndex] += count.ways;
            for (int box = 0; box < boxCount; ++box)
                moments[(std::size_t)mineIndex * boxCount + box] += current->momentValues[count.momentOffset + momentSlot[box]].value;
        }
    std::vector<long double> &expectations = workspace.graph_solver.expectations;
    expectations.assign(moments.size(), 0.0L);
    for (int mineIndex = 0; mineIndex < distributionSize; ++mineIndex)
        if (ways[mineIndex] != 0.0L)
            for (int box = 0; box < boxCount; ++box)
                expectations[(std::size_t)mineIndex * boxCount + box] = moments[(std::size_t)mineIndex * boxCount + box] / ways[mineIndex];
    return {start, boxCount, std::move(ways), expectations};
}

Workspace::ObservedWorkspace::GraphLayer::Count &Workspace::ObservedWorkspace::GraphLayer::findOrAddCount(State &state, int componentMines, int neighborMines) {
    for (int index = state.firstCount; index >= 0; index = counts[index].next)
        if (counts[index].componentMines == componentMines && counts[index].neighborMines == neighborMines)
            return counts[index];
    const int index = counts.size();
    counts.push_back({componentMines, neighborMines, 0.0L, -1});
    if (state.lastCount >= 0)
        counts[state.lastCount].next = index;
    else
        state.firstCount = index;
    state.lastCount = index;
    return counts.back();
}

void Workspace::ObservedWorkspace::GraphLayer::reset() {
    states.clear();
    counts.clear();
    frontierValues.clear();
    index.clear();
    states.push_back({0, 0, 0});
    counts.push_back({0, 0, 1.0L, -1});
}

template <typename Plan> void Workspace::ObservedWorkspace::GraphLayer::advance(const Plan &plan, GraphLayer &nextLayer, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox) const {
    // 推进同一 frontier 转移，同时把当前 Box 的雷分到点击格邻居和其余格中。
    nextLayer.states.clear();
    nextLayer.counts.clear();
    nextLayer.frontierValues.clear();
    nextLayer.index.clear();
    nextLayer.states.reserve(states.size() * (plan.boxSize + 1));
    nextLayer.counts.reserve(counts.size() * (plan.boxSize + 1));
    nextLayer.frontierValues.reserve(frontierValues.size() + plan.boxSize + 1);
    const int adjacent = adjacentBoxCells[plan.box];
    const bool isXBox = plan.box == xBox;
    const int size = plan.boxSize;
    const int pool = isXBox ? size - 1 : size;
    for (const State &state : states) {
        int minMine = 0;
        int maxMine = plan.boxSize;
        for (const typename Plan::Check &check : plan.checks) {
            int partial = 0;
            for (int i = 0; i < check.readCount; ++i)
                partial += frontierValues[state.frontierOffset + check.readSlots[i]];
            minMine = (std::max)(minMine, check.sum - partial - check.remainingSize);
            maxMine = (std::min)(maxMine, check.sum - partial);
        }
        for (int mine = minMine; mine <= maxMine; ++mine) {
            U128Hasher hasher;
            for (int source : plan.gather) {
                const char value = source < 0 ? mine : frontierValues[state.frontierOffset + source];
                hasher.mix((std::uint64_t)(unsigned char)(value));
            }
            const U128 hash = hasher.finalize();
            State *target;
            if (const std::size_t *found = nextLayer.index.find(hash)) {
                target = &nextLayer.states[*found];
            } else {
                const std::size_t id = nextLayer.states.size();
                nextLayer.states.push_back({nextLayer.frontierValues.size(), -1, -1});
                for (int source : plan.gather)
                    nextLayer.frontierValues.push_back(source < 0 ? mine : frontierValues[state.frontierOffset + source]);
                nextLayer.index.emplace(hash, id);
                target = &nextLayer.states.back();
            }
            if (!isXBox && adjacent == 0) {
                const long double factor = Combination::chooseSmall(size, mine);
                for (int sourceIndex = state.firstCount; sourceIndex >= 0; sourceIndex = counts[sourceIndex].next) {
                    const Count &source = counts[sourceIndex];
                    Count &destination = nextLayer.findOrAddCount(*target, source.componentMines + mine, source.neighborMines);
                    destination.ways += source.ways * factor;
                }
                continue;
            }
            const int maxAdjacent = (std::min)(adjacent, mine);
            for (int neighborMines = 0; neighborMines <= maxAdjacent; ++neighborMines) {
                const int remaining = mine - neighborMines;
                if (remaining > pool - adjacent)
                    continue;
                const long double factor = Combination::chooseSmall(adjacent, neighborMines) * Combination::chooseSmall(pool - adjacent, remaining);
                for (int sourceIndex = state.firstCount; sourceIndex >= 0; sourceIndex = counts[sourceIndex].next) {
                    const Count &source = counts[sourceIndex];
                    Count &destination = nextLayer.findOrAddCount(*target, source.componentMines + mine, source.neighborMines + neighborMines);
                    destination.ways += source.ways * factor;
                }
            }
        }
    }
}

void GraphSolver::observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace) {
    const GraphStepGenerator::Graph graph = GraphStepGenerator::Graph::fromShape(shape, shapes, workspace);
    const std::vector<Structure::BoxId> order = GraphStepGenerator::makeOrder(graph, OrderAlgo::Adjacent, workspace);
    Workspace::ObservedWorkspace::GraphLayer *current = &workspace.observed.graph_current;
    Workspace::ObservedWorkspace::GraphLayer *next = &workspace.observed.graph_next;
    current->reset();
    next->reset();
    const GraphStepGenerator::Callback callback = [&](const GraphStepGenerator::StepPlan &plan) {
        current->advance(plan, *next, adjacentBoxCells, xBox);
        Workspace::ObservedWorkspace::GraphLayer *previous = current;
        current = next;
        next = previous;
    };
    GraphStepGenerator::forEach(shape, shapes, order, workspace, callback);

    std::array<int, 9> starts;
    std::array<int, 9> ends;
    starts.fill((std::numeric_limits<int>::max)());
    ends.fill(-1);
    for (const Workspace::ObservedWorkspace::GraphLayer::State &state : current->states)
        for (int index = state.firstCount; index >= 0; index = current->counts[index].next) {
            const Workspace::ObservedWorkspace::GraphLayer::Count &count = current->counts[index];
            starts[count.neighborMines] = (std::min)(starts[count.neighborMines], count.componentMines);
            ends[count.neighborMines] = (std::max)(ends[count.neighborMines], count.componentMines);
        }
    std::size_t totalSize = 0;
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        const std::size_t size = ends[neighborMines] < 0 ? 0 : (std::size_t)(ends[neighborMines] - starts[neighborMines] + 1);
        result.distributions_[neighborMines] = {ends[neighborMines] < 0 ? 0 : starts[neighborMines], totalSize, size};
        totalSize += size;
    }
    result.ways_.assign(totalSize, 0.0L);
    for (const Workspace::ObservedWorkspace::GraphLayer::State &state : current->states)
        for (int index = state.firstCount; index >= 0; index = current->counts[index].next) {
            const Workspace::ObservedWorkspace::GraphLayer::Count &count = current->counts[index];
            const ObversedResult::Distribution &distribution = result.distributions_[count.neighborMines];
            result.ways_[distribution.offset + count.componentMines - distribution.start] += count.ways;
        }
}

}
}
