#pragma once

#include <algorithm>
#include <cstddef>

#include "core/datastructure/vector.h"

namespace mss {

// 二项式系数：小规模查表，大规模复用逐行展开的 Pascal 三角缓存。
struct Combination {
    struct Workspace {
        // -1 表示尚未展开任何一行。
        Workspace() : max_n(-1) {}

      private:
        friend struct Combination;

        int max_n;
        vector<long double> values;
    };

    // 前置条件：0 <= k <= n <= 9。
    static long double chooseSmall(int n, int k) {
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

    // 前置条件：0 <= k <= n；同一 Workspace 串行复用缓存，新展开的行全部写入。
    static long double chooseLarge(int n, int k, Workspace &workspace) {
        if (workspace.max_n < n) {
            const int first = (std::max)(workspace.max_n + 1, 0);
            const std::size_t valueSize = (std::size_t)(n + 1) * (n + 2) / 2;
            workspace.values.resize(valueSize);
            for (int row = first; row <= n; ++row) {
                const std::size_t offset = (std::size_t)row * (row + 1) / 2;
                workspace.values.data[offset] = 1.0L;
                workspace.values.data[offset + row] = 1.0L;
                for (int column = 1; column < row; ++column)
                    workspace.values.data[offset + column] = workspace.values.data[offset - row + column - 1] + workspace.values.data[offset - row + column];
            }
            workspace.max_n = n;
        }
        return workspace.values.data[(std::size_t)n * (n + 1) / 2 + k];
    }
};

} // 命名空间 mss
