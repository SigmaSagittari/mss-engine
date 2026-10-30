#include "algo/probability_engine/structure/structure.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "core/algorithm/neighbors.h"

namespace mss {

// 数字状态底层值连续为 0~8；下面两个判断只接收对应状态。
static bool isNumber(ObservedBoard::CellState state) { return (int)state <= (int)ObservedBoard::CellState::Num8; }
static int numberValue(ObservedBoard::CellState state) { return (int)state; }

// 一个 H 格的数字邻接签名：只依赖相邻数字格坐标，全盘分析和增量更新得到相同签名。
static U128 cellSignature(int row, int col, const ObservedBoard::Result &board) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    U128 hash;
    forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
        if (!isNumber(board.cells[neighborRow][neighborCol]))
            return;
        const std::uint64_t position = ((std::uint64_t)neighborRow << 32) | (std::uint64_t)neighborCol;
        hash += U128{splitmix64(position), splitmix64(position + 0x9e3779b97f4a7c15ULL)};
    });
    return hash;
}

// 把实例的 Box 和数字约束登记到 cellLoc；component 是当前 Result 中的组件下标。
static void remapInstance(Structure::InstanceId id, Structure::ComponentId component, const Structure::Pool &pool, Structure::Result &result) {
    const Structure::Instance &instance = pool.instances.list.data[id];
    const int *boxOf = pool.instances.boxOf.data.data + instance.boxOf.offset;
    const Structure::CellId *cells = pool.instances.cells.data.data + instance.cells.offset;
    const int boxCount = (int)instance.boxOf.count - 1;
    for (int box = 0; box < boxCount; ++box)
        for (int i = boxOf[box]; i < boxOf[box + 1]; ++i)
            result.cellLoc.data[cells[i]] = Structure::Result::CellLocation{component, box};
    const Structure::CellId *constraintCells = pool.instances.constraintCells.data.data + instance.constraintCells.offset;
    for (int i = 0; i < (int)instance.constraintCells.count; ++i)
        result.cellLoc.data[constraintCells[i]] = Structure::Result::CellLocation{component, -1};
}

// 清掉实例占用的格子登记，cellLoc 回到“不属于组件”。
static void clearInstance(Structure::InstanceId id, const Structure::Pool &pool, Structure::Result &result) {
    const Structure::Instance &instance = pool.instances.list.data[id];
    const Structure::CellId *cells = pool.instances.cells.data.data + instance.cells.offset;
    for (int i = 0; i < (int)instance.cells.count; ++i)
        result.cellLoc.data[cells[i]] = Structure::Result::CellLocation{};
    const Structure::CellId *constraintCells = pool.instances.constraintCells.data.data + instance.constraintCells.offset;
    for (int i = 0; i < (int)instance.constraintCells.count; ++i)
        result.cellLoc.data[constraintCells[i]] = Structure::Result::CellLocation{};
}

// 沿“数字—相邻 H 格”二部关系收集一个连通组件；T 格不参与遍历。
static void collectComponent(Structure::CellId start, const ObservedBoard::Result &board, const Basic::Result &basic, Grid<char> &visited, vector<Structure::CellId> &cells) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    const std::pair<int, int> startPosition = board.pos(start);
    visited[startPosition.first][startPosition.second] = 1;
    cells.push_back(start);
    for (int i = 0; i < (int)cells.size; ++i) {
        const std::pair<int, int> position = board.pos(cells.data[i]);
        const int row = position.first;
        const int col = position.second;
        if (isNumber(board.cells[row][col]))
            forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                if (basic.marks[neighborRow][neighborCol] == Basic::Mark::H && !visited[neighborRow][neighborCol]) {
                    visited[neighborRow][neighborCol] = 1;
                    cells.push_back(board.id(neighborRow, neighborCol));
                }
            });
        if (basic.marks[row][col] == Basic::Mark::H)
            forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                if (isNumber(board.cells[neighborRow][neighborCol]) && !visited[neighborRow][neighborCol]) {
                    visited[neighborRow][neighborCol] = 1;
                    cells.push_back(board.id(neighborRow, neighborCol));
                }
            });
    }
}

// 对 Box 大小和数字约束序列生成 Shape 的内容身份。
static U128 computeShapeHash(vectorPool<int>::vector boxSizes, vectorPool<Structure::Shape::Constraint>::vector constraints, const Structure::Pool::Shapes &pool) {
    U128Hasher hasher;
    const int *sizes = pool.boxSizes.data.data + boxSizes.offset;
    for (int i = 0; i < (int)boxSizes.count; ++i)
        hasher.mix((std::uint64_t)sizes[i]);
    const Structure::Shape::Constraint *records = pool.constraints.data.data + constraints.offset;
    const Structure::BoxId *ids = pool.boxIds.data.data;
    for (int i = 0; i < (int)constraints.count; ++i) {
        hasher.mix((std::uint64_t)records[i].sum);
        const Structure::BoxId *members = ids + records[i].boxIds.offset;
        for (int k = 0; k < (int)records[i].boxIds.count; ++k)
            hasher.mix((std::uint64_t)members[k] + 0x9e3779b9ULL);
    }
    return hasher.finalize();
}

// 对 Shape ID、Box 布局和数字约束格生成 Instance 的内容身份。
static U128 computeInstanceHash(const Structure::Instance &instance, const Structure::Pool::Instances &pool) {
    U128Hasher hasher;
    hasher.mix((std::uint64_t)instance.shape);
    const Structure::CellId *cells = pool.cells.data.data + instance.cells.offset;
    hasher.mix((std::uint64_t)instance.cells.count);
    for (int i = 0; i < (int)instance.cells.count; ++i)
        hasher.mix(cells[i]);
    const int *boxOf = pool.boxOf.data.data + instance.boxOf.offset;
    hasher.mix((std::uint64_t)instance.boxOf.count);
    for (int i = 0; i < (int)instance.boxOf.count; ++i)
        hasher.mix((std::uint64_t)boxOf[i]);
    const Structure::CellId *constraintCells = pool.constraintCells.data.data + instance.constraintCells.offset;
    hasher.mix((std::uint64_t)instance.constraintCells.count);
    for (int i = 0; i < (int)instance.constraintCells.count; ++i)
        hasher.mix(constraintCells[i]);
    return hasher.finalize();
}

void Structure::Pool::clear() {
    shapes.boxSizes.data.resize(0);
    shapes.constraints.data.resize(0);
    shapes.boxIds.data.resize(0);
    shapes.list.resize(0);
    shapes.index.clear();
    instances.cells.data.resize(0);
    instances.boxOf.data.resize(0);
    instances.constraintCells.data.resize(0);
    instances.list.resize(0);
    instances.index.clear();
    deltas.removed.data.resize(0);
    deltas.added.data.resize(0);
}

Structure::ShapeId Structure::Pool::Shapes::intern(Shape shape) {
    const ShapeId *found = index.find(shape.hash);
    if (found != nullptr) {
        boxSizes.pop_back(shape.boxSizes);
        constraints.pop_back(shape.constraints);
        boxIds.pop_back(shape.boxIds);
        return *found;
    }
    const ShapeId id = (ShapeId)list.size;
    list.push_back(shape);
    index.emplace(shape.hash, id);
    return id;
}

Structure::InstanceId Structure::Pool::Instances::intern(Instance instance) {
    const U128 hash = computeInstanceHash(instance, *this);
    const InstanceId *found = index.find(hash);
    if (found != nullptr) {
        cells.pop_back(instance.cells);
        boxOf.pop_back(instance.boxOf);
        constraintCells.pop_back(instance.constraintCells);
        return *found;
    }
    const InstanceId id = (InstanceId)list.size;
    list.push_back(instance);
    index.emplace(hash, id);
    return id;
}

// 按数字邻接签名合并 H 格为 Box，并构造扣除相邻 F 后的约束。
static Structure::InstanceId buildComponent(const vector<Structure::CellId> &componentCells, const ObservedBoard::Result &board, const Basic::Result &basic, Grid<int> &boxOfCell, Structure::Pool &pool, Structure::Workspace &workspace) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    Structure::Workspace::Boxes &boxes = workspace.boxes;
    boxes.hashBox.clear();
    vectorPool<int>::vector shapeBoxSizes = pool.shapes.boxSizes.push_back();
    vectorPool<Structure::Shape::Constraint>::vector shapeConstraints = pool.shapes.constraints.push_back();
    vectorPool<Structure::BoxId>::vector shapeBoxIds = pool.shapes.boxIds.push_back();
    for (int i = 0; i < (int)componentCells.size; ++i) {
        const std::pair<int, int> position = board.pos(componentCells.data[i]);
        const int row = position.first;
        const int col = position.second;
        if (basic.marks[row][col] != Basic::Mark::H)
            continue;
        const U128 hash = cellSignature(row, col, board);
        Structure::BoxId box;
        const Structure::BoxId *found = boxes.hashBox.find(hash);
        if (found != nullptr)
            box = *found;
        else {
            box = (Structure::BoxId)shapeBoxSizes.count;
            pool.shapes.boxSizes.push_back(shapeBoxSizes, 0);
            boxes.hashBox.emplace(hash, box);
            if ((int)boxes.buckets.size < (int)shapeBoxSizes.count)
                boxes.buckets.resize(shapeBoxSizes.count);
        }
        int &size = pool.shapes.boxSizes.data.data[shapeBoxSizes.offset + box];
        boxes.buckets.data[box].cells[size++] = componentCells.data[i];
        boxOfCell[row][col] = box;
    }

    vectorPool<Structure::CellId>::vector instanceCells = pool.instances.cells.push_back();
    vectorPool<int>::vector instanceBoxOf = pool.instances.boxOf.push_back();
    vectorPool<Structure::CellId>::vector instanceConstraintCells = pool.instances.constraintCells.push_back();
    pool.instances.boxOf.push_back(instanceBoxOf, 0);
    int cellOffset = 0;
    for (int box = 0; box < (int)shapeBoxSizes.count; ++box) {
        const int size = pool.shapes.boxSizes.data.data[shapeBoxSizes.offset + box];
        for (int i = 0; i < size; ++i)
            pool.instances.cells.push_back(instanceCells, boxes.buckets.data[box].cells[i]);
        cellOffset += size;
        pool.instances.boxOf.push_back(instanceBoxOf, cellOffset);
    }

    const int boxCount = (int)shapeBoxSizes.count;
    boxes.boxUsed.resize(boxCount);
    std::memset(boxes.boxUsed.data, 0, (std::size_t)boxCount);
    for (int i = 0; i < (int)componentCells.size; ++i) {
        const Structure::CellId cell = componentCells.data[i];
        const std::pair<int, int> position = board.pos(cell);
        const int row = position.first;
        const int col = position.second;
        if (!isNumber(board.cells[row][col]))
            continue;
        int sum = numberValue(board.cells[row][col]);
        const int start = (int)shapeBoxIds.count;
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (basic.marks[neighborRow][neighborCol] == Basic::Mark::F)
                --sum;
            if (basic.marks[neighborRow][neighborCol] != Basic::Mark::H)
                return;
            const Structure::BoxId box = (Structure::BoxId)boxOfCell[neighborRow][neighborCol];
            if (boxes.boxUsed.data[box] == 0) {
                boxes.boxUsed.data[box] = 1;
                pool.shapes.boxIds.push_back(shapeBoxIds, box);
            }
        });
        const int count = (int)shapeBoxIds.count - start;
        const Structure::BoxId *boxIds = pool.shapes.boxIds.data.data + shapeBoxIds.offset;
        for (int k = start; k < start + count; ++k)
            boxes.boxUsed.data[boxIds[k]] = 0;
        pool.shapes.constraints.push_back(shapeConstraints, {sum, {shapeBoxIds.offset + (std::size_t)start, (std::size_t)count}});
        pool.instances.constraintCells.push_back(instanceConstraintCells, cell);
    }

    const Structure::Shape shape{shapeBoxSizes, shapeConstraints, shapeBoxIds, computeShapeHash(shapeBoxSizes, shapeConstraints, pool.shapes)};
    const Structure::ShapeId shapeId = pool.shapes.intern(shape);
    const Structure::Instance instance{shapeId, instanceCells, instanceBoxOf, instanceConstraintCells};
    return pool.instances.intern(instance);
}

void Structure::analyze(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool, Workspace &workspace) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    const std::size_t cellCount = (std::size_t)rows * cols;
    result.components.resize(0);
    result.cellLoc.resize(cellCount);
    for (std::size_t i = 0; i < cellCount; ++i)
        result.cellLoc.data[i] = Result::CellLocation{};

    Grid<char> &visited = workspace.analyze.visited;
    Grid<int> &boxOfCell = workspace.analyze.boxOfCell;
    if (visited.rows() != rows || visited.cols() != cols) {
        visited.resize(rows, cols, 0);
        boxOfCell.resize(rows, cols, 0);
    } else
        visited.fill(0);

    vector<CellId> &cells = workspace.analyze.cells;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (basic.marks[row][col] == Basic::Mark::H && !visited[row][col]) {
                cells.resize(0);
                collectComponent(board.id(row, col), board, basic, visited, cells);
                result.components.push_back(buildComponent(cells, board, basic, boxOfCell, pool, workspace));
            }
    for (int component = 0; component < (int)result.components.size; ++component)
        remapInstance(result.components.data[component], component, pool, result);
}

Structure::Delta Structure::update(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool, const CellId *changedCells, int changedCount, Workspace &workspace) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    Workspace::Update &scratch = workspace.update;
    Grid<char> &visited = scratch.visited;
    Grid<int> &boxOfCell = scratch.boxOfCell;
    if (visited.rows() != rows || visited.cols() != cols) {
        visited.resize(rows, cols, 0);
        boxOfCell.resize(rows, cols, 0);
    }
    if (scratch.dirty.rows() != rows || scratch.dirty.cols() != cols)
        scratch.dirty.resize(rows, cols, 0);

    Delta delta{pool.deltas.removed.push_back(), pool.deltas.added.push_back()};
    scratch.dirtyCells.resize(0);
    scratch.removed.resize(0);

    auto markDirty = [&](int row, int col) {
        if (scratch.dirty[row][col])
            return;
        scratch.dirty[row][col] = 1;
        scratch.dirtyCells.push_back(board.id(row, col));
    };
    auto invalidate = [&](ComponentId component) {
        scratch.removed.push_back(component);
        const Instance &instance = pool.instances.list.data[result.components.data[component]];
        const CellId *cells = pool.instances.cells.data.data + instance.cells.offset;
        for (int i = 0; i < (int)instance.cells.count; ++i) {
            const std::pair<int, int> position = board.pos(cells[i]);
            markDirty(position.first, position.second);
            result.cellLoc.data[cells[i]] = Result::CellLocation{};
        }
        const CellId *constraintCells = pool.instances.constraintCells.data.data + instance.constraintCells.offset;
        for (int i = 0; i < (int)instance.constraintCells.count; ++i) {
            const std::pair<int, int> position = board.pos(constraintCells[i]);
            markDirty(position.first, position.second);
            result.cellLoc.data[constraintCells[i]] = Result::CellLocation{};
        }
    };

    for (int i = 0; i < changedCount; ++i) {
        const std::pair<int, int> position = board.pos(changedCells[i]);
        markDirty(position.first, position.second);
        forEachAdjacent(position.first, position.second, rows, cols, [&](int neighborRow, int neighborCol) { markDirty(neighborRow, neighborCol); });
    }
    // 每个受影响的旧组件整体标脏后重建，重建只读最终状态。
    for (int i = 0; i < (int)scratch.dirtyCells.size; ++i) {
        const Result::CellLocation location = result.cellLoc.data[scratch.dirtyCells.data[i]];
        if (location.component == -1)
            continue;
        invalidate(location.component);
    }
    vector<CellId> &cells = scratch.cells;
    for (int i = 0; i < (int)scratch.dirtyCells.size; ++i) {
        const std::pair<int, int> position = board.pos(scratch.dirtyCells.data[i]);
        if (basic.marks[position.first][position.second] != Basic::Mark::H || visited[position.first][position.second])
            continue;
        cells.resize(0);
        collectComponent(scratch.dirtyCells.data[i], board, basic, visited, cells);
        pool.deltas.added.push_back(delta.added, buildComponent(cells, board, basic, boxOfCell, pool, workspace));
    }
    // 被删除的旧组件按下标降序记录，逆序恢复时可直接补回尾部。
    if (scratch.removed.size > 0)
        std::sort(scratch.removed.data, scratch.removed.data + scratch.removed.size, [](ComponentId lhs, ComponentId rhs) { return lhs > rhs; });
    for (int i = 0; i < (int)scratch.removed.size; ++i) {
        const ComponentId component = scratch.removed.data[i];
        pool.deltas.removed.push_back(delta.removed, {component, result.components.data[component]});
    }
    applyDelta(result, delta, pool);

    // 新连接只会出现在变化格及其邻格；远端传播落在已命中的旧组件中，整组格子已在 invalidate 中标脏。
    for (int i = 0; i < (int)scratch.dirtyCells.size; ++i) {
        const std::pair<int, int> position = board.pos(scratch.dirtyCells.data[i]);
        scratch.dirty[position.first][position.second] = 0;
        visited[position.first][position.second] = 0;
    }
    return delta;
}

void Structure::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    const Delta::Change *removed = pool.deltas.removed.data.data + delta.removed.offset;
    const InstanceId *added = pool.deltas.added.data.data + delta.added.offset;
    const int removedCount = (int)delta.removed.count;
    const int addedCount = (int)delta.added.count;
    if (reverse) {
        for (int i = addedCount - 1; i >= 0; --i) {
            clearInstance(result.components.data[result.components.size - 1], pool, result);
            result.components.size -= 1;
        }
        for (int i = removedCount - 1; i >= 0; --i) {
            const Delta::Change &change = removed[i];
            const int tail = (int)result.components.size;
            if (change.component != tail) {
                result.components.push_back(result.components.data[change.component]);
                remapInstance(result.components.data[tail], tail, pool, result);
                result.components.data[change.component] = change.previous;
            } else
                result.components.push_back(change.previous);
            remapInstance(result.components.data[change.component], change.component, pool, result);
        }
        return;
    }
    for (int i = 0; i < removedCount; ++i) {
        const Delta::Change &change = removed[i];
        clearInstance(result.components.data[change.component], pool, result);
        const int last = (int)result.components.size - 1;
        if (change.component != last) {
            result.components.data[change.component] = result.components.data[last];
            remapInstance(result.components.data[change.component], change.component, pool, result);
        }
        result.components.size -= 1;
    }
    for (int i = 0; i < addedCount; ++i) {
        const int component = (int)result.components.size;
        result.components.push_back(added[i]);
        remapInstance(added[i], component, pool, result);
    }
}

} // 命名空间 mss
