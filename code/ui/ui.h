#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss::UI {

// UI 持有可切换算法的参数；MovePolicy 仅在调用期间借用这些值。
struct Config {
    struct Brute {
        int limit;
        BruteForce::Config config;
    };
    struct Fifty {
        mss::FiftyFifty::Algorithm algorithm;
        LongTermRisk::Config oldJava;
        mss::FiftyFifty::TemplateConfig templates;
    };
    Position::Config game;
    MovePolicy::Algorithm algorithm;
    Selection::Config selection;
    JavaEval::Config javaEval;
    AnytimeSearch::Config anytime;
    Fifty fiftyFifty;
    Brute brute;

    Config(bool parallel);
    MovePolicy::Config policy(const FiftyFifty::Config &fifty) const;
};

// UI 唯一公开入口；本地服务和计算会话由该函数持有。
void run(bool openBrowser);

}
