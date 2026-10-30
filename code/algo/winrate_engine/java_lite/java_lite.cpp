#include "algo/winrate_engine/java_lite/java_lite.h"

namespace mss {

void JavaLite::Pool::clear() {
    deltas.data.resize(0);
    probabilities.resize(0);
}

JavaLite::Summary JavaLite::shortTerm(const PositionState &state, const Probability::Result &probability, const Probability::Pool &pool, int remainingSafe) {
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
        const Probability::Result::Component &layout = pool.components.data.data[probability.components.offset + component];
        const int *offsets = state.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        const int boxCount = (int)instance.boxOf.count - 1;
        for (int box = 0; box < boxCount; ++box) {
            const int count = offsets[box + 1] - offsets[box];
            const long double safety = 1.0L - pool.boxProbabilities.data.data[probability.boxProbabilities.offset + layout.boxOffset + box];
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
JavaLite::Result JavaLite::evaluateImpl(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, Pool *pool, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, long double floor, SuggestResult::Details *details) {
    const long double candidates = state.probability.candidates;
    const long double mineProbability = observation.probability[9];
    vectorPool<Delta>::vector recordedDeltas = RetainDeltas ? pool->deltas.push_back() : vectorPool<Delta>::vector{};
    int nodes = 0;
    long double remainingSafety = 1.0L - mineProbability;
    // 必雷格没有可展开的数字结局，value 保持 0。
    if (remainingSafety == 0.0L)
        return Result(0.0L, 1.0L, recordedDeltas, nodes, true);

    long double value = 0.0L;
    long double progress = 0.0L;
    const bool reorder = Prune && !isSafeMove && floor >= 0.0L;
    int order[9], digitCount = 0, deltaDigits[9];
    long double contributions[9]{}, progressByDigit[9]{};
    unsigned evaluated = 0;
    const int detailsBegin = details ? details->outcomes.size : 0;
    for (int digit = 0; digit <= 8; ++digit) {
        if (observation.probability[digit] == 0.0L) continue;
        int position = digitCount;
        if (reorder)
            while (position > 0 && observation.probability[order[position - 1]] < observation.probability[digit]) {
                order[position] = order[position - 1];
                --position;
            }
        order[position] = digit;
        ++digitCount;
    }
    auto optimistic = [&]() {
        if (reorder) {
            // 沿原数字顺序求和；未展开项取最大贡献，避免重排累加使上界向下漂移。
            long double maximumValue = 0.0L, maximumProgress = 0.0L;
            for (int digit = 0; digit <= 8; ++digit) {
                if (observation.probability[digit] == 0.0L) continue;
                maximumValue += evaluated & (1u << digit) ? contributions[digit] : observation.probability[digit];
                maximumProgress += evaluated & (1u << digit) ? progressByDigit[digit] : observation.probability[digit];
            }
            return maximumValue * (1.0L + maximumProgress * progressContribution);
        }
        const long double remaining = remainingSafety;
        const long double bonus = isSafeMove ? 1.0L : 1.0L + (progress + remaining) * progressContribution;
        return (value + remaining) * bonus;
    };

    if constexpr (Prune) {
        if (!isSafeMove && floor >= 0.0L && optimistic() < floor)
            return Result(value, 1.0L + (isSafeMove ? 0.0L : progress * progressContribution), recordedDeltas, nodes, false);
    }

    for (int ordinal = 0; ordinal < digitCount; ++ordinal) {
        const int digit = order[ordinal];
        Probability::Pool &results = RetainDeltas ? pool->probabilityPool : state.probabilityPool;
        state.applyReveal(cell, (ObservedBoard::CellState)digit, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal, results);
        ++nodes;
        const Summary summary = shortTerm(state, workspace.reveal.probability, results, remainingSafe - 1);
        const int offEdgeClears = state.basic.unknownSum != 0 && workspace.reveal.probability.tCellProbability == 0.0L ? state.basic.unknownSum : 0;
        contributions[digit] = observation.probability[digit] * summary.blendedSafety;
        value += contributions[digit];
        if (details) details->outcomes.push_back(SuggestResult::Details::Outcome(cell, digit, summary.blendedSafety));
        if constexpr (RetainDeltas) {
            pool->deltas.push_back(recordedDeltas, Delta(summary.knownSafeCount, workspace.reveal.probability.candidates / candidates, workspace.reveal.board, workspace.reveal.basic, workspace.reveal.structure));
            pool->probabilities.push_back(workspace.reveal.probability);
            deltaDigits[ordinal] = digit;
        }
        if (summary.knownSafeCount - offEdgeClears > linkedClears) {
            progress += observation.probability[digit];
            progressByDigit[digit] = observation.probability[digit];
        }
        evaluated |= 1u << digit;
        remainingSafety -= observation.probability[digit];
        if constexpr (RetainDeltas) {
            Structure::applyDelta(state.structure, workspace.reveal.structure, state.shapes, true);
            Basic::applyDelta(state.basic, workspace.reveal.basic, state.basicPool, true);
            ObservedBoard::applyDelta(state.board, workspace.reveal.board, state.basicPool.board, true);
        } else
            state.undoReveal(workspace.reveal, results);
        if constexpr (Prune) {
            if (floor >= 0.0L && optimistic() < floor)
                return Result(value, 1.0L + (isSafeMove ? 0.0L : progress * progressContribution), recordedDeltas, nodes, false);
        }
    }
    if (reorder) {
        // 发布前恢复原求和顺序，以及续搜和明细使用的数字顺序。
        value = progress = 0.0L;
        for (int digit = 0; digit <= 8; ++digit) {
            if (observation.probability[digit] == 0.0L) continue;
            value += contributions[digit];
            if (progressByDigit[digit] != 0.0L) progress += progressByDigit[digit];
        }
        if constexpr (RetainDeltas)
            for (int i = 1; i < digitCount; ++i) {
                const int digit = deltaDigits[i];
                const Delta delta = pool->deltas.data.data[recordedDeltas.offset + i];
                const Probability::Result probability = pool->probabilities.data[recordedDeltas.offset + i];
                int position = i;
                while (position > 0 && deltaDigits[position - 1] > digit) {
                    pool->deltas.data.data[recordedDeltas.offset + position] = pool->deltas.data.data[recordedDeltas.offset + position - 1];
                    pool->probabilities.data[recordedDeltas.offset + position] = pool->probabilities.data[recordedDeltas.offset + position - 1];
                    deltaDigits[position] = deltaDigits[position - 1];
                    --position;
                }
                pool->deltas.data.data[recordedDeltas.offset + position] = delta;
                pool->probabilities.data[recordedDeltas.offset + position] = probability;
                deltaDigits[position] = digit;
            }
        if (details)
            for (int i = detailsBegin + 1; i < (int)details->outcomes.size; ++i) {
                const SuggestResult::Details::Outcome outcome = details->outcomes.data[i];
                int position = i;
                while (position > detailsBegin && details->outcomes.data[position - 1].digit > outcome.digit) {
                    details->outcomes.data[position] = details->outcomes.data[position - 1];
                    --position;
                }
                details->outcomes.data[position] = outcome;
            }
    }
    const long double progressMultiplier = 1.0L + (isSafeMove ? 0.0L : progress * progressContribution);
    return Result(value, progressMultiplier, recordedDeltas, nodes, true);
}

JavaLite::Result JavaLite::evaluate(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, Pool *pool, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, bool prune, long double floor, SuggestResult::Details *details) {
    if (!prune || floor < 0.0L) {
        if (pool == nullptr) return evaluateImpl<false, false>(state, shapeConfig, progressContribution, workspace, pool, cell, observation, remainingSafe, linkedClears, isSafeMove, floor, details);
        return evaluateImpl<false, true>(state, shapeConfig, progressContribution, workspace, pool, cell, observation, remainingSafe, linkedClears, isSafeMove, floor, details);
    }
    if (pool == nullptr) return evaluateImpl<true, false>(state, shapeConfig, progressContribution, workspace, pool, cell, observation, remainingSafe, linkedClears, isSafeMove, floor, details);
    return evaluateImpl<true, true>(state, shapeConfig, progressContribution, workspace, pool, cell, observation, remainingSafe, linkedClears, isSafeMove, floor, details);
}

} // 命名空间 mss
