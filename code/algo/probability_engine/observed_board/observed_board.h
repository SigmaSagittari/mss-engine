#pragma once

#include <cstdint>
#include <utility>

#include "core/datastructure/grid.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 观测盘面：行优先 CellId，尺寸创建后固定；旗标属于 Game，不在盘面状态里。
struct ObservedBoard {
    using CellId = int;

    // Num0..Num8 的底层值必须连续为 0..8，Basic 依赖此编码。
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

        // rows/cols 必须为正，mines 必须在 [0, rows*cols) 内；全部格子初始化为 Hidden。
        Result(int rows, int cols, int mines) : totalMines(mines), cells(rows, cols, CellState::Hidden) {}

        // row/col 必须在盘面内；CellId 按 row * cells.cols() + col 编号。
        CellId id(int row, int col) const { return row * cells.cols() + col; }
        // cell 必须是本盘面 id() 生成的有效编号；结果是 id() 的逆映射。
        std::pair<int, int> pos(CellId cell) const { return {cell / cells.cols(), cell % cells.cols()}; }
    };

    struct Delta {
        // 单格变化：previous 必须等于应用前状态；正向只允许 Hidden→数字/ForcedMine/ForcedSafe 与 ForcedSafe→数字。
        struct Change {
            CellId cell;
            CellState previous;
            CellState next;
        };

        // 按应用顺序记录；同一格可多次出现，撤回时逆序。切片只在 Pool 存活期间有效。
        vectorPool<Change>::vector changes;
    };

    // Delta 记录池；池扩容后切片仍然有效，clear 或退尾使对应切片失效。
    struct Pool {
        vectorPool<Delta::Change> changes;
    };

    // delta 必须来自 pool；pool 在应用期间不得 clear 或退尾。
    // 正向按记录顺序应用；reverse 按逆序撤回，且 result 必须处于正向应用后的状态。
    static void apply(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);
};

} // 命名空间 mss
