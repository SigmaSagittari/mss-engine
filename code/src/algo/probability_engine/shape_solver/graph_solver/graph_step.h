#pragma once

#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"

namespace mss {
namespace ShapeSolver {

struct GraphStepGenerator {
    struct Graph {
        // Box b 的邻居位于 adjacent[offsets[b]..offsets[b + 1])；两段视图借用 workspace，下一次建图会覆盖。
        std::span<const int> offsets;
        std::span<const Structure::BoxId> adjacent;

        // 每条约束中的 Box 两两相邻；Shape 和 Pool 必须属于同一套结构数据。
        // 返回的邻接图引用 workspace.graph_solver.graph，直到同一工作区下次建图前有效。
        static Graph fromShape(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace);
        // box 必须是该图中的 Box ID；返回的邻居视图与 Graph 使用相同的失效时机。
        std::span<const Structure::BoxId> neighbors(Structure::BoxId box) const;
    };

  private:
    static std::pair<int, int> orderScore(const Graph &graph, const std::vector<Structure::BoxId> &order, Workspace &workspace);
    // source 必须是图中有效的 Box ID。
    static Structure::BoxId farthestBox(const Graph &graph, Structure::BoxId source, Workspace &workspace);
    // Adjacent 与 Window3/SA 共用贪心构造。
    static std::vector<Structure::BoxId> makeGreedyOrder(const Graph &graph, Structure::BoxId first, Workspace &workspace);
    // 逐个优化三个连续 Box 的局部排列。
    static std::vector<Structure::BoxId> makeWindow3Order(const Graph &graph, const std::vector<Structure::BoxId> &order, Workspace &workspace);
    // 从 seed 出发用模拟退火搜索更优排列。
    static std::vector<Structure::BoxId> makeSAOrder(const Graph &graph, const std::vector<Structure::BoxId> &seed, Workspace &workspace);
    // 先用 Adjacent；边界仍宽时再尝试 Window3。
    static std::vector<Structure::BoxId> makeAutoOrder(const Graph &graph, Workspace &workspace);
    // 在 Auto 基础上，边界仍宽时再尝试 SA。
    static std::vector<Structure::BoxId> makeAutoSAOrder(const Graph &graph, Workspace &workspace);

  public:
    // graph 至少含一个 Box；顺序只影响 frontier 状态规模，不改变最终分布，具体策略由 OrderAlgo 选择。
    // 返回的顺序由调用方独立持有，Workspace 的复用不会使其失效。
    static std::vector<Structure::BoxId> makeOrder(const Graph &graph, const OrderAlgo &algo, Workspace &workspace);

    struct StepPlan {
        using Check = Workspace::GraphSolverWorkspace::Step::Check;
        using Closing = Workspace::GraphSolverWorkspace::Step::Closing;

        // 本步骤正在分配雷数的 Box 及其格子数。
        Structure::BoxId box;
        int boxSize;
        // 该 Box 涉及的约束；消费方据此计算本步可分配的雷数范围。
        std::span<const Check> checks;
        // 新 frontier 的来源：非负值复制旧 slot，-1 插入本步的雷数。
        std::span<const int> gather;
        // 本步闭合的 Box；消费方据此累计它们的雷数矩。
        std::span<const Closing> closings;

        StepPlan(Structure::BoxId box, int boxSize, std::span<const Check> checks, std::span<const int> gather, std::span<const Closing> closings) : box(box), boxSize(boxSize), checks(checks), gather(gather), closings(closings) {}
    };

    using Callback = std::function<void(const StepPlan &)>;

    // order 必须是 shape 中所有 Box 的一个排列；callback 必须非空且不得保存 StepPlan 引用。
    // 每个 Box 恰好回调一次；StepPlan 及其视图只在对应回调期间有效，同一工作区不可并发共享或在回调期间重入。
    static void forEach(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<Structure::BoxId> &order, Workspace &workspace, const Callback &callback);
};

}
}
