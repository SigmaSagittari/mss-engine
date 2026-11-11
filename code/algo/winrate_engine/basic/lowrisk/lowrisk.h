#pragma once

#include "algo/probability_engine/probability/probability.h"
#include "algo/winrate_engine/result_type.h"

namespace mss {

struct LowRisk {
    // 输入来自同一有解盘面。默认从 H、T 格中选雷率最低的非必雷格；同率时 H 优先，T 取最小格号。
    // 指定 cells 时按输入顺序给互异的未揭格评分，跳过必雷格；分数为点击安全率。
    SuggestResult analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                          const Probability::BoxResult &boxes, const Probability::Pool &pool, const vector<ObservedBoard::CellId> *cells = nullptr);
};

} // 命名空间 mss
