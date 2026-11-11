#include "algo/probability_engine/structure/structure.h"

#include <algorithm>
#include <bit>
#include <cstddef>

#include "core/algorithm/neighbors.h"

namespace mss {

struct Structure::Workspace {
    struct Build {
        vector<CellId> digits;
    } build;

    struct Update {
        vector<std::uint8_t> selected;
        vector<std::uint8_t> seenMarks;
    } update;
};

__declspec(noinline) Structure::Workspace &Structure::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void Structure::growFlags(vector<std::uint8_t> &flags, std::size_t count) {
    const std::size_t previousSize = flags.size;
    if (count <= previousSize)
        return;
    flags.resize(count);
    std::fill_n(flags.data + previousSize, count - previousSize, 0);
}

int Structure::neighborDirection(int deltaRow, int deltaCol) {
    const int index = (deltaRow + 1) * 3 + deltaCol + 1;
    return index - (index > 4);
}

void Structure::Pool::clear() {
    shapes.constraints.data.resize(0);
    shapes.boxIds.data.resize(0);
    shapes.list.resize(0);
    shapes.index.clear();
    instances.cells.data.resize(0);
    instances.boxes.data.resize(0);
    instances.constraintCells.data.resize(0);
    instances.list.resize(0);
    instances.index.clear();
    deltas.removed.data.resize(0);
    deltas.added.data.resize(0);
}

Structure::ShapeId Structure::Pool::Shapes::intern(Shape shape, const BoxPool &boxPool, BoxPool::vector boxes) {
    U128Hasher hash;
    hash.mix((std::uint64_t) boxes.count);
    for (const vectorPool<CellId>::vector &box : boxPool.span(boxes))
        hash.mix((std::uint64_t) box.count);
    hash.mix((std::uint64_t) shape.constraints.count);
    for (const Shape::Constraint &constraint : constraints.span(shape.constraints)) {
        hash.mix(constraint.sum);
        hash.mix((std::uint64_t) constraint.boxIds.count);
        for (BoxId box : boxIds.span(constraint.boxIds))
            hash.mix(box);
    }

    const U128 key = hash.finalize();
    const ShapeId *found = index.find(key);
    if (found != nullptr) {
        constraints.pop_back(shape.constraints);
        boxIds.pop_back(shape.boxIds);
        return *found;
    }
    const ShapeId id = (ShapeId) list.size;
    list.push_back(shape);
    index.emplace(key, id);
    return id;
}

Structure::InstanceId Structure::Pool::Instances::intern(Instance instance) {
    U128Hasher hash;
    hash.mix(instance.shape);
    for (const vectorPool<CellId>::vector &box : boxes.span(instance.boxes))
        for (CellId cell : cells.span(box))
            hash.mix(cell);
    for (CellId cell : constraintCells.span(instance.constraintCells))
        hash.mix(cell);

    const U128 key = hash.finalize();
    const InstanceId *found = index.find(key);
    // 命中时把本次追加的数据整段退掉：格号按 Box 顺序连续追加，首个 Box 切片的位置就是这段格号的
    // 起点，退到那里即可。
    if (found != nullptr) {
        cells.pop_back(boxes.data.data[instance.boxes.offset]);
        boxes.pop_back(instance.boxes);
        constraintCells.pop_back(instance.constraintCells);
        return *found;
    }
    const InstanceId id = (InstanceId) list.size;
    list.push_back(instance);
    index.emplace(key, id);
    return id;
}

Structure::InstanceId Structure::buildInstance(CellId start, Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool) {
    Workspace &workspace = getWorkspace();
    vector<CellId> &digits = workspace.build.digits;
    vector<Result::CellLocation> &locations = result.cellLoc;
    std::uint8_t *adjacentH = result.adjacentH[0];
    int *adjacentDigits = result.adjacentDigits[0];
    const ObservedBoard::CellState *states = board.cells[0];
    const Basic::Mark *marks = basic.marks[0];
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    const int instanceIndex = (int) result.instanceIds.size;
    const int offsets[8] = {-cols - 1, -cols, -cols + 1, -1, 1, cols - 1, cols, cols + 1};
    constexpr int shifts[8] = {0, 1, 2, 5, 7, 10, 11, 12};

    // 边界处理：先假设 8 个方向都有效，再按所在边把整条越界的方向去掉。
    // 位号就是 offsets 的下标：低 3 位是左上、上、右上，高 3 位是左下、下、右下；
    // 左列占位 0、3、5，右列占位 2、4、7，被中心位隔断，所以两个列的常量看不出是一条边。
    auto validDirections = [&](CellId cell) {
        const auto [row, col] = board.pos(cell);
        unsigned valid = 0xff;
        if (row == 0) valid &= ~0b00000111u;
        if (row + 1 == rows) valid &= ~0b11100000u;
        if (col == 0) valid &= ~0b00101001u;
        if (col + 1 == cols) valid &= ~0b10010100u;
        return valid;
    };
    // H 首次出现时立即展开，只把数字入队，保留二部图 BFS 中的数字顺序。
    auto visitHidden = [&](CellId cell) {
        if (locations.data[cell].instanceIndex >= 0)
            return;
        locations.data[cell].instanceIndex = instanceIndex;
        int mask = 0;
        for (unsigned bits = validDirections(cell); bits; bits &= bits - 1) {
            const int direction = std::countr_zero(bits);
            const CellId digit = cell + offsets[direction];
            if (ObservedBoard().isNumber(states[digit])) {
                mask |= 1 << shifts[direction];
                if (locations.data[digit].instanceIndex < 0) {
                    locations.data[digit].instanceIndex = instanceIndex;
                    digits.push_back(digit);
                }
            }
        }
        adjacentDigits[cell] = mask;
    };

    Pool::Shapes &shapes = pool.shapes;
    Pool::Instances &instances = pool.instances;
    Shape shape{shapes.constraints.push_back(), shapes.boxIds.push_back()};
    Pool::BoxPool::vector instanceBoxes = instances.boxes.push_back();
    vectorPool<CellId>::vector constraintCells = instances.constraintCells.push_back();
    visitHidden(start);
    for (std::size_t i = 0; i < digits.size; ++i) {
        const CellId digit = digits.data[i];
        int sum = ObservedBoard().numberValue(states[digit]);
        unsigned hidden = 0;
        for (unsigned bits = validDirections(digit); bits; bits &= bits - 1) {
            const int direction = std::countr_zero(bits);
            const CellId cell = digit + offsets[direction];
            const Basic::Mark mark = marks[cell];
            sum -= mark == Basic::Mark::F;
            if (mark == Basic::Mark::H) {
                hidden |= 1u << direction;
                visitHidden(cell);
            }
        }
        adjacentH[digit] = hidden;

        // 归组：当前数字的邻格里，邻接数字集合和首格相同的 H 并成一个 Box。
        // 两份掩码各自左移「它相对当前数字」的位号，都移到了当前数字的坐标系，比的是同一批实际
        // 数字格，掩码相等就是集合相同。
        // 同一 Box 的成员邻接的数字集合相同，任意一个数字的邻格里都包含它的全部成员，扫一遍就能
        // 收齐，不需要并查集。
        const std::size_t begin = shapes.boxIds.data.size;
        while (hidden) {
            const int direction = std::countr_zero(hidden);
            hidden &= hidden - 1;
            const CellId cell = digit + offsets[direction];
            BoxId box = locations.data[cell].box;
            if (box >= 0) {
                // 首成员在每个共有数字的邻格里都出现，由它写这一段 Box 编号，其余成员跳过。
                const vectorPool<CellId>::vector &members = instances.boxes.data.data[instanceBoxes.offset + box];
                if (instances.cells.data.data[members.offset] != cell)
                    continue;
            } else {
                box = (BoxId) instanceBoxes.count;
                vectorPool<CellId>::vector members = instances.cells.push_back();
                instances.cells.push_back(members, cell);
                locations.data[cell].box = box;
                const int pattern = adjacentDigits[cell] << shifts[direction];
                for (unsigned bits = hidden; bits; bits &= bits - 1) {
                    const int other = std::countr_zero(bits);
                    const CellId neighbor = digit + offsets[other];
                    if ((adjacentDigits[neighbor] << shifts[other]) == pattern) {
                        instances.cells.push_back(members, neighbor);
                        locations.data[neighbor].box = box;
                        hidden &= ~(1u << other);
                    }
                }
                instances.boxes.push_back(instanceBoxes, members);
            }
            shapes.boxIds.push_back(shape.boxIds, box);
        }
        shapes.constraints.push_back(shape.constraints, {sum, {begin, shapes.boxIds.data.size - begin}});
        instances.constraintCells.push_back(constraintCells, digit);
    }
    digits.resize(0);
    const ShapeId shapeId = shapes.intern(shape, instances.boxes, instanceBoxes);
    return instances.intern({shapeId, instanceBoxes, constraintCells});
}

void Structure::setLocations(Result &result, const Pool &pool, InstanceId id, int instanceIndex) {
    const Instance &instance = pool.instances.list.data[id];
    BoxId box = 0;
    for (const vectorPool<CellId>::vector &cells : pool.instances.boxes.span(instance.boxes)) {
        const Result::CellLocation location{instanceIndex, instanceIndex < 0 ? -1 : box};
        for (CellId cell : pool.instances.cells.span(cells)) {
            result.cellLoc.data[cell] = location;
            if (instanceIndex < 0)
                result.adjacentDigits[0][cell] = 0;
        }
        ++box;
    }
    for (CellId cell : pool.instances.constraintCells.span(instance.constraintCells)) {
        result.cellLoc.data[cell] = {instanceIndex, -1};
        if (instanceIndex < 0)
            result.adjacentH[0][cell] = 0;
    }
}

void Structure::setInstanceMasks(Result &result, const Pool &pool, InstanceId id) {
    const Instance &instance = pool.instances.list.data[id];
    const Shape &shape = pool.shapes.list.data[instance.shape];
    const vectorPool<CellId>::vector *boxes = pool.instances.boxes.span(instance.boxes).begin();
    const CellId *digits = pool.instances.constraintCells.span(instance.constraintCells).begin();
    const int cols = result.adjacentH.cols();
    std::uint8_t *adjacentH = result.adjacentH[0];
    int *adjacentDigits = result.adjacentDigits[0];
    for (const Shape::Constraint &constraint : pool.shapes.constraints.span(shape.constraints)) {
        const CellId digit = *digits++;
        const int row = digit / cols;
        const int col = digit % cols;
        std::uint8_t mask = 0;
        for (BoxId box : pool.shapes.boxIds.span(constraint.boxIds))
            for (CellId cell : pool.instances.cells.span(boxes[box])) {
                const int deltaRow = cell / cols - row;
                const int deltaCol = cell % cols - col;
                mask |= 1u << neighborDirection(deltaRow, deltaCol);
                adjacentDigits[cell] |= 1 << (6 - 5 * deltaRow - deltaCol);
            }
        adjacentH[digit] = mask;
    }
}

void Structure::analyze(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    const std::size_t cellCount = (std::size_t) rows * cols;
    result.instanceIds.resize(0);
    result.cellLoc.resize(cellCount);
    std::fill_n(result.cellLoc.data, cellCount, Result::CellLocation{});
    result.adjacentH.resize(rows, cols, 0);
    result.adjacentDigits.resize(rows, cols, 0);

    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const CellId cell = board.id(row, col);
            if (basic.marks[row][col] == Basic::Mark::H && result.cellLoc.data[cell].instanceIndex < 0)
                result.instanceIds.push_back(buildInstance(cell, result, board, basic, pool));
        }
}

Structure::Delta Structure::update(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool,
                                   const ObservedBoard::Delta &observed, const Basic::Delta &marks, const Basic::Pool &basicPool) {
    Delta delta{pool.deltas.removed.push_back(), pool.deltas.added.push_back()};
    if (observed.changes.count == 0 && marks.changes.count == 0)
        return delta;

    Workspace &workspace = getWorkspace();
    vector<std::uint8_t> &selected = workspace.update.selected;
    vector<std::uint8_t> &seenMarks = workspace.update.seenMarks;
    const vector<Result::CellLocation> &locations = result.cellLoc;
    const Basic::Mark *finalMarks = basic.marks[0];
    const ObservedBoard::CellState *finalCells = board.cells[0];
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    growFlags(seenMarks, locations.size);
    growFlags(selected, result.instanceIds.size);

    auto invalidate = [&](int index) {
        if (selected.data[index])
            return;
        selected.data[index] = 1;
        pool.deltas.removed.push_back(delta.removed, {index, result.instanceIds.data[index]});
    };

    // 每格取首次记录的旧标记，与最终标记比较。
    for (const Basic::Delta::Change &change : basicPool.changes.span(marks.changes)) {
        const CellId cell = change.cell;
        if (seenMarks.data[cell])
            continue;
        seenMarks.data[cell] = 1;
        const Basic::Mark next = finalMarks[cell];
        if (change.previous == Basic::Mark::H && next != Basic::Mark::H) {
            // 这个 H 的旧数字都在同一块里，作废这一块就够。
            invalidate(locations.data[cell].instanceIndex);
        } else if ((change.previous == Basic::Mark::F) != (next == Basic::Mark::F)) {
            const auto [row, col] = board.pos(cell);
            forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
                const Result::CellLocation location = locations.data[board.id(r, c)];
                if (location.instanceIndex >= 0 && location.box < 0)
                    invalidate(location.instanceIndex);
            });
        }
    }

    // 新数字把挨着的旧块连成一片：这些块整块作废，重建时从其中任一个 H 出发就会连到一起。
    for (const ObservedBoard::Delta::Change &change : basicPool.board.changes.span(observed.changes)) {
        if (ObservedBoard().isNumber(change.previous) || !ObservedBoard().isNumber(finalCells[change.cell]))
            continue;
        const auto [row, col] = board.pos(change.cell);
        forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
            const CellId cell = board.id(r, c);
            const Result::CellLocation location = locations.data[cell];
            if (finalMarks[cell] == Basic::Mark::H && location.box >= 0)
                invalidate(location.instanceIndex);
        });
    }

    if (delta.removed.count > 1) {
        Delta::Change *removed = pool.deltas.removed.data.data + delta.removed.offset;
        std::sort(removed, removed + delta.removed.count,
                  [](const Delta::Change &a, const Delta::Change &b) { return a.instanceIndex > b.instanceIndex; });
    }
    applyDelta(result, delta, pool);

    auto rebuild = [&](CellId cell) {
        if (finalMarks[cell] != Basic::Mark::H || locations.data[cell].instanceIndex >= 0)
            return;
        const InstanceId id = buildInstance(cell, result, board, basic, pool);
        result.instanceIds.push_back(id);
        pool.deltas.added.push_back(delta.added, id);
    };
    // 旧块的存活 H 和本批新 H 覆盖所有待建块。建图可能扩池，跨调用只保留下标和切片。
    for (const Delta::Change &change : pool.deltas.removed.span(delta.removed)) {
        selected.data[change.instanceIndex] = 0;
        const Instance previous = pool.instances.list.data[change.previous];
        for (std::size_t i = 0; i < previous.boxes.count; ++i) {
            const vectorPool<CellId>::vector members = pool.instances.boxes.data.data[previous.boxes.offset + i];
            for (std::size_t j = 0; j < members.count; ++j)
                rebuild(pool.instances.cells.data.data[members.offset + j]);
        }
    }
    for (const Basic::Delta::Change &change : basicPool.changes.span(marks.changes)) {
        seenMarks.data[change.cell] = 0;
        rebuild(change.cell);
    }
    return delta;
}

// 删除用尾元素补空位，块的下标会跟着变：removed 按 instanceIndex 降序记录，删除时下标更小的块
// 还没被交换碰过；撤回按升序走，把当初换进来的尾元素换回末尾，下标就复原。
void Structure::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    vector<InstanceId> &instanceIds = result.instanceIds;
    if (reverse) {
        for (std::size_t i = 0; i < delta.added.count; ++i)
            setLocations(result, pool, instanceIds.data[--instanceIds.size], -1);
        for (const Delta::Change &change : pool.deltas.removed.reverseSpan(delta.removed)) {
            const int tail = (int) instanceIds.size;
            if (change.instanceIndex != tail) {
                const InstanceId moved = instanceIds.data[change.instanceIndex];
                instanceIds.push_back(moved);
                setLocations(result, pool, moved, tail);
                instanceIds.data[change.instanceIndex] = change.previous;
            } else
                instanceIds.push_back(change.previous);
            setLocations(result, pool, change.previous, change.instanceIndex);
            setInstanceMasks(result, pool, change.previous);
        }
        return;
    }

    for (const Delta::Change &change : pool.deltas.removed.span(delta.removed)) {
        const int instanceIndex = change.instanceIndex;
        setLocations(result, pool, instanceIds.data[instanceIndex], -1);
        const int tail = (int) --instanceIds.size;
        if (instanceIndex != tail) {
            instanceIds.data[instanceIndex] = instanceIds.data[tail];
            setLocations(result, pool, instanceIds.data[instanceIndex], instanceIndex);
        }
    }
    for (InstanceId id : pool.deltas.added.span(delta.added)) {
        setLocations(result, pool, id, (int) instanceIds.size);
        setInstanceMasks(result, pool, id);
        instanceIds.push_back(id);
    }
}

} // 命名空间 mss
