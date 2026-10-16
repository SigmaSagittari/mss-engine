#pragma once

#include <span>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/structure/structure.h"

namespace mss::winrate {

using CellId = ObservedBoard::CellId;
using CellState = ObservedBoard::CellState;
using Cells = std::span<const CellId>;
using Board = ObservedBoard::Result;
using Basic = mss::Basic::Result;
using Structure = mss::Structure::Result;
using Probability = mss::Probability::Result;
using Shapes = mss::Structure::Pool;
using Distributions = ShapeSolver::DistributionPool;

// Game 与蒙特卡洛模拟使用同一个后续落子策略类型。
enum class SuggestAlgo { LowRisk, JavaEval, JavaBrute };

// 只借用同一盘面的引擎对象；各池覆盖分支切片和续算结果的生命周期，临时分支使用后恢复。
struct Input {
    Board &board;
    Basic &basic;
    Structure &structure;
    const Probability &probability;
    Shapes &shapes;
    Distributions &distributions;
    ObservedBoard::Pool &boardPool;
    mss::Basic::Pool &basicPool;

    Input(Board &board, Basic &basic, Structure &structure, const Probability &probability, Shapes &shapes, Distributions &distributions, ObservedBoard::Pool &boardPool, mss::Basic::Pool &basicPool) : board(board), basic(basic), structure(structure), probability(probability), shapes(shapes), distributions(distributions), boardPool(boardPool), basicPool(basicPool) {}
};

// 只读算法不借用分支分布池；对象及池视图的有效期沿用原引擎契约。
struct View {
    const Board &board;
    const Basic &basic;
    const Structure &structure;
    const Probability &probability;
    const Shapes &shapes;

    View(const Board &board, const Basic &basic, const Structure &structure, const Probability &probability, const Shapes &shapes) : board(board), basic(basic), structure(structure), probability(probability), shapes(shapes) {}
    View(const Input &input) : board(input.board), basic(input.basic), structure(input.structure), probability(input.probability), shapes(input.shapes) {}
};

} // 命名空间 mss::winrate
