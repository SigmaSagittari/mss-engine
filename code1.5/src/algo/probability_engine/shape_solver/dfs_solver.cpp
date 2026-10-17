#include "algo/probability_engine/shape_solver/dfs_solver.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <utility>

#include "core/utility/combination.h"

namespace mss {
namespace ShapeSolver {

template <typename Callback> void DfsSolver::enumerateAssignments(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace, Callback &&callback) {
    Workspace::DfsSolverWorkspace &ws = workspace.dfs_solver;
    const int boxCount = shape.boxes.size;
    const int constraintCount = shape.constraintCount();
    const std::span<const Structure::Shape::Box> boxes = shape.boxes.span(shapes.boxes);

    ws.boxHead.assign(boxCount, -1);
    ws.constraintNext.clear();
    ws.constraintIds.clear();
    ws.constraintSum.assign(constraintCount, 0);
    ws.constraintMaxAdd.assign(constraintCount, 0);
    ws.currentSum.assign(constraintCount, 0);
    ws.assignedSize.assign(constraintCount, 0);
    ws.assignment.assign(boxCount, 0);

    // 反向索引让每次赋值只检查关联约束；当前和与剩余容量给出可行性剪枝界限。
    std::size_t incidenceCount = 0;
    for (int constraint = 0; constraint < constraintCount; ++constraint)
        incidenceCount += shape.constraint(shapes, constraint).boxIds.size();
    ws.constraintNext.reserve(incidenceCount);
    ws.constraintIds.reserve(incidenceCount);
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        const Structure::Shape::ConstraintView view = shape.constraint(shapes, constraint);
        ws.constraintSum[constraint] = view.sum;
        for (Structure::BoxId box : view.boxIds) {
            ws.constraintMaxAdd[constraint] += boxes[box].size;
            ws.constraintNext.push_back(ws.boxHead[box]);
            ws.constraintIds.push_back(constraint);
            ws.boxHead[box] = (int)(ws.constraintIds.size()) - 1;
        }
    }

    ws.frames.clear();
    ws.frames.reserve(boxCount + 1);
    ws.frames.push_back({0, 0, -1, 0, 1.0L});
    while (!ws.frames.empty()) {
        Workspace::DfsSolverWorkspace::Frame &frame = ws.frames.back();
        if (frame.index == boxCount) {
            callback(ws.assignment, frame.ways);
            const int appliedIndex = frame.appliedIndex;
            const int appliedMine = frame.appliedMine;
            ws.frames.pop_back();
            if (appliedIndex >= 0) {
                const int maxMine = boxes[appliedIndex].size;
                for (int link = ws.boxHead[appliedIndex]; link >= 0; link = ws.constraintNext[link]) {
                    const int constraint = ws.constraintIds[link];
                    ws.currentSum[constraint] -= appliedMine;
                    ws.assignedSize[constraint] -= maxMine;
                }
            }
            continue;
        }

        const int index = frame.index;
        const int maxMine = boxes[index].size;
        if (frame.nextMine > maxMine) {
            const int appliedIndex = frame.appliedIndex;
            const int appliedMine = frame.appliedMine;
            ws.frames.pop_back();
            if (appliedIndex >= 0) {
                const int appliedMaxMine = boxes[appliedIndex].size;
                for (int link = ws.boxHead[appliedIndex]; link >= 0; link = ws.constraintNext[link]) {
                    const int constraint = ws.constraintIds[link];
                    ws.currentSum[constraint] -= appliedMine;
                    ws.assignedSize[constraint] -= appliedMaxMine;
                }
            }
            continue;
        }

        const int mine = frame.nextMine++;
        ws.assignment[index] = mine;
        bool valid = true;
        for (int link = ws.boxHead[index]; link >= 0; link = ws.constraintNext[link]) {
            const int constraint = ws.constraintIds[link];
            const int sum = ws.currentSum[constraint] + mine;
            const int remaining = ws.constraintMaxAdd[constraint] - (ws.assignedSize[constraint] + maxMine);
            if (sum > ws.constraintSum[constraint] || sum + remaining < ws.constraintSum[constraint]) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;

        for (int link = ws.boxHead[index]; link >= 0; link = ws.constraintNext[link]) {
            const int constraint = ws.constraintIds[link];
            ws.currentSum[constraint] += mine;
            ws.assignedSize[constraint] += maxMine;
        }
        ws.frames.push_back({index + 1, 0, index, mine, frame.ways * Combination::chooseSmall(maxMine, mine)});
    }
}

void DfsSolver::forEachAssignment(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace, const AssignmentCallback &callback) {
    enumerateAssignments(shape, shapes, workspace, [&](const std::vector<char> &assignment, long double ways) { callback(assignment, ways); });
}

DistributionResult DfsSolver::analyze(const Structure::Shape &shape, const Structure::Pool &shapes, DistributionPool &distPool, Workspace &workspace) {
    const int boxCount = shape.boxes.size;
    int maxMineCount = 0;
    for (const Structure::Shape::Box &box : shape.boxes.span(shapes.boxes))
        maxMineCount += box.size;

    std::vector<long double> &ways = workspace.dfs_solver.ways;
    RawGrid<long double> &moments = workspace.dfs_solver.moments;
    ways.assign(maxMineCount + 1, 0.0L);
    moments.resize(maxMineCount + 1, boxCount, 0.0L);
    enumerateAssignments(shape, shapes, workspace, [&](const std::vector<char> &assignment, long double weight) {
        int mineCount = 0;
        for (char mine : assignment)
            mineCount += mine;
        ways[mineCount] += weight;
        for (int box = 0; box < boxCount; ++box)
            moments[mineCount][box] += weight * assignment[box];
    });

    int start = 0;
    while (start <= maxMineCount && ways[start] == 0.0L)
        ++start;
    if (start > maxMineCount)
        return {0, boxCount, {0, 0}, {0, 0}};
    int end = maxMineCount;
    while (ways[end] == 0.0L)
        --end;

    vectorPool<long double>::vector compactWays = distPool.values.push_back();
    for (int mineCount = start; mineCount <= end; ++mineCount) compactWays.push_back(distPool.values, ways[mineCount]);
    vectorPool<long double>::vector expectationValues = distPool.values.push_back();
    for (int mineCount = start; mineCount <= end; ++mineCount)
        for (int box = 0; box < boxCount; ++box)
            expectationValues.push_back(distPool.values, ways[mineCount] == 0.0L ? 0.0L : moments[mineCount][box] / ways[mineCount]);
    return {start, boxCount, compactWays, expectationValues};
}

void DfsSolver::observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace) {
    int maxMineCount = 0;
    for (const Structure::Shape::Box &box : shape.boxes.span(shapes.boxes))
        maxMineCount += box.size;
    std::vector<std::array<long double, 9>> &accumulated = workspace.observed.dfsWays;
    accumulated.assign(maxMineCount + 1, {});

    // 观测表按邻居雷数卷积；xBox 排除被点击的那个格子，只统计点击安全的布局。
    enumerateAssignments(shape, shapes, workspace, [&](const std::vector<char> &assignment, long double weight) {
        int componentMines = 0;
        std::array<long double, 9> convolution{};
        convolution[0] = 1.0L;
        for (int box = 0; box < (int)(assignment.size()); ++box) {
            const int mines = assignment[box];
            componentMines += mines;
            const int adjacent = adjacentBoxCells[box];
            const bool isXBox = box == xBox;
            if (adjacent == 0 && !isXBox)
                continue;
            const int size = shape.boxes.span(shapes.boxes)[box].size;
            const int pool = isXBox ? size - 1 : size;
            std::array<long double, 9> local{};
            const int maxAdjacent = (std::min)(adjacent, mines);
            for (int neighborMines = 0; neighborMines <= maxAdjacent; ++neighborMines) {
                const int remaining = mines - neighborMines;
                if (remaining > pool - adjacent)
                    continue;
                local[neighborMines] = Combination::chooseSmall(adjacent, neighborMines) * Combination::chooseSmall(pool - adjacent, remaining) / Combination::chooseSmall(size, mines);
            }
            std::array<long double, 9> next{};
            for (int currentMines = 0; currentMines <= 8; ++currentMines)
                if (convolution[currentMines] != 0.0L)
                    for (int neighborMines = 0; neighborMines <= 8 - currentMines; ++neighborMines)
                        next[currentMines + neighborMines] += convolution[currentMines] * local[neighborMines];
            convolution = next;
        }
        for (int neighborMines = 0; neighborMines <= 8; ++neighborMines)
            if (convolution[neighborMines] != 0.0L)
                accumulated[componentMines][neighborMines] += weight * convolution[neighborMines];
    });

    std::array<int, 9> starts;
    std::array<int, 9> ends;
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        starts[neighborMines] = (std::numeric_limits<int>::max)();
        ends[neighborMines] = -1;
        for (int componentMines = 0; componentMines <= maxMineCount; ++componentMines)
            if (accumulated[componentMines][neighborMines] != 0.0L) {
                starts[neighborMines] = (std::min)(starts[neighborMines], componentMines);
                ends[neighborMines] = componentMines;
            }
    }
    std::size_t totalSize = 0;
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        const std::size_t size = ends[neighborMines] < 0 ? 0 : (std::size_t)(ends[neighborMines] - starts[neighborMines] + 1);
        result.distributions_[neighborMines] = {ends[neighborMines] < 0 ? 0 : starts[neighborMines], totalSize, size};
        totalSize += size;
    }
    result.ways_.assign(totalSize, 0.0L);
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines)
        for (int componentMines = starts[neighborMines]; componentMines <= ends[neighborMines]; ++componentMines)
            result.ways_[result.distributions_[neighborMines].offset + componentMines - starts[neighborMines]] = accumulated[componentMines][neighborMines];
}

}
}
