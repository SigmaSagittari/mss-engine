#pragma once

#include "algo/winrate_engine/selection/selection.h"

namespace mss {

// 直接消费 Selection 评分并给出推荐；Anytime 消费同一评分继续搜索。
struct JavaEval {
    using Config = Selection::Config;
    using Workspace = Selection::Workspace;

    // state 已同步且概率有效；自动模式要求入口已排除安全格，候选保持 Selection 顺序。
    // cells 非空时只评估指定格，格子互异且未揭开；空数组返回空结果。
    // 返回前恢复 state；details 非空时重置并写入本次逐数字评分，结果自行拥有数组。
    static SuggestResult analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells = nullptr, SuggestResult::Details *details = nullptr);
};

} // 命名空间 mss
