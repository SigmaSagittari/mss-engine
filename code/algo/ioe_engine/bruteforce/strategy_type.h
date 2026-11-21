#pragma once

#include "algo/ioe_engine/ioe_type.h"

namespace mss {

struct IoeStrategyBoard {
    IoeCells mines;
    IoeCells zeros;
    std::uint8_t digits[IoeBoard::cells];
};

} // 命名空间 mss
