#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/vector.h"

namespace mss {

// 一局局面的纯状态：盘面、推导结果、结构、概率和它们拥有的池。
// PositionState 保存计算状态；Config、错误文案和查询缓存由 Position 持有。
struct PositionState {
    PositionState(const ObservedBoard::Result &board) : board(board), basicPool(boardPool), shapes(basicPool), distributions(shapes), probabilityPool(shapes, distributions, combinations), probabilityValid(false) {}

    ObservedBoard::Result board;
    Basic::Result basic;
    Structure::Result structure;
    ObservedBoard::Pool boardPool;
    Basic::Pool basicPool;
    Structure::Pool shapes;
    ShapeSolver::DistributionPool distributions;
    Combination::Pool combinations;
    Probability::Pool probabilityPool;
    Probability::Result probability;
    bool probabilityValid;
    // 自上次结构同步以来揭开的格子；空表示结构已同步。
    vector<ObservedBoard::CellId> structureLazyUpdate;

    // 临时把 mines/safes 标成确定雷/安全，数合法全盘布局数；返回前恢复全部状态。
    // mines/safes 必须互不重复且都在盘面内。
    long double countCandidatesWithForces(span<const ObservedBoard::CellId> mines, span<const ObservedBoard::CellId> safes, const ShapeSolverConfig &config, vector<ObservedBoard::CellId> &changedCells);

    // 单格临时揭示；Reveal 只能在同一次同步调用内使用，undo 前不得复用或销毁池。
    // 危险：这是复用的输出槽；applyReveal 返回前完整写入全部字段，此前不得读取或复制。
    struct Reveal {
        ObservedBoard::Delta board;
        Basic::Delta basic;
        Structure::Delta structure;
        Probability::Result probability;
    };
    // 概率追加到 results，Reveal::probability 的切片依赖该池。
    void applyReveal(ObservedBoard::CellId cell, ObservedBoard::CellState next, const ShapeSolverConfig &config, Reveal &reveal, Probability::Pool &results);
    // results 与 applyReveal 相同；撤回并退掉本次概率及三层增量，切片必须位于对应池尾。
    void undoReveal(Reveal &reveal, Probability::Pool &results);
};

} // 命名空间 mss
