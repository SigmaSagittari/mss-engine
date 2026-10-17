#pragma once

#include <functional>
#include <vector>

#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/utility/grid.h"

namespace mss {
namespace ShapeSolver {

struct DfsSolver {
    using AssignmentCallback = std::function<void(const std::vector<char> &, long double)>;

    // 汇总合法 Box 雷数赋值，结果数据写入 distPool，返回布局权重和 Box 条件期望雷数的下标。
    static DistributionResult analyze(const Structure::Shape &shape, const Structure::Pool &shapes, DistributionPool &distPool, Workspace &workspace);
    // adjacentBoxCells 长度必须等于 shape.boxes.size()，其第 b 项是第 b 个 Box 中与点击格相邻的格数。
    // xBox 为点击格所属 Box 的 ID，或在该组件外时为 -1；非负时必须小于 shape.boxes.size()。
    // 覆写 result 为按邻居雷数和组件雷数联合统计的中间权重，供外层 DP 使用。
    static void observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace);
    // callback 必须非空且不会被保存；assignment 引用工作区复用的缓冲，只在回调期间有效，同一工作区不可并发或在回调期间重入；ways 是该赋值对应的具体布局权重。
    static void forEachAssignment(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace, const AssignmentCallback &callback);

  private:
    // 仅供本类在 dfs_solver.cpp 内实例化；枚举循环的定义不暴露在头文件。
    template <typename Callback> static void enumerateAssignments(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace, Callback &&callback);
};

}
}
