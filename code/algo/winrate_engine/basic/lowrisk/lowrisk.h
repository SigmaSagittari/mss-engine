#pragma once

#include "algo/winrate_engine/result_type.h"
#include "game/position_state.h"

namespace mss {

// 只比较本次点击的安全率；不展开数字，不使用 50-50 或长期风险。
struct LowRisk {
    // state 已同步且概率有效；自动模式用于无安全格的局面，只返回一个最小雷率候选。
    // cells 非空时直接评估指定格；输入格互异且未揭开，必雷不作为招法。
    SuggestResult analyze(const PositionState &state, const vector<ObservedBoard::CellId> *cells = nullptr);
};

} // 命名空间 mss
