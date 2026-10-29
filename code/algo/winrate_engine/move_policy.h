#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/basic/java_eval/java_eval.h"
#include "algo/winrate_engine/bruteforce/bruteforce.h"

namespace mss {

// 三项独立选择：普通算法、50-50 算法族、暴力接管阈值。Position::suggest 负责分派。
// 只借用配置，不拥有工作区或池；配置须覆盖调用及后台任务的生命周期。
struct MovePolicy {
    struct Basic {
        enum class Algorithm { LowRisk, JavaEval, Anytime };
        Algorithm algorithm;
        // 只读取所选算法的配置；LowRisk 两个指针均可为空。
        const JavaEval::Config *javaEval;
        const AnytimeSearch::Config *anytime;
        Basic(Algorithm algorithm, const JavaEval::Config *javaEval, const AnytimeSearch::Config *anytime) : algorithm(algorithm), javaEval(javaEval), anytime(anytime) {}
    } basic;

    mss::FiftyFifty::Config fiftyFifty;

    struct Brute {
        // 0 关闭暴力接管，此时不读取 config；启用时 config 必须有效。
        int limit;
        const BruteForce::Config *config;
        Brute(int limit, const BruteForce::Config *config) : limit(limit), config(config) {}
    } brute;

    MovePolicy(Basic basic, mss::FiftyFifty::Config fiftyFifty, Brute brute) : basic(basic), fiftyFifty(fiftyFifty), brute(brute) {}
    void hash(U128Hasher &hasher) const;
};

} // 命名空间 mss
