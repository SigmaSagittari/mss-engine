#pragma once

#include "algo/probability_engine/bruteforce/bruteforce.h"
#include "algo/winrate_engine/java_transplant/java_evaluate.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace mss {

// 程序版本是固定信息，与可调配置分开。
inline constexpr char version[] = "1.5";

struct Config {
    struct Global {
        // 含调用线程；BruteForce 根并行和完整对局并行测试共用此上限，须为正数。
        int threadLimit;

        explicit Global(int threadLimit) : threadLimit(threadLimit) {}
    } global;

    BruteForce::Config bruteForce;
    LongTermRisk::Config longTermRisk;
    Pseudo::Config pseudo;
    JavaEval::Config javaEval;
    MonteCarlo::Config monteCarlo;

    Config(Global global, bool checkAllMoves, BruteForce::Config::Solver solver, int candidateThreshold, LongTermRisk::Config longTermRisk, Pseudo::Config pseudo, JavaEval::Config javaEval, MonteCarlo::Config monteCarlo) : global(global), bruteForce(checkAllMoves, solver, candidateThreshold, this->global.threadLimit), longTermRisk(longTermRisk), pseudo(pseudo), javaEval(javaEval), monteCarlo(monteCarlo) {}
    // 副本引用自己的 Global；MC 按值持有配置时不能引用原请求栈上的线程上限。
    Config(const Config &other) : global(other.global), bruteForce(other.bruteForce, global.threadLimit), longTermRisk(other.longTermRisk), pseudo(other.pseudo), javaEval(other.javaEval), monteCarlo(other.monteCarlo) {}
};

} // 命名空间 mss
