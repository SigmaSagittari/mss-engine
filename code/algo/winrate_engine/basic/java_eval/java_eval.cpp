#include "algo/winrate_engine/basic/java_eval/java_eval.h"

namespace mss {

SuggestResult JavaEval::analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    if (details) details->clear();
    Selection::Result selected;
    const int nodes = Selection::begin(state, state.probabilityPool, shapeConfig, config, fiftyConfig, fiftyPool, selected, workspace, Selection::Mode::JavaEval, cells);
    SuggestResult result(SuggestResult::Engine::JavaEval, selected.reason());
    result.nodes = nodes;
    while (selected.next < (int)selected.cells.size) {
        const Selection::Evaluation evaluated = Selection::next(selected, state, state.probabilityPool, shapeConfig, config, fiftyConfig, workspace, nullptr, -1.0L, details);
        result.nodes += evaluated.nodes();
        if (evaluated.javaLite.valid) result.candidates.push_back(SuggestResult::Candidate(evaluated.cell, evaluated.score()));
    }
    result.recommend();
    return result;
}

} // 命名空间 mss
