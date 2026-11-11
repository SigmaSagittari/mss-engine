#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"

namespace mss {

void FiftyFifty::prepareTemplates(FiftyFiftyData::Pool &templates, int &prefix, int requestedPrefix) {
    if (prefix == requestedPrefix) return;
    FiftyFiftyData().compile(templates, requestedPrefix);
    prefix = requestedPrefix;
}

Semi::Kind FiftyFifty::detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position, const Position::ProbabilityResult &probability,
                             const Probability::Pool &probabilities, const Config &config, Pool &pool,
                             const vector<ObservedBoard::CellId> &dead) {
    if (config.algorithm == Algorithm::OldJava)
        return Semi().update(cells, position, probability, probabilities, dead);
    prepareTemplates(pool.templates, pool.prefix, config.templates->prefix);
    return TemplateSemi().detect(cells, position, probability, probabilities, pool.templates);
}

void FiftyFifty::prepareRisk(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                            Probability::Pool &probabilities, const Config &config,
                            Pool &pool, Grid<long double> *coarse) {
    if (config.algorithm == Algorithm::OldJava)
        LongTermRisk().update(result.oldJava, position, positions, probability, probabilities, *config.oldJava);
    else {
        const TemplateConfig &templates = *config.templates;
        prepareTemplates(pool.templates, pool.prefix, templates.prefix);
        TemplateLongTermRisk risk;
        if (coarse) {
            constexpr int coarsePrefix = 100;
            const bool shared = templates.prefix == coarsePrefix;
            if (!shared && pool.coarseTemplates.patterns.size == 0) FiftyFiftyData().compile(pool.coarseTemplates, coarsePrefix);
            const FiftyFiftyData::Pool &table = shared ? pool.templates : pool.coarseTemplates;
            risk.update(result.templates, position, probability, probabilities, table, shared ? templates.eventLimit : 1);
            risk.coarse(*coarse, result.templates, position, positions, probability, probabilities, table);
            if (shared) return;
        }
        risk.update(result.templates, position, probability, probabilities, pool.templates, templates.eventLimit);
    }
}

long double FiftyFifty::influence(ObservedBoard::CellId cell, Result &result, Position::Result &position, Position::Pool &positions,
                                 const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                 const Config &config, const Pool &pool) {
    if (config.algorithm == Algorithm::OldJava)
        return LongTermRisk().influence(cell, position, positions, probability, probabilities, result.oldJava).compensationWays / probability.totalWays;
    return TemplateLongTermRisk().influence(cell, result.templates, position, positions, probability, probabilities, pool.templates).compensation;
}

} // 命名空间 mss
