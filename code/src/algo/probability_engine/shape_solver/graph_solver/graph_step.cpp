#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/utility/rng.h"

namespace mss {
namespace ShapeSolver {

GraphStepGenerator::Graph GraphStepGenerator::Graph::fromShape(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace) {
    // 同一数字约束中的 Box 两两相邻，图上的邻接关系正是消元时需要保留的 frontier 关系。
    const int boxCount = shape.boxes.size;
    Workspace::GraphSolverWorkspace::Graph &graph = workspace.graph_solver.graph;
    graph.offsets.assign(boxCount + 1, 0);
    graph.head.assign(boxCount, -1);
    graph.to.clear();
    graph.next.clear();
    graph.marked.assign(boxCount, 0);
    auto addEdge = [&](Structure::BoxId from, Structure::BoxId target) {
        graph.next.push_back(graph.head[from]);
        graph.to.push_back(target);
        graph.head[from] = (int)(graph.to.size()) - 1;
    };
    for (int constraint = 0; constraint < (int)(shape.constraintCount()); ++constraint) {
        const Structure::Shape::ConstraintView view = shape.constraint(shapes, constraint);
        for (int i = 0; i < (int)(view.boxIds.size()); ++i)
            for (int j = i + 1; j < (int)(view.boxIds.size()); ++j) {
                addEdge(view.boxIds[i], view.boxIds[j]);
                addEdge(view.boxIds[j], view.boxIds[i]);
            }
    }
    for (Structure::BoxId box = 0; box < boxCount; ++box) {
        for (int edge = graph.head[box]; edge >= 0; edge = graph.next[edge]) {
            const Structure::BoxId target = graph.to[edge];
            if (graph.marked[target])
                continue;
            graph.marked[target] = 1;
            ++graph.offsets[box + 1];
        }
        for (int edge = graph.head[box]; edge >= 0; edge = graph.next[edge])
            graph.marked[graph.to[edge]] = 0;
    }
    for (int box = 0; box < boxCount; ++box)
        graph.offsets[box + 1] += graph.offsets[box];
    graph.adjacent.resize(graph.offsets.back());
    for (Structure::BoxId box = 0; box < boxCount; ++box) {
        int write = graph.offsets[box];
        for (int edge = graph.head[box]; edge >= 0; edge = graph.next[edge]) {
            const Structure::BoxId target = graph.to[edge];
            if (graph.marked[target])
                continue;
            graph.marked[target] = 1;
            graph.adjacent[write++] = target;
        }
        for (int edge = graph.head[box]; edge >= 0; edge = graph.next[edge])
            graph.marked[graph.to[edge]] = 0;
    }
    return {graph.offsets, graph.adjacent};
}

std::span<const Structure::BoxId> GraphStepGenerator::Graph::neighbors(Structure::BoxId box) const {
    const int begin = offsets[box];
    const int end = offsets[box + 1];
    // 没有任何边时底层数组为空，直接返回空视图以免对空存储区做指针偏移。
    if (begin == end)
        return {};
    return std::span<const Structure::BoxId>(adjacent.data() + begin, end - begin);
}

std::pair<int, int> GraphStepGenerator::orderScore(const Graph &graph, const std::vector<Structure::BoxId> &order, Workspace &workspace) {
    const int boxCount = order.size();
    Workspace::GraphSolverWorkspace::OrderScratch &scratch = workspace.graph_solver.order_scratch;
    scratch.remaining.resize((int)(graph.offsets.size()) - 1);
    scratch.selected.assign(scratch.remaining.size(), 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        scratch.remaining[box] = graph.neighbors(box).size();
    int frontier = 0;
    int peak = 0;
    int area = 0;
    for (Structure::BoxId box : order) {
        if (scratch.remaining[box] != 0)
            ++frontier;
        for (Structure::BoxId neighbor : graph.neighbors(box))
            if (scratch.selected[neighbor] && scratch.remaining[neighbor] == 1)
                --frontier;
        scratch.selected[box] = 1;
        for (Structure::BoxId neighbor : graph.neighbors(box))
            --scratch.remaining[neighbor];
        peak = (std::max)(peak, frontier);
        area += frontier;
    }
    return {peak, area};
}

Structure::BoxId GraphStepGenerator::farthestBox(const Graph &graph, Structure::BoxId source, Workspace &workspace) {
    const int boxCount = (int)(graph.offsets.size()) - 1;
    Workspace::GraphSolverWorkspace::OrderScratch &scratch = workspace.graph_solver.order_scratch;
    scratch.distance.assign(boxCount, -1);
    scratch.queue.clear();
    scratch.queue.reserve(boxCount);
    scratch.distance[source] = 0;
    scratch.queue.push_back(source);
    Structure::BoxId farthest = source;
    for (int head = 0; head < (int)(scratch.queue.size()); ++head) {
        const Structure::BoxId box = scratch.queue[head];
        if (scratch.distance[box] > scratch.distance[farthest] || (scratch.distance[box] == scratch.distance[farthest] && box < farthest))
            farthest = box;
        for (Structure::BoxId neighbor : graph.neighbors(box))
            if (scratch.distance[neighbor] < 0) {
                scratch.distance[neighbor] = scratch.distance[box] + 1;
                scratch.queue.push_back(neighbor);
            }
    }
    return farthest;
}

std::vector<Structure::BoxId> GraphStepGenerator::makeGreedyOrder(const Graph &graph, Structure::BoxId first, Workspace &workspace) {
    const int boxCount = (int)(graph.offsets.size()) - 1;
    Workspace::GraphSolverWorkspace::OrderScratch &scratch = workspace.graph_solver.order_scratch;
    std::vector<Structure::BoxId> order;
    order.reserve(boxCount);
    scratch.remaining.resize(boxCount);
    scratch.selected.assign(boxCount, 0);
    for (Structure::BoxId box = 0; box < boxCount; ++box)
        scratch.remaining[box] = graph.neighbors(box).size();
    for (int step = 0; step < boxCount; ++step) {
        Structure::BoxId best = -1;
        if (step == 0 && first >= 0) {
            best = first;
        } else {
            int bestDelta = 0;
            for (Structure::BoxId candidate = 0; candidate < boxCount; ++candidate) {
                if (scratch.selected[candidate])
                    continue;
                int closes = 0;
                for (Structure::BoxId neighbor : graph.neighbors(candidate))
                    if (scratch.selected[neighbor] && scratch.remaining[neighbor] == 1)
                        ++closes;
                const int delta = (scratch.remaining[candidate] != 0) - closes;
                if (best < 0 || delta < bestDelta) {
                    best = candidate;
                    bestDelta = delta;
                }
            }
        }
        if (best < 0)
            assert_(false, "GraphStepGenerator::makeGreedyOrder: 没有可选 Box");
        scratch.selected[best] = 1;
        order.push_back(best);
        for (Structure::BoxId neighbor : graph.neighbors(best))
            --scratch.remaining[neighbor];
    }
    return order;
}

std::vector<Structure::BoxId> GraphStepGenerator::makeWindow3Order(const Graph &graph, const std::vector<Structure::BoxId> &source, Workspace &workspace) {
    const int boxCount = (int)(graph.offsets.size()) - 1;
    std::vector<Structure::BoxId> order = source;
    for (int start = 0; start < boxCount; start += 3) {
        const int length = (std::min)(3, boxCount - start);
        std::array<Structure::BoxId, 3> candidate{};
        std::array<Structure::BoxId, 3> best{};
        for (int i = 0; i < length; ++i) {
            candidate[i] = order[start + i];
            best[i] = candidate[i];
        }
        std::pair<int, int> bestScore = orderScore(graph, order, workspace);
        auto consider = [&] {
            for (int i = 0; i < length; ++i)
                order[start + i] = candidate[i];
            const std::pair<int, int> score = orderScore(graph, order, workspace);
            if (score < bestScore) {
                bestScore = score;
                best = candidate;
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
            std::sort(candidate.begin(), candidate.end());
            do {
                consider();
            } while (std::next_permutation(candidate.begin(), candidate.end()));
        }
        for (int i = 0; i < length; ++i)
            order[start + i] = best[i];
    }
    return order;
}

std::vector<Structure::BoxId> GraphStepGenerator::makeSAOrder(const Graph &graph, const std::vector<Structure::BoxId> &seed, Workspace &workspace) {
    const int boxCount = (int)(graph.offsets.size()) - 1;
    auto nextRandom = [](std::uint64_t &state) {
        const std::uint64_t value = splitmix64(state);
        state += 0x9e3779b97f4a7c15ULL;
        return value;
    };
    auto unitRandom = [&](std::uint64_t &state) { return (nextRandom(state) >> 11) * (1.0 / 9007199254740992.0); };
    auto energy = [](std::pair<int, int> score) { return score.first * 20000 + score.second; };
    constexpr int rounds = 8;
    constexpr int iterations = 50000;
    constexpr double startTemperature = 40000.0;
    constexpr double endTemperature = 0.1;
    const double cooling = std::pow(endTemperature / startTemperature, 1.0 / (iterations - 1));
    // 当前、候选和最优顺序只属于本次搜索；候选缓冲在每轮迭代中复用。
    std::vector<Structure::BoxId> bestOrder = seed;
    std::vector<Structure::BoxId> current = seed;
    std::vector<Structure::BoxId> candidate;
    candidate.reserve(boxCount);
    std::pair<int, int> bestScore = orderScore(graph, bestOrder, workspace);
    for (int round = 0; round < rounds; ++round) {
        std::uint64_t state = 0x9e3779b97f4a7c15ULL + round * 0x6a09e667f3bcc909ULL;
        current = seed;
        for (int perturb = 0; perturb < round; ++perturb) {
            const int left = (int)(nextRandom(state) % boxCount);
            const int right = (int)(nextRandom(state) % boxCount);
            std::swap(current[left], current[right]);
        }
        std::pair<int, int> currentScore = orderScore(graph, current, workspace);
        int currentEnergy = energy(currentScore);
        double temperature = startTemperature;
        for (int iteration = 0; iteration < iterations; ++iteration) {
            candidate = current;
            const int left = (int)(nextRandom(state) % boxCount);
            const int right = (int)(nextRandom(state) % boxCount);
            const bool swap = nextRandom(state) % 2 == 0;
            if (swap) {
                std::swap(candidate[left], candidate[right]);
            } else {
                const Structure::BoxId value = candidate[left];
                if (left < right) {
                    for (int i = left; i < right; ++i)
                        candidate[i] = candidate[i + 1];
                } else if (right < left) {
                    for (int i = left; i > right; --i)
                        candidate[i] = candidate[i - 1];
                }
                candidate[right] = value;
            }
            const std::pair<int, int> candidateScore = orderScore(graph, candidate, workspace);
            const int candidateEnergy = energy(candidateScore);
            const int difference = currentEnergy - candidateEnergy;
            const bool accept = difference >= 0 || unitRandom(state) < std::exp(difference / temperature);
            if (candidateScore < bestScore) {
                bestOrder = candidate;
                bestScore = candidateScore;
            }
            if (accept) {
                current.swap(candidate);
                currentScore = candidateScore;
                currentEnergy = candidateEnergy;
            }
            temperature *= cooling;
        }
    }
    return bestOrder;
}

std::vector<Structure::BoxId> GraphStepGenerator::makeAutoOrder(const Graph &graph, Workspace &workspace) {
    const Structure::BoxId initial = farthestBox(graph, 0, workspace);
    std::vector<Structure::BoxId> greedy = makeGreedyOrder(graph, initial, workspace);
    const std::pair<int, int> bestScore = orderScore(graph, greedy, workspace);
    if (bestScore.first > 15) {
        std::vector<Structure::BoxId> candidate = makeWindow3Order(graph, greedy, workspace);
        if (orderScore(graph, candidate, workspace) < bestScore)
            return candidate;
    }
    return greedy;
}

std::vector<Structure::BoxId> GraphStepGenerator::makeAutoSAOrder(const Graph &graph, Workspace &workspace) {
    std::vector<Structure::BoxId> automatic = makeAutoOrder(graph, workspace);
    const std::pair<int, int> bestScore = orderScore(graph, automatic, workspace);
    if (bestScore.first <= 15)
        return automatic;
    const std::vector<Structure::BoxId> seed = makeGreedyOrder(graph, -1, workspace);
    std::vector<Structure::BoxId> candidate = makeSAOrder(graph, seed, workspace);
    if (orderScore(graph, candidate, workspace) < bestScore)
        return candidate;
    return automatic;
}

std::vector<Structure::BoxId> GraphStepGenerator::makeOrder(const Graph &graph, const OrderAlgo &algo, Workspace &workspace) {
    if (algo == OrderAlgo::Adjacent)
        return makeGreedyOrder(graph, farthestBox(graph, 0, workspace), workspace);
    if (algo == OrderAlgo::Window3)
        return makeWindow3Order(graph, makeGreedyOrder(graph, farthestBox(graph, 0, workspace), workspace), workspace);
    if (algo == OrderAlgo::SA)
        return makeSAOrder(graph, makeGreedyOrder(graph, -1, workspace), workspace);
    if (algo == OrderAlgo::Auto)
        return makeAutoOrder(graph, workspace);
    if (algo == OrderAlgo::AutoSA)
        return makeAutoSAOrder(graph, workspace);
    assert_(false, "GraphStepGenerator::makeOrder: 无效的消元顺序策略");
    // 此路径已由断言终止；返回值仅用于消除编译器警告。
    return {};
}

void GraphStepGenerator::forEach(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<Structure::BoxId> &order, Workspace &workspace, const Callback &callback) {
    const int boxCount = shape.boxes.size;
    Workspace::GraphSolverWorkspace::Step &ws = workspace.graph_solver.step;
    ws.position.resize(boxCount);
    for (int step = 0; step < boxCount; ++step)
        ws.position[order[step]] = step;
    const int constraintCount = shape.constraintCount();
    ws.constraintLast.assign(constraintCount, -1);
    ws.boxHead.assign(boxCount, -1);
    ws.nextLink.clear();
    ws.constraintIds.clear();
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        const Structure::Shape::ConstraintView view = shape.constraint(shapes, constraint);
        for (Structure::BoxId box : view.boxIds)
            ws.constraintLast[constraint] = (std::max)(ws.constraintLast[constraint], ws.position[box]);
        for (Structure::BoxId box : view.boxIds) {
            ws.nextLink.push_back(ws.boxHead[box]);
            ws.constraintIds.push_back(constraint);
            ws.boxHead[box] = (int)(ws.constraintIds.size()) - 1;
        }
    }
    ws.closeStep = ws.position;
    for (int constraint = 0; constraint < constraintCount; ++constraint)
        for (Structure::BoxId box : shape.constraint(shapes, constraint).boxIds)
            ws.closeStep[box] = (std::max)(ws.closeStep[box], ws.constraintLast[constraint]);
    ws.closeHead.assign(boxCount, -1);
    ws.closeNext.assign(boxCount, -1);
    for (Structure::BoxId box = 0; box < boxCount; ++box) {
        ws.closeNext[box] = ws.closeHead[ws.closeStep[box]];
        ws.closeHead[ws.closeStep[box]] = box;
    }

    ws.layout.clear();
    ws.nextLayout.clear();
    ws.slotOf.assign(boxCount, -1);
    StepPlan plan;
    for (int step = 0; step < boxCount; ++step) {
        plan.box = order[step];
        plan.boxSize = shape.boxes.span(shapes.boxes)[plan.box].size;
        ws.checks.clear();
        ws.gather.clear();
        ws.closings.clear();
        for (int link = ws.boxHead[plan.box]; link >= 0; link = ws.nextLink[link]) {
            const Structure::Shape::ConstraintView view = shape.constraint(shapes, ws.constraintIds[link]);
            StepPlan::Check check;
            check.sum = view.sum;
            for (Structure::BoxId member : view.boxIds) {
                if (ws.position[member] < step)
                    check.readSlots[check.readCount++] = ws.slotOf[member];
                else if (ws.position[member] > step)
                    check.remainingSize += shape.boxes.span(shapes.boxes)[member].size;
            }
            ws.checks.push_back(check);
        }
        ws.nextLayout.clear();
        for (int oldSlot = 0; oldSlot < (int)(ws.layout.size()); ++oldSlot) {
            const Structure::BoxId box = ws.layout[oldSlot];
            if (ws.closeStep[box] == step)
                continue;
            ws.gather.push_back(oldSlot);
            ws.nextLayout.push_back(box);
        }
        if (ws.closeStep[plan.box] > step) {
            ws.gather.push_back(-1);
            ws.nextLayout.push_back(plan.box);
        }
        for (Structure::BoxId box = ws.closeHead[step]; box >= 0; box = ws.closeNext[box])
            ws.closings.push_back({box, box == plan.box ? -1 : ws.slotOf[box]});
        plan.checks = std::span<const StepPlan::Check>(ws.checks);
        plan.gather = ws.gather;
        plan.closings = std::span<const StepPlan::Closing>(ws.closings);
        callback(plan);
        for (Structure::BoxId box : ws.layout)
            ws.slotOf[box] = -1;
        for (int slot = 0; slot < (int)(ws.nextLayout.size()); ++slot)
            ws.slotOf[ws.nextLayout[slot]] = slot;
        ws.layout.swap(ws.nextLayout);
    }
}

}
}
