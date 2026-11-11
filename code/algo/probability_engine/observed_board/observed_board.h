#pragma once

#include <cstdint>
#include <utility>

#include "core/datastructure/grid.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct ObservedBoard {
    using CellId = int;

    // 数字格的编码与揭示数字一致。
    enum class CellState : std::uint8_t {
        Num0 = 0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Hidden,
        ForcedMine,
        ForcedSafe,
    };

    struct Result {
        int totalMines;
        Grid<CellState> cells;

        Result(int rows, int cols, int totalMines) : totalMines(totalMines), cells(rows, cols, CellState::Hidden) {}

        CellId id(int row, int col) const { return row * cells.cols() + col; }
        std::pair<int, int> pos(CellId cell) const { return {cell / cells.cols(), cell % cells.cols()}; }
    };

    struct Delta {
        struct Change {
            CellId cell;
            CellState previous;
            CellState next;
        };

        // 变更记录切自 ObservedBoard::Pool::changes，清池或截断对应段后失效。
        vectorPool<Change>::vector changes;
    };

    struct Pool {
        vectorPool<Delta::Change> changes;

        void clear();
    };

    void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);

    bool isNumber(CellState state) const { return (int)state <= (int)CellState::Num8; }
    int numberValue(CellState state) const { return (int)state; }
};

} // 命名空间 mss
