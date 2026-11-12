#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"

#include "algo/winrate_engine/fifty_fifty/templates/semi.h"

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

FiftyFiftyKind FiftyFifty::detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position, const Position::ProbabilityResult &probability,
                                 const Probability::Pool &probabilities, const Config &config, Pool &pool) {
    return TemplateSemi().detect(cells, position, probability, probabilities, prepareTemplates(pool, config.prefix));
}

const FiftyFiftyData::Pool &FiftyFifty::prepareRisk(TemplateLongTermRisk::Result &result, Position::Result &position, Position::Pool &positions,
                                                 const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                                 const Config &config, Pool &pool, Grid<long double> *coarse) {
    const int prefix = config.prefix, eventLimit = *config.eventLimit;
    const FiftyFiftyData::Pool &table = prepareTemplates(pool, prefix);
    TemplateLongTermRisk risk;
    if (coarse) {
        const bool shared = prefix == coarsePrefix;
        const FiftyFiftyData::Pool &coarseTable = shared ? table : prepareTemplates(pool, coarsePrefix);
        risk.update(result, position, probability, probabilities, coarseTable, shared ? eventLimit : 1);
        risk.coarse(*coarse, result, position, positions, probability, probabilities, coarseTable);
        if (shared) return table;
    }
    risk.update(result, position, probability, probabilities, table, eventLimit);
    return table;
}

} // 命名空间 mss
