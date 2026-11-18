#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "algo/winrate_engine/move_policy.h"

namespace mss::UI {

// UI 持有搜索参数；MovePolicy 仅在调用期间借用这些值。
struct Config {
    struct Fifty {
        int prefix, eventLimit;
    };
    ShapeSolverConfig::SA annealing;
    Position::Config game;
    bool longTermRisk;
    AnytimeSearch::Config anytime;
    Fifty fiftyFifty;
    MovePolicy::Config::Brute brute;

    Config(bool parallel);
};

// UI 唯一公开入口；本地服务和计算会话由该函数持有。
void run(bool openBrowser);

}
