#pragma once

#include "algo/probability_engine/bruteforce/bruteforce.h"
#include "algo/winrate_engine/anytime_search/anytime_search.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss {

// 程序版本是固定信息，与可调配置分开。
inline constexpr char version[] = "1.5";

struct Config {
    BruteForce::Config bruteForce;
    LongTermRisk::Config longTermRisk;
    Pseudo::Config pseudo;
    MoveEval::Config moveEval;
    JavaEval::Config javaEval;
    AnytimeSearch::Config anytimeSearch;
    MonteCarlo::Config monteCarlo;

    Config(BruteForce::Config bruteForce, LongTermRisk::Config longTermRisk, Pseudo::Config pseudo, MoveEval::Config moveEval, JavaEval::Config javaEval, AnytimeSearch::Config anytimeSearch, MonteCarlo::Config monteCarlo) : bruteForce(bruteForce), longTermRisk(longTermRisk), pseudo(pseudo), moveEval(moveEval), javaEval(javaEval), anytimeSearch(anytimeSearch), monteCarlo(monteCarlo) {}
};

} // 命名空间 mss
