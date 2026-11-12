#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/assert.h"

namespace mss {

struct ShapeSolverGraphStep::Workspace {
    struct Adjacency {
        struct Build {
            // head/to/next 是按边组织的链表，marked 做邻居去重。
            vector<int> head;
            vector<int> next;
            vector<Structure::BoxId> to;
            vector<char> marked;
        } build;
    } adjacency;
    struct Order {
        vector<Structure::BoxId> candidate;
        vector<Structure::BoxId> best;
        vector<Structure::BoxId> current;
        // SA 的初始顺序，与 current/candidate 分开。
        vector<Structure::BoxId> seed;
        struct Score {
            vector<int> remaining;
            vector<char> selected;
        } score;
        struct Bfs {
            vector<int> distance;
            vector<Structure::BoxId> queue;
        } bfs;
    } order;
};

__declspec(noinline) ShapeSolverGraphStep::Workspace &ShapeSolverGraphStep::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

using StepGenerator = ShapeSolverGraphStep;

const Structure::Shape::Constraint *ShapeSolverGraphStep::shapeConstraints(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes) { return shapes.constraints.data.data + shape.constraints.offset; }
const Structure::BoxId *ShapeSolverGraphStep::constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool::Shapes &shapes) { return shapes.boxIds.data.data + constraint.boxIds.offset; }

// Box b 的邻居视图；空邻居返回空 span，不对空存储做指针偏移。
span<const Structure::BoxId> ShapeSolverGraphStep::neighbors(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, Structure::BoxId box) {
    const int begin = offsets.data[box];
    const int end = offsets.data[box + 1];
    if (begin == end)
        return {};
    return span<const Structure::BoxId>(adjacent.data + begin, end - begin);
}

std::pair<int, int> ShapeSolverGraphStep::orderScore(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const vector<Structure::BoxId> &order) {
    Workspace &workspace = getWorkspace();
    vector<int> &remaining = workspace.order.score.remaining;
    vector<char> &selected = workspace.order.score.selected;
    const int boxCount = order.size;
    remaining.resize(boxCount);
    selected.resize(boxCount);
    std::fill(selected.data, selected.data + boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        remaining.data[box] = neighbors(offsets, adjacentBoxes, box).size;
    int frontier = 0;
    int peak = 0;
    int area = 0;
    for (int step = 0; step < boxCount; ++step) {
        const Structure::BoxId box = order[step];
        if (remaining.data[box] != 0)
            ++frontier;
        const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, box);
        for (int i = 0; i < adjacent.size; ++i)
            if (selected.data[adjacent[i]] && remaining.data[adjacent[i]] == 1)
                --frontier;
        selected.data[box] = 1;
        for (int i = 0; i < adjacent.size; ++i)
            remaining.data[adjacent[i]] -= 1;
        peak = (std::max)(peak, frontier);
        area += frontier;
    }
    return {peak, area};
}

Structure::BoxId ShapeSolverGraphStep::farthestBox(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, Structure::BoxId source) {
    Workspace &workspace = getWorkspace();
    vector<int> &distance = workspace.order.bfs.distance;
    vector<Structure::BoxId> &queue = workspace.order.bfs.queue;
    const int boxCount = (int)offsets.size - 1;
    distance.resize(boxCount);
    std::fill(distance.data, distance.data + boxCount, -1);
    queue.resize(0);
    distance.data[source] = 0;
    queue.push_back(source);
    Structure::BoxId farthest = source;
    for (int head = 0; head < (int)queue.size; ++head) {
        const Structure::BoxId box = queue.data[head];
        if (distance.data[box] > distance.data[farthest] || (distance.data[box] == distance.data[farthest] && box < farthest))
            farthest = box;
        const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, box);
        for (int i = 0; i < adjacent.size; ++i)
            if (distance.data[adjacent[i]] < 0) {
                distance.data[adjacent[i]] = distance.data[box] + 1;
                queue.push_back(adjacent[i]);
            }
    }
    return farthest;
}

void ShapeSolverGraphStep::makeGreedyOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, Structure::BoxId first, vector<Structure::BoxId> &order) {
    Workspace &workspace = getWorkspace();
    vector<int> &remaining = workspace.order.score.remaining;
    vector<char> &selected = workspace.order.score.selected;
    const int boxCount = (int)offsets.size - 1;
    order.resize(0);
    remaining.resize(boxCount);
    selected.resize(boxCount);
    std::fill(selected.data, selected.data + boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        remaining.data[box] = neighbors(offsets, adjacentBoxes, box).size;
    for (int step = 0; step < boxCount; ++step) {
        Structure::BoxId best = -1;
        if (step == 0 && first >= 0) {
            best = first;
        } else {
            int bestDelta = 0;
            for (Structure::BoxId candidate = 0; candidate < boxCount; ++candidate) {
                if (selected.data[candidate])
                    continue;
                const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, candidate);
                int closes = 0;
                for (int i = 0; i < adjacent.size; ++i)
                    if (selected.data[adjacent[i]] && remaining.data[adjacent[i]] == 1)
                        ++closes;
                const int delta = (remaining.data[candidate] != 0) - closes;
                if (best < 0 || delta < bestDelta) {
                    best = candidate;
                    bestDelta = delta;
                }
            }
        }
        assert_(best >= 0, "ShapeSolverGraph::makeGreedyOrder: 没有可选 Box");
        selected.data[best] = 1;
        order.push_back(best);
        const span<const Structure::BoxId> adjacent = neighbors(offsets, adjacentBoxes, best);
        for (int i = 0; i < adjacent.size; ++i)
            remaining.data[adjacent[i]] -= 1;
    }
}

void ShapeSolverGraphStep::makeWindow3Order(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, vector<Structure::BoxId> &order) {
    const int boxCount = (int)order.size;
    for (int start = 0; start < boxCount; start += 3) {
        const int length = (std::min)(3, boxCount - start);
        Structure::BoxId candidate[3];
        Structure::BoxId best[3];
        for (int i = 0; i < length; ++i) {
            candidate[i] = order.data[start + i];
            best[i] = candidate[i];
        }
        std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, order);
        auto consider = [&]() {
            for (int i = 0; i < length; ++i)
                order.data[start + i] = candidate[i];
            const std::pair<int, int> score = orderScore(offsets, adjacentBoxes, order);
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
void ShapeSolverGraphStep::makeSAOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const vector<Structure::BoxId> &seed, const ShapeSolverConfig &config, vector<Structure::BoxId> &best) {
    Workspace &workspace = getWorkspace();
    const int boxCount = seed.size;
    auto nextRandom = [](std::uint64_t &state) {
        const std::uint64_t value = splitmix64(state);
        state += 0x9e3779b97f4a7c15ULL;
        return value;
    };
    auto unitRandom = [&](std::uint64_t &state) { return (nextRandom(state) >> 11) * (1.0 / 9007199254740992.0); };
    auto energy = [](std::pair<int, int> score) { return score.first * 20000 + score.second; };

    vector<Structure::BoxId> &current = workspace.order.current;
    vector<Structure::BoxId> &candidate = workspace.order.candidate;
    best = seed;
    current = seed;
    candidate = seed;
    std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, best);
    const ShapeSolverConfig::SA &sa = *config.sa;
    const double cooling = std::pow(sa.endTemperature / sa.startTemperature, 1.0 / (sa.iterations - 1));
    for (int round = 0; round < sa.rounds; ++round) {
        std::uint64_t state = 0x9e3779b97f4a7c15ULL + (std::uint64_t)round * 0x6a09e667f3bcc909ULL;
        current = seed;
        for (int perturb = 0; perturb < round; ++perturb) {
            const int left = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            const int right = (int)(nextRandom(state) % (std::uint64_t)boxCount);
            std::swap(current.data[left], current.data[right]);
        }
        std::pair<int, int> currentScore = orderScore(offsets, adjacentBoxes, current);
        int currentEnergy = energy(currentScore);
        double temperature = sa.startTemperature;
        for (int iteration = 0; iteration < sa.iterations; ++iteration) {
            candidate = current;
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
            const std::pair<int, int> candidateScore = orderScore(offsets, adjacentBoxes, candidate);
            const int candidateEnergy = energy(candidateScore);
            const int difference = currentEnergy - candidateEnergy;
            const bool accept = difference >= 0 || unitRandom(state) < std::exp(difference / temperature);
            if (candidateScore < bestScore) {
                bestScore = candidateScore;
                best = candidate;
            }
            if (accept) {
                current = candidate;
                currentScore = candidateScore;
                currentEnergy = candidateEnergy;
            }
            temperature *= cooling;
        }
    }
}

void ShapeSolverGraphStep::makeAutoOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const ShapeSolverConfig &config, vector<Structure::BoxId> &order) {
    Workspace &workspace = getWorkspace();
    vector<Structure::BoxId> &candidate = workspace.order.candidate;
    const Structure::BoxId initial = farthestBox(offsets, adjacentBoxes, 0);
    makeGreedyOrder(offsets, adjacentBoxes, initial, order);
    const std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, order);
    if (bestScore.first > config.window3Threshold) {
        candidate = order;
        makeWindow3Order(offsets, adjacentBoxes, candidate);
        if (orderScore(offsets, adjacentBoxes, candidate) < bestScore)
            order = candidate;
    }
}

void ShapeSolverGraphStep::makeAutoSAOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const ShapeSolverConfig &config, vector<Structure::BoxId> &order) {
    Workspace &workspace = getWorkspace();
    vector<Structure::BoxId> &seed = workspace.order.seed;
    vector<Structure::BoxId> &best = workspace.order.best;
    makeAutoOrder(offsets, adjacentBoxes, config, order);
    const std::pair<int, int> bestScore = orderScore(offsets, adjacentBoxes, order);
    if (bestScore.first <= config.window3Threshold)
        return;
    makeGreedyOrder(offsets, adjacentBoxes, -1, seed);
    makeSAOrder(offsets, adjacentBoxes, seed, config, best);
    if (orderScore(offsets, adjacentBoxes, best) < bestScore)
        order = best;
}

void ShapeSolverGraphStep::buildAdjacency(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, int boxCount, vector<int> &offsets, vector<Structure::BoxId> &adjacent) {
    Workspace &workspace = getWorkspace();
    Workspace::Adjacency::Build &build = workspace.adjacency.build;
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

void ShapeSolverGraphStep::makeOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, const ShapeSolverConfig &config, vector<Structure::BoxId> &order) {
    Workspace &workspace = getWorkspace();
    Workspace::Order &scratch = workspace.order;
    if (config.order == ShapeSolverConfig::Order::Adjacent) {
        makeGreedyOrder(offsets, adjacent, farthestBox(offsets, adjacent, 0), order);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::Window3) {
        makeGreedyOrder(offsets, adjacent, farthestBox(offsets, adjacent, 0), order);
        makeWindow3Order(offsets, adjacent, order);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::SA) {
        makeGreedyOrder(offsets, adjacent, -1, scratch.seed);
        makeSAOrder(offsets, adjacent, scratch.seed, config, scratch.best);
        order = scratch.best;
        return;
    }
    if (config.order == ShapeSolverConfig::Order::Auto) {
        makeAutoOrder(offsets, adjacent, config, order);
        return;
    }
    if (config.order == ShapeSolverConfig::Order::AutoSA) {
        makeAutoSAOrder(offsets, adjacent, config, order);
        return;
    }
    assert_(false, "ShapeSolverGraphStep::makeOrder: 无效的消元顺序策略");
}

void ShapeSolverGraphStep::prepareSteps(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, const vector<Structure::BoxId> &order, Sequence &sequence) {
    Sequence::Constraints &constraints = sequence.constraints;
    Sequence::Close &close = sequence.close;
    Sequence::Frontier &frontier = sequence.frontier;
    const int boxCount = order.size;
    const int constraintCount = (int)shape.constraints.count;
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);

    sequence.position.resize(boxCount);
    for (int step = 0; step < boxCount; ++step)
        sequence.position[order[step]] = step;
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

ShapeSolverGraphStep::StepPlan ShapeSolverGraphStep::makeStep(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, const vector<Structure::BoxId> &order, int step, Sequence &sequence) {
    const Sequence::Constraints &constraints = sequence.constraints;
    const Sequence::Close &close = sequence.close;
    Sequence::Frontier &frontier = sequence.frontier;
    Sequence::Plan &buffer = sequence.plan;
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);
    const Structure::BoxId currentBox = order[step];
    const int boxSize = (int)boxes[currentBox].count;
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
                check.remainingSize += (int)boxes[member].count;
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

    const StepGenerator::StepPlan plan{currentBox, boxSize, buffer.checks, buffer.gather, buffer.closings};

    for (int i = 0; i < (int)frontier.layout.size; ++i)
        frontier.slotOf.data[frontier.layout.data[i]] = -1;
    for (int slot = 0; slot < (int)frontier.layoutNext.size; ++slot)
        frontier.slotOf.data[frontier.layoutNext.data[slot]] = slot;
    frontier.layout.swap(frontier.layoutNext);
    return plan;
}

} // 命名空间 mss
