#pragma once

#include "algo/winrate_engine/types.h"

namespace mss {

// 内部基础评分为 0.8f + 0.2s；不作为对外算法或策略暴露。
struct JavaLite {
  private:
    friend struct MoveEval;
    struct Summary {
        long double blendedSafety;
        int clears;
        Summary(long double blendedSafety, int clears) : blendedSafety(blendedSafety), clears(clears) {}
    };
    static Summary evaluate(const winrate::View &input, int remainingSafe);
};

} // 命名空间 mss
