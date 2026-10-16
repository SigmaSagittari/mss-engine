#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mss {

struct Combination {
    struct Workspace {
        // -1 表示尚未展开任何一行，首次 chooseLarge 从第 0 行建立缓存。
        Workspace() : max_n(-1) {}

      private:
        friend struct Combination;

        int max_n;
        std::vector<long double> values;
    };

    // 前置条件：0 <= k <= n <= 9；常量表只属于此查询。
    static long double chooseSmall(int n, int k) {
        static constexpr long double values[10][10] = {
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

    // 前置条件：0 <= k <= n；同一 Workspace 串行复用逐行展开的 Pascal 三角缓存。
    static long double chooseLarge(int n, int k, Workspace &workspace) {
        if (workspace.max_n < n) {
            const int first = (std::max)(workspace.max_n + 1, 0);
            const std::size_t valueSize = (std::size_t)(n + 1) * (n + 2) / 2;
            if (valueSize > workspace.values.capacity())
                workspace.values.reserve((std::max)(valueSize, workspace.values.capacity() * 2));
            workspace.values.resize(valueSize);
            for (int row = first; row <= n; ++row) {
                const std::size_t offset = (std::size_t)(row) * (row + 1) / 2;
                workspace.values[offset] = 1.0L;
                workspace.values[offset + row] = 1.0L;
                for (int column = 1; column < row; ++column)
                    workspace.values[offset + column] = workspace.values[offset - row + column - 1] + workspace.values[offset - row + column];
            }
            workspace.max_n = n;
        }
        return workspace.values[(std::size_t)(n) * (n + 1) / 2 + k];
    }
};

}
