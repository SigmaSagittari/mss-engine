#pragma once

#include "algo/probability_engine/probability/probability.h"
#include "core/algorithm/poly.h"

namespace mss {

struct Observe {
    struct Result {
        // 下标 0..8 对应点开后的数字，9 对应爆炸；只写有贡献的结局，其余为零。
        // probability[9] 来自 mineProbability，必安、必雷按 Probability::BoxResult 的精确值约定判定。
        long double probability[10];

        Result() : probability{} {}
    };

    // 输入来自同一盘面，totalWays 取自 Probability::analyze 且大于零。cell 是 Hidden 或 ForcedSafe。
    void observe(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                 const Probability::BoxResult &boxes, long double totalWays, const Probability::Pool &probabilities,
                 const ShapeSolver::Config &config, ObservedBoard::CellId cell, Result &result);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    void multiplyPoly(const vector<long double> &left, const Poly &right, vector<long double> &output);
    void swapValues(vector<long double> &left, vector<long double> &right);
    long double choose(int n, int k, Combination::Pool &combinations);
};

} // 命名空间 mss
