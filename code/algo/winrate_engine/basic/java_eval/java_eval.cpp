#include "algo/winrate_engine/basic/java_eval/java_eval.h"

#include <functional>

namespace mss {

void JavaEval::Config::hash(U128Hasher &hasher) const {
    auto mixReal = [&](long double value) { hasher.mix((std::uint64_t)std::hash<long double>{}(value)); };
    mixReal(selection.selectionThreshold1);
    mixReal(selection.selectionThreshold2);
    hasher.mix(selection.spaceThreshold);
    hasher.mix(selection.interchangeable);
    hasher.mix(selection.equivalence);
    hasher.mix(selection.dominance);
    mixReal(progressContribution);
    mixReal(influenceScale);
}

JavaEval::Summary JavaEval::shortTerm(const PositionState &state, const Probability::Result &probability, int remainingSafe) {
    int known = state.board.totalMines + remainingSafe - state.basic.mineSum - state.basic.unknownSum;
    int knownSafeCount = 0;
    long double best = state.basic.unknownSum != 0 ? 1.0L - probability.tCellProbability : 0.0L;
    long double second = best;
    auto add = [&](long double safety) {
        if (safety > best) {
            second = best;
            best = safety;
        } else if (safety > second)
            second = safety;
    };
    for (int component = 0; component < (int)state.structure.components.size; ++component) {
        const Structure::Instance &instance = state.shapes.instances.list.data[state.structure.components.data[component]];
        const Probability::Result::Component &layout = probability.components.data[component];
        const int *offsets = state.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        const int boxCount = (int)instance.boxOf.count - 1;
        for (int box = 0; box < boxCount; ++box) {
            const int count = offsets[box + 1] - offsets[box];
            const long double safety = 1.0L - probability.boxProbabilities.data[layout.boxOffset + box];
            known -= count;
            if (safety == 1.0L)
                knownSafeCount += count;
            add(safety);
            if (count > 1)
                add(safety);
        }
    }
    if (known != 0)
        add(1.0L);
    if (known > 1)
        add(1.0L);
    knownSafeCount += known;
    if (state.basic.unknownSum != 0 && probability.tCellProbability == 0.0L)
        knownSafeCount += state.basic.unknownSum;
    return Summary(remainingSafe == 0 ? 1.0L : (4.0L * best + second) / 5.0L, knownSafeCount);
}

template <bool Prune, bool RetainDeltas>
JavaEval::Result JavaEval::javaLiteImpl(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Delta> &deltas, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, long double floor, long double riskMultiplier, SuggestResult::Details *details) {
    const long double candidates = state.probability.candidates;
    const long double mineProbability = observation.probability[9];
    Result result(cell, 0.0L, 1.0L, riskMultiplier);
    result.deltas = deltas.push_back();
    long double remainingSafety = 1.0L - mineProbability;
    // 必雷格没有可展开的数字结局；直接结束并保留无修正乘数。
    if (remainingSafety == 0.0L)
        return result;

    long double value = 0.0L;
    long double progress = 0.0L;
    auto optimistic = [&]() {
        const long double remaining = remainingSafety;
        const long double bonus = isSafeMove ? 1.0L : 1.0L + (progress + remaining) * progressContribution;
        return (value + remaining) * bonus;
    };

    if constexpr (Prune) {
        if (!isSafeMove && floor >= 0.0L && optimistic() < floor) {
            result.valid = false;
            return result;
        }
    }

    for (int digit = 0; digit <= 8; ++digit) {
        if (observation.probability[digit] == 0.0L)
            continue;
        state.applyReveal(cell, (ObservedBoard::CellState)digit, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal);
        ++result.nodes;
        const Summary summary = shortTerm(state, workspace.reveal.probability, remainingSafe - 1);
        const int offEdgeClears = state.basic.unknownSum != 0 && workspace.reveal.probability.tCellProbability == 0.0L ? state.basic.unknownSum : 0;
        value += observation.probability[digit] * summary.blendedSafety;
        if (details) details->outcomes.push_back(SuggestResult::Details::Outcome(cell, digit, summary.blendedSafety));
        if constexpr (RetainDeltas) {
            deltas.push_back(result.deltas, Delta(summary.knownSafeCount, workspace.reveal.probability.candidates / candidates, workspace.reveal.board, workspace.reveal.basic, workspace.reveal.structure));
        }
        if (summary.knownSafeCount - offEdgeClears > linkedClears)
            progress += observation.probability[digit];
        remainingSafety -= observation.probability[digit];
        if constexpr (RetainDeltas) {
            Structure::applyDelta(state.structure, state.shapes, workspace.reveal.structure, true);
            Basic::applyDelta(state.basic, workspace.reveal.basic, state.basicPool, true);
            ObservedBoard::apply(state.board, workspace.reveal.board, state.basicPool.board, true);
        } else
            state.undoReveal(workspace.reveal);
        if constexpr (Prune) {
            if (floor >= 0.0L && optimistic() < floor) {
                result.valid = false;
                return result;
            }
        }
    }
    result.progressMultiplier = 1.0L + (isSafeMove ? 0.0L : progress * progressContribution);
    result.value = value;
    return result;
}

JavaEval::Result JavaEval::javaLite(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Delta> &deltas, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, long double floor, SuggestResult::Details *details) {
    return javaLiteImpl<true, true>(state, shapeConfig, progressContribution, workspace, deltas, cell, observation, remainingSafe, linkedClears, isSafeMove, floor, 1.0L, details);
}

JavaEval::Result JavaEval::evaluate(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, vectorPool<Delta> &deltas, FiftyFifty::Result &risk, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool retainDeltas, SuggestResult::Details *details) {
    const long double candidates = state.probability.candidates;
    const long double safetyTally = candidates - observation.probability[9] * candidates;
    const long double tally = FiftyFifty::influence(cell, state, risk, shapeConfig, workspace.selection.fiftyFifty);
    const long double riskMultiplier = safetyTally == 0.0L ? 1.0L : (safetyTally + config.influenceScale * tally) / safetyTally;
    return retainDeltas ? javaLiteImpl<false, true>(state, shapeConfig, config.progressContribution, workspace, deltas, cell, observation, remainingSafe, linkedClears, false, -1.0L, riskMultiplier, details) : javaLiteImpl<false, false>(state, shapeConfig, config.progressContribution, workspace, deltas, cell, observation, remainingSafe, linkedClears, false, -1.0L, riskMultiplier, details);
}

SuggestResult JavaEval::analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    if (details) details->clear();
    Selection::Result selected;
    const Selection::Result::Source source = Selection::find(state, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, workspace.selection, true, cells);
    const bool pseudo = source == Selection::Result::Source::Forced || source == Selection::Result::Source::Semi;
    // 仅两格 Forced 不展开；Semi 和较长的 Forced 链需要比较数字结局。
    const bool skipEvaluation = source == Selection::Result::Source::Forced && selected.cells.size < 4;
    const int observationNodes = skipEvaluation ? 0 : Selection::removeDead(selected, source, state, shapeConfig, workspace.probability);
    int remainingSafe = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    int forcedSafes = 0;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col) {
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) --remainingSafe;
            else if (state.board.cells[row][col] == ObservedBoard::CellState::ForcedSafe) ++forcedSafes;
        }
    const SuggestResult::Reason reason = source == Selection::Result::Source::Forced ? SuggestResult::Reason::Forced : source == Selection::Result::Source::Semi ? SuggestResult::Reason::Semi : SuggestResult::Reason::ScoreWins;
    SuggestResult result(SuggestResult::Engine::JavaEval, reason);
    result.nodes = observationNodes;
    vectorPool<Delta> deltas;
    Selection::Config selectionConfig = config.selection;
    if (cells != nullptr) selectionConfig.interchangeable = selectionConfig.equivalence = selectionConfig.dominance = false;
    for (int i = 0; i < (int)selected.cells.size; ++i) {
        const ObservedBoard::CellId cell = selected.cells.data[i];
        if (skipEvaluation || selected.observations.data[i].probability[9] == 0.0L) {
            result.candidates.push_back(SuggestResult::Candidate(cell, 1));
            continue;
        }
        if (!Selection::prepare(selected, i, state, shapeConfig, selectionConfig, remainingSafe, forcedSafes, false, result.nodes, workspace.selection))
            continue;
        const int linkedClears = selected.prepared.data[selected.prepared.size - 1].linkedClears;
        const Result evaluated = pseudo ? javaLiteImpl<false, false>(state, shapeConfig, config.progressContribution, workspace, deltas, cell, selected.observations.data[i], remainingSafe, linkedClears, false, -1.0L, 1.0L, details) : evaluate(state, shapeConfig, config, workspace, deltas, selected.fiftyFifty, cell, selected.observations.data[i], remainingSafe, linkedClears, false, details);
        result.nodes += evaluated.nodes;
        result.candidates.push_back(SuggestResult::Candidate(cell, evaluated.score()));
    }
    result.recommend();
    return result;
}

} // 命名空间 mss
