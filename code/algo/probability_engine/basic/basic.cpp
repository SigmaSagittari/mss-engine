#include "algo/probability_engine/basic/basic.h"

#include <cstddef>
#include <cstring>
#include <utility>

#include "core/algorithm/neighbors.h"
#include "core/assert.h"

namespace mss {

// 数字状态底层值连续为 0~8；下面两个判断只接收对应状态。
static bool isNumber(ObservedBoard::CellState state) { return (int)state <= (int)ObservedBoard::CellState::Num8; }
static int numberValue(ObservedBoard::CellState state) { return (int)state; }
static bool isCandidate(Basic::Mark mark) { return mark == Basic::Mark::H || mark == Basic::Mark::T; }

void Basic::analyze(Result &result, const ObservedBoard::Result &state, Basic::Workspace &workspace) {
    const int rows = state.cells.rows();
    const int cols = state.cells.cols();
    result.unknownSum = 0;
    result.mineSum = 0;
    result.marks.resize(rows, cols, Mark::S);

    // 隐藏格改为 T，强制雷改为 F，数字与强制安全格保持 S。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const ObservedBoard::CellState cell = state.cells[row][col];
            if (cell == ObservedBoard::CellState::Hidden)
                result.marks[row][col] = Mark::T;
            else if (cell == ObservedBoard::CellState::ForcedMine)
                result.marks[row][col] = Mark::F;
            else if (cell != ObservedBoard::CellState::ForcedSafe && !isNumber(cell))
                assert_(false, "Basic::analyze: unknown observed cell state");
        }

    // 挨着已翻开数字的隐藏候选格从 T 改为 H。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (isNumber(state.cells[row][col]))
                forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                    if (state.cells[neighborRow][neighborCol] == ObservedBoard::CellState::Hidden && result.marks[neighborRow][neighborCol] == Mark::T)
                        result.marks[neighborRow][neighborCol] = Mark::H;
                });

    vector<ObservedBoard::CellId> &pending = workspace.pending;
    pending.resize(0);
    // 先把所有数字入队；推导后追加受影响的数字，重复项出队时按最新标记重算。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (isNumber(state.cells[row][col]))
                pending.push_back(state.id(row, col));

    for (int head = 0; head < (int)pending.size; ++head) {
        const std::pair<int, int> position = state.pos(pending.data[head]);
        const int row = position.first;
        const int col = position.second;
        int mineCount = 0;
        int candidateCount = 0;
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (result.marks[neighborRow][neighborCol] == Mark::F)
                ++mineCount;
            else if (state.cells[neighborRow][neighborCol] == ObservedBoard::CellState::Hidden && isCandidate(result.marks[neighborRow][neighborCol]))
                ++candidateCount;
        });

        const int remaining = numberValue(state.cells[row][col]) - mineCount;
        if (remaining != 0 && remaining != candidateCount)
            continue;

        const Mark mark = remaining == 0 ? Mark::S : Mark::F;
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (state.cells[neighborRow][neighborCol] != ObservedBoard::CellState::Hidden || !isCandidate(result.marks[neighborRow][neighborCol]))
                return;
            result.marks[neighborRow][neighborCol] = mark;
            forEachAdjacent(neighborRow, neighborCol, rows, cols, [&](int affectedRow, int affectedCol) {
                if (isNumber(state.cells[affectedRow][affectedCol]))
                    pending.push_back(state.id(affectedRow, affectedCol));
            });
        });
    }

    // 传播结束后一次性统计邻域计数。
    result.mineAround.resize(rows, cols, 0);
    result.hideAround.resize(rows, cols, 0);
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                if (result.marks[neighborRow][neighborCol] == Mark::F)
                    ++result.mineAround[row][col];
                else if (isCandidate(result.marks[neighborRow][neighborCol]))
                    ++result.hideAround[row][col];
            });

    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const Mark mark = result.marks[row][col];
            if (mark == Mark::F)
                ++result.mineSum;
            if (mark == Mark::T)
                ++result.unknownSum;
        }
}

Basic::Delta Basic::update(Result &result, const ObservedBoard::Result &board, const ObservedBoard::Delta &updates, Pool &pool, Workspace &workspace) {
    Delta delta{pool.changes.push_back()};
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    vector<ObservedBoard::CellId> &pending = workspace.pending;
    vector<unsigned char> &queued = workspace.queued;
    pending.resize(0);
    const std::size_t queueSize = (std::size_t)rows * cols;
    // queued 与 CellId 一一对应；数字处理完才清零，避免推导邻格时将自己重新入队。
    if (queued.size != queueSize) {
        queued.resize(queueSize);
        std::memset(queued.data, 0, queueSize);
    }

    // 每次真实变化都记录旧标记；F/候选身份改变时增量修正邻格缓存并重排受影响数字。
    auto setMark = [&](int row, int col, Mark mark) {
        Mark &current = result.marks[row][col];
        if (current == mark)
            return;
        const Mark previous = current;
        if (previous == Mark::T)
            --result.unknownSum;
        if (previous == Mark::F)
            --result.mineSum;
        if (mark == Mark::T)
            ++result.unknownSum;
        if (mark == Mark::F)
            ++result.mineSum;
        current = mark;
        pool.changes.push_back(delta.changes, {board.id(row, col), previous, mark});

        const bool previousMine = previous == Mark::F;
        const bool nextMine = mark == Mark::F;
        const bool previousCandidate = isCandidate(previous);
        const bool nextCandidate = isCandidate(mark);
        if (previousMine == nextMine && previousCandidate == nextCandidate)
            return;
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (previousMine)
                --result.mineAround[neighborRow][neighborCol];
            if (nextMine)
                ++result.mineAround[neighborRow][neighborCol];
            if (previousCandidate)
                --result.hideAround[neighborRow][neighborCol];
            if (nextCandidate)
                ++result.hideAround[neighborRow][neighborCol];
            if (!isNumber(board.cells[neighborRow][neighborCol]))
                return;
            const ObservedBoard::CellId neighbor = board.id(neighborRow, neighborCol);
            if (!queued.data[neighbor]) {
                queued.data[neighbor] = 1;
                pending.push_back(neighbor);
            }
        });
    };

    const ObservedBoard::Delta::Change *boardChanges = pool.board.changes.data.data + updates.changes.offset;
    for (int i = 0; i < (int)updates.changes.count; ++i) {
        const ObservedBoard::Delta::Change &change = boardChanges[i];
        const std::pair<int, int> position = board.pos(change.cell);
        const int row = position.first;
        const int col = position.second;
        if (change.next == ObservedBoard::CellState::ForcedMine || change.next == ObservedBoard::CellState::ForcedSafe) {
            setMark(row, col, change.next == ObservedBoard::CellState::ForcedMine ? Mark::F : Mark::S);
            continue;
        }

        setMark(row, col, Mark::S);
        if (isNumber(change.next) && !queued.data[change.cell]) {
            queued.data[change.cell] = 1;
            pending.push_back(change.cell);
        }
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (board.cells[neighborRow][neighborCol] == ObservedBoard::CellState::Hidden && result.marks[neighborRow][neighborCol] == Mark::T)
                setMark(neighborRow, neighborCol, Mark::H);
        });
    }

    for (int head = 0; head < (int)pending.size; ++head) {
        const std::pair<int, int> position = board.pos(pending.data[head]);
        const int row = position.first;
        const int col = position.second;
        const int candidateCount = result.hideAround[row][col];
        const int remaining = numberValue(board.cells[row][col]) - result.mineAround[row][col];
        if (candidateCount != 0 && (remaining == 0 || remaining == candidateCount)) {
            const Mark mark = remaining == 0 ? Mark::S : Mark::F;
            forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                if (board.cells[neighborRow][neighborCol] != ObservedBoard::CellState::Hidden || !isCandidate(result.marks[neighborRow][neighborCol]))
                    return;
                setMark(neighborRow, neighborCol, mark);
            });
        }
        queued.data[pending.data[head]] = 0;
    }
    return delta;
}

void Basic::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    const Delta::Change *changes = pool.changes.data.data + delta.changes.offset;
    for (int i = 0; i < (int)delta.changes.count; ++i) {
        const Delta::Change &change = changes[reverse ? (int)delta.changes.count - 1 - i : i];
        const ObservedBoard::CellId cell = change.cell;
        const Mark mark = reverse ? change.previous : change.next;
        const int row = cell / result.marks.cols();
        const int col = cell % result.marks.cols();
        const Mark current = result.marks[row][col];
        if (current == Mark::T)
            --result.unknownSum;
        if (current == Mark::F)
            --result.mineSum;
        if (mark == Mark::T)
            ++result.unknownSum;
        if (mark == Mark::F)
            ++result.mineSum;

        const bool currentMine = current == Mark::F;
        const bool nextMine = mark == Mark::F;
        const bool currentCandidate = isCandidate(current);
        const bool nextCandidate = isCandidate(mark);
        if (currentMine != nextMine || currentCandidate != nextCandidate)
            forEachAdjacent(row, col, result.marks.rows(), result.marks.cols(), [&](int neighborRow, int neighborCol) {
                if (currentMine)
                    --result.mineAround[neighborRow][neighborCol];
                if (nextMine)
                    ++result.mineAround[neighborRow][neighborCol];
                if (currentCandidate)
                    --result.hideAround[neighborRow][neighborCol];
                if (nextCandidate)
                    ++result.hideAround[neighborRow][neighborCol];
            });
        result.marks[row][col] = mark;
    }
}

bool Basic::checkValid(const ObservedBoard::Result &board, const Result &result) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    int mineSum = 0;
    int unknownSum = 0;
    int candidateSum = 0;

    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const Mark mark = result.marks[row][col];
            const ObservedBoard::CellState state = board.cells[row][col];
            if (mark == Mark::F)
                ++mineSum;
            if (mark == Mark::T)
                ++unknownSum;
            if (isCandidate(mark))
                ++candidateSum;
            if (state == ObservedBoard::CellState::ForcedMine && mark != Mark::F)
                return false;
            if ((state == ObservedBoard::CellState::ForcedSafe || isNumber(state)) && mark != Mark::S)
                return false;

            int mineAround = 0;
            int hideAround = 0;
            bool adjacentNumber = false;
            forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                if (isNumber(board.cells[neighborRow][neighborCol]))
                    adjacentNumber = true;
                if (result.marks[neighborRow][neighborCol] == Mark::F)
                    ++mineAround;
                else if (isCandidate(result.marks[neighborRow][neighborCol]))
                    ++hideAround;
            });
            if (state == ObservedBoard::CellState::Hidden && ((mark == Mark::H && !adjacentNumber) || (mark == Mark::T && adjacentNumber)))
                return false;
            if (mineAround != result.mineAround[row][col] || hideAround != result.hideAround[row][col])
                return false;
            if (isNumber(state) && (numberValue(state) < mineAround || numberValue(state) > mineAround + hideAround))
                return false;
        }

    if (mineSum != result.mineSum || unknownSum != result.unknownSum)
        return false;
    if (mineSum > board.totalMines || mineSum + candidateSum < board.totalMines)
        return false;
    return true;
}

} // 命名空间 mss
