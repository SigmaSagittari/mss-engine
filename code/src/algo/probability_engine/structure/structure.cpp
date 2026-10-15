#include "algo/probability_engine/structure/structure.h"

#include <utility>

#include "core/utility/neighbors.h"

namespace mss {

bool Structure::isNumber(ObservedBoard::CellState state) { return (int)(state) <= (int)(ObservedBoard::CellState::Num8); }

int Structure::numberValue(ObservedBoard::CellState state) { return (int)(state); }

U128 Structure::cellSignature(int row, int col, const ObservedBoard::Result &board) {
    U128 hash;
    forEachAdjacent(row, col, board.board.rows(), board.board.cols(), [&](int neighborRow, int neighborCol) {
        if (!isNumber(board.board[neighborRow][neighborCol]))
            return;
        // 坐标种子只依赖数字格坐标，使全盘分析和增量更新得到相同签名。
        const std::uint64_t position = ((std::uint64_t)(neighborRow) << 32) | (std::uint64_t)(neighborCol);
        hash += U128{splitmix64(position), splitmix64(position + 0x9e3779b97f4a7c15ULL)};
    });
    return hash;
}

Structure::Shape::ConstraintView Structure::Shape::constraint(const Pool &pool, std::size_t i) const {
    const Constraint &data = constraints_.span(pool.constraints)[i];
    return {data.sum, boxIds_.span(pool.boxIds).subspan(data.offset, data.count)};
}

std::size_t Structure::Shape::constraintCount() const { return constraints_.size; }

std::size_t Structure::Instance::Boxes::count() const { return boxOf.size - 1; }

std::size_t Structure::Instance::Boxes::cellCount(const Pool &pool, std::size_t box) const {
    const std::span<const int> offsets = boxOf.span(pool.boxOf);
    return offsets[box + 1] - offsets[box];
}

void Structure::remapInstance(InstanceId instance, ComponentId component, const Pool &pool, std::vector<Result::CellLocation> &cellLoc) {
    const Instance &data = pool.getInstance(instance);
    const std::span<const int> boxOf = data.boxes.boxOf.span(pool.boxOf);
    const std::span<const ObservedBoard::CellId> cells = data.boxes.cells.span(pool.cells);
    for (int box = 0; box < (int)(data.boxes.count()); ++box)
        for (int i = boxOf[box]; i < boxOf[box + 1]; ++i)
            cellLoc[cells[i]] = Result::CellLocation{component, box};
    for (ObservedBoard::CellId cell : data.constraintCells.span(pool.constraintCells))
        cellLoc[cell] = Result::CellLocation{component, -1};
}

void Structure::clearInstance(InstanceId instance, const Pool &pool, std::vector<Result::CellLocation> &cellLoc) {
    const Instance &data = pool.getInstance(instance);
    for (ObservedBoard::CellId cell : data.boxes.cells.span(pool.cells))
        cellLoc[cell] = Result::CellLocation{};
    for (ObservedBoard::CellId cell : data.constraintCells.span(pool.constraintCells))
        cellLoc[cell] = Result::CellLocation{};
}

Structure::ShapeId Structure::Pool::internShape(Shape shape) {
    if (const ShapeId *found = shapeIndex_.find(shape.hash)) {
        boxes.pop_back(shape.boxes);
        constraints.pop_back(shape.constraints_);
        boxIds.pop_back(shape.boxIds_);
        return *found;
    }
    const ShapeId id = shapes_.size();
    shapes_.push_back(std::move(shape));
    shapeIndex_.emplace(shapes_[id].hash, id);
    return id;
}

Structure::InstanceId Structure::Pool::internInstance(Instance data) {
    const U128 hash = computeInstanceHash(data, *this);
    if (const InstanceId *found = instanceIndex_.find(hash)) {
        cells.pop_back(data.boxes.cells);
        boxOf.pop_back(data.boxes.boxOf);
        constraintCells.pop_back(data.constraintCells);
        return *found;
    }
    const InstanceId id = instances_.size();
    instances_.push_back(std::move(data));
    instanceIndex_.emplace(hash, id);
    return id;
}

const Structure::Shape &Structure::Pool::getShape(ShapeId id) const { return shapes_[id]; }

const Structure::Shape &Structure::Pool::get(ShapeId id) const { return getShape(id); }

const Structure::Instance &Structure::Pool::getInstance(InstanceId id) const { return instances_[id]; }

std::size_t Structure::Pool::size() const { return shapes_.size(); }

void Structure::Pool::clear() {
    shapes_.clear();
    instances_.clear();
    shapeIndex_.clear();
    instanceIndex_.clear();
    boxes.clear();
    constraints.clear();
    boxIds.clear();
    cells.clear();
    boxOf.clear();
    constraintCells.clear();
}

U128 Structure::Pool::computeInstanceHash(const Instance &data, const Pool &pool) {
    U128Hasher hasher;
    hasher.mix((std::uint64_t)(data.shape));
    hasher.mix((std::uint64_t)(data.boxes.cells.size));
    for (ObservedBoard::CellId cell : data.boxes.cells.span(pool.cells))
        hasher.mix(cell);
    hasher.mix((std::uint64_t)(data.boxes.boxOf.size));
    for (int offset : data.boxes.boxOf.span(pool.boxOf))
        hasher.mix((std::uint64_t)(offset));
    hasher.mix((std::uint64_t)(data.constraintCells.size));
    for (ObservedBoard::CellId cell : data.constraintCells.span(pool.constraintCells))
        hasher.mix(cell);
    return hasher.finalize();
}

Structure::Result Structure::analyze(const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool, Workspace &workspace) {
    const int rows = board.board.rows();
    const int cols = board.board.cols();
    Result result;
    result.cellLoc.assign((std::size_t)rows * cols, Result::CellLocation{});

    if (workspace.analyze.visited.rows() != rows || workspace.analyze.visited.cols() != cols) {
        workspace.analyze.visited.resize(rows, cols, 0);
        workspace.analyze.cellHash.resize(rows, cols, U128{});
        workspace.analyze.cells.reserve((std::size_t)rows * cols / 2);
    } else {
        workspace.analyze.visited.fill(0);
        workspace.analyze.cellHash.fill(U128{});
    }
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (basic.marks[row][col] == Basic::Mark::H)
                workspace.analyze.cellHash[row][col] = cellSignature(row, col, board);
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (basic.marks[row][col] == Basic::Mark::H && !workspace.analyze.visited[row][col]) {
                workspace.analyze.cells.clear();
                collectComponent(board.id(row, col), board, basic, workspace.analyze.visited, workspace.analyze.cells);
                result.components.push_back(buildComponent(workspace.analyze.cells, board, basic, workspace.analyze.cellHash, pool, workspace));
            }
    for (ComponentId component = 0; component < (int)(result.components.size()); ++component)
        remapInstance(result.components[component], component, pool, result.cellLoc);
    return result;
}

void Structure::collectComponent(ObservedBoard::CellId start, const ObservedBoard::Result &board, const Basic::Result &basic, RawGrid<char> &visited,
                                 std::vector<ObservedBoard::CellId> &cells) {
    const auto [startRow, startCol] = board.pos(start);
    visited[startRow][startCol] = 1;
    cells.push_back(start);
    for (int i = 0; i < (int)(cells.size()); ++i) {
        const auto [row, col] = board.pos(cells[i]);
        if (isNumber(board.board[row][col]))
            forEachAdjacent(row, col, board.board.rows(), board.board.cols(), [&](int neighborRow, int neighborCol) {
                if (basic.marks[neighborRow][neighborCol] == Basic::Mark::H && !visited[neighborRow][neighborCol]) {
                    visited[neighborRow][neighborCol] = 1;
                    cells.push_back(board.id(neighborRow, neighborCol));
                }
            });
        if (basic.marks[row][col] == Basic::Mark::H)
            forEachAdjacent(row, col, board.board.rows(), board.board.cols(), [&](int neighborRow, int neighborCol) {
                if (isNumber(board.board[neighborRow][neighborCol]) && !visited[neighborRow][neighborCol]) {
                    visited[neighborRow][neighborCol] = 1;
                    cells.push_back(board.id(neighborRow, neighborCol));
                }
            });
    }
}

Structure::InstanceId Structure::buildComponent(const std::vector<ObservedBoard::CellId> &componentCells, const ObservedBoard::Result &board,
                                                const Basic::Result &basic, RawGrid<U128> &cellHash, Pool &pool, Workspace &workspace) {
    workspace.hashBox.clear();
    workspace.boxOfCells.assign(componentCells.size(), -1);
    vectorPool<Shape::Box>::vector shapeBoxes = pool.boxes.push_back();
    vectorPool<Shape::Constraint>::vector shapeConstraints = pool.constraints.push_back();
    vectorPool<BoxId>::vector shapeBoxIds = pool.boxIds.push_back();
    for (int i = 0; i < (int)(componentCells.size()); ++i) {
        const auto [row, col] = board.pos(componentCells[i]);
        if (basic.marks[row][col] != Basic::Mark::H)
            continue;
        const U128 hash = cellHash[row][col];
        BoxId box;
        if (const BoxId *found = workspace.hashBox.find(hash))
            box = *found;
        else {
            box = shapeBoxes.size;
            shapeBoxes.push_back(pool.boxes, {0});
            workspace.hashBox.emplace(hash, box);
        }
        ++shapeBoxes.span(pool.boxes)[box].size;
        workspace.boxOfCells[i] = box;
        cellHash[row][col] = U128{(std::uint64_t)(box), 0};
    }
    workspace.bucketSize.assign(shapeBoxes.size, 0);
    if ((int)(workspace.buckets.size()) < shapeBoxes.size)
        workspace.buckets.resize(shapeBoxes.size);
    for (int i = 0; i < (int)(componentCells.size()); ++i) {
        const BoxId box = workspace.boxOfCells[i];
        if (box == -1)
            continue;
        workspace.buckets[box][workspace.bucketSize[box]++] = componentCells[i];
    }
    Instance::Boxes instanceBoxes{pool.cells.push_back(), pool.boxOf.push_back()};
    vectorPool<ObservedBoard::CellId>::vector constraintCells = pool.constraintCells.push_back();
    int boxOffset = 0;
    instanceBoxes.boxOf.push_back(pool.boxOf, 0);
    for (int box = 0; box < shapeBoxes.size; ++box) {
        for (int i = 0; i < workspace.bucketSize[box]; ++i)
            instanceBoxes.cells.push_back(pool.cells, workspace.buckets[box][i]);
        boxOffset += workspace.bucketSize[box];
        instanceBoxes.boxOf.push_back(pool.boxOf, boxOffset);
    }
    workspace.boxUsed.assign(shapeBoxes.size, 0);
    workspace.allBoxIds.clear();
    for (ObservedBoard::CellId cell : componentCells) {
        const auto [row, col] = board.pos(cell);
        if (!isNumber(board.board[row][col]))
            continue;
        int sum = numberValue(board.board[row][col]);
        const std::uint32_t start = workspace.allBoxIds.size();
        forEachAdjacent(row, col, board.board.rows(), board.board.cols(), [&](int neighborRow, int neighborCol) {
            if (basic.marks[neighborRow][neighborCol] == Basic::Mark::F)
                --sum;
            if (basic.marks[neighborRow][neighborCol] != Basic::Mark::H)
                return;
            const BoxId box = cellHash[neighborRow][neighborCol].lo;
            if (!workspace.boxUsed[box]) {
                workspace.boxUsed[box] = 1;
                workspace.allBoxIds.push_back(box);
            }
        });
        for (int i = (int)start; i < (int)(workspace.allBoxIds.size()); ++i)
            workspace.boxUsed[workspace.allBoxIds[i]] = 0;
        const std::uint8_t count = workspace.allBoxIds.size() - start;
        shapeConstraints.push_back(pool.constraints, {sum, start, count});
        constraintCells.push_back(pool.constraintCells, cell);
    }
    for (BoxId box : workspace.allBoxIds)
        shapeBoxIds.push_back(pool.boxIds, box);
    // 组件组装完后再构造记录，哈希和 ShapeId 在创建时就是实际值。
    const ShapeId shape = pool.internShape(Shape{shapeBoxes, computeHash(shapeBoxes, shapeConstraints, shapeBoxIds, pool), shapeConstraints, shapeBoxIds});
    return pool.internInstance(Instance{shape, instanceBoxes, constraintCells});
}

U128 Structure::computeHash(vectorPool<Shape::Box>::vector boxes, vectorPool<Shape::Constraint>::vector constraints, vectorPool<BoxId>::vector boxIds, const Pool &pool) {
    U128Hasher hasher;
    for (const Shape::Box &box : boxes.span(pool.boxes))
        hasher.mix((std::uint64_t)(box.size));
    for (int i = 0; i < constraints.size; ++i) {
        const Shape::Constraint &constraint = constraints.span(pool.constraints)[i];
        hasher.mix((std::uint64_t)(constraint.sum));
        for (std::uint32_t k = 0; k < constraint.count; ++k)
            hasher.mix((std::uint64_t)(boxIds.span(pool.boxIds)[constraint.offset + k]) + 0x9e3779b9ULL);
    }
    return hasher.finalize();
}

void Structure::update(Result &result, Delta &delta, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool,
                       std::span<const ObservedBoard::CellId> changedCells, Workspace &workspace) {
    const int rows = board.board.rows();
    const int cols = board.board.cols();
    Workspace::Update &scratch = workspace.update;
    if (scratch.dirty.rows() != rows || scratch.dirty.cols() != cols) {
        scratch.dirty.resize(rows, cols, 0);
        scratch.visited.resize(rows, cols, 0);
        scratch.cellHash.resize(rows, cols, U128{});
        scratch.cells.reserve((std::size_t)rows * cols / 2);
    }
    scratch.dirtyCells.clear();
    scratch.staged.clear();
    scratch.removed.assign(result.components.size(), 0);
    auto markDirty = [&](int row, int col) {
        if (scratch.dirty[row][col])
            return;
        scratch.dirty[row][col] = 1;
        scratch.dirtyCells.push_back(board.id(row, col));
    };
    auto invalidate = [&](ComponentId component) {
        scratch.removed[component] = 1;
        const Instance &instance = pool.getInstance(result.components[component]);
        for (ObservedBoard::CellId cell : instance.boxes.cells.span(pool.cells)) {
            const auto [row, col] = board.pos(cell);
            markDirty(row, col);
            result.cellLoc[cell] = Result::CellLocation{};
        }
        for (ObservedBoard::CellId cell : instance.constraintCells.span(pool.constraintCells)) {
            const auto [row, col] = board.pos(cell);
            markDirty(row, col);
            result.cellLoc[cell] = Result::CellLocation{};
        }
    };
    for (ObservedBoard::CellId cell : changedCells) {
        const auto [row, col] = board.pos(cell);
        markDirty(row, col);
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            markDirty(neighborRow, neighborCol);
        });
    }
    for (int i = 0; i < (int)(scratch.dirtyCells.size()); ++i) {
        const Result::CellLocation location = result.cellLoc[scratch.dirtyCells[i]];
        if (location.component == -1 || scratch.removed[location.component])
            continue;
        invalidate(location.component);
    }
    for (int i = 0; i < (int)(scratch.dirtyCells.size()); ++i) {
        const ObservedBoard::CellId start = scratch.dirtyCells[i];
        const auto [row, col] = board.pos(start);
        if (basic.marks[row][col] != Basic::Mark::H || scratch.visited[row][col])
            continue;
        scratch.cells.clear();
        collectComponent(start, board, basic, scratch.visited, scratch.cells);
        for (ObservedBoard::CellId cell : scratch.cells) {
            const auto [componentRow, componentCol] = board.pos(cell);
            if (basic.marks[componentRow][componentCol] == Basic::Mark::H)
                scratch.cellHash[componentRow][componentCol] = cellSignature(componentRow, componentCol, board);
        }
        scratch.staged.push_back(buildComponent(scratch.cells, board, basic, scratch.cellHash, pool, workspace));
    }
    delta.removed.clear();
    delta.added.assign(scratch.staged.begin(), scratch.staged.end());
    for (int i = (int)(result.components.size()) - 1; i >= 0; --i) {
        if (!scratch.removed[i])
            continue;
        delta.removed.push_back({i, result.components[i]});
    }
    applyDelta(result, pool, delta.removed, delta.added);
    // 新连接只会出现在变化格及其邻格；远端传播变化落在已命中的旧组件中，前面的失效流程已登记整组格子。因此 collectComponent 访问的格子均在 dirtyCells，按 dirtyCells 清理即可。
    for (ObservedBoard::CellId cell : scratch.dirtyCells) {
        const auto [row, col] = board.pos(cell);
        scratch.dirty[row][col] = 0;
        scratch.visited[row][col] = 0;
        scratch.cellHash[row][col] = U128{};
    }
}

void Structure::applyDelta(Result &result, const Pool &pool, std::span<const Delta::Change> removed, std::span<const InstanceId> added, bool reverse) {
    if (reverse) {
        for (int i = (int)added.size() - 1; i >= 0; --i) {
            clearInstance(result.components.back(), pool, result.cellLoc);
            result.components.pop_back();
        }
        for (int i = (int)(removed.size()) - 1; i >= 0; --i) {
            const Delta::Change &change = removed[i];
            const ComponentId tail = result.components.size();
            if (change.component != tail) {
                result.components.push_back(std::move(result.components[change.component]));
                remapInstance(result.components.back(), tail, pool, result.cellLoc);
                result.components[change.component] = change.previous;
            } else
                result.components.push_back(change.previous);
            remapInstance(result.components[change.component], change.component, pool, result.cellLoc);
        }
        return;
    }
    for (const Delta::Change &change : removed) {
        clearInstance(result.components[change.component], pool, result.cellLoc);
        const ComponentId last = result.components.size() - 1;
        if (change.component != last) {
            result.components[change.component] = result.components[last];
            remapInstance(result.components[change.component], change.component, pool, result.cellLoc);
        }
        result.components.pop_back();
    }
    for (InstanceId instance : added) {
        const ComponentId component = result.components.size();
        result.components.push_back(instance);
        remapInstance(instance, component, pool, result.cellLoc);
    }
}

} // 命名空间 mss
