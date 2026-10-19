#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/assert.h"

namespace mss {

using Adjacency = ShapeSolverGraphStep::Adjacency;
using StepWorkspace = ShapeSolverGraphStep::Workspace;
using StepGenerator = ShapeSolverGraphStep;

static int shapeBoxCount(const Structure::Shape &shape) { return (int)shape.boxSizes.count; }
static const int *shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.boxSizes.data.data + shape.boxSizes.offset; }
static const Structure::Shape::Constraint *shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.constraints.data.data + shape.constraints.offset; }
static const Structure::BoxId *constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes) { return shapes.shapes.boxIds.data.data + constraint.boxIds.offset; }

// Box b 的邻居视图；空邻居返回空 span，不对空存储做指针偏移。
static span<const Structure::BoxId> neighbors(const Adjacency &adjacency, Structure::BoxId box) {
    const int begin = adjacency.offsets.data[box];
    const int end = adjacency.offsets.data[box + 1];
    if (begin == end)
        return {};
    return span<const Structure::BoxId>(adjacency.adjacent.data + begin, end - begin);
}

static void copyOrder(span<const Structure::BoxId> source, vector<Structure::BoxId> &target) {
    target.resize(0);
    for (int i = 0; i < source.size; ++i)
        target.push_back(source[i]);
}

void ShapeSolverGraphStep::Adjacency::swapStorage(Workspace &workspace) {
    offsets.swap(workspace.offsets);
    adjacent.swap(workspace.adjacent);
    order.swap(workspace.order);
}

static std::pair<int, int> orderScore(const Adjacency &adjacency, span<const Structure::BoxId> order, StepWorkspace &workspace) {
    const int boxCount = order.size;
    workspace.remaining.resize(boxCount);
    workspace.selected.resize(boxCount);
    std::fill(workspace.selected.data, workspace.selected.data + boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        workspace.remaining.data[box] = neighbors(adjacency, box).size;
    int frontier = 0;
    int peak = 0;
    int area = 0;
    for (int step = 0; step < boxCount; ++step) {
        const Structure::BoxId box = order[step];
        if (workspace.remaining.data[box] != 0)
            ++frontier;
        const span<const Structure::BoxId> adjacent = neighbors(adjacency, box);
        for (int i = 0; i < adjacent.size; ++i)
            if (workspace.selected.data[adjacent[i]] && workspace.remaining.data[adjacent[i]] == 1)
                --frontier;
        workspace.selected.data[box] = 1;
        for (int i = 0; i < adjacent.size; ++i)
            workspace.remaining.data[adjacent[i]] -= 1;
        peak = (std::max)(peak, frontier);
        area += frontier;
    }
    return {peak, area};
}

static Structure::BoxId farthestBox(const Adjacency &adjacency, Structure::BoxId source, StepWorkspace &workspace) {
    const int boxCount = (int)adjacency.offsets.size - 1;
    workspace.distance.resize(boxCount);
    std::fill(workspace.distance.data, workspace.distance.data + boxCount, -1);
    workspace.queue.resize(0);
    workspace.distance.data[source] = 0;
    workspace.queue.push_back(source);
    Structure::BoxId farthest = source;
    for (int head = 0; head < (int)workspace.queue.size; ++head) {
        const Structure::BoxId box = workspace.queue.data[head];
        if (workspace.distance.data[box] > workspace.distance.data[farthest] || (workspace.distance.data[box] == workspace.distance.data[farthest] && box < farthest))
            farthest = box;
        const span<const Structure::BoxId> adjacent = neighbors(adjacency, box);
        for (int i = 0; i < adjacent.size; ++i)
            if (workspace.distance.data[adjacent[i]] < 0) {
                workspace.distance.data[adjacent[i]] = workspace.distance.data[box] + 1;
                workspace.queue.push_back(adjacent[i]);
            }
    }
    return farthest;
}

static void makeGreedyOrder(const Adjacency &adjacency, Structure::BoxId first, vector<Structure::BoxId> &order, StepWorkspace &workspace) {
    const int boxCount = (int)adjacency.offsets.size - 1;
    order.resize(0);
    workspace.remaining.resize(boxCount);
    workspace.selected.resize(boxCount);
    std::fill(workspace.selected.data, workspace.selected.data + boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        workspace.remaining.data[box] = neighbors(adjacency, box).size;
    for (int step = 0; step < boxCount; ++step) {
        Structure::BoxId best = -1;
        if (step == 0 && first >= 0) {
            best = first;
        } else {
            int bestDelta = 0;
            for (Structure::BoxId candidate = 0; candidate < boxCount; ++candidate) {
                if (workspace.selected.data[candidate])
                    continue;
                const span<const Structure::BoxId> adjacent = neighbors(adjacency, candidate);
                int closes = 0;
                for (int i = 0; i < adjacent.size; ++i)
                    if (workspace.selected.data[adjacent[i]] && workspace.remaining.data[adjacent[i]] == 1)
                        ++closes;
                const int delta = (workspace.remaining.data[candidate] != 0) - closes;
                if (best < 0 || delta < bestDelta) {
                    best = candidate;
                    bestDelta = delta;
                }
            }
        }
        assert_(best >= 0, "ShapeSolverGraph::makeGreedyOrder: 没有可选 Box");
        workspace.selected.data[best] = 1;
        order.push_back(best);
        const span<const Structure::BoxId> adjacent = neighbors(adjacency, best);
        for (int i = 0; i < adjacent.size; ++i)
            workspace.remaining.data[adjacent[i]] -= 1;
    }
}

static void makeWindow3Order(const Adjacency &adjacency, vector<Structure::BoxId> &order, StepWorkspace &workspace) {
    const int boxCount = (int)order.size;
    for (int start = 0; start < boxCount; start += 3) {
        const int length = (std::min)(3, boxCount - start);
        Structure::BoxId candidate[3];
        Structure::BoxId best[3];
        for (int i = 0; i < length; ++i) {
            candidate[i] = order.data[start + i];
            best[i] = candidate[i];
        }
        std::pair<int, int> bestScore = orderScore(adjacency, span<const Structure::BoxId>(order.data, boxCount), workspace);
        auto consider = [&]() {
            for (int i = 0; i < length; ++i)
                order.data[start + i] = candidate[i];
            const std::pair<int, int> score = orderScore(adjacency, span<const Structure::BoxId>(order.data, boxCount), workspace);
            if (score < bestScore) {
                bestScore = score;
                for (int i = 0; i < length; ++i)
                    best[i] = candidate[i];
            }
        };
        if (length == 1) {
            consider();
        } else if (length == 2) {
            if (candidate[1] < candidate[0])
                std::swap(candidate[0], candidate[1]);
            consider();
            std::swap(candidate[0], candidate[1]);
            consider();
        } else {
            std::sort(candidate, candidate + 3);
            do {
                consider();
            } while (std::next_permutation(candidate, candidate + 3));
        }
        for (int i = 0; i < length; ++i)
            order.data[start + i] = best[i];
    }
}

// 从 seed 出发做模拟退火，最优顺序留在 workspace.best。
static void makeSAOrder(const Adjacency &adjacency, span<const Structure::BoxId> seed, const ShapeSolverConfig &config, StepWorkspace &workspace) {
    const int boxCount = seed.size;
    auto nextRandom = [](std::uint64_t &state) {
        const std::uint64_t value = splitmix64(state);
        state += 0x9e3779b97f4a7c15ULL;
        return value;
    };
    auto unitRandom = [&](std::uint64_t &state) { return (nextRandom(state) >> 11) * (1.0 / 9007199254740992.0); };
    auto energy = [](std::pair<int, int> score) { return score.first * 20000 + score.second; };

    vector<Structure::BoxId> &best = workspace.best;
    vector<Structure::BoxId> &current = workspace.current;
    vector<Structure::BoxId> &candidate = workspace.candidate;
    copyOrder(seed, best);
    copyOrder(seed, current);
    copyOrder(seed, candidate);
    std::pair<int, int> bestScore = orderScore(adjacency, span<const Structure::BoxId>(best.data, boxCount), workspace);
    const double cooling = std::pow(config.saEndTemperature / config.saStartTemperature, 1.0 / (config.saIterations - 1));
    for (int round = 0; round < config.saRounds; ++round) {
        std::uint64_t state = 0x9e3779b97f4a7c15ULL + (std::uint64_t)round * 0x6a09e667f3bcc909ULL;
        copyOrder(seed, current);
        for (int perturb = 0; perturb < round; ++perturb) {
            const int left = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            const int right = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            std::swap(current.data[left], current.data[right]);
        }
        std::pair<int, int> currentScore = orderScore(adjacency, span<const Structure::BoxId>(current.data, boxCount), workspace);
        int currentEnergy = energy(currentScore);
        double temperature = config.saStartTemperature;
        for (int iteration = 0; iteration < config.saIterations; ++iteration) {
            copyOrder(span<const Structure::BoxId>(current.data, boxCount), candidate);
            const int left = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            const int right = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            const bool swap = nextRandom(state) % 2 == 0;
            if (swap) {
                std::swap(candidate.data[left], candidate.data[right]);
            } else {
                const Structure::BoxId value = candidate.data[left];
                if (left < right) {
                    for (int i = left; i < right; ++i)
                        candidate.data[i] = candidate.data[i + 1];
                } else if (right < left) {
                    for (int i = left; i > right; --i)
                        candidate.data[i] = candidate.data[i - 1];
                }
                candidate.data[right] = value;
            }
            const std::pair<int, int> candidateScore = orderScore(adjacency, span<const Structure::BoxId>(candidate.data, boxCount), workspace);
            const int candidateEnergy = energy(candidateScore);
            const int difference = currentEnergy - candidateEnergy;
            const bool accept = difference >= 0 || unitRandom(state) < std::exp(difference / temperature);
            if (candidateScore < bestScore) {
                bestScore = candidateScore;
                copyOrder(span<const Structure::BoxId>(candidate.data, boxCount), best);
            }
            if (accept) {
                copyOrder(span<const Structure::BoxId>(candidate.data, boxCount), current);
                currentScore = candidateScore;
                currentEnergy = candidateEnergy;
            }
            temperature *= cooling;
        }
    }
}

static void makeAutoOrder(const Adjacency &adjacency, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, StepWorkspace &workspace) {
    const Structure::BoxId initial = farthestBox(adjacency, 0, workspace);
    makeGreedyOrder(adjacency, initial, order, workspace);
    const std::pair<int, int> bestScore = orderScore(adjacency, span<const Structure::BoxId>(order.data, (int)order.size), workspace);
    if (bestScore.first > config.window3Threshold) {
        copyOrder(span<const Structure::BoxId>(order.data, (int)order.size), workspace.candidate);
        makeWindow3Order(adjacency, workspace.candidate, workspace);
        if (orderScore(adjacency, span<const Structure::BoxId>(workspace.candidate.data, (int)workspace.candidate.size), workspace) < bestScore)
            copyOrder(span<const Structure::BoxId>(workspace.candidate.data, (int)workspace.candidate.size), order);
    }
}

static void makeAutoSAOrder(const Adjacency &adjacency, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, StepWorkspace &workspace) {
    makeAutoOrder(adjacency, config, order, workspace);
    const std::pair<int, int> bestScore = orderScore(adjacency, span<const Structure::BoxId>(order.data, (int)order.size), workspace);
    if (bestScore.first <= config.window3Threshold)
        return;
    makeGreedyOrder(adjacency, -1, workspace.seed, workspace);
    makeSAOrder(adjacency, span<const Structure::BoxId>(workspace.seed.data, (int)workspace.seed.size), config, workspace);
    if (orderScore(adjacency, span<const Structure::BoxId>(workspace.best.data, (int)workspace.best.size), workspace) < bestScore)
        copyOrder(span<const Structure::BoxId>(workspace.best.data, (int)workspace.best.size), order);
}

void ShapeSolverGraphStep::buildAdjacency(const Structure::Shape &shape, const Structure::Pool &shapes, Adjacency &result, StepGenerator::Workspace &workspace) {
    const int boxCount = shapeBoxCount(shape);
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);
    result.offsets.resize(boxCount + 1);
    std::fill(result.offsets.data, result.offsets.data + boxCount + 1, 0);
    workspace.head.resize(boxCount);
    std::fill(workspace.head.data, workspace.head.data + boxCount, -1);
    workspace.to.resize(0);
    workspace.next.resize(0);
    workspace.marked.resize(boxCount);
    std::fill(workspace.marked.data, workspace.marked.data + boxCount, 0);
    for (int constraint = 0; constraint < (int)shape.constraints.count; ++constraint) {
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i)
            for (int j = i + 1; j < memberCount; ++j) {
                workspace.next.push_back(workspace.head.data[members[i]]);
                workspace.to.push_back(members[j]);
                workspace.head.data[members[i]] = (int)workspace.to.size - 1;
                workspace.next.push_back(workspace.head.data[members[j]]);
                workspace.to.push_back(members[i]);
                workspace.head.data[members[j]] = (int)workspace.to.size - 1;
            }
    }
    for (int box = 0; box < boxCount; ++box) {
        for (int edge = workspace.head.data[box]; edge >= 0; edge = workspace.next.data[edge]) {
            const Structure::BoxId target = workspace.to.data[edge];
            if (workspace.marked.data[target])
                continue;
            workspace.marked.data[target] = 1;
            result.offsets.data[box + 1] += 1;
        }
        for (int edge = workspace.head.data[box]; edge >= 0; edge = workspace.next.data[edge])
            workspace.marked.data[workspace.to.data[edge]] = 0;
    }
    for (int box = 0; box < boxCount; ++box)
        result.offsets.data[box + 1] += result.offsets.data[box];
    result.adjacent.resize(result.offsets.data[boxCount]);
    for (int box = 0; box < boxCount; ++box) {
        int write = result.offsets.data[box];
        for (int edge = workspace.head.data[box]; edge >= 0; edge = workspace.next.data[edge]) {
            const Structure::BoxId target = workspace.to.data[edge];
            if (workspace.marked.data[target])
                continue;
            workspace.marked.data[target] = 1;
            result.adjacent.data[write++] = target;
        }
        for (int edge = workspace.head.data[box]; edge >= 0; edge = workspace.next.data[edge])
            workspace.marked.data[workspace.to.data[edge]] = 0;
    }
}

void ShapeSolverGraphStep::makeOrder(const StepGenerator::Adjacency &adjacency, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, StepGenerator::Workspace &workspace) {
    if (config.order == ShapeSolverConfig::Order::Adjacent) {
        makeGreedyOrder(adjacency, farthestBox(adjacency, 0, workspace), order, workspace);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::Window3) {
        makeGreedyOrder(adjacency, farthestBox(adjacency, 0, workspace), order, workspace);
        makeWindow3Order(adjacency, order, workspace);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::SA) {
        makeGreedyOrder(adjacency, -1, workspace.seed, workspace);
        makeSAOrder(adjacency, span<const Structure::BoxId>(workspace.seed.data, (int)workspace.seed.size), config, workspace);
        copyOrder(span<const Structure::BoxId>(workspace.best.data, (int)workspace.best.size), order);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::Auto) {
        makeAutoOrder(adjacency, config, order, workspace);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::AutoSA) {
        makeAutoSAOrder(adjacency, config, order, workspace);
        return;
    }
    assert_(false, "ShapeSolverGraphStep::makeOrder: 无效的消元顺序策略");
}

void ShapeSolverGraphStep::forEachStep(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, StepGenerator::Workspace &workspace, const StepCallback &callback) {
    const int boxCount = order.size;
    const int constraintCount = (int)shape.constraints.count;
    const int *boxSizes = shapeBoxSizes(shape, shapes);
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);

    workspace.position.resize(boxCount);
    for (int step = 0; step < boxCount; ++step)
        workspace.position.data[order[step]] = step;
    workspace.constraintLast.resize(constraintCount);
    std::fill(workspace.constraintLast.data, workspace.constraintLast.data + constraintCount, -1);
    workspace.boxHead.resize(boxCount);
    std::fill(workspace.boxHead.data, workspace.boxHead.data + boxCount, -1);
    workspace.nextLink.resize(0);
    workspace.constraintIds.resize(0);
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i)
            workspace.constraintLast.data[constraint] = (std::max)(workspace.constraintLast.data[constraint], workspace.position.data[members[i]]);
        for (int i = 0; i < memberCount; ++i) {
            workspace.nextLink.push_back(workspace.boxHead.data[members[i]]);
            workspace.constraintIds.push_back(constraint);
            workspace.boxHead.data[members[i]] = (int)workspace.constraintIds.size - 1;
        }
    }
    workspace.closeStep.resize(boxCount);
    for (int box = 0; box < boxCount; ++box)
        workspace.closeStep.data[box] = workspace.position.data[box];
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i)
            workspace.closeStep.data[members[i]] = (std::max)(workspace.closeStep.data[members[i]], workspace.constraintLast.data[constraint]);
    }
    workspace.closeHead.resize(boxCount);
    std::fill(workspace.closeHead.data, workspace.closeHead.data + boxCount, -1);
    workspace.closeNext.resize(boxCount);
    std::fill(workspace.closeNext.data, workspace.closeNext.data + boxCount, -1);
    for (Structure::BoxId box = 0; box < boxCount; ++box) {
        workspace.closeNext.data[box] = workspace.closeHead.data[workspace.closeStep.data[box]];
        workspace.closeHead.data[workspace.closeStep.data[box]] = box;
    }

    workspace.layout.resize(0);
    workspace.layoutNext.resize(0);
    workspace.slotOf.resize(boxCount);
    std::fill(workspace.slotOf.data, workspace.slotOf.data + boxCount, -1);
    for (int step = 0; step < boxCount; ++step) {
        const Structure::BoxId currentBox = order[step];
        const int boxSize = boxSizes[currentBox];
        workspace.checks.resize(0);
        workspace.gather.resize(0);
        workspace.closings.resize(0);
        for (int link = workspace.boxHead.data[currentBox]; link >= 0; link = workspace.nextLink.data[link]) {
            const Structure::Shape::Constraint &constraint = records[workspace.constraintIds.data[link]];
            StepGenerator::Check check;
            check.sum = constraint.sum;
            check.remainingSize = 0;
            check.readCount = 0;
            const Structure::BoxId *members = constraintMembers(constraint, shapes);
            const int memberCount = (int)constraint.boxIds.count;
            for (int i = 0; i < memberCount; ++i) {
                const Structure::BoxId member = members[i];
                if (workspace.position.data[member] < step)
                    check.readSlots[check.readCount++] = workspace.slotOf.data[member];
                else if (workspace.position.data[member] > step)
                    check.remainingSize += boxSizes[member];
            }
            workspace.checks.push_back(check);
        }
        workspace.layoutNext.resize(0);
        for (int oldSlot = 0; oldSlot < (int)workspace.layout.size; ++oldSlot) {
            const Structure::BoxId box = workspace.layout.data[oldSlot];
            if (workspace.closeStep.data[box] == step)
                continue;
            workspace.gather.push_back(oldSlot);
            workspace.layoutNext.push_back(box);
        }
        if (workspace.closeStep.data[currentBox] > step) {
            workspace.gather.push_back(-1);
            workspace.layoutNext.push_back(currentBox);
        }
        for (Structure::BoxId box = workspace.closeHead.data[step]; box >= 0; box = workspace.closeNext.data[box])
            workspace.closings.push_back({box, box == currentBox ? -1 : workspace.slotOf.data[box]});

        const StepGenerator::StepPlan plan{currentBox, boxSize, span<const StepGenerator::Check>(workspace.checks.data, (int)workspace.checks.size), span<const int>(workspace.gather.data, (int)workspace.gather.size), span<const StepGenerator::Closing>(workspace.closings.data, (int)workspace.closings.size)};
        callback(plan);

        for (int i = 0; i < (int)workspace.layout.size; ++i)
            workspace.slotOf.data[workspace.layout.data[i]] = -1;
        for (int slot = 0; slot < (int)workspace.layoutNext.size; ++slot)
            workspace.slotOf.data[workspace.layoutNext.data[slot]] = slot;
        workspace.layout.swap(workspace.layoutNext);
    }
}

} // 命名空间 mss
