#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/assert.h"

namespace mss {

ObservedBoard::Result::Result() = default;

ObservedBoard::Result::Result(int rows, int cols, int mines)
    : totalMines(mines), board(rows, cols, CellState::Hidden) {
}

ObservedBoard::CellId ObservedBoard::Result::id(int row, int col) const { return row * board.cols() + col; }

std::pair<int, int> ObservedBoard::Result::pos(CellId cell) const { return {cell / board.cols(), cell % board.cols()}; }

void ObservedBoard::Delta::clear() { changes.clear(); }

ObservedBoard::Result ObservedBoard::analyze(int rows, int cols, int mines) { return Result(rows, cols, mines); }

void ObservedBoard::update(Result &board, Delta &delta) {
    for (Delta::Change &change : delta.changes) {
        assert_(change.next != CellState::Hidden, "ObservedBoard::update: next state must not be Hidden");
        const std::pair<int, int> position = board.pos(change.cell);
        assert_(board.board[position.first][position.second] == CellState::Hidden, "ObservedBoard::update: target must be Hidden");
        board.board[position.first][position.second] = change.next;
    }
}

void ObservedBoard::reverseDelta(Result &board, const Delta &delta) {
    for (int i = (int)delta.changes.size() - 1; i >= 0; --i) {
        const Delta::Change &change = delta.changes[i];
        const std::pair<int, int> position = board.pos(change.cell);
        board.board[position.first][position.second] = CellState::Hidden;
    }
}

} // namespace mss
