#pragma once

#include "algo/winrate_engine/anytime_search/anytime_search.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss {

// 程序版本是固定信息，与可调配置分开。
inline constexpr char version[] = "1.5";

struct Config {
    GameControl::Config game;
    AnytimeSearch::Config anytimeSearch;
    MonteCarlo::Config monteCarlo;
    Config(GameControl::Config game, AnytimeSearch::Config anytimeSearch, MonteCarlo::Config monteCarlo) : game(game), anytimeSearch(anytimeSearch), monteCarlo(monteCarlo) {}
};

} // 命名空间 mss
