#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss::UI {

// UI 持有可切换算法的参数；MovePolicy 仅在调用期间借用这些值。
struct Config {
    struct Brute {
        int limit;
        BruteForce::Config config;
    };
    GameControl::Config game;
    MovePolicy::Basic::Algorithm algorithm;
    JavaEval::Config javaEval;
    AnytimeSearch::Config anytime;
    MovePolicy::FiftyFifty::Algorithm fiftyAlgorithm;
    FiftyFifty::TemplateConfig templates;
    LongTermRisk::Config risk;
    Brute brute;
    MonteCarlo::Config monteCarlo;
};

// UI 唯一公开入口；本地服务和计算会话由该函数持有。
void run();

}
