#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"

namespace mss {

int FiftyFifty::templateCount() { return TemplateSemi::templateCount(); }

void FiftyFifty::detect(Semi::Result &result, const PositionState &state, const Config &config, Pool &pool, Workspace &workspace) {
    if (config.algorithm == Algorithm::OldJava) Semi::update(result, state, span<const ObservedBoard::CellId>(), workspace.oldJava.semi);
    else TemplateSemi::detect(result, state, config.templates->prefix, pool.templates.semi, workspace.templates.semi);
}

void FiftyFifty::prepareRisk(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Pool &pool, Workspace &workspace, bool collectSemi) {
    if (config.algorithm == Algorithm::OldJava) LongTermRisk::update(result.oldJava, state, span<const ObservedBoard::CellId>(), shapeConfig, *config.oldJava, workspace.oldJava.risk, collectSemi ? &result.semi : nullptr);
    else TemplateLongTermRisk::update(result.templates, state, config.templates->prefix, config.templates->risk, pool.templates.risk, workspace.templates.risk);
}

long double FiftyFifty::influence(ObservedBoard::CellId cell, PositionState &state, Result &result, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace) {
    if (config.algorithm == Algorithm::OldJava) return LongTermRisk::influence(cell, state, result.oldJava, shapeConfig, workspace.oldJava.risk) / state.probability.candidates;
    return TemplateLongTermRisk::influence(cell, state, result.templates, shapeConfig, workspace.templates.risk);
}

LongTermRisk::Details FiftyFifty::describeOldJava(ObservedBoard::CellId cell, PositionState &state, const ShapeSolverConfig &shapeConfig, const LongTermRisk::Config &config, Workspace::OldJava &workspace) {
    LongTermRisk::Details details = LongTermRisk::describe(cell, state, shapeConfig, config, workspace.risk);
    if (Semi::update(workspace.description, state, span<const ObservedBoard::CellId>(), workspace.semi, cell) != Semi::Kind::None) details.body = 1.0L;
    return details;
}

} // 命名空间 mss
