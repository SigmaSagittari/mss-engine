#include "algo/probability_engine/basic/basic.h"

#include <cstddef>
#include <utility>

#include "core/assert.h"
#include "core/utility/neighbors.h"

namespace mss {

Basic::Result Basic::analyze(const ObservedBoard::Result &state, Basic::Workspace &workspace) {
    Result result;
    const int rows = state.board.rows();
    const int cols = state.board.cols();
    result.marks.resize(rows, cols, Mark::S);

    // 先把所有格子设为 S；隐藏格改为 T，强制雷改为 F，其余合法状态保持 S。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const ObservedBoard::CellState cell = state.board[row][col];
            if (cell == ObservedBoard::CellState::Hidden)
                result.marks[row][col] = Mark::T;
            else if (cell == ObservedBoard::CellState::ForcedMine)
                result.marks[row][col] = Mark::F;
            else if (cell != ObservedBoard::CellState::ForcedSafe && !isNumber(cell))
                assert_(false, "Basic::analyze: unknown observed cell state");
        }

    // 挨着已翻开数字的隐藏候选格从 T 改为 H；H/T 都是候选雷，只有 T 计入 unknownSum。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (isNumber(state.board[row][col]))
                forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                    if (state.board[neighborRow][neighborCol] == ObservedBoard::CellState::Hidden && result.marks[neighborRow][neighborCol] == Mark::T)
                        result.marks[neighborRow][neighborCol] = Mark::H;
                });

    std::vector<ObservedBoard::CellId> &pending = workspace.pending;
    pending.clear();
    // 先将所有数字入队；推导后追加受影响的数字，不去重，重复项出队时按最新标记重算。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (isNumber(state.board[row][col]))
                pending.push_back(state.id(row, col));

    for (int head = 0; head < (int)pending.size(); ++head) {
        const std::pair<int, int> position = state.pos(pending[head]);
        const int row = position.first;
        const int col = position.second;
        int mineCount = 0;
        int candidateCount = 0;
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (result.marks[neighborRow][neighborCol] == Mark::F)
                ++mineCount;
            else if (state.board[neighborRow][neighborCol] == ObservedBoard::CellState::Hidden && isCandidate(result.marks[neighborRow][neighborCol]))
                ++candidateCount;
        });

        const int remaining = numberValue(state.board[row][col]) - mineCount;
        // 只有剩余雷数为 0 或等于候选数时，才能确定所有相邻候选格。
        if (remaining != 0 && remaining != candidateCount)
            continue;

        const Mark mark = remaining == 0 ? Mark::S : Mark::F;
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (state.board[neighborRow][neighborCol] != ObservedBoard::CellState::Hidden || !isCandidate(result.marks[neighborRow][neighborCol]))
                return;
            result.marks[neighborRow][neighborCol] = mark;
            forEachAdjacent(neighborRow, neighborCol, rows, cols, [&](int affectedRow, int affectedCol) {
                if (isNumber(state.board[affectedRow][affectedCol]))
                    pending.push_back(state.id(affectedRow, affectedCol));
            });
        });
    }

    // 分析阶段不维护增量缓存，传播结束后一次性统计所有格子的邻域计数。
    result.mineAround.resize(rows, cols, 0);
    result.hideAround.resize(rows, cols, 0);
    // 汇总最终标记；unknownSum 只计 T，不计 H。
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

    return result;
}

Basic::Delta Basic::update(Result &result, const ObservedBoard::Result &board, const ObservedBoard::Delta &updates, const ObservedBoard::Pool &boardPool, Pool &pool, Workspace &workspace) {
    Delta delta(pool.changes.push_back());

    const int rows = board.board.rows();
    const int cols = board.board.cols();
    std::vector<ObservedBoard::CellId> &pending = workspace.pending;
    std::vector<unsigned char> &queued = workspace.queued;
    pending.clear();
    const std::size_t queueSize = (std::size_t)rows * cols;
    // queued 与 CellId 一一对应；数字处理完才清零，避免推导邻格时将自己重新入队。
    if (queued.size() != queueSize)
        queued.resize(queueSize, 0);

    // 每次真实变化都记录旧标记；F/候选身份改变时再增量修正邻格缓存并重排受影响数字。
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
        delta.changes.push_back(pool.changes, {board.id(row, col), previous, mark});

        const bool previousMine = previous == Mark::F;
        const bool nextMine = mark == Mark::F;
        const bool previousCandidate = isCandidate(previous);
        const bool nextCandidate = isCandidate(mark);
        // 数字约束只看邻格是否为 F/候选；H 与 T 互换不改这两项计数，无需重排相邻数字。
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
            if (!isNumber(board.board[neighborRow][neighborCol]))
                return;
            const ObservedBoard::CellId neighbor = board.id(neighborRow, neighborCol);
            if (!queued[neighbor]) {
                queued[neighbor] = 1;
                pending.push_back(neighbor);
            }
        });
    };

    for (const ObservedBoard::Delta::Change &change : updates.changes.span(boardPool.changes)) {
        const std::pair<int, int> position = board.pos(change.cell);
        const int row = position.first;
        const int col = position.second;
        // 强制状态不揭示数字；setMark 已处理受影响的相邻数字。
        if (change.next == ObservedBoard::CellState::ForcedMine || change.next == ObservedBoard::CellState::ForcedSafe) {
            const Mark forcedMark = change.next == ObservedBoard::CellState::ForcedMine ? Mark::F : Mark::S;
            setMark(row, col, forcedMark);
            continue;
        }

        // 新揭开的格子变为安全；若是数字，后续用其约束继续传播。
        setMark(row, col, Mark::S);
        if (isNumber(change.next) && !queued[change.cell]) {
            queued[change.cell] = 1;
            pending.push_back(change.cell);
        }
        forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
            if (board.board[neighborRow][neighborCol] == ObservedBoard::CellState::Hidden && result.marks[neighborRow][neighborCol] == Mark::T)
                setMark(neighborRow, neighborCol, Mark::H);
        });
    }

    // 有候选且能全部确定时才遍历邻格；本次处理会耗尽该数字的候选，无需再处理自身。
    for (int head = 0; head < (int)pending.size(); ++head) {
        const std::pair<int, int> position = board.pos(pending[head]);
        const int row = position.first;
        const int col = position.second;
        const int candidateCount = result.hideAround[row][col];
        const int remaining = numberValue(board.board[row][col]) - result.mineAround[row][col];
        // 两个边界分别表示所有候选格安全、所有候选格为雷；中间情况不作推断。
        if (candidateCount != 0 && (remaining == 0 || remaining == candidateCount)) {
            const Mark mark = remaining == 0 ? Mark::S : Mark::F;
            forEachAdjacent(row, col, rows, cols, [&](int neighborRow, int neighborCol) {
                if (board.board[neighborRow][neighborCol] != ObservedBoard::CellState::Hidden || !isCandidate(result.marks[neighborRow][neighborCol]))
                    return;
                setMark(neighborRow, neighborCol, mark);
            });
        }
        queued[pending[head]] = 0;
    }
    return delta;
}

void Basic::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    const std::span<const Delta::Change> changes = delta.changes.span(pool.changes);
    // 同一格可能分多次记录；正向依序重放，逆向倒序撤回。
    for (int i = 0; i < (int)changes.size(); ++i) {
        const Delta::Change &change = changes[reverse ? changes.size() - 1 - i : i];
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
    const int rows = board.board.rows();
    const int cols = board.board.cols();
    int mineSum = 0;
    int unknownSum = 0;
    int candidateSum = 0;

    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const Mark mark = result.marks[row][col];
            const ObservedBoard::CellState state = board.board[row][col];
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
                if (isNumber(board.board[neighborRow][neighborCol]))
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
    // 总雷数必须落在 mineSum 到 mineSum + candidateSum 之间：已推导安全的隐藏格不可能是雷。
    if (mineSum > board.totalMines || mineSum + candidateSum < board.totalMines)
        return false;
    return true;
}

// 约定 CellState 中 Num0~Num8 的底层值连续且排在其它状态之前，故可直接比较底层值并把该值当数字使用。
bool Basic::isNumber(ObservedBoard::CellState state) { return (int)state <= (int)ObservedBoard::CellState::Num8; }

int Basic::numberValue(ObservedBoard::CellState state) { return (int)state; }

bool Basic::isCandidate(Mark mark) { return mark == Mark::H || mark == Mark::T; }

} // namespace mss
