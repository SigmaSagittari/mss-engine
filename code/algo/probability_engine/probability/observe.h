#pragma once

#include "algo/probability_engine/probability/probability.h"

namespace mss {

struct Observe {
    struct Result {
        // 下标 0..8 对应点开后的数字，9 对应爆炸；只写有贡献的结局，其余为零。
        long double probability[10];

        Result() : probability{} {}
    };

    struct Workspace {
        ShapeSolver::Workspace shapeSolver;
        Combination::Workspace combination;
        struct Components {
            vector<Structure::ComponentId> captured;
            vector<char> seen;
            vector<int> adjacentBoxCells;
            // 每个被捕获组件复用同一份观测结果；下一次 observe 会覆盖它。
            ShapeSolver::Observed observed;
        } components;
        struct Convolution {
            // dp[邻居雷数][已处理组件与邻居 T 格雷数]；两张表交替累积。
            Grid<long double> dp;
            Grid<long double> nextDp;
        } convolution;
        struct Rest {
            // ways 按绝对剩余雷数索引；其余是未捕获组件的多项式卷积暂存。
            vector<long double> ways;
            vector<long double> coefficients;
            vector<long double> scratch;
        } rest;
    };

    // 输入格必须是 Hidden 或 ForcedSafe，且 board/basic/structure/probability 来自同一刷新、candidates 大于零。
    // probability 的切片来自 probabilities，其引用的分布缓存与结构池同期；result 完整覆写。爆炸概率复用传入的全盘概率结果。
    static void observe(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability, const Probability::Pool &probabilities, const ShapeSolver::Config &config, ObservedBoard::CellId cell, Result &result, Workspace &workspace);
};

} // 命名空间 mss
