#include "algo/probability_engine/shape_solver/graph_solver/graph_solver.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "core/algorithm/combination.h"

namespace mss {

int ShapeSolverGraph::Layer::frontierValue(const State &state, int slot) const {
    const std::uint64_t word = frontierWords.data[state.wordOffset + slot / 16];
    return (int)((word >> ((slot & 15) * 4)) & 0xf);
}

void ShapeSolverGraph::Layer::reset() {
    states.resize(0);
    counts.resize(0);
    momentBoxes.resize(0);
    momentValues.resize(0);
    frontierWords.resize(0);
    index.clear();
    states.push_back({0, 0, 0});
    counts.push_back({1.0L, 0, 0, -1});
}

void ShapeSolverGraph::Layer::advance(const StepGenerator::StepPlan &plan, Layer &next) const {
    // 按 StepPlan 限定当前 Box 的雷数；合并相同 frontier 与组件总雷数，并传播已闭合 Box 的加权雷数。
    next.states.resize(0);
    next.counts.resize(0);
    next.momentValues.resize(0);
    next.frontierWords.resize(0);
    next.index.clear();
    next.momentBoxes = momentBoxes;
    for (int i = 0; i < plan.closings.size; ++i)
        next.momentBoxes.push_back(plan.closings[i].box);

    for (int stateIndex = 0; stateIndex < (int)states.size; ++stateIndex) {
        const State &state = states.data[stateIndex];
        int minMine = 0;
        int maxMine = plan.boxSize;
        for (int checkIndex = 0; checkIndex < plan.checks.size; ++checkIndex) {
            const StepGenerator::Check &check = plan.checks[checkIndex];
            int partial = 0;
            for (int i = 0; i < check.readCount; ++i)
                partial += frontierValue(state, check.readSlots[i]);
            minMine = (std::max)(minMine, check.sum - partial - check.remainingSize);
            maxMine = (std::min)(maxMine, check.sum - partial);
        }
        for (int mine = minMine; mine <= maxMine; ++mine) {
            const int packedOffset = (int)next.frontierWords.size;
            for (int slot = 0; slot < plan.gather.size; ++slot) {
                if ((slot & 15) == 0)
                    next.frontierWords.push_back(0);
                const int source = plan.gather[slot];
                const std::uint64_t value = source < 0 ? (std::uint64_t)mine : (std::uint64_t)frontierValue(state, source);
                next.frontierWords.data[next.frontierWords.size - 1] |= value << ((slot & 15) * 4);
            }
            U128Hasher hasher;
            for (int i = packedOffset; i < (int)next.frontierWords.size; ++i)
                hasher.mix(next.frontierWords.data[i]);
            const U128 hash = hasher.finalize();
            State *target;
            if (const int *found = next.index.find(hash)) {
                next.frontierWords.size = (std::size_t)packedOffset;
                target = &next.states.data[*found];
            } else {
                const int id = (int)next.states.size;
                next.states.push_back({packedOffset, -1, -1});
                next.index.emplace(hash, id);
                target = &next.states.data[id];
            }
            const long double factor = Combination::chooseSmall(plan.boxSize, mine);
            const bool factorIsOne = factor == 1.0L;
            for (int sourceIndex = state.firstCount; sourceIndex >= 0; sourceIndex = counts.data[sourceIndex].next) {
                const Count &source = counts.data[sourceIndex];
                const int mineCount = source.mineCount + mine;
                int targetIndex = target->firstCount;
                while (targetIndex >= 0 && next.counts.data[targetIndex].mineCount != mineCount)
                    targetIndex = next.counts.data[targetIndex].next;
                const bool firstContribution = targetIndex < 0;
                if (firstContribution) {
                    targetIndex = (int)next.counts.size;
                    next.counts.push_back({0.0L, mineCount, (int)next.momentValues.size, -1});
                    if (target->lastCount >= 0)
                        next.counts.data[target->lastCount].next = targetIndex;
                    else
                        target->firstCount = targetIndex;
                    target->lastCount = targetIndex;
                    next.momentValues.resize((std::size_t)(next.momentValues.size + next.momentBoxes.size));
                }
                Count &targetCount = next.counts.data[targetIndex];
                const long double ways = factorIsOne ? source.ways : source.ways * factor;
                targetCount.ways += ways;
                for (int slot = 0; slot < (int)momentBoxes.size; ++slot) {
                    const long double contribution = momentValues.data[source.momentOffset + slot] * (factorIsOne ? 1.0L : factor);
                    long double &targetValue = next.momentValues.data[targetCount.momentOffset + slot];
                    targetValue = firstContribution ? contribution : targetValue + contribution;
                }
                for (int i = 0; i < plan.closings.size; ++i) {
                    const StepGenerator::Closing &closing = plan.closings[i];
                    const long double boxMine = closing.oldSlot < 0 ? (long double)mine : (long double)frontierValue(state, closing.oldSlot);
                    const long double contribution = boxMine * ways;
                    long double &targetValue = next.momentValues.data[targetCount.momentOffset + momentBoxes.size + i];
                    targetValue = firstContribution ? contribution : targetValue + contribution;
                }
            }
        }
    }
}

void ShapeSolverGraph::ObservedLayer::reset() {
    states.resize(0);
    counts.resize(0);
    frontierValues.resize(0);
    index.clear();
    states.push_back({0, 0, 0});
    counts.push_back({1.0L, 0, 0, -1});
}

ShapeSolverGraph::ObservedCount &ShapeSolverGraph::ObservedLayer::findOrAddCount(State &state, int componentMines, int neighborMines) {
    for (int index = state.firstCount; index >= 0; index = counts.data[index].next)
        if (counts.data[index].componentMines == componentMines && counts.data[index].neighborMines == neighborMines)
            return counts.data[index];
    const int index = (int)counts.size;
    counts.push_back({0.0L, componentMines, neighborMines, -1});
    if (state.lastCount >= 0)
        counts.data[state.lastCount].next = index;
    else
        state.firstCount = index;
    state.lastCount = index;
    return counts.data[index];
}

void ShapeSolverGraph::ObservedLayer::advance(const StepGenerator::StepPlan &plan, ObservedLayer &next, const int *adjacentBoxCells, Structure::BoxId xBox) const {
    // 推进同一 frontier 转移，同时把当前 Box 的雷分到点击格邻居和其余格中。
    next.states.resize(0);
    next.counts.resize(0);
    next.frontierValues.resize(0);
    next.index.clear();
    const int adjacent = adjacentBoxCells[plan.box];
    const bool isXBox = plan.box == xBox;
    const int size = plan.boxSize;
    const int pool = isXBox ? size - 1 : size;
    for (int stateIndex = 0; stateIndex < (int)states.size; ++stateIndex) {
        const State &state = states.data[stateIndex];
        int minMine = 0;
        int maxMine = plan.boxSize;
        for (int checkIndex = 0; checkIndex < plan.checks.size; ++checkIndex) {
            const StepGenerator::Check &check = plan.checks[checkIndex];
            int partial = 0;
            for (int i = 0; i < check.readCount; ++i)
                partial += frontierValues.data[state.wordOffset + check.readSlots[i]];
            minMine = (std::max)(minMine, check.sum - partial - check.remainingSize);
            maxMine = (std::min)(maxMine, check.sum - partial);
        }
        for (int mine = minMine; mine <= maxMine; ++mine) {
            U128Hasher hasher;
            for (int slot = 0; slot < plan.gather.size; ++slot) {
                const int source = plan.gather[slot];
                const char value = source < 0 ? (char)mine : frontierValues.data[state.wordOffset + source];
                hasher.mix((std::uint64_t)(unsigned char)value);
            }
            const U128 hash = hasher.finalize();
            State *target;
            if (const int *found = next.index.find(hash)) {
                target = &next.states.data[*found];
            } else {
                const int id = (int)next.states.size;
                next.states.push_back({(int)next.frontierValues.size, -1, -1});
                for (int slot = 0; slot < plan.gather.size; ++slot) {
                    const int source = plan.gather[slot];
                    const char value = source < 0 ? (char)mine : frontierValues.data[state.wordOffset + source];
                    next.frontierValues.push_back(value);
                }
                next.index.emplace(hash, id);
                target = &next.states.data[id];
            }
            if (!isXBox && adjacent == 0) {
                const long double factor = Combination::chooseSmall(size, mine);
                for (int sourceIndex = state.firstCount; sourceIndex >= 0; sourceIndex = counts.data[sourceIndex].next) {
                    const ObservedCount &source = counts.data[sourceIndex];
                    ObservedCount &destination = next.findOrAddCount(*target, source.componentMines + mine, source.neighborMines);
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
                for (int sourceIndex = state.firstCount; sourceIndex >= 0; sourceIndex = counts.data[sourceIndex].next) {
                    const ObservedCount &source = counts.data[sourceIndex];
                    ObservedCount &destination = next.findOrAddCount(*target, source.componentMines + mine, source.neighborMines + neighborMines);
                    destination.ways += source.ways * factor;
                }
            }
        }
    }
}

static int shapeBoxCount(const Structure::Shape &shape) { return (int)shape.boxSizes.count; }

ShapeSolverType::Distribution ShapeSolverGraph::analyze(const Structure::Shape &shape, const Structure::Pool &shapes, const ShapeSolverConfig &config, vectorPool<long double> &values, Workspace &workspace) {
    const int boxCount = shapeBoxCount(shape);
    vector<int> &offsets = workspace.steps.adjacency.offsets;
    vector<Structure::BoxId> &adjacent = workspace.steps.adjacency.adjacent;
    vector<Structure::BoxId> &order = workspace.steps.order.values;
    StepGenerator::buildAdjacency(shape, shapes, offsets, adjacent, workspace.steps);
    StepGenerator::makeOrder(offsets, adjacent, config, order, workspace.steps);

    Layer *current = &workspace.current;
    Layer *next = &workspace.next;
    current->reset();
    next->reset();
    StepGenerator::Sequence &sequence = workspace.steps.sequence;
    const span<const Structure::BoxId> orderSpan(order.data, (int)order.size);
    StepGenerator::prepareSteps(shape, shapes, orderSpan, sequence);
    for (int step = 0; step < orderSpan.size; ++step) {
        const StepGenerator::StepPlan plan = StepGenerator::makeStep(shape, shapes, orderSpan, step, sequence);
        current->advance(plan, *next);
        std::swap(current, next);
    }
    ShapeSolverType::Distribution result;
    result.boxCount = boxCount;
    if (current->states.size == 0) {
        result.ways = {0, 0};
        result.expectations = {0, 0};
        result.start = 0;
        return result;
    }
    int start = (std::numeric_limits<int>::max)();
    int end = -1;
    for (int stateIndex = 0; stateIndex < (int)current->states.size; ++stateIndex) {
        const State &layerState = current->states.data[stateIndex];
        for (int index = layerState.firstCount; index >= 0; index = current->counts.data[index].next) {
            start = (std::min)(start, current->counts.data[index].mineCount);
            end = (std::max)(end, current->counts.data[index].mineCount);
        }
    }

    const int distributionSize = end - start + 1;
    vector<int> &momentSlot = workspace.momentSlot;
    momentSlot.resize(boxCount);
    std::fill(momentSlot.data, momentSlot.data + boxCount, -1);
    for (int slot = 0; slot < (int)current->momentBoxes.size; ++slot)
        momentSlot.data[current->momentBoxes.data[slot]] = slot;
    result.ways = values.push_back();
    for (int i = 0; i < distributionSize; ++i)
        values.push_back(result.ways, 0.0L);
    vector<long double> &moments = workspace.moments;
    moments.resize((std::size_t)distributionSize * boxCount);
    std::fill(moments.data, moments.data + moments.size, 0.0L);
    for (int stateIndex = 0; stateIndex < (int)current->states.size; ++stateIndex) {
        const State &layerState = current->states.data[stateIndex];
        for (int index = layerState.firstCount; index >= 0; index = current->counts.data[index].next) {
            const Count &count = current->counts.data[index];
            const int mineIndex = count.mineCount - start;
            values.data.data[result.ways.offset + mineIndex] += count.ways;
            for (int box = 0; box < boxCount; ++box)
                moments.data[(std::size_t)mineIndex * boxCount + box] += current->momentValues.data[count.momentOffset + momentSlot.data[box]];
        }
    }
    result.expectations = values.push_back();
    for (int mineIndex = 0; mineIndex < distributionSize; ++mineIndex) {
        const long double weight = values.data.data[result.ways.offset + mineIndex];
        for (int box = 0; box < boxCount; ++box)
            values.push_back(result.expectations, weight == 0.0L ? 0.0L : moments.data[(std::size_t)mineIndex * boxCount + box] / weight);
    }
    result.start = start;
    return result;
}

void ShapeSolverGraph::observe(const Structure::Shape &shape, const Structure::Pool &shapes, span<const int> adjacentBoxCells, Structure::BoxId xBox, const ShapeSolverConfig &config, ShapeSolverType::Observed &result, Workspace &workspace) {
    vector<int> &offsets = workspace.steps.adjacency.offsets;
    vector<Structure::BoxId> &adjacent = workspace.steps.adjacency.adjacent;
    vector<Structure::BoxId> &order = workspace.steps.order.values;
    StepGenerator::buildAdjacency(shape, shapes, offsets, adjacent, workspace.steps);
    StepGenerator::makeOrder(offsets, adjacent, config, order, workspace.steps);

    result.values.data.resize(0);
    ObservedLayer *current = &workspace.observedCurrent;
    ObservedLayer *next = &workspace.observedNext;
    current->reset();
    next->reset();
    StepGenerator::Sequence &sequence = workspace.steps.sequence;
    const span<const Structure::BoxId> orderSpan(order.data, (int)order.size);
    StepGenerator::prepareSteps(shape, shapes, orderSpan, sequence);
    for (int step = 0; step < orderSpan.size; ++step) {
        const StepGenerator::StepPlan plan = StepGenerator::makeStep(shape, shapes, orderSpan, step, sequence);
        current->advance(plan, *next, adjacentBoxCells.data, xBox);
        std::swap(current, next);
    }
    int starts[9];
    int ends[9];
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        starts[neighborMines] = (std::numeric_limits<int>::max)();
        ends[neighborMines] = -1;
    }
    for (int stateIndex = 0; stateIndex < (int)current->states.size; ++stateIndex) {
        const State &layerState = current->states.data[stateIndex];
        for (int index = layerState.firstCount; index >= 0; index = current->counts.data[index].next) {
            const ObservedCount &count = current->counts.data[index];
            starts[count.neighborMines] = (std::min)(starts[count.neighborMines], count.componentMines);
            ends[count.neighborMines] = (std::max)(ends[count.neighborMines], count.componentMines);
        }
    }
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        result.start[neighborMines] = ends[neighborMines] < 0 ? 0 : starts[neighborMines];
        result.ways[neighborMines] = result.values.push_back();
        for (int mine = starts[neighborMines]; mine <= ends[neighborMines]; ++mine)
            result.values.push_back(result.ways[neighborMines], 0.0L);
    }
    for (int stateIndex = 0; stateIndex < (int)current->states.size; ++stateIndex) {
        const State &layerState = current->states.data[stateIndex];
        for (int index = layerState.firstCount; index >= 0; index = current->counts.data[index].next) {
            const ObservedCount &count = current->counts.data[index];
            result.values.data.data[result.ways[count.neighborMines].offset + count.componentMines - result.start[count.neighborMines]] += count.ways;
        }
    }
}

} // 命名空间 mss
