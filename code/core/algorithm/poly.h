#pragma once

#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 多项式视图：coefficients 借用连续数组，调用方保证它比 Poly 活得久。
// 系数下标 i 对应 start + i 次；空数组表示零多项式。
struct Poly {
    int start;
    span<const long double> coefficients;

    const long double *data() const { return coefficients.data; }
    int size() const { return coefficients.size; }
};

} // 命名空间 mss
