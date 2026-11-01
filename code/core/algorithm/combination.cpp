#include "core/algorithm/combination.h"

namespace mss {

long double Combination::chooseLarge(int n, int k, Pool &pool) {
    if (pool.max_n < n) {
        const int first = (std::max)(pool.max_n + 1, 0);
        const std::size_t valueSize = (std::size_t)(n + 1) * (n + 2) / 2;
        pool.values.resize(valueSize);
        for (int row = first; row <= n; ++row) {
            const std::size_t offset = (std::size_t)row * (row + 1) / 2;
            pool.values.data[offset] = 1.0L;
            pool.values.data[offset + row] = 1.0L;
            for (int column = 1; column < row; ++column)
                pool.values.data[offset + column] = pool.values.data[offset - row + column - 1] + pool.values.data[offset - row + column];
        }
        pool.max_n = n;
    }
    return pool.values.data[(std::size_t)n * (n + 1) / 2 + k];
}

} // 命名空间 mss
