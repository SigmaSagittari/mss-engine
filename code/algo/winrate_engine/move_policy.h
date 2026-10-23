#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/bruteforce/bruteforce.h"

namespace mss {

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

    struct FiftyFifty {
        enum class Algorithm { OldJava, Templates };
        Algorithm algorithm;
        // 只读取所选分支的配置；LowRisk 不读取本组。
        const Pseudo::Config *oldJava;
        const mss::FiftyFifty::TemplateConfig *templates;
        const LongTermRisk::Config *risk;
        FiftyFifty(Algorithm algorithm, const Pseudo::Config *oldJava, const mss::FiftyFifty::TemplateConfig *templates, const LongTermRisk::Config *risk) : algorithm(algorithm), oldJava(oldJava), templates(templates), risk(risk) {}
    } fiftyFifty;

    struct Brute {
        // 0 关闭暴力接管，此时不读取 config；启用时 config 必须有效。
        int limit;
        const BruteForce::Config *config;
        Brute(int limit, const BruteForce::Config *config) : limit(limit), config(config) {}
    } brute;

    MovePolicy(Basic basic, FiftyFifty fiftyFifty, Brute brute) : basic(basic), fiftyFifty(fiftyFifty), brute(brute) {}
    void hash(U128Hasher &hasher) const;
};

} // 命名空间 mss
