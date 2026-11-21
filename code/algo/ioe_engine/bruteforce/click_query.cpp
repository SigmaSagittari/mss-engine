#include "algo/ioe_engine/bruteforce/click_query.h"

#include <algorithm>

#include "algo/ioe_engine/dom/dom.h"

namespace mss {

std::size_t IoeClickQuery::Cache::Hash::operator()(const Key &key) const {
    const std::uint64_t seed = key.revealed.word(0) ^ (key.revealed.word(1) * 0x9e3779b97f4a7c15ULL);
    return splitmix64(seed ^ splitmix64(key.flags.word(0) ^ (key.flags.word(1) * 0xd1b54a32d192ed03ULL) ^ key.layout));
}

void IoeClickQuery::Cache::clear(std::size_t layouts) {
    queried = 0;
    distances.clear();
    estimateIndex.clear();
    estimates.size = 0;
    planPool.clear();
    plans.resize(layouts);
    for (std::size_t i = 0; i < layouts; ++i) plans[i] = {Coverage::Untried, {0, 0}};
}

void IoeClickQuery::estimate(Cache &cache, int id, const IoeStrategyBoard &layout, const IoeView &view, int remaining, Estimate &result) {
    const IoeClickPlans::Result &entry = cache.plans[id];
    result.coverage = entry.plans.count ? entry.coverage : Coverage::Unavailable;
    if (result.coverage == Coverage::Unavailable) return;
    const Cache::Key key{view.revealed, view.flags, id};
    const std::uint32_t *known = cache.estimateIndex.find(key);
    // 同一盘面在给定剩余点击数下能采用的单击、双击会变。
    if (known && cache.estimates[*known].remaining == remaining) {
        result.moves = cache.estimates[*known].moves;
        return;
    }
    IoeClickPlans().estimate(layout, view, entry, cache.planPool, remaining, result.moves);
    if (known) cache.estimates[*known] = {result.moves, remaining};
    else if (cache.estimates.size < (1U << 18)) {
        cache.estimateIndex.emplace(key, cache.estimates.size);
        cache.estimates.push_back({result.moves, remaining});
    }
}

IoeClickQuery::Bound IoeClickQuery::bound(const Estimate &estimate, const IoeMove &move, bool opensCenter, int remaining, int rootMinimum) {
    if (estimate.coverage == Coverage::Unavailable) return {remaining, false};
    int clicks = remaining;
    if (opensCenter && !estimate.moves.opens.test(move.cell)) ++clicks;
    if (move.kind == IoeMove::Kind::Chord) clicks = std::max(clicks, (int)estimate.moves.chords[move.cell]);
    if (estimate.coverage == Coverage::RootOptimum) clicks = std::min(clicks, rootMinimum + 1);
    return {clicks, estimate.coverage == Coverage::Complete
        && (move.kind == IoeMove::Kind::Open || (move.kind == IoeMove::Kind::Chord && !opensCenter))};
}

int IoeClickQuery::minimum(Cache &cache, int id, const IoeStrategyBoard &layout, const Request &request, const Root &root) {
    IoeClickPlans::Result &plans = cache.plans[id];
    const IoeView &view = request.view;
    const int budget = request.budget;
    const Cache::Key key{view.revealed, view.flags, id};
    int *known = cache.distances.find(key);
    if (known) {
        if (*known >= 0) return *known <= budget ? *known : overBudget;
        if (budget <= -*known - 1) return overBudget;
    } else {
        if (cache.queried == request.limit) return limitReached;
        ++cache.queried;
        known = &cache.distances[key];
    }
    if (request.exact >= 0) {
        *known = request.exact;
        return request.exact <= budget ? request.exact : overBudget;
    }

    if (plans.coverage == Coverage::Untried)
        plans = IoeClickPlans().build(layout, root.view, root.minimum, root.budget, cache.planPool);
    if (plans.coverage == Coverage::Complete) {
        const int distance = IoeClickPlans().minimum(layout, view, plans, cache.planPool, budget);
        *known = distance == overBudget ? -budget - 1 : distance;
        return distance;
    }
    const int distance = IoeDom().minimum(layout.mines, view, 2 * IoeBoard::cells).clicks;
    *known = distance;
    return distance <= budget ? distance : overBudget;
}

} // 命名空间 mss
