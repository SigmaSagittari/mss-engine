#pragma once

#include <cstdint>
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
        int totalMines = 0;
        // 按行连续存储的 0-based 盘面。
        RawGrid<CellState> board;

        // 默认结果是空盘面，不含可用 CellId。
        Result();
        // 调用者须保证 rows、cols、mines 描述合法盘面；本构造不校验，所有格子初始化为 Hidden。
        Result(int rows, int cols, int mines);
        // row/col 必须在盘面内；CellId 按 row * board.cols() + col 编号。
        CellId id(int row, int col) const;
        // cell 必须是本盘面 id() 生成的有效编号；结果是 id() 的逆映射。
        std::pair<int, int> pos(CellId cell) const;
    };

    struct Delta {
        struct Change {
            // 加入 changes 前必须设置为本盘面的有效编号和非 Hidden 状态。
            CellId cell = -1;
            CellState next = CellState::Hidden;
        };

        // 同一 Delta 的 CellId 必须按同一盘面尺寸编码且不得重复；应用前每个目标格必须为 Hidden。
        std::vector<Change> changes;
        // 清空变化但保留容量，供后续复用。
        void clear();
    };

    // 参数必须描述合法盘面；只记录尺寸和总雷数并创建全 Hidden 盘面，不检查输入或推断数字、雷位。
    static Result analyze(int rows, int cols, int mines);
    // 调用者须保证 CellId 按 board 尺寸编码且本批唯一、目标格为 Hidden、next 为数字或 Forced 状态；实现只断言 next 非 Hidden 和目标格为 Hidden。成功后修改 board，不消费 delta。
    static void update(Result &board, Delta &delta);
    // 调用前须保证 board 是该 Delta 更新后的状态；不检查当前值，按 changes 逆序直接恢复为 Hidden，后续 Delta 必须先撤回。
    static void reverseDelta(Result &board, const Delta &delta);
};

} // namespace mss
