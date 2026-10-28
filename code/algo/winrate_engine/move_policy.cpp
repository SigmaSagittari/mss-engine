#include "algo/winrate_engine/move_policy.h"

#include <functional>

namespace mss {

void MovePolicy::hash(U128Hasher &hasher) const {
    hasher.mix((int)basic.algorithm);
    if (basic.algorithm == Basic::Algorithm::JavaEval) basic.javaEval->hash(hasher);
    if (basic.algorithm == Basic::Algorithm::Anytime) {
        basic.anytime->javaEval.hash(hasher);
        hasher.mix((std::uint64_t)basic.anytime->nodeLimit);
        for (int i = 0; i < 3; ++i) hasher.mix(basic.anytime->width[i]);
    }
    if (basic.algorithm != Basic::Algorithm::LowRisk) {
        hasher.mix((int)fiftyFifty.algorithm);
        if (fiftyFifty.algorithm == mss::FiftyFifty::Algorithm::OldJava) hasher.mix((std::uint64_t)std::hash<long double>{}(fiftyFifty.oldJava->influenceThreshold));
        else {
            hasher.mix(fiftyFifty.templates->prefix);
            hasher.mix(fiftyFifty.templates->risk.eventLimit);
        }
    }
    hasher.mix(brute.limit);
    if (brute.limit != 0) {
        hasher.mix((int)brute.config->checkAllMoves); hasher.mix((int)brute.config->parallel); hasher.mix(brute.config->threadCount);
    }
}

} // 命名空间 mss
