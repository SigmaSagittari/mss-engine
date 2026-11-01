#pragma once

#include "algo/probability_engine/probability/probability.h"
#include "core/algorithm/poly.h"

namespace mss {

struct Observe {
    struct Result {
        // 下标 0..8 对应点开后的数字，9 对应爆炸；只写有贡献的结局，其余为零。
        long double probability[10];

        Result() : probability{} {}
    };

    // 输入格必须是 Hidden 或 ForcedSafe，且 board/basic/structure/probability 来自同一刷新、candidates 大于零。
    // probability 的切片来自 probabilities，其引用的分布缓存与结构池同期；result 完整覆写。爆炸概率复用传入的全盘概率结果。
    void observe(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability, const Probability::Pool &probabilities, const ShapeSolver::Config &config, ObservedBoard::CellId cell, Result &result);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    void multiplyPoly(const Poly &left, const Poly &right, vector<long double> &output);
    void swapValues(vector<long double> &left, vector<long double> &right);
    long double choose(int n, int k, Combination::Pool &combinations);
};

} // 命名空间 mss
