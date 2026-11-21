#pragma once

#include "algo/ioe_engine/ioe_type.h"
#include "algo/probability_engine/basic/basic.h"
#include "core/datastructure/vector.h"

namespace mss {

struct IoeConstraints {
    struct Equation {
        IoeCells unknown;
        std::uint8_t demand;
    };

    struct Result {
        IoeCells fixedMines;
        IoeCells unknown;
        int remainingMines;
        int cellCount;
        int equationCount;
        std::uint8_t digits[IoeBoard::cells]; // 0～8 为已揭数字，9 为尚未揭开。
        // 危险：只填充 cellCount 个格号和 equationCount 条约束，其余槽位留空。
        std::uint8_t cells[IoeBoard::cells];
        Equation equations[IoeBoard::cells];
    };

    // basic 对应 board，且盘面已确认存在至少一种合法雷图。
    Result build(const ObservedBoard::Result &board, const Basic::Result &basic);
    // 按格号列表逐格先安后雷；每次回调接收一张完整雷图。
    void forEach(const Result &constraints, IoeObserver<IoeCells> visit);
    void sort(const Result &constraints, vector<IoeLayout> &layouts);

  private:
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss
