#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"

namespace mss {

const FiftyFiftyData::Pool &FiftyFifty::prepareTemplates(Pool &pool, int prefix) {
    if (prefix == coarsePrefix) {
        if (pool.prefix100.patterns.size == 0) FiftyFiftyData().compile(pool.prefix100, prefix);
        return pool.prefix100;
    }
    if (pool.prefix != prefix) {
        FiftyFiftyData().compile(pool.templates, prefix);
        pool.prefix = prefix;
    }
    return pool.templates;
}

Semi::Kind FiftyFifty::detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position, const Position::ProbabilityResult &probability,
                             const Probability::Pool &probabilities, const Config &config, Pool &pool,
                             const vector<ObservedBoard::CellId> &dead) {
    if (config.algorithm == Algorithm::OldJava)
        return Semi().update(cells, position, probability, probabilities, dead);
    return TemplateSemi().detect(cells, position, probability, probabilities, prepareTemplates(pool, config.templates->prefix));
}

const FiftyFiftyData::Pool *FiftyFifty::prepareRisk(Result &result, Position::Result &position, Position::Pool &positions,
                                                    const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                                    const Config &config, Pool &pool, Grid<long double> *coarse) {
    if (config.algorithm == Algorithm::OldJava) {
        LongTermRisk().update(result.oldJava, position, positions, probability, probabilities, *config.oldJava);
        return nullptr;
    }
    const int prefix = config.templates->prefix, eventLimit = *config.eventLimit;
    const FiftyFiftyData::Pool &table = prepareTemplates(pool, prefix);
    TemplateLongTermRisk risk;
    if (coarse) {
        const bool shared = prefix == coarsePrefix;
        const FiftyFiftyData::Pool &coarseTable = shared ? table : prepareTemplates(pool, coarsePrefix);
        risk.update(result.templates, position, probability, probabilities, coarseTable, shared ? eventLimit : 1);
        risk.coarse(*coarse, result.templates, position, positions, probability, probabilities, coarseTable);
        if (shared) return &table;
    }
    risk.update(result.templates, position, probability, probabilities, table, eventLimit);
    return &table;
}

long double FiftyFifty::influence(ObservedBoard::CellId cell, Result &result, Position::Result &position, Position::Pool &positions,
                                 const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                 Algorithm algorithm, const FiftyFiftyData::Pool *templates) {
    if (algorithm == Algorithm::OldJava)
        return LongTermRisk().influence(cell, position, positions, probability, probabilities, result.oldJava).compensationWays / probability.totalWays;
    return TemplateLongTermRisk().influence(cell, result.templates, position, positions, probability, probabilities, *templates).compensation;
}

} // 命名空间 mss
