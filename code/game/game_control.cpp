#include "game/game_control.h"

#include <algorithm>
#include <cstdint>

#include "core/algorithm/neighbors.h"
#include "core/algorithm/rng.h"

namespace mss {

GameControl::MineBoard::MineBoard() : board_() {}

GameControl::MineBoard::MineBoard(Grid<char> board) : board_(board) {}

GameControl::MineBoard::MineBoard(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed) : board_() {
    generate(rows, cols, mines, firstRow, firstCol, seed);
}

void GameControl::MineBoard::generate(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed) {
    board_.resize(rows, cols, 0);
    const int cellCount = rows * cols;
    const int firstCell = firstRow * cols + firstCol;
    vector<int> cells;
    cells.resize(cellCount - 1);
    int index = 0;
    for (int cell = 0; cell < cellCount; ++cell)
        if (cell != firstCell)
            cells.data[index++] = cell;

    Random random(seed.lo, seed.hi);
    for (int i = (int)cells.size - 1; i > 0; --i) {
        const int j = (int)(random.next() % (std::uint64_t)(i + 1));
        std::swap(cells.data[i], cells.data[j]);
    }
    for (int i = 0; i < mines; ++i) {
        const int cell = cells.data[i];
        board_[cell / cols][cell % cols] = 1;
    }
}

int GameControl::MineBoard::number(int row, int col) const {
    if (board_[row][col])
        return 9;
    int result = 0;
    forEachAdjacent(row, col, board_.rows(), board_.cols(), [&](int neighborRow, int neighborCol) { result += board_[neighborRow][neighborCol]; });
    return result;
}


} // 命名空间 mss

