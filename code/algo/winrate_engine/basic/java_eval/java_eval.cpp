#include "algo/winrate_engine/basic/java_eval/java_eval.h"

namespace mss {

JavaEval::Summary JavaEval::shortTerm(const PositionState &state, const Probability::Result &probability, int remainingSafe) {
    int known = state.board.totalMines + remainingSafe - state.basic.mineSum - state.basic.unknownSum;
    int clears = 0;
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
                clears += count;
            add(safety);
            if (count > 1)
                add(safety);
        }
    }
    if (known != 0)
        add(1.0L);
    if (known > 1)
        add(1.0L);
    clears += known;
    if (state.basic.unknownSum != 0 && probability.tCellProbability == 0.0L)
        clears += state.basic.unknownSum;
    return Summary(remainingSafe == 0 ? 1.0L : (4.0L * best + second) / 5.0L, clears);
}

template <bool Prune, bool RetainDeltas>
JavaEval::Analysis JavaEval::javaLiteImpl(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Branch> &branches, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int forcedSafes, bool safeSearch, span<const Peer> peers, long double floor, long double influence) {
    const long double candidates = state.probability.candidates;
    const long double mineProbability = observation.probability[9];
    Analysis analysis(cell, 0.0L, 1.0L);
    analysis.branches = branches.push_back();
    long double remainingSafety = 1.0L - mineProbability;
    analysis.node = 1;
    // 必雷格没有可展开的数字结局；直接结束并保留无修正乘数。
    if (remainingSafety == 0.0L)
        return analysis;

    vector<ObservedBoard::CellId> &alternatives = workspace.alternatives;
    alternatives.resize(0);
    for (int i = 0; i < (int)peers.size; ++i)
        if (peers.data[i].probability < mineProbability)
            alternatives.push_back(peers.data[i].cell);

    long double value = 0.0L;
    long double progress = 0.0L;
    int linkedClears = 0;
    auto optimistic = [&]() {
        const long double remaining = remainingSafety;
        const long double bonus = safeSearch ? 1.0L : 1.0L + (progress + remaining) * progressContribution;
        return (value + remaining) * bonus;
    };

    // 安全搜索不计算强制安全签名；1.5 在 safeSearch 时直接令 linkedClears=0，跳过这次展开。
    if (!safeSearch) {
        state.applyReveal(cell, ObservedBoard::CellState::ForcedSafe, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal);
        const Summary forcedSummary = shortTerm(state, workspace.reveal.probability, remainingSafe);
        const int forcedOffEdgeClears = state.basic.unknownSum != 0 && workspace.reveal.probability.tCellProbability == 0.0L ? state.basic.unknownSum : 0;
        linkedClears = forcedSummary.clears - forcedOffEdgeClears - forcedSafes - (workspace.reveal.previous == ObservedBoard::CellState::ForcedSafe ? 0 : 1);
        // 其余格用必安子集判断两格是否互相必安。
        if (peers.size != 0) {
            U128Hasher hasher;
            for (int i = 0; i < (int)peers.size; ++i)
                if (Probability::mineProbability(workspace.reveal.probability, peers.data[i].cell, state.board, state.basic, state.structure) == 0.0L)
                    hasher.mix(peers.data[i].cell);
            analysis.equivalence = hasher.finalize();
            analysis.hasEquivalence = true;
        }
        state.undoReveal(workspace.reveal);
        if constexpr (Prune) {
            if (floor >= 0.0L && optimistic() < floor) {
                analysis.valid = false;
                return analysis;
            }
        }
    }

    for (int digit = 0; digit <= 8; ++digit) {
        if (observation.probability[digit] == 0.0L)
            continue;
        state.applyReveal(cell, (ObservedBoard::CellState)digit, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal);
        const Summary summary = shortTerm(state, workspace.reveal.probability, remainingSafe - 1);
        const int offEdgeClears = state.basic.unknownSum != 0 && workspace.reveal.probability.tCellProbability == 0.0L ? state.basic.unknownSum : 0;
        value += observation.probability[digit] * summary.blendedSafety;
        if constexpr (RetainDeltas) {
            branches.push_back(analysis.branches, Branch(summary.clears, workspace.reveal.probability.candidates / candidates));
            branches.data.data[analysis.branches.offset + analysis.branches.count - 1].boardDelta = workspace.reveal.board;
            branches.data.data[analysis.branches.offset + analysis.branches.count - 1].basicDelta = workspace.reveal.basic;
            branches.data.data[analysis.branches.offset + analysis.branches.count - 1].structureDelta = workspace.reveal.structure;
        }
        if (summary.clears - offEdgeClears > linkedClears)
            progress += observation.probability[digit];
        remainingSafety -= observation.probability[digit];
        int alive = 0;
        for (int i = 0; i < (int)alternatives.size; ++i)
            if (Probability::mineProbability(workspace.reveal.probability, alternatives.data[i], state.board, state.basic, state.structure) == 0.0L)
                alternatives.data[alive++] = alternatives.data[i];
        alternatives.size = alive;
        if constexpr (RetainDeltas) {
            Structure::applyDelta(state.structure, state.shapes, workspace.reveal.structure, true);
            Basic::applyDelta(state.basic, workspace.reveal.basic, state.basicPool, true);
            ObservedBoard::apply(state.board, workspace.reveal.board, state.basicPool.board, true);
        } else
            state.undoReveal(workspace.reveal);
        if constexpr (Prune) {
            if (floor >= 0.0L && optimistic() < floor) {
                analysis.valid = false;
                return analysis;
            }
        }
    }
    analysis.hasAlternative = alternatives.size != 0;
    // 与 1.5 相同，先合并乘数，再乘原始评分，避免分两次乘产生额外舍入。
    analysis.multiplier = influence * (1.0L + (safeSearch ? 0.0L : progress * progressContribution));
    analysis.value = value * analysis.multiplier;
    return analysis;
}

JavaEval::Analysis JavaEval::javaLite(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Branch> &branches, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int forcedSafes, bool safeSearch, span<const Peer> peers, long double floor) {
    return javaLiteImpl<true, true>(state, shapeConfig, progressContribution, workspace, branches, cell, observation, remainingSafe, forcedSafes, safeSearch, peers, floor, 1.0L);
}

JavaEval::Analysis JavaEval::evaluate(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, vectorPool<Branch> &branches, FiftyFifty::Result &risk, const FiftyFifty::Config &fiftyConfig, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int forcedSafes, span<const Peer> peers, bool retainDeltas) {
    const long double candidates = state.probability.candidates;
    const long double safetyTally = candidates - observation.probability[9] * candidates;
    const long double tally = FiftyFifty::influence(cell, state, risk, shapeConfig, fiftyConfig, workspace.selection.fiftyFifty);
    const long double influence = safetyTally == 0.0L ? 1.0L : (safetyTally + config.influenceScale * tally) / safetyTally;
    return retainDeltas ? javaLiteImpl<false, true>(state, shapeConfig, config.progressContribution, workspace, branches, cell, observation, remainingSafe, forcedSafes, false, peers, -1.0L, influence) : javaLiteImpl<false, false>(state, shapeConfig, config.progressContribution, workspace, branches, cell, observation, remainingSafe, forcedSafes, false, peers, -1.0L, influence);
}

SuggestResult JavaEval::analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells) {
    Selection::Result selected;
    selected.swapStorage(workspace.selection);
    Selection::find(state, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, workspace.selection, true, cells);
    const bool pseudo = selected.reason == Selection::Reason::Forced || selected.reason == Selection::Reason::Semi;
    // 仅两格 Forced 不展开；Semi 和较长的 Forced 链需要比较数字结局。
    const bool skipEvaluation = selected.reason == Selection::Reason::Forced && selected.cells.size < 4;
    if (!skipEvaluation) Selection::removeDead(selected, state, shapeConfig, workspace.probability);
    int remainingSafe = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    int forcedSafes = 0;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col) {
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) --remainingSafe;
            else if (state.board.cells[row][col] == ObservedBoard::CellState::ForcedSafe) ++forcedSafes;
        }
    SuggestResult result(SuggestResult::Engine::JavaEval);
    result.reason = selected.reason;
    vectorPool<Branch> branches;
    for (int i = 0; i < (int)selected.cells.size; ++i) {
        const ObservedBoard::CellId cell = selected.cells.data[i];
        if (skipEvaluation || selected.observations.data[i].probability[9] == 0.0L) {
            result.candidates.push_back(SuggestResult::Candidate(cell, 1));
            continue;
        }
        const Analysis analysis = pseudo ? javaLiteImpl<false, false>(state, shapeConfig, config.progressContribution, workspace, branches, cell, selected.observations.data[i], remainingSafe, forcedSafes, false, span<const Peer>(), -1.0L, 1.0L) : evaluate(state, shapeConfig, config, workspace, branches, selected.fiftyFifty, fiftyConfig, cell, selected.observations.data[i], remainingSafe, forcedSafes, span<const Peer>(), false);
        result.nodes += analysis.node;
        result.candidates.push_back(SuggestResult::Candidate(cell, analysis.value));
    }
    result.recommend();
    selected.swapStorage(workspace.selection);
    return result;
}

} // 命名空间 mss
