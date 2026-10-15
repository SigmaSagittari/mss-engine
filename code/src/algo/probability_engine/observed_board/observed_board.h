#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "core/utility/grid.h"

namespace mss {

struct ObservedBoard {
    using CellId = int;

    // Num0~Num8 的底层值必须连续为 0~8；Basic 依赖此编码识别数字并取得数字值。
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
        // Forced 状态表示尚未翻开的格子已被外部确定为雷或安全。
        ForcedMine,
        ForcedSafe,
    };

    // 生成 CellId 或 Delta 后不得改变盘面尺寸，否则旧编号会映射到不同格子。
    struct Result {
        // 全盘雷数，包含已标为 ForcedMine 的格子。
        int totalMines;
        // 按行连续存储的 0-based 盘面。
        RawGrid<CellState> board;

        // 调用者须保证 rows、cols、mines 描述合法盘面；本构造不校验，所有格子初始化为 Hidden。
        Result(int rows, int cols, int mines);
        // row/col 必须在盘面内；CellId 按 row * board.cols() + col 编号。
        CellId id(int row, int col) const;
        // cell 必须是本盘面 id() 生成的有效编号；结果是 id() 的逆映射。
        std::pair<int, int> pos(CellId cell) const;
    };

    struct Delta {
        struct Change {
            // 用实际旧状态记录变化，撤回 ForcedSafe 的揭示时不能恢复成 Hidden。
            CellId cell;
            CellState previous;
            CellState next;

            Change(CellId cell, CellState previous, CellState next) : cell(cell), previous(previous), next(next) {}
        };

        // CellId 必须属于同一盘面且不得重复；previous 必须等于应用前的状态。
        // 允许 Hidden 转为数字或 Forced 状态，以及 ForcedSafe 转为数字。
        std::vector<Change> changes;
        // 清空变化但保留容量，供后续复用。
        void clear();
    };

    // 参数必须描述合法盘面；只记录尺寸和总雷数并创建全 Hidden 盘面，不检查输入或推断数字、雷位。
    static Result analyze(int rows, int cols, int mines);
    // 正向时 board 对应更新前状态，满足 Delta 转换契约；reverse 时对应更新后状态且必须后进先出。切片只在调用期间借用。
    static void applyDelta(Result &board, std::span<const Delta::Change> changes, bool reverse = false);
};

} // namespace mss
