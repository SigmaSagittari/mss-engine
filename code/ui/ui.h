#pragma once

#include "algo/winrate_engine/anytime_search/anytime_search.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss::UI {

struct Config {
    GameControl::Config game;
    JavaBrute::Config javaBrute;
    AnytimeSearch::Config anytimeSearch;
    MonteCarlo::Config monteCarlo;

    Config(GameControl::Config game, JavaBrute::Config javaBrute, AnytimeSearch::Config anytimeSearch, MonteCarlo::Config monteCarlo) : game(game), javaBrute(javaBrute), anytimeSearch(anytimeSearch), monteCarlo(monteCarlo) {}
};

// UI 唯一公开入口；本地服务和计算会话由该函数持有。
void run();

}
