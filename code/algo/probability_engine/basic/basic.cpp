#include "algo/probability_engine/basic/basic.h"

#include <algorithm>
#include <bit>
#include <cstddef>

#include "core/algorithm/neighbors.h"
#include "core/assert.h"

namespace mss {

struct Basic::Workspace {
    vector<ObservedBoard::CellId> pending;
    vector<std::uint8_t> queued;
};

__declspec(noinline) Basic::Workspace &Basic::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void Basic::growQueued(vector<std::uint8_t> &queued, std::size_t cellCount) {
    const std::size_t previousSize = queued.size;
    if (cellCount <= previousSize)
        return;
    queued.resize(cellCount);
    std::fill_n(queued.data + previousSize, cellCount - previousSize, 0);
}

ObservedBoard::CellId Basic::Result::firstT() const {
    if (unknownSum == 0)
        return -1;
    int word = 0;
    while (tWords.data[word] == 0)
        ++word;
    return word * 64 + std::countr_zero(tWords.data[word]);
}

void Basic::Pool::clear() {
    changes.data.resize(0);
}

bool Basic::isCandidate(Mark mark) { return mark == Mark::H || mark == Mark::T; }

void Basic::enqueue(const ObservedBoard::Result &board, int row, int col, vector<ObservedBoard::CellId> &pending, vector<std::uint8_t> &queued) {
    if (!ObservedBoard().isNumber(board.cells[row][col]))
        return;
    const ObservedBoard::CellId cell = board.id(row, col);
    if (!queued.data[cell]) {
        queued.data[cell] = 1;
        pending.push_back(cell);
    }
}

template <typename Callback> Basic::Delta::Change Basic::changeMark(Result &result, int row, int col, Mark mark, bool safe, Callback &&affected) {
    const ObservedBoard::CellId cell = row * result.marks.cols() + col;
    const std::uint64_t bit = std::uint64_t{1} << (cell % 64);
    const Mark previous = result.marks[row][col];
    const bool previousSafe = (result.safeWords[cell / 64] & bit) != 0;
    const Delta::Change change{cell, previous, mark, previousSafe, safe};
    if (previousSafe != safe)
        result.safeWords[cell / 64] ^= bit;
    if (previous == mark)
        return change;

    result.marks[row][col] = mark;
    if ((previous == Mark::T) != (mark == Mark::T)) {
        result.tWords[cell / 64] ^= bit;
        result.unknownSum += mark == Mark::T ? 1 : -1;
    }

    const int mineDelta = (mark == Mark::F) - (previous == Mark::F);
    const int hideDelta = isCandidate(mark) - isCandidate(previous);
    if (mineDelta == 0 && hideDelta == 0)
        return change;
    if (mineDelta != 0)
        result.mineSum += mineDelta;
    forEachAdjacent(row, col, result.marks.rows(), result.marks.cols(), [&](int r, int c) {
        if (mineDelta != 0)
            result.mineAround[r][c] += mineDelta;
        if (hideDelta != 0)
            result.hideAround[r][c] += hideDelta;
        affected(r, c);
    });
    return change;
}

template <typename SetMark> void Basic::propagate(const ObservedBoard::Result &board, const Result &result, vector<ObservedBoard::CellId> &pending, vector<std::uint8_t> &queued, SetMark &&setMark) {
    for (std::size_t i = 0; i < pending.size; ++i) {
        const ObservedBoard::CellId cell = pending.data[i];
        const auto [row, col] = board.pos(cell);
        const int candidates = result.hideAround[row][col];
        if (candidates != 0) {
            const int remaining = ObservedBoard().numberValue(board.cells[row][col]) - result.mineAround[row][col];
            if (remaining == 0 || remaining == candidates) {
                const Mark mark = remaining == 0 ? Mark::S : Mark::F;
                forEachAdjacent(row, col, board.cells.rows(), board.cells.cols(), [&](int r, int c) {
                    if (isCandidate(result.marks[r][c]))
                        setMark(r, c, mark);
                });
            }
        }
        // 当前数字处理完再出队，避免它被自己的推导重新排队。
        queued.data[cell] = 0;
    }
    pending.resize(0);
}

void Basic::analyze(Result &result, const ObservedBoard::Result &board) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    const std::size_t cellCount = (std::size_t) rows * cols;
    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::CellId> &pending = workspace.pending;
    vector<std::uint8_t> &queued = workspace.queued;
    growQueued(queued, cellCount);

    result.marks.resize(rows, cols, Mark::S);
    result.mineAround.resize(rows, cols, 0);
    result.hideAround.resize(rows, cols, 0);
    result.tWords.resize((cellCount + 63) / 64);
    std::fill_n(result.tWords.data, result.tWords.size, 0);
    result.safeWords.resize((cellCount + 63) / 64);
    std::fill_n(result.safeWords.data, result.safeWords.size, 0);
    result.mineSum = 0;
    result.unknownSum = 0;

    using CellState = ObservedBoard::CellState;
    auto noEnqueue = [](int, int) {};
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const CellState state = board.cells[row][col];
            if (state == CellState::ForcedMine)
                changeMark(result, row, col, Mark::F, false, noEnqueue);
            else if (ObservedBoard().isNumber(state)) {
                enqueue(board, row, col, pending, queued);
                forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
                    if (board.cells[r][c] == CellState::Hidden)
                        changeMark(result, r, c, Mark::H, false, noEnqueue);
                });
            } else if (state == CellState::ForcedSafe)
                changeMark(result, row, col, Mark::S, true, noEnqueue);
            else if (state != CellState::Hidden)
                assert_(false, "整盘分析遇到未知的观测格状态");
        }

    // 前沿格已标为 H，剩下的隐藏格直接进入 T 位图。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (result.marks[row][col] == Mark::S && board.cells[row][col] == CellState::Hidden)
                changeMark(result, row, col, Mark::T, false, noEnqueue);

    propagate(board, result, pending, queued, [&](int row, int col, Mark mark) {
        changeMark(result, row, col, mark, mark == Mark::S, [&](int r, int c) { enqueue(board, r, c, pending, queued); });
    });
}

Basic::Delta Basic::update(Result &result, const ObservedBoard::Result &board, const ObservedBoard::Delta &updates, Pool &pool) {
    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::CellId> &pending = workspace.pending;
    vector<std::uint8_t> &queued = workspace.queued;
    growQueued(queued, (std::size_t) board.cells.rows() * board.cells.cols());

    Delta delta{pool.changes.push_back()};
    auto enqueueAffected = [&](int row, int col) { enqueue(board, row, col, pending, queued); };
    auto setMark = [&](int row, int col, Mark mark) {
        const ObservedBoard::CellState state = board.cells[row][col];
        const bool safe = mark == Mark::S && (state == ObservedBoard::CellState::Hidden || state == ObservedBoard::CellState::ForcedSafe);
        const Delta::Change change = changeMark(result, row, col, mark, safe, enqueueAffected);
        if (change.previous != change.next || change.previousSafe != change.nextSafe)
            pool.changes.push_back(delta.changes, change);
    };

    using CellState = ObservedBoard::CellState;
    for (const ObservedBoard::Delta::Change &change : pool.board.changes.span(updates.changes)) {
        const auto [row, col] = board.pos(change.cell);
        setMark(row, col, change.next == CellState::ForcedMine ? Mark::F : Mark::S);
        if (!ObservedBoard().isNumber(change.next))
            continue;
        enqueueAffected(row, col);
        // 同一批里，邻格的观测状态可能已经变了，Basic 还没更新它的标记。
        forEachAdjacent(row, col, board.cells.rows(), board.cells.cols(), [&](int r, int c) {
            if (result.marks[r][c] == Mark::T && board.cells[r][c] == CellState::Hidden)
                setMark(r, c, Mark::H);
        });
    }

    propagate(board, result, pending, queued, setMark);
    return delta;
}

void Basic::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    const int cols = result.marks.cols();
    auto noEnqueue = [](int, int) {};
    if (reverse) {
        for (const Delta::Change &change : pool.changes.reverseSpan(delta.changes))
            changeMark(result, change.cell / cols, change.cell % cols, change.previous, change.previousSafe, noEnqueue);
    } else
        for (const Delta::Change &change : pool.changes.span(delta.changes))
            changeMark(result, change.cell / cols, change.cell % cols, change.next, change.nextSafe, noEnqueue);
}

bool Basic::checkValid(const ObservedBoard::Result &board, const Result &result) {
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    int mineSum = 0;
    int unknownSum = 0;
    int candidateSum = 0;

    using CellState = ObservedBoard::CellState;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const Mark mark = result.marks[row][col];
            const CellState state = board.cells[row][col];
            const bool number = ObservedBoard().isNumber(state);
            const ObservedBoard::CellId cell = board.id(row, col);
            const bool t = mark == Mark::T;
            if (t != ((result.tWords.data[cell / 64] >> (cell % 64)) & 1))
                return false;
            const bool safe = mark == Mark::S && (state == CellState::Hidden || state == CellState::ForcedSafe);
            if (safe != ((result.safeWords[cell / 64] >> (cell % 64)) & 1))
                return false;
            if (state == CellState::ForcedMine && mark != Mark::F)
                return false;
            if ((state == CellState::ForcedSafe || number) && mark != Mark::S)
                return false;
            mineSum += mark == Mark::F;
            unknownSum += t;
            candidateSum += isCandidate(mark);

            int mines = 0;
            int candidates = 0;
            bool adjacentNumber = false;
            forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
                mines += result.marks[r][c] == Mark::F;
                candidates += isCandidate(result.marks[r][c]);
                adjacentNumber |= ObservedBoard().isNumber(board.cells[r][c]);
            });
            if (state == CellState::Hidden && isCandidate(mark) && t == adjacentNumber)
                return false;
            if (mines != result.mineAround[row][col] || candidates != result.hideAround[row][col])
                return false;
            if (number) {
                const int remaining = ObservedBoard().numberValue(state) - mines;
                if (remaining < 0 || remaining > candidates)
                    return false;
            }
        }

    return mineSum == result.mineSum && unknownSum == result.unknownSum && mineSum <= board.totalMines && mineSum + candidateSum >= board.totalMines;
}

} // 命名空间 mss
