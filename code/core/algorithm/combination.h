#pragma once

#include <algorithm>
#include <cstddef>

#include "core/datastructure/vector.h"

namespace mss {

// 二项式系数：小规模查表，大规模复用逐行展开的 Pascal 三角缓存。
struct Combination {
    // 各行沿用生成时的浮点精度；跨线程接续计算时复用已缓存系数。
    struct Pool {
        // -1 表示尚未展开任何一行。
        Pool() : max_n(-1) {}

      private:
        friend struct Combination;

        int max_n;
        vector<long double> values;
    };

    // 前置条件：0 <= k <= n <= 9。
    long double chooseSmall(int n, int k) {
        static const long double values[10][10] = {
            {1},
            {1, 1},
            {1, 2, 1},
            {1, 3, 3, 1},
            {1, 4, 6, 4, 1},
            {1, 5, 10, 10, 5, 1},
            {1, 6, 15, 20, 15, 6, 1},
            {1, 7, 21, 35, 35, 21, 7, 1},
            {1, 8, 28, 56, 70, 56, 28, 8, 1},
            {1, 9, 36, 84, 126, 126, 84, 36, 9, 1}
        };
        return values[n][k];
    }

    // 前置条件：0 <= k <= n。
    long double chooseLarge(int n, int k, Pool &pool);
};

} // 命名空间 mss
