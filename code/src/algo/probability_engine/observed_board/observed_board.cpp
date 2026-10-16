#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/assert.h"

namespace mss {

ObservedBoard::Result::Result(int rows, int cols, int mines)
    : totalMines(mines), board(rows, cols, CellState::Hidden) {
}

ObservedBoard::CellId ObservedBoard::Result::id(int row, int col) const { return row * board.cols() + col; }

std::pair<int, int> ObservedBoard::Result::pos(CellId cell) const { return {cell / board.cols(), cell % board.cols()}; }

ObservedBoard::Result ObservedBoard::analyze(int rows, int cols, int mines) { return Result(rows, cols, mines); }

void ObservedBoard::applyDelta(Result &board, const Delta &delta, const Pool &pool, bool reverse) {
    const std::span<const Delta::Change> changes = delta.changes.span(pool.changes);
    for (int i = 0; i < (int)changes.size(); ++i) {
        const Delta::Change &change = changes[reverse ? changes.size() - 1 - i : i];
        const std::pair<int, int> position = board.pos(change.cell);
        if (!reverse) {
            assert_(change.next != CellState::Hidden, "ObservedBoard::applyDelta: 新状态不能是隐藏格");
            assert_(board.board[position.first][position.second] == change.previous, "ObservedBoard::applyDelta: 旧状态不匹配");
        }
        board.board[position.first][position.second] = reverse ? change.previous : change.next;
    }
}

} // namespace mss
