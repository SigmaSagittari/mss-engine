#pragma once

#include <vector>

#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"

namespace mss {
namespace ShapeSolver {

struct GraphSolver {
    // shape 至少含一个 Box；按 algo 选择消元顺序，返回组件总雷数分布和各 Box 的条件期望雷数。
    static DistributionResult analyze(const Structure::Shape &shape, const Structure::Pool &shapes, const OrderAlgo &algo, Workspace &workspace);

    // shape 至少含一个 Box；adjacentBoxCells[b] 是第 b 个 Box 中与点击格相邻的格数，长度必须等于 Box 数。
    // xBox 是点击格所属 Box，或在组件外时为 -1；非负时必须小于 Box 数。
    // 覆写 result 为按组件雷数和邻居雷数统计的布局权重；沿用 1.4，观测固定使用 Adjacent 顺序。
    static void observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace);
};

}
}
