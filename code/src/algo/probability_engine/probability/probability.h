#pragma once

#include <functional>
#include <span>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/utility/combination.h"
#include "core/utility/grid.h"
#include "core/utility/poly.h"

namespace mss {

struct Probability {
    class Result {
      public:
        struct Component {
            int boxOffset;
            int boxCount;
        };

        // 组件下标与 Structure::Result::components 对应；候选数为零时不得查询概率。
        const std::vector<Component> &components() const;
        // 每个组件的 Box 概率按 Component::boxOffset 连续存放。
        const std::vector<long double> &boxProbabilities() const;
        // component 和 box 必须是当前 Structure::Result 中的有效下标，且 candidates() 必须大于零。
        long double boxProbability(Structure::ComponentId component, Structure::BoxId box) const;
        // board、basic、structure 必须与生成此结果时一致，且 candidates() 必须大于零。
        long double mineProbability(ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure) const;
        // structure、shapes 必须与生成此结果时一致，且 candidates() 必须大于零；按 Box 顺序回调每个前沿格的 CellId 和钳制后雷概率，不遍历 T 格；回调期间不得修改 shapes。
        template <typename Callback>
        void frontierCells(const Structure::Result &structure, const Structure::Pool &shapes, Callback &&callback) const;
        // 满足全盘总雷数约束的加权布局数；为零表示没有合法布局。
        long double candidates() const;
        // 任意一个 T 格的条件雷概率。
        long double tCellProbability() const;

      private:
        friend struct Probability;

        std::vector<Component> components_;
        std::vector<long double> boxProbabilities_;
        long double candidates_ = 0.0L;
        long double tCellProbability_ = 0.0L;
    };

    struct LogicComponentInput {
        Poly frontier;
        int offFrontierCount;
    };

    struct Workspace {
        ShapeSolver::Workspace &shapeSolver;
        Combination::Workspace &combination;
        PolyProductTree frontierProducts;
        std::vector<ShapeSolver::DistributionId> distributionIds;
        std::vector<Poly> frontiers;

        struct Sample {
            std::vector<std::vector<long double>> suffix;
            std::vector<ObservedBoard::CellId> freeCells;
        } sample;

        // 乘积树先执行逐组件回调、后返回全盘分母；暂存各档分子以便之后统一归一化。
        struct LogicComponentValue {
            int component = 0;
            int mines = 0;
            long double localWays = 0.0L;
            long double totalWays = 0.0L;
        };
        std::vector<LogicComponentValue> logicComponentValues;

        struct Observe {
            // 点击格周围涉及的 Structure 组件及其去重标记。
            std::vector<Structure::ComponentId> captured;
            std::vector<char> seen;
            std::vector<int> adjacentBoxCells;
            // 每个捕获组件复用同一份 ShapeSolver 观测结果；下一次 observe 会覆盖它。
            ShapeSolver::ObversedResult observed;
            // dp[邻居雷数][已处理组件与邻居 T 格雷数]；两张表逐个组件交替累积。
            RawGrid<long double> dp;
            RawGrid<long double> nextDp;
            // restWays 按绝对剩余雷数索引；后两项复用未捕获组件的多项式卷积。
            std::vector<long double> restWays;
            std::vector<long double> restCoefficients;
            std::vector<long double> productScratch;
            int restStart = 0;
        } observe;

        // 构造时绑定外层工作区；引用目标必须比本对象活得久，同一工作区不得并发或重入使用。
        Workspace(ShapeSolver::Workspace &shapeSolverWorkspace, Combination::Workspace &combinationWorkspace) : shapeSolver(shapeSolverWorkspace), combination(combinationWorkspace) {}
    };

    // board、basic、structure 必须来自同一刷新；shape 分布池和工作区可跨调用复用。
    // 无合法全盘布局时返回结果的候选数为零，此时不得查询格子概率。
    // 创建并返回全盘概率分析结果。
    static Result analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Workspace &workspace, const ShapeSolver::OrderAlgo &algo = ShapeSolver::OrderAlgo::Auto);

    // 按 Structure 组件顺序重建 Result；结果拥有自己的连续 Box 概率数组，不依赖 Workspace 生命周期。
    static void analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Result &result, Workspace &workspace, const ShapeSolver::OrderAlgo &algo = ShapeSolver::OrderAlgo::Auto);

    // 输入必须来自同一当前盘面且至少存在一种合法全盘布局；assignments 与 shapes 同期清空。
    // 按具体全盘雷位近似均匀抽样；随机组件始终由 DFS 枚举；返回网格中 1 为雷、0 为安全格。
    static RawGrid<char> sampleMineBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, ShapeSolver::AssignmentPool &assignments, Workspace &workspace, U128 seed);

    // components 必须覆盖本次统计的所有逻辑块，且各块的 T 格互不重叠；frontier 系数数组必须至少保留到本次调用结束。
    // components[i].frontier 表示第 i 个逻辑块的前沿多项式；系数下标 t 对应 frontier.start+t 个前沿雷，无前沿块使用常数多项式 1。offFrontierCount 是该块非前沿 T 格数。
    // totalMines 是扣除已知雷后仍需分配到这些前沿格和 T 格的剩余雷数。
    // 对每个全局概率非零的组件雷数档 k 调用 callback，参数依次为组件下标 i、该块总雷数 k、局部布局数 W_i(k)、满足全盘剩余雷数后该档的概率 P_i(k)。
    // 返回值 Z 是满足全盘剩余雷数的加权布局总数，不是概率；不同格子布局分别计数。Z 为 0 表示不存在合法全盘布局。
    // 工作区必须跨调用复用；同一工作区不得并发或重入使用，回调不得重入本函数。
    static long double analyzeLogicComponents(const std::vector<LogicComponentInput> &components, int totalMines, Workspace &workspace, const std::function<void(int, int, long double, long double)> &callback);

  private:
    static long double countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Workspace &workspace);
    static long double limitProbability(long double probability);
};

template <typename Callback>
void Probability::Result::frontierCells(const Structure::Result &structure, const Structure::Pool &shapes, Callback &&callback) const {
    for (int component = 0; component < (int)components_.size(); ++component) {
        const Component &componentResult = components_[component];
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const std::span<const int> offsets = instance.boxes.boxOf.span(shapes.boxOf);
        const std::span<const ObservedBoard::CellId> cells = instance.boxes.cells.span(shapes.cells);
        for (int box = 0; box < componentResult.boxCount; ++box) {
            const long double probability = Probability::limitProbability(boxProbabilities_[componentResult.boxOffset + box]);
            for (int i = offsets[box]; i < offsets[box + 1]; ++i) {
                callback(cells[i], probability);
            }
        }
    }
}

}
