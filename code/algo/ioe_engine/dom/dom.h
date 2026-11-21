#pragma once

#include "algo/ioe_engine/ioe_type.h"
#include "core/datastructure/grid.h"

namespace mss {

struct IoeDom {
    // 预算不足时返回此值。其他结果为实际最少剩余点击，包含预算等号。
    static constexpr int overBudget = -1;

    struct Result {
        int clicks;
        bool searched; // 只有进入精确计算时，逐盘筛选才增加 DOM 调用次数。
    };

    // 9×9 雷图。revealed 是已揭格，flags 是玩家实插旗。
    Result minimum(const IoeCells &mines, const IoeView &view, int budget);
    // 标准初、中、高级的完整未揭雷图，每格 0 或 1。
    int minimum(const Grid<char> &mines, int budget);

  private:
    template <int Rows, int Cols> struct Engine;
};

} // 命名空间 mss
