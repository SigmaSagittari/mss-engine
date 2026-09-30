#pragma once

#include <array>

#include "algo/probability_engine/probability/probability.h"

namespace mss {

struct Observe {
    struct Result {
        std::array<long double, 10> probability = {};
    };

    // 输入格必须是隐藏格，且盘面、基础结果、结构结果与概率结果必须来自同一次刷新；概率结果候选数必须大于零。
    // 返回数组的下标 0..8 对应点开后数字，下标 9 对应爆炸；爆炸概率复用传入的全盘概率结果。观测与全盘概率计算共用工作区，同一工作区不得并发或重入使用。
    static Result observe(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Probability::Result &probability, ShapeSolver::DistributionPool &distributions, ObservedBoard::CellId cell, Probability::Workspace &workspace, const ShapeSolver::OrderAlgo &algo = ShapeSolver::OrderAlgo::Auto);
};

}
