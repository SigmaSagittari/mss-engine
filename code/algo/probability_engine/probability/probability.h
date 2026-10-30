#pragma once

#include <functional>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/algorithm/combination.h"
#include "core/algorithm/poly.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct Probability {
    struct Pool;
    struct Result {
        struct Component {
            // 本组件在 boxProbabilities 里的起点和 Box 数。
            int boxOffset;
            int boxCount;
        };

        // 切片依赖调用方传入的 Pool；扩容后仍有效，池清空或销毁后失效。
        // 组件下标与 Structure::Result::components 对应。
        vectorPool<Component>::vector components;
        // 按 Component::boxOffset 连续存放的底层概率，已归一化但未截断，不能依赖 == 1 判断必雷。
        // 常规查询使用 mineProbability()/frontierCells()；直接读取时调用方须遵守未截断的数值语义。
        vectorPool<long double>::vector boxProbabilities;
        // 满足全盘总雷数约束的加权布局数；为零表示没有合法布局，此时不得查询概率。
        long double candidates;
        // 任意一个 T 格的条件雷概率。
        long double tCellProbability;

        // 危险：PositionState、Reveal 和 Workspace 延迟分析并复用结果槽，避免提前计算概率。
        // 首次由 analyze 完整赋值前，所有字段都不得读取或复制。
        // 按 Box 顺序回调每个前沿格的 CellId 和钳制后的雷概率；不遍历 T 格。
        // probabilities 为结果所属池，structure 和 probabilities.shapes 必须与生成此结果时一致。
        template <typename Callback> void frontierCells(const Pool &probabilities, const Structure::Result &structure, Callback &&callback) const;
    };

    // 持有概率结果数组，引用共享的结构池和分布缓存；依赖池必须覆盖本池的使用期。
    struct Pool {
        const Structure::Pool &shapes;
        ShapeSolver::DistributionPool &distributions;
        vectorPool<Result::Component> components;
        vectorPool<long double> boxProbabilities;

        Pool(const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions) : shapes(shapes), distributions(distributions) {}
        // 只清空本层概率结果，保留引用的共享池。
        void clear();
    };

    // analyzeLogicComponents 的输入：一块的前沿多项式，以及该块非前沿 T 格数。
    struct LogicComponentInput {
        // frontier 的系数数组必须活到 analyzeLogicComponents 调用结束。
        // 系数下标 t 对应 frontier.start + t 个前沿雷；无前沿块用常数多项式 1。
        Poly frontier;
        int offFrontierCount;
    };

    // 临时空间：树形组合下层的 shapeSolver 和 combination；可跨调用复用，不可并发共享。
    struct Workspace {
        ShapeSolver::Workspace shapeSolver;
        Combination::Workspace combination;
        // 本次 analyze 的组件分布 ID。
        vector<ShapeSolver::DistributionId> distributionIds;

        // 多项式乘积树的临时存储：系数池、节点和 outside 缓冲。
        PolyProductTree::Workspace frontierProducts;
        // 各组件按雷数分布的多项式，系数借用 distributions.values。
        vector<Poly> frontiers;
        // 乘积树先回调、后返回全盘分母；暂存各档分子以便统一归一化。
        struct LogicValue {
            int component;
            int mines;
            long double localWays;
            long double totalWays;
        };
        vector<LogicValue> logicValues;

        struct Sample {
            // 组件后缀多项式的系数池；suffix[i] 是第 i 个后缀，suffixStart 是它的起点。
            vectorPool<long double> values;
            vector<vectorPool<long double>::vector> suffix;
            vector<int> suffixStart;
            vector<ObservedBoard::CellId> freeCells;
        } sample;
    };

    // board/basic/structure 必须来自同一刷新；results 追加本次结果，返回值借用该池。
    // results 引用的结构池与分布缓存须匹配；workspace 可跨调用复用，results 扩容不影响已有切片。
    // 无合法全盘布局时 candidates 为零，返回池尾空切片，此时不得查询格子概率。
    static Result analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const ShapeSolver::Config &config, Pool &results, Workspace &workspace);

    // 输入必须来自同一当前盘面且至少存在一种合法全盘布局；assignments 与 assignments.shapes 同期清空。
    // 按具体全盘雷位近似均匀抽样；函数会 resize 并清零 result，1 表示雷。
    static void sampleMineBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, ShapeSolver::AssignmentPool &assignments, U128 seed, Grid<char> &result, Workspace &workspace);

    // components 必须覆盖本次统计的所有逻辑块，且各块 T 格互不重叠；frontier 系数至少活到调用结束。
    // totalMines 是扣除已知雷后仍需分配到前沿格和 T 格的剩余雷数。
    // 对每个全局概率非零的组件雷数档 k 回调 (组件下标, 块总雷数, 局部布局数 W_i(k), 该档概率 P_i(k))。
    // 返回满足全盘剩余雷数的加权布局总数 Z；Z 为零表示不存在合法全盘布局。
    static long double analyzeLogicComponents(span<const LogicComponentInput> components, int totalMines, Workspace &workspace, const std::function<void(int, int, long double, long double)> &callback);

    // 格子 → 组件/Box/T 的概率查询；result 的切片来自 pool，candidates 必须大于零，输入必须与生成结果时一致。
    // 逻辑推导的必安、必雷分别精确返回 0、1，调用方直接判等。
    static long double mineProbability(const Result &result, const Pool &pool, ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure);

  private:
    // 前沿多项式乘自由格组合数：freeCells 个自由格里恰好放 freeMines 个雷。
    static long double countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Workspace &workspace);

    // 归一化舍入可能让本应为 1 的概率略低于 1；沿用 1.4 的阈值截断，实战保证不会截出问题，此函数确保逻辑解必定为 0/1。
    static long double limitProbability(long double probability);
};

template <typename Callback>
void Probability::Result::frontierCells(const Pool &probabilities, const Structure::Result &structure, Callback &&callback) const {
    const Structure::Pool &pool = probabilities.shapes;
    const Component *layouts = probabilities.components.data.data + components.offset;
    const long double *values = probabilities.boxProbabilities.data.data + boxProbabilities.offset;
    for (int component = 0; component < (int)components.count; ++component) {
        const Component &layout = layouts[component];
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const int *offsets = pool.instances.boxOf.data.data + instance.boxOf.offset;
        const Structure::CellId *cells = pool.instances.cells.data.data + instance.cells.offset;
        for (int box = 0; box < layout.boxCount; ++box) {
            const long double probability = Probability::limitProbability(values[layout.boxOffset + box]);
            for (int i = offsets[box]; i < offsets[box + 1]; ++i)
                callback(cells[i], probability);
        }
    }
}

} // 命名空间 mss
