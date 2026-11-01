#include "algo/probability_engine/shape_solver/dfs_solver.h"

#include <algorithm>
#include <limits>

#include "core/algorithm/combination.h"

namespace mss {

struct ShapeSolverDfs::Workspace {
    struct Enumerate {
        struct Constraints {
            // Box -> 关联约束的边链表。
            vector<int> boxHead;
            vector<int> constraintNext;
            vector<int> constraintIds;
            vector<int> constraintSum;
            vector<int> constraintMaxAdd;
            vector<int> currentSum;
            vector<int> assignedSize;
        } constraints;
        // 当前枚举到的各 Box 雷数；只在回调期间有效。
        vector<char> assignment;
        struct Frame {
            // 下一层要分配的 Box 和当前 Box 下一种待试的雷数。
            int index;
            int nextMine;
            // 进入本帧时已应用的父层 Box 及其雷数；根帧为 -1 和 0。
            int appliedIndex;
            int appliedMine;
            long double ways;
        };
        vector<Frame> frames;
    };
    Enumerate enumerate;
    struct Assignments {
        vector<int> counts;
        vector<int> cursor;
        vector<long double> ways;
    } assignments;
    struct Analyze {
        vector<long double> ways;
        Grid<long double> moments;
    } analyze;
    // 观测累计表和卷积缓冲；每次调用覆写，只保留存储容量。
    vectorPool<long double> observedValues;
};

__declspec(noinline) ShapeSolverDfs::Workspace &ShapeSolverDfs::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

// 三个只读视图：Box 数、每 Box 的格子数、约束数组。都借用 shapes。
int ShapeSolverDfs::shapeBoxCount(const Structure::Shape &shape) { return (int)shape.boxSizes.count; }
const int *ShapeSolverDfs::shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.boxSizes.data.data + shape.boxSizes.offset; }
const Structure::Shape::Constraint *ShapeSolverDfs::shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.constraints.data.data + shape.constraints.offset; }
const Structure::BoxId *ShapeSolverDfs::constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes) { return shapes.shapes.boxIds.data.data + constraint.boxIds.offset; }

template <typename Callback> void ShapeSolverDfs::enumerateAssignments(const Structure::Shape &shape, const Structure::Pool &shapes, Callback &&callback) {
    Workspace &workspace = getWorkspace();
    vector<char> &assignment = workspace.enumerate.assignment;
    vector<Workspace::Enumerate::Frame> &frames = workspace.enumerate.frames;
    const int boxCount = shapeBoxCount(shape);
    const int constraintCount = (int)shape.constraints.count;
    const int *boxes = shapeBoxSizes(shape, shapes);
    const Structure::Shape::Constraint *records = shapeConstraints(shape, shapes);

    Workspace::Enumerate::Constraints &constraints = workspace.enumerate.constraints;
    constraints.boxHead.resize(boxCount);
    std::fill(constraints.boxHead.data, constraints.boxHead.data + boxCount, -1);
    constraints.constraintNext.resize(0);
    constraints.constraintIds.resize(0);
    constraints.constraintSum.resize(constraintCount);
    constraints.constraintMaxAdd.resize(constraintCount);
    constraints.currentSum.resize(constraintCount);
    constraints.assignedSize.resize(constraintCount);
    std::fill(constraints.constraintMaxAdd.data, constraints.constraintMaxAdd.data + constraintCount, 0);
    std::fill(constraints.currentSum.data, constraints.currentSum.data + constraintCount, 0);
    std::fill(constraints.assignedSize.data, constraints.assignedSize.data + constraintCount, 0);
    assignment.resize(boxCount);

    // 反向索引让每次赋值只检查关联约束；当前和与剩余容量给出可行性剪枝界限。
    for (int constraint = 0; constraint < constraintCount; ++constraint) {
        constraints.constraintSum.data[constraint] = records[constraint].sum;
        const Structure::BoxId *members = constraintMembers(records[constraint], shapes);
        const int memberCount = (int)records[constraint].boxIds.count;
        for (int i = 0; i < memberCount; ++i) {
            const int box = members[i];
            constraints.constraintMaxAdd.data[constraint] += boxes[box];
            constraints.constraintNext.push_back(constraints.boxHead.data[box]);
            constraints.constraintIds.push_back(constraint);
            constraints.boxHead.data[box] = (int)constraints.constraintIds.size - 1;
        }
    }

    frames.resize(0);
    frames.push_back({0, 0, -1, 0, 1.0L});
    while (frames.size > 0) {
        Workspace::Enumerate::Frame &frame = frames.data[frames.size - 1];
        if (frame.index == boxCount) {
            callback(assignment, frame.ways);
            const int appliedIndex = frame.appliedIndex;
            const int appliedMine = frame.appliedMine;
            frames.size -= 1;
            if (appliedIndex >= 0) {
                const int maxMine = boxes[appliedIndex];
                for (int link = constraints.boxHead.data[appliedIndex]; link >= 0; link = constraints.constraintNext.data[link]) {
                    const int constraint = constraints.constraintIds.data[link];
                    constraints.currentSum.data[constraint] -= appliedMine;
                    constraints.assignedSize.data[constraint] -= maxMine;
                }
            }
            continue;
        }

        const int index = frame.index;
        const int maxMine = boxes[index];
        if (frame.nextMine > maxMine) {
            const int appliedIndex = frame.appliedIndex;
            const int appliedMine = frame.appliedMine;
            frames.size -= 1;
            if (appliedIndex >= 0) {
                const int appliedMaxMine = boxes[appliedIndex];
                for (int link = constraints.boxHead.data[appliedIndex]; link >= 0; link = constraints.constraintNext.data[link]) {
                    const int constraint = constraints.constraintIds.data[link];
                    constraints.currentSum.data[constraint] -= appliedMine;
                    constraints.assignedSize.data[constraint] -= appliedMaxMine;
                }
            }
            continue;
        }

        const int mine = frame.nextMine;
        frame.nextMine += 1;
        assignment.data[index] = (char)mine;
        bool valid = true;
        for (int link = constraints.boxHead.data[index]; link >= 0; link = constraints.constraintNext.data[link]) {
            const int constraint = constraints.constraintIds.data[link];
            const int sum = constraints.currentSum.data[constraint] + mine;
            const int remaining = constraints.constraintMaxAdd.data[constraint] - (constraints.assignedSize.data[constraint] + maxMine);
            if (sum > constraints.constraintSum.data[constraint] || sum + remaining < constraints.constraintSum.data[constraint]) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;

        for (int link = constraints.boxHead.data[index]; link >= 0; link = constraints.constraintNext.data[link]) {
            const int constraint = constraints.constraintIds.data[link];
            constraints.currentSum.data[constraint] += mine;
            constraints.assignedSize.data[constraint] += maxMine;
        }
        frames.push_back({index + 1, 0, index, mine, frame.ways * Combination().chooseSmall(maxMine, mine)});
    }
}

ShapeSolverType::Distribution ShapeSolverDfs::analyze(const Structure::Shape &shape, const Structure::Pool &shapes, vectorPool<long double> &values) {
    Workspace &workspace = getWorkspace();
    const int boxCount = shapeBoxCount(shape);
    const int *boxes = shapeBoxSizes(shape, shapes);
    int maxMineCount = 0;
    for (int box = 0; box < boxCount; ++box)
        maxMineCount += boxes[box];

    vector<long double> &ways = workspace.analyze.ways;
    Grid<long double> &moments = workspace.analyze.moments;
    ways.resize(maxMineCount + 1);
    std::fill(ways.data, ways.data + ways.size, 0.0L);
    moments.resize(maxMineCount + 1, boxCount, 0.0L);
    enumerateAssignments(shape, shapes, [&](const vector<char> &assignment, long double weight) {
        int mineCount = 0;
        for (int box = 0; box < boxCount; ++box)
            mineCount += assignment.data[box];
        ways.data[mineCount] += weight;
        for (int box = 0; box < boxCount; ++box)
            moments[mineCount][box] += weight * assignment.data[box];
    });

    int start = 0;
    while (start <= maxMineCount && ways.data[start] == 0.0L)
        ++start;
    ShapeSolverType::Distribution result;
    result.boxCount = boxCount;
    if (start > maxMineCount) {
        result.ways = {0, 0};
        result.expectations = {0, 0};
        result.start = 0;
        return result;
    }
    int end = maxMineCount;
    while (ways.data[end] == 0.0L)
        --end;

    result.ways = values.push_back();
    for (int mine = start; mine <= end; ++mine)
        values.push_back(result.ways, ways.data[mine]);
    result.expectations = values.push_back();
    for (int mine = start; mine <= end; ++mine) {
        const long double total = ways.data[mine];
        for (int box = 0; box < boxCount; ++box)
            values.push_back(result.expectations, total == 0.0L ? 0.0L : moments[mine][box] / total);
    }
    result.start = start;
    return result;
}

void ShapeSolverDfs::observe(const Structure::Shape &shape, const Structure::Pool &shapes, span<const int> adjacentBoxCells, Structure::BoxId xBox, ShapeSolverType::Observed &result) {
    Workspace &workspace = getWorkspace();
    const int boxCount = shapeBoxCount(shape);
    const int *boxes = shapeBoxSizes(shape, shapes);
    int maxMineCount = 0, maxNeighborCount = 0;
    for (int box = 0; box < boxCount; ++box) {
        maxMineCount += boxes[box];
        maxNeighborCount += adjacentBoxCells[box];
    }
    const int stride = maxNeighborCount + 1;

    result.values.data.resize(0);
    vectorPool<long double> &values = workspace.observedValues;
    values.data.resize(0);
    vectorPool<long double>::vector accumulated = values.push_back();
    accumulated.count = (std::size_t)(maxMineCount + 1) * stride;
    vectorPool<long double>::vector convolution{accumulated.count, 0}, next{accumulated.count + stride, 0}, local{accumulated.count + 2 * stride, 0};
    // 危险：三个卷积缓冲只预留容量；每次赋值先初始化切片有效范围，范围外不读写。
    values.data.resize(accumulated.count + 3 * stride);
    std::fill(values.data.data, values.data.data + accumulated.count, 0.0L);

    enumerateAssignments(shape, shapes, [&](const vector<char> &assignment, long double weight) {
        int componentMines = 0;
        int first = 0;
        convolution.count = 1;
        values.data.data[convolution.offset] = 1.0L;
        for (int box = 0; box < boxCount; ++box) {
            const int mines = assignment.data[box];
            componentMines += mines;
            const int adjacent = adjacentBoxCells[box];
            const bool isXBox = box == xBox;
            if (adjacent == 0 && !isXBox)
                continue;
            const int size = boxes[box];
            const int pool = isXBox ? size - 1 : size;
            const int minAdjacent = (std::max)(0, mines - pool + adjacent);
            const int maxAdjacent = (std::min)(adjacent, mines);
            if (minAdjacent > maxAdjacent)
                return;
            local.count = maxAdjacent - minAdjacent + 1;
            for (int neighborMines = minAdjacent; neighborMines <= maxAdjacent; ++neighborMines) {
                const int remaining = mines - neighborMines;
                values.data.data[local.offset + neighborMines - minAdjacent] = Combination().chooseSmall(adjacent, neighborMines) * Combination().chooseSmall(pool - adjacent, remaining) / Combination().chooseSmall(size, mines);
            }
            // 只初始化和计算可达邻雷数；交换两张表，不搬运未使用的槽位。
            next.count = convolution.count + local.count - 1;
            std::fill(values.data.data + next.offset, values.data.data + next.offset + next.count, 0.0L);
            for (int current = 0; current < (int)convolution.count; ++current) {
                const long double ways = values.data.data[convolution.offset + current];
                if (ways != 0.0L)
                    for (int neighbor = 0; neighbor < (int)local.count; ++neighbor)
                        values.data.data[next.offset + current + neighbor] += ways * values.data.data[local.offset + neighbor];
            }
            std::swap(convolution, next);
            first += minAdjacent;
        }
        for (int neighbor = 0; neighbor < (int)convolution.count; ++neighbor)
            if (values.data.data[convolution.offset + neighbor] != 0.0L)
                values.data.data[accumulated.offset + componentMines * stride + first + neighbor] += weight * values.data.data[convolution.offset + neighbor];
    });

    int starts[9];
    int ends[9];
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        starts[neighborMines] = (std::numeric_limits<int>::max)();
        ends[neighborMines] = -1;
        if (neighborMines <= maxNeighborCount)
            for (int mine = 0; mine <= maxMineCount; ++mine)
                if (values.data.data[accumulated.offset + mine * stride + neighborMines] != 0.0L) {
                    starts[neighborMines] = (std::min)(starts[neighborMines], mine);
                    ends[neighborMines] = mine;
                }
    }
    for (int neighborMines = 0; neighborMines <= 8; ++neighborMines) {
        result.start[neighborMines] = ends[neighborMines] < 0 ? 0 : starts[neighborMines];
        result.ways[neighborMines] = result.values.push_back();
        for (int mine = starts[neighborMines]; mine <= ends[neighborMines]; ++mine)
            result.values.push_back(result.ways[neighborMines], values.data.data[accumulated.offset + mine * stride + neighborMines]);
    }
}

void ShapeSolverDfs::forEachAssignment(const Structure::Shape &shape, const Structure::Pool &shapes, const std::function<void(const vector<char> &assignment, long double ways)> &callback) {
    enumerateAssignments(shape, shapes, [&](const vector<char> &assignment, long double ways) { callback(assignment, ways); });
}

ShapeSolverDfs::Assignments ShapeSolverDfs::buildAssignments(const Structure::Shape &shape, const Structure::Pool &shapes, AssignmentStorage &storage) {
    Workspace &workspace = getWorkspace();
    const int boxCount = shapeBoxCount(shape);
    const int *boxes = shapeBoxSizes(shape, shapes);
    int maxMines = 0;
    for (int box = 0; box < boxCount; ++box)
        maxMines += boxes[box];
    const int groupCount = maxMines + 1;

    vectorPool<char>::vector boxMines = storage.boxMines.push_back();
    vectorPool<Assignment>::vector entries = storage.entries.push_back();
    vectorPool<long double>::vector ways = storage.ways.push_back();
    vectorPool<int>::vector order = storage.order.push_back();
    vectorPool<int>::vector groupStart = storage.groupStart.push_back();

    vector<int> &counts = workspace.assignments.counts;
    vector<long double> &groupWays = workspace.assignments.ways;
    counts.resize(groupCount);
    std::fill(counts.data, counts.data + counts.size, 0);
    groupWays.resize(groupCount);
    std::fill(groupWays.data, groupWays.data + groupWays.size, 0.0L);

    enumerateAssignments(shape, shapes, [&](const vector<char> &assignment, long double weight) {
        int mineCount = 0;
        for (int box = 0; box < boxCount; ++box)
            mineCount += assignment.data[box];
        const int offset = (int)boxMines.count;
        for (int box = 0; box < boxCount; ++box)
            storage.boxMines.push_back(boxMines, assignment.data[box]);
        groupWays.data[mineCount] += weight;
        storage.entries.push_back(entries, Assignment{groupWays.data[mineCount], offset, mineCount});
        counts.data[mineCount] += 1;
    });

    const int total = (int)entries.count;
    storage.groupStart.push_back(groupStart, 0);
    int passed = 0;
    for (int mine = 0; mine <= maxMines; ++mine) {
        passed += counts.data[mine];
        storage.groupStart.push_back(groupStart, passed);
    }
    for (int mine = 0; mine <= maxMines; ++mine)
        storage.ways.push_back(ways, groupWays.data[mine]);

    // 枚举顺序里各雷数档是交错的；这里只排下标，组内保持累计权重递增。
    if (total > 0) {
        storage.order.data.resize(order.offset + (std::size_t)total);
        order.count = (std::size_t)total;
        vector<int> &cursor = workspace.assignments.cursor;
        cursor.resize(groupCount);
        std::fill(cursor.data, cursor.data + cursor.size, 0);
        const int *starts = storage.groupStart.data.data + groupStart.offset;
        for (int index = 0; index < total; ++index) {
            const int mine = storage.entries.data.data[entries.offset + index].mineCount;
            storage.order.data.data[order.offset + starts[mine] + cursor.data[mine]] = index;
            cursor.data[mine] += 1;
        }
    }

    Assignments result;
    result.boxCount = boxCount;
    result.boxMines = boxMines;
    result.entries = entries;
    result.order = order;
    result.groupStart = groupStart;
    result.ways = ways;
    return result;
}

} // 命名空间 mss
