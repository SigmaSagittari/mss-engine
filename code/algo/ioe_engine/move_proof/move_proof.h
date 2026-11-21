#pragma once

#include "algo/ioe_engine/ioe_type.h"

namespace mss {

struct IoeMoveProof {
    struct Facts {
        IoeCells mines; // 已确定为雷的格，可以只提供其中一部分。
        IoeCells safe; // 已确定安全的格，可以只提供其中一部分。
        IoeCells possibleZeros; // 覆盖全部可能的零格，允许保守扩大。
    };

    // 集合和 digits 均按 9×9 盘面的行优先格号索引；digits 提供 81 个槽，只读取已揭格的数字。
    // 证明首招可用于最优策略时返回该招，否则返回空招。
    IoeMove prove(const IoeView &view, const std::uint8_t *digits, const Facts &facts);
};

} // 命名空间 mss
