#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss::UI {

// UI 持有可切换算法的参数；MovePolicy 仅在调用期间借用这些值。
struct Config {
    struct Fifty {
        mss::FiftyFifty::Algorithm algorithm;
        LongTermRisk::Config oldJava;
        mss::FiftyFifty::TemplateConfig templates;
        int eventLimit;
    };
    ShapeSolverConfig::SA annealing;
    Position::Config game;
    MovePolicy::Algorithm algorithm;
    bool longTermRisk;
    JavaEval::Config javaEval;
    AnytimeSearch::Config anytime;
    Fifty fiftyFifty;
    MovePolicy::Config::Brute brute;

    Config(bool parallel);
    MovePolicy::Config::Guess guess() const;
    MovePolicy::Config policy(const MovePolicy::Config::Guess &guess) const;
};

// UI 唯一公开入口；本地服务和计算会话由该函数持有。
void run(bool openBrowser);

}
