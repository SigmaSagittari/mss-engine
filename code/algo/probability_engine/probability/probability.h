#pragma once

#include <cstddef>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/algorithm/combination.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct Poly;

struct Probability {
    struct Pool;
    struct Result {
        struct Block {
            int start; // probabilities 首项对应的块内总雷数，含 T 格。
            // 来自 Pool::blockProbabilities，第 i 项是全盘雷数约束下本块恰有 start + i 颗雷的概率。
            vectorPool<long double>::vector probabilities;
        };

        vectorPool<Block>::vector blocks; // 来自 Pool::blocks，顺序与输入分布一致。
        vectorPool<long double>::vector probabilities; // 本次分析占用的整段 Pool::blockProbabilities。
        long double totalWays; // 满足全盘约束的布局数，为零时两段切片为空。
    };

    // 用 mineProbability、frontierCells 或 lowestProbabilities 给出的单格雷概率 p 判定。
    // 必安写 p == 0.0L，必雷写 p == 1.0L。
    // 用 eps 或 1 - eps 作判定阈值是错误写法，即使能证明当前取值范围内判定结果相同。
    struct BoxResult {
        // 外层切片来自 Pool::instanceBoxes，顺序与 structure.instanceIds 一致。
        // 内层切片来自 Pool::boxProbabilities，按 Box 编号保存单格雷概率原值，判定用查询出口。
        vectorPool<vectorPool<long double>::vector>::vector instanceBoxes;
        long double tCellProbability; // T 格的单格雷概率。

        // 按实例、Box 顺序回调每个 H 格及其雷概率。pool 和 structure 与投影时一致。
        template <typename Callback> void frontierCells(const Pool &probabilities, const Structure::Result &structure, Callback &&callback) const;
        // 取至多 count 个 H 格的最低雷概率，升序覆写 output，count 非负。
        void lowestProbabilities(const Pool &probabilities, const Structure::Result &structure, int count, vector<long double> &output) const;
    };

    // 两种结果的切片在池清空、销毁或对应段被截断时失效。
    struct Pool {
        ShapeSolver::DistributionPool &distributions;
        Combination::Pool &combinations;
        vectorPool<Result::Block> blocks;
        vectorPool<long double> blockProbabilities;
        vectorPool<vectorPool<long double>::vector> instanceBoxes;
        vectorPool<long double> boxProbabilities;

        Pool(ShapeSolver::DistributionPool &distributions, Combination::Pool &combinations) : distributions(distributions), combinations(combinations) {}
        void clear();
    };

    // ID 来自 results.distributions，每项对应一块，重复 ID 各算一次。
    // 各块互不重叠，totalMines 是其中的剩余雷数。返回各块的雷数档后验概率。
    Result analyze(const vector<ShapeSolver::DistributionId> &distributionIds, int totalMines, Pool &results);

    // 按 totalMines 合并各块的摆法数，ID 约定与 analyze 相同。无解时返回 0。
    long double countWays(const vector<ShapeSolver::DistributionId> &distributionIds, int totalMines, Pool &results);

    // 复用 analyze 的 ID 向量与池。ID 按 structure.instanceIds 排列，末尾可带一项纯 T 分布。
    BoxResult projectBoxes(const Result &result, const vector<ShapeSolver::DistributionId> &distributionIds,
                           const Structure::Result &structure, Pool &results);

    // 输入必须来自同一当前盘面且至少存在一种合法全盘布局；assignments 与 assignments.shapes 同期清空。
    // 按具体全盘雷位近似均匀抽样；函数会 resize 并清零 result，1 表示雷。
    void sampleMineBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, ShapeSolver::AssignmentPool &assignments, Combination::Pool &combinations, U128 seed, Grid<char> &result);

    // 查询同一盘面的格子雷概率，要求全盘有解。判定约定见 BoxResult。
    long double mineProbability(const BoxResult &result, const Pool &pool, ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure);

  private:
    struct Workspace;
    friend struct PolyProductTree;
    Workspace &getWorkspace();
    bool collectFrontiers(const vector<ShapeSolver::DistributionId> &distributionIds, const ShapeSolver::DistributionPool &distributions, vector<Poly> &frontiers, int &tSum);
    long double countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Pool &combinations);
    long double limitProbability(long double probability);
    long double pickFreeWays(int freeCells, int freeMines, Combination::Pool &combinations);
};

template <typename Callback>
void Probability::BoxResult::frontierCells(const Pool &probabilities, const Structure::Result &structure, Callback &&callback) const {
    const Structure::Pool::Instances &instances = probabilities.distributions.shapes.instances;
    for (std::size_t i = 0; i < instanceBoxes.count; ++i) {
        const vectorPool<long double>::vector boxes = probabilities.instanceBoxes.span(instanceBoxes)[i];
        const Structure::Instance &instance = instances.list[structure.instanceIds[i]];
        for (std::size_t box = 0; box < boxes.count; ++box) {
            const vectorPool<Structure::CellId>::vector cells = instances.boxes.span(instance.boxes)[box];
            const long double probability = Probability().limitProbability(probabilities.boxProbabilities.span(boxes)[box]);
            for (Structure::CellId cell : instances.cells.span(cells))
                callback(cell, probability);
        }
    }
}

} // 命名空间 mss
