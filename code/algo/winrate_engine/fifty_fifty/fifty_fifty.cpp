#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"

namespace mss {

struct FiftyFifty::Workspace {
    vector<ObservedBoard::CellId> description;
};

__declspec(noinline) FiftyFifty::Workspace &FiftyFifty::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

int FiftyFifty::templateCount() { return TemplateSemi().templateCount(); }

Semi::Kind FiftyFifty::detect(vector<ObservedBoard::CellId> &cells, const PositionState &state, const Probability::Pool &probabilities, const Config &config, Pool &pool) {
    if (config.algorithm == Algorithm::OldJava) return Semi().update(cells, state, probabilities, span<const ObservedBoard::CellId>());
    return TemplateSemi().detect(cells, state, probabilities, config.templates->prefix, pool.templates.semi);
}

Semi::Kind FiftyFifty::prepareRisk(Result &result, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, Pool &pool, vector<ObservedBoard::CellId> *semiCells) {
    if (config.algorithm == Algorithm::OldJava) return LongTermRisk().update(result.oldJava, state, probabilities, span<const ObservedBoard::CellId>(), shapeConfig, *config.oldJava, semiCells);
    TemplateLongTermRisk().update(result.templates, state, probabilities, config.templates->prefix, config.templates->risk, pool.templates.risk);
    return Semi::Kind::None;
}

long double FiftyFifty::influence(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, Result &result, const ShapeSolverConfig &shapeConfig, const Config &config) {
    if (config.algorithm == Algorithm::OldJava) return LongTermRisk().influence(cell, state, probabilities, result.oldJava, shapeConfig) / state.probability.candidates;
    return TemplateLongTermRisk().influence(cell, state, probabilities, result.templates, shapeConfig);
}

LongTermRisk::Details FiftyFifty::describeOldJava(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const LongTermRisk::Config &config) {
    Workspace &workspace = getWorkspace();
    LongTermRisk::Details details = LongTermRisk().describe(cell, state, probabilities, shapeConfig, config);
    if (Semi().update(workspace.description, state, probabilities, span<const ObservedBoard::CellId>(), cell) != Semi::Kind::None) details.body = 1.0L;
    return details;
}

} // 命名空间 mss
