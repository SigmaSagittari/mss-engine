#pragma once

#include <algorithm>
#include <array>
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

  private:
    static constexpr int small_max = 9;
    using SmallTable = std::array<std::array<long double, small_max + 1>, small_max + 1>;
    inline static constexpr SmallTable small_table = []() constexpr {
        SmallTable values{};
        for (int n = 0; n <= small_max; ++n) {
            values[n][0] = 1.0L;
            values[n][n] = 1.0L;
            for (int k = 1; k < n; ++k)
                values[n][k] = values[n - 1][k - 1] + values[n - 1][k];
        }
        return values;
    }();

  public:
    // 前置条件：0 <= k <= n <= 9；结果直接读取编译期生成的 Pascal 三角表。
    static constexpr long double chooseSmall(int n, int k) { return small_table[n][k]; }

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
