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
    // 危险：默认创建的 Result 各字段均未初始化，用于预留尚未计算的概率结果。
    // 读取或复制前，须先赋入 Probability::analyze 的返回值，或另一个已初始化的 Result。
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

        // 对每个前沿格调用 callback(CellId, 雷概率)，按组件、Box 顺序遍历；跳过非前沿格。
        // probabilities 必须是保存本 Result 数据的概率池，structure 必须是计算本 Result 时使用的结构结果。
        // 要求 candidates > 0；雷概率 >= 1 - 1e-10 时按 1 返回。
        template <typename Callback> void frontierCells(const Pool &probabilities, const Structure::Result &structure, Callback &&callback) const;
        // probabilities 必须是保存本 Result 数据的概率池，structure 必须是计算本 Result 时使用的结构结果。
        // 要求 candidates > 0、count >= 0；覆写 output，按升序保存最多 count 个前沿格雷概率并复用容量。
        // 同一 Box 的概率按格数重复；雷概率 >= 1 - 1e-10 时按 1 返回。
        void lowestProbabilities(const Pool &probabilities, const Structure::Result &structure, int count, vector<long double> &output) const;
    };

    // 持有概率结果数组，引用共享的结构池、分布缓存和组合数缓存；依赖池必须覆盖本池的使用期。
    struct Pool {
        const Structure::Pool &shapes;
        ShapeSolver::DistributionPool &distributions;
        Combination::Pool &combinations;
        vectorPool<Result::Component> components;
        vectorPool<long double> boxProbabilities;

        Pool(const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Combination::Pool &combinations) : shapes(shapes), distributions(distributions), combinations(combinations) {}
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

    // board/basic/structure 必须来自同一刷新；results 追加本次结果，返回值借用该池。
    // results 引用的结构池与分布缓存须匹配，扩容不影响已有切片。
    // 无合法全盘布局时 candidates 为零，返回池尾空切片，此时不得查询格子概率。
    Result analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const ShapeSolver::Config &config, Pool &results);

    // 输入必须来自同一当前盘面且至少存在一种合法全盘布局；assignments 与 assignments.shapes 同期清空。
    // 按具体全盘雷位近似均匀抽样；函数会 resize 并清零 result，1 表示雷。
    void sampleMineBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, ShapeSolver::AssignmentPool &assignments, Combination::Pool &combinations, U128 seed, Grid<char> &result);

    // components 必须覆盖本次统计的所有逻辑块，且各块 T 格互不重叠；frontier 系数至少活到调用结束。
    // totalMines 是扣除已知雷后仍需分配到前沿格和 T 格的剩余雷数。
    // 对每个全局概率非零的组件雷数档 k 回调 (组件下标, 块总雷数, 局部布局数 W_i(k), 该档概率 P_i(k))。
    // 返回满足全盘剩余雷数的加权布局总数 Z；Z 为零表示不存在合法全盘布局。
    long double analyzeLogicComponents(span<const LogicComponentInput> components, int totalMines, Combination::Pool &combinations, const std::function<void(int, int, long double, long double)> &callback);

    // 格子 → 组件/Box/T 的概率查询；result 的切片来自 pool，candidates 必须大于零，输入必须与生成结果时一致。
    // 逻辑推导的必安、必雷分别精确返回 0、1，调用方直接判等。
    long double mineProbability(const Result &result, const Pool &pool, ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure);

  private:
    struct Workspace;
    friend struct PolyProductTree;
    Workspace &getWorkspace();
    const long double *assignmentWays(const ShapeSolver::AssignmentPool &pool, const ShapeSolver::Assignments &assignments);
    // 前沿多项式乘自由格组合数：freeCells 个自由格里恰好放 freeMines 个雷。
    long double countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Pool &combinations);

    // 归一化舍入可能让本应为 1 的概率略低于 1；沿用 1.4 的阈值截断，实战保证不会截出问题，此函数确保逻辑解必定为 0/1。
    long double limitProbability(long double probability);
    long double pickFreeWays(int freeCells, int freeMines, Combination::Pool &combinations);
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
            const long double probability = Probability().limitProbability(values[layout.boxOffset + box]);
            for (int i = offsets[box]; i < offsets[box + 1]; ++i)
                callback(cells[i], probability);
        }
    }
}

} // 命名空间 mss
