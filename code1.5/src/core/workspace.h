#pragma once

#include "algo/winrate_engine/anytime_search/anytime_search.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss {

struct Workspace {
    GameControl::Workspace game;
    AnytimeSearch::Workspace anytimeSearch;
    MonteCarlo::Workspace monteCarlo;
    Workspace() : anytimeSearch(game.javaEval) {}
};

} // 命名空间 mss
