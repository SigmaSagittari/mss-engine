#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/assert.h"

namespace mss {

using OrderWorkspace = ShapeSolverGraphStep::Workspace::Order;
using StepGenerator = ShapeSolverGraphStep;

static int shapeBoxCount(const Structure::Shape &shape) { return (int)shape.boxSizes.count; }
static const int *shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.boxSizes.data.data + shape.boxSizes.offset; }
static const Structure::Shape::Constraint *shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.constraints.data.data + shape.constraints.offset; }
static const Structure::BoxId *constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes) { return shapes.shapes.boxIds.data.data + constraint.boxIds.offset; }

// Box b 的邻居视图；空邻居返回空 span，不对空存储做指针偏移。
static span<const Structure::BoxId> neighbors(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, Structure::BoxId box) {
    const int begin = offsets.data[box];
    const int end = offsets.data[box + 1];
    if (begin == end)
        return {};
    return span<const Structure::BoxId>(adjacent.data + begin, end - begin);
}

static void copyOrder(span<const Structure::BoxId> source, vector<Structure::BoxId> &target) {
    target.resize(0);
    for (int i = 0; i < source.size; ++i)
        target.push_back(source[i]);
}

static std::pair<int, int> orderScore(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, span<const Structure::BoxId> order, OrderWorkspace::Score &workspace) {
    const int boxCount = order.size;
    workspace.remaining.resize(boxCount);
    workspace.selected.resize(boxCount);
    std::fill(workspace.selected.data, workspace.selected.data + boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        workspace.remaining.data[box] = neighbors(offsets, adjacentBoxes, box).size;
    int frontier = 0;
    int peak = 0;
    int area = 0;
    for (int step = 0; step < boxCount; ++step) {
        const Structure::BoxId box = order[step];
        if (workspace.remaining.data[box] != 0)
            ++frontier;
        const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, box);
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

static Structure::BoxId farthestBox(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, Structure::BoxId source, OrderWorkspace::Bfs &workspace) {
    const int boxCount = (int)offsets.size - 1;
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
        const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, box);
        for (int i = 0; i < adjacent.size; ++i)
            if (workspace.distance.data[adjacent[i]] < 0) {
                workspace.distance.data[adjacent[i]] = workspace.distance.data[box] + 1;
                workspace.queue.push_back(adjacent[i]);
            }
    }
    return farthest;
}

static void makeGreedyOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, Structure::BoxId first, vector<Structure::BoxId> &order, OrderWorkspace::Score &workspace) {
    const int boxCount = (int)offsets.size - 1;
    order.resize(0);
    workspace.remaining.resize(boxCount);
    workspace.selected.resize(boxCount);
    std::fill(workspace.selected.data, workspace.selected.data + boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        workspace.remaining.data[box] = neighbors(offsets, adjacentBoxes, box).size;
    for (int step = 0; step < boxCount; ++step) {
        Structure::BoxId best = -1;
        if (step == 0 && first >= 0) {
            best = first;
        } else {
            int bestDelta = 0;
            for (Structure::BoxId candidate = 0; candidate < boxCount; ++candidate) {
                if (workspace.selected.data[candidate])
                    continue;
                const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, candidate);
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
        const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, best);
        for (int i = 0; i < adjacent.size; ++i)
            workspace.remaining.data[adjacent[i]] -= 1;
    }
}

static void makeWindow3Order(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, vector<Structure::BoxId> &order, OrderWorkspace::Score &workspace) {
    const int boxCount = (int)order.size;
    for (int start = 0; start < boxCount; start += 3) {
        const int length = (std::min)(3, boxCount - start);
        Structure::BoxId candidate[3];
        Structure::BoxId best[3];
        for (int i = 0; i < length; ++i) {
            candidate[i] = order.data[start + i];
            best[i] = candidate[i];
        }
        std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(order.data, boxCount), workspace);
        auto consider = [&]() {
            for (int i = 0; i < length; ++i)
                order.data[start + i] = candidate[i];
            const std::pair<int, int> score = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(order.data, boxCount), workspace);
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

// 从 seed 出发做模拟退火，最优顺序写入 best。
static void makeSAOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, span<const Structure::BoxId> seed, const ShapeSolverConfig &config, vector<Structure::BoxId> &best, OrderWorkspace &workspace) {
    const int boxCount = seed.size;
    auto nextRandom = [](std::uint64_t &state) {
        const std::uint64_t value = splitmix64(state);
        state += 0x9e3779b97f4a7c15ULL;
        return value;
    };
    auto unitRandom = [&](std::uint64_t &state) { return (nextRandom(state) >> 11) * (1.0 / 9007199254740992.0); };
    auto energy = [](std::pair<int, int> score) { return score.first * 20000 + score.second; };

    vector<Structure::BoxId> &current = workspace.current;
    vector<Structure::BoxId> &candidate = workspace.candidate;
    copyOrder(seed, best);
    copyOrder(seed, current);
    copyOrder(seed, candidate);
    std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(best.data, boxCount), workspace.score);
    const double cooling = std::pow(config.sa.endTemperature / config.sa.startTemperature, 1.0 / (config.sa.iterations - 1));
    for (int round = 0; round < config.sa.rounds; ++round) {
        std::uint64_t state = 0x9e3779b97f4a7c15ULL + (std::uint64_t)round * 0x6a09e667f3bcc909ULL;
        copyOrder(seed, current);
        for (int perturb = 0; perturb < round; ++perturb) {
            const int left = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            const int right = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            std::swap(current.data[left], current.data[right]);
        }
        std::pair<int, int> currentScore = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(current.data, boxCount), workspace.score);
        int currentEnergy = energy(currentScore);
        double temperature = config.sa.startTemperature;
        for (int iteration = 0; iteration < config.sa.iterations; ++iteration) {
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
            const std::pair<int, int> candidateScore = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(candidate.data, boxCount), workspace.score);
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

static void makeAutoOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, OrderWorkspace &workspace) {
    const Structure::BoxId initial = farthestBox(offsets, adjacentBoxes, 0, workspace.bfs);
    makeGreedyOrder(offsets, adjacentBoxes, initial, order, workspace.score);
    const std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(order.data, (int)order.size), workspace.score);
    if (bestScore.first > config.window3Threshold) {
        copyOrder(span<const Structure::BoxId>(order.data, (int)order.size), workspace.candidate);
        makeWindow3Order(offsets, adjacentBoxes, workspace.candidate, workspace.score);
        if (orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(workspace.candidate.data, (int)workspace.candidate.size), workspace.score) < bestScore)
            copyOrder(span<const Structure::BoxId>(workspace.candidate.data, (int)workspace.candidate.size), order);
    }
}

static void makeAutoSAOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, OrderWorkspace &workspace) {
    makeAutoOrder(offsets, adjacentBoxes, config, order, workspace);
    const std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(order.data, (int)order.size), workspace.score);
    if (bestScore.first <= config.window3Threshold)
        return;
    makeGreedyOrder(offsets, adjacentBoxes, -1, workspace.seed, workspace.score);
    makeSAOrder(offsets, adjacentBoxes, span<const Structure::BoxId>(workspace.seed.data, (int)workspace.seed.size), config, workspace.best, workspace);
    if (orderScore(offsets, adjacentBoxes, span<const Structure::BoxId>(workspace.best.data, (int)workspace.best.size), workspace.score) < bestScore)
        copyOrder(span<const Structure::BoxId>(workspace.best.data, (int)workspace.best.size), order);
}

void ShapeSolverGraphStep::buildAdjacency(const Structure::Shape &shape, const Structure::Pool &shapes, vector<int> &offsets, vector<Structure::BoxId> &adjacent, StepGenerator::Workspace &workspace) {
    Workspace::Adjacency::Build &build = workspace.adjacency.build;
    const int boxCount = shapeBoxCount(shape);
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);
    offsets.resize(boxCount + 1);
    std::fill(offsets.data, offsets.data + boxCount + 1, 0);
    build.head.resize(boxCount);
    std::fill(build.head.data, build.head.data + boxCount, -1);
    build.to.resize(0);
    build.next.resize(0);
    build.marked.resize(boxCount);
    std::fill(build.marked.data, build.marked.data + boxCount, 0);
    for (int constraint = 0; constraint < (int)shape.constraints.count; ++constraint) {
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i)
            for (int j = i + 1; j < memberCount; ++j) {
                build.next.push_back(build.head.data[members[i]]);
                build.to.push_back(members[j]);
                build.head.data[members[i]] = (int)build.to.size - 1;
                build.next.push_back(build.head.data[members[j]]);
                build.to.push_back(members[i]);
                build.head.data[members[j]] = (int)build.to.size - 1;
            }
    }
    for (int box = 0; box < boxCount; ++box) {
        for (int edge = build.head.data[box]; edge >= 0; edge = build.next.data[edge]) {
            const Structure::BoxId target = build.to.data[edge];
            if (build.marked.data[target])
                continue;
            build.marked.data[target] = 1;
            offsets.data[box + 1] += 1;
        }
        for (int edge = build.head.data[box]; edge >= 0; edge = build.next.data[edge])
            build.marked.data[build.to.data[edge]] = 0;
    }
    for (int box = 0; box < boxCount; ++box)
        offsets.data[box + 1] += offsets.data[box];
    adjacent.resize(offsets.data[boxCount]);
    for (int box = 0; box < boxCount; ++box) {
        int write = offsets.data[box];
        for (int edge = build.head.data[box]; edge >= 0; edge = build.next.data[edge]) {
            const Structure::BoxId target = build.to.data[edge];
            if (build.marked.data[target])
                continue;
            build.marked.data[target] = 1;
            adjacent.data[write++] = target;
        }
        for (int edge = build.head.data[box]; edge >= 0; edge = build.next.data[edge])
            build.marked.data[build.to.data[edge]] = 0;
    }
}

void ShapeSolverGraphStep::makeOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, StepGenerator::Workspace &workspace) {
    Workspace::Order &scratch = workspace.order;
    if (config.order == ShapeSolverConfig::Order::Adjacent) {
        makeGreedyOrder(offsets, adjacent, farthestBox(offsets, adjacent, 0, scratch.bfs), order, scratch.score);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::Window3) {
        makeGreedyOrder(offsets, adjacent, farthestBox(offsets, adjacent, 0, scratch.bfs), order, scratch.score);
        makeWindow3Order(offsets, adjacent, order, scratch.score);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::SA) {
        makeGreedyOrder(offsets, adjacent, -1, scratch.seed, scratch.score);
        makeSAOrder(offsets, adjacent, span<const Structure::BoxId>(scratch.seed.data, (int)scratch.seed.size), config, scratch.best, scratch);
        copyOrder(span<const Structure::BoxId>(scratch.best.data, (int)scratch.best.size), order);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::Auto) {
        makeAutoOrder(offsets, adjacent, config, order, scratch);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::AutoSA) {
        makeAutoSAOrder(offsets, adjacent, config, order, scratch);
        return;
    }
    assert_(false, "ShapeSolverGraphStep::makeOrder: 无效的消元顺序策略");
}

void ShapeSolverGraphStep::prepareSteps(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, Sequence &sequence) {
    Sequence::Constraints &constraints = sequence.constraints;
    Sequence::Close &close = sequence.close;
    Sequence::Frontier &frontier = sequence.frontier;
    const int boxCount = order.size;
    const int constraintCount = (int)shape.constraints.count;
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);

    sequence.position.resize(boxCount);
    for (int step = 0; step < boxCount; ++step)
        sequence.position.data[order[step]] = step;
    constraints.constraintLast.resize(constraintCount);
    std::fill(constraints.constraintLast.data, constraints.constraintLast.data + constraintCount, -1);
    constraints.boxHead.resize(boxCount);
    std::fill(constraints.boxHead.data, constraints.boxHead.data + boxCount, -1);
    constraints.nextLink.resize(0);
    constraints.constraintIds.resize(0);
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i)
            constraints.constraintLast.data[constraint] = (std::max)(constraints.constraintLast.data[constraint], sequence.position.data[members[i]]);
        for (int i = 0; i < memberCount; ++i) {
            constraints.nextLink.push_back(constraints.boxHead.data[members[i]]);
            constraints.constraintIds.push_back(constraint);
            constraints.boxHead.data[members[i]] = (int)constraints.constraintIds.size - 1;
        }
    }
    close.closeStep.resize(boxCount);
    for (int box = 0; box < boxCount; ++box)
        close.closeStep.data[box] = sequence.position.data[box];
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i)
            close.closeStep.data[members[i]] = (std::max)(close.closeStep.data[members[i]], constraints.constraintLast.data[constraint]);
    }
    close.closeHead.resize(boxCount);
    std::fill(close.closeHead.data, close.closeHead.data + boxCount, -1);
    close.closeNext.resize(boxCount);
    std::fill(close.closeNext.data, close.closeNext.data + boxCount, -1);
    for (Structure::BoxId box = 0; box < boxCount; ++box) {
        close.closeNext.data[box] = close.closeHead.data[close.closeStep.data[box]];
        close.closeHead.data[close.closeStep.data[box]] = box;
    }

    frontier.layout.resize(0);
    frontier.layoutNext.resize(0);
    frontier.slotOf.resize(boxCount);
    std::fill(frontier.slotOf.data, frontier.slotOf.data + boxCount, -1);
}

ShapeSolverGraphStep::StepPlan ShapeSolverGraphStep::makeStep(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, int step, Sequence &sequence) {
    const Sequence::Constraints &constraints = sequence.constraints;
    const Sequence::Close &close = sequence.close;
    Sequence::Frontier &frontier = sequence.frontier;
    Sequence::Plan &buffer = sequence.plan;
    const int *boxSizes = shapeBoxSizes(shape, shapes);
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);
    const Structure::BoxId currentBox = order[step];
    const int boxSize = boxSizes[currentBox];
    buffer.checks.resize(0);
    buffer.gather.resize(0);
    buffer.closings.resize(0);
    for (int link = constraints.boxHead.data[currentBox]; link >= 0; link = constraints.nextLink.data[link]) {
        const Structure::Shape::Constraint &constraint = records[constraints.constraintIds.data[link]];
        const int index = (int)buffer.checks.size;
        buffer.checks.resize(index + 1);
        StepGenerator::Check &check = buffer.checks.data[index];
        check.sum = constraint.sum;
        check.remainingSize = 0;
        check.readCount = 0;
        const Structure::BoxId *members = constraintMembers(constraint, shapes);
        const int memberCount = (int)constraint.boxIds.count;
        for (int i = 0; i < memberCount; ++i) {
            const Structure::BoxId member = members[i];
            if (sequence.position.data[member] < step)
                check.readSlots[check.readCount++] = frontier.slotOf.data[member];
            else if (sequence.position.data[member] > step)
                check.remainingSize += boxSizes[member];
        }
    }
    frontier.layoutNext.resize(0);
    for (int oldSlot = 0; oldSlot < (int)frontier.layout.size; ++oldSlot) {
        const Structure::BoxId box = frontier.layout.data[oldSlot];
        if (close.closeStep.data[box] == step)
            continue;
        buffer.gather.push_back(oldSlot);
        frontier.layoutNext.push_back(box);
    }
    if (close.closeStep.data[currentBox] > step) {
        buffer.gather.push_back(-1);
        frontier.layoutNext.push_back(currentBox);
    }
    for (Structure::BoxId box = close.closeHead.data[step]; box >= 0; box = close.closeNext.data[box])
        buffer.closings.push_back({box, box == currentBox ? -1 : frontier.slotOf.data[box]});

    const StepGenerator::StepPlan plan{currentBox, boxSize, span<const StepGenerator::Check>(buffer.checks.data, (int)buffer.checks.size), span<const int>(buffer.gather.data, (int)buffer.gather.size), span<const StepGenerator::Closing>(buffer.closings.data, (int)buffer.closings.size)};

    for (int i = 0; i < (int)frontier.layout.size; ++i)
        frontier.slotOf.data[frontier.layout.data[i]] = -1;
    for (int slot = 0; slot < (int)frontier.layoutNext.size; ++slot)
        frontier.slotOf.data[frontier.layoutNext.data[slot]] = slot;
    frontier.layout.swap(frontier.layoutNext);
    return plan;
}

} // 命名空间 mss
