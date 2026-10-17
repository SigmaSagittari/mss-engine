#pragma once

#include <vector>

#include "algo/probability_engine/shape_solver/dfs_solver.h"
#include "algo/probability_engine/shape_solver/graph_solver/graph_solver.h"
#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"

namespace mss {
namespace ShapeSolver {

// 该阈值只选择求解后端，不影响结果；沿用 1.4 的 Box 数分界。
inline constexpr int graphThreshold = 35;

// workspace 与 distPool 可跨调用顺序复用，同一实例不得并发共享。
// 小组件走 DFS，大组件走 Graph；algo 只传给 Graph，控制消元顺序。
// 返回的 ID 只在 distPool 未清空时有效。
DistributionId analyze(const Structure::Shape &shape, const Structure::Pool &shapes, DistributionPool &distPool, Workspace &workspace, const OrderAlgo &algo = OrderAlgo::Auto);

// 随机生成专用缓存：始终由 DFS 枚举所有合法 Box 雷数赋值，按 ShapeId 与普通分布分开保存。
// pool 对应同一 shapes 且不可并发共享；返回引用由 pool 持有，下一次插入或清空 pool 后失效。
const AssignmentPool::Result &assignments(Structure::ShapeId shapeId, const Structure::Shape &shape, const Structure::Pool &shapes, AssignmentPool &pool, Workspace &workspace);
// componentMines 必须是 ways 中非零的雷数档；返回的视图在 pool 更新或清空后失效。
std::span<const char> sampleAssignment(const AssignmentPool::Result &result, int componentMines, long double unit);

// adjacentBoxCells 长度必须等于 Box 数，第 b 项是第 b 个 Box 中与点击格相邻的格数。
// xBox 是点击格所属 Box，或在组件外时为 -1；非负时必须小于 Box 数。
// 按组件大小选择 DFS 或 Graph，将组件雷数与邻居雷数的联合权重写入 result。
void observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace);

}
}
