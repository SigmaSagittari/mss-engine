#include "algo/winrate_engine/basic/java_eval/java_eval.h"

#include <algorithm>

namespace mss {

struct JavaEval::Workspace {
    struct FrontierCell {
        ObservedBoard::CellId cell;
        long double mine;
    };

    vector<ObservedBoard::Delta::Change> changes;
    vector<FrontierCell> frontier;
    vector<long double> lowest;
};

__declspec(noinline) JavaEval::Workspace &JavaEval::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

JavaEval::Outcome JavaEval::scoreOutcome(const Position::Result &position, const Position::ProbabilityResult &probability,
                                          const Probability::Pool &probabilities, int remaining) {
    Workspace &workspace = getWorkspace();
    vector<long double> &lowest = workspace.lowest;
    probability.boxes.lowestProbabilities(probabilities, position.structure, 2, lowest);
    int known = 0;
    position.basic.safeCells([&](ObservedBoard::CellId) { ++known; });
    const long double offEdge = position.basic.unknownSum != 0 ? 1.0L - probability.boxes.tCellProbability : 0.0L;
    const long double best = (std::max)(lowest.size > 0 ? 1.0L - lowest[0] : 0.0L, offEdge);
    const long double second = (std::max)(lowest.size > 1 ? 1.0L - lowest[1] : 0.0L, offEdge);
    const long double safety = remaining == 1 || known > 1 ? 1.0L : known == 1 ? (4.0L + best) / 5.0L : (4.0L * best + second) / 5.0L;
    probability.boxes.frontierCells(probabilities, position.structure, [&](ObservedBoard::CellId, long double p) { known += p == 0.0L; });
    return {safety, known};
}

template <bool Details, typename Record>
SuggestResult JavaEval::analyzeImpl(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                    Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                                    int remainingSafe, const Config &config, Compensation mode, Record record) {
    using Cell = ObservedBoard::CellState;
    using CellId = ObservedBoard::CellId;
    const ObservedBoard::Result &board = position.board;
    if (remainingSafe == 0) return {};

    SuggestResult result(SuggestResult::Engine::JavaEval, SuggestResult::Reason::ScoreWins);
    result.nodes = selected.nodes;
    Workspace &workspace = getWorkspace();
    long double bestScore = -1.0L; // 尚未算出可比较的完整分数。
    vector<ObservedBoard::Delta::Change> &changes = workspace.changes;
    vector<Workspace::FrontierCell> &frontier = workspace.frontier;
    auto reveal = [&](CellId cell, Cell next) {
        const auto [row, col] = board.pos(cell);
        changes.resize(0);
        changes.push_back({cell, board.cells[row][col], next});
        return Position().updateWithDelta(position, changes, positions);
    };
    auto undo = [&](const Position::Delta &delta, const Position::ProbabilityResult &child, vectorPool<long double>::vector boxTail) {
        probabilities.boxProbabilities.pop_back(boxTail);
        probabilities.instanceBoxes.pop_back(child.boxes.instanceBoxes);
        Position().applyDelta(position, delta, positions, true);
        positions.popDelta(delta);
    };
    auto evaluate = [&](const Selection::Candidate &candidate, long double compensation) -> Evaluation {
        const Observe::Result &observation = candidate.observation;
        const long double mine = observation.probability[9];
        if (mine == 1.0L) return {compensation, true};
        if constexpr (!Details)
            if (mine == 0.0L) return {1.0L + compensation, true};

        unsigned evaluated = 0;
        long double contributions[9]{}, progressByDigit[9]{};
        auto score = [&]() {
            long double value = 0.0L, progress = 0.0L;
            // 未算的数字按满分和有推进计。固定求和顺序，避免重排数字改变最终分数。
            for (int digit = 0; digit <= 8; ++digit) {
                const bool done = evaluated & (1u << digit);
                value += done ? contributions[digit] : observation.probability[digit];
                progress += done ? progressByDigit[digit] : observation.probability[digit];
            }
            const long double shortScore = value * (1.0L + progress * config.progressContribution);
            // OldJava 补回的是安全概率，按安全分支的平均评分折算。
            const long double compensationBase = mode == Compensation::ScaleWithoutProgress ? value : shortScore;
            return shortScore + (mode == Compensation::Additive ? compensation : compensationBase / (1.0L - mine) * compensation);
        };
        if constexpr (!Details)
            if (bestScore >= 0.0L && score() < bestScore) return {0.0L, false};

        int linkedClears = 0;
        if (mine != 0.0L) {
            position.basic.safeCells([&](CellId cell) {
                const auto [row, col] = board.pos(cell);
                linkedClears += board.cells[row][col] != Cell::ForcedSafe;
            });
            frontier.resize(0);
            probability.boxes.frontierCells(probabilities, position.structure, [&](CellId cell, long double otherMine) {
                if (cell != candidate.cell) frontier.push_back({cell, otherMine});
            });
            const Position::Delta delta = reveal(candidate.cell, Cell::ForcedMine);
            const vectorPool<long double>::vector boxTail = probabilities.boxProbabilities.push_back();
            const Position::ProbabilityResult forced = Position().analyzeProbability(position, positions, probabilities);
            ++result.nodes;
            // 反推本格为安、尚未知道数字时，原前沿里有多少格已确定安全。
            for (int i = 0; i < (int)frontier.size; ++i) {
                const Workspace::FrontierCell &other = frontier[i];
                const long double forcedMine = Probability().mineProbability(forced.boxes, probabilities, other.cell, board, position.basic, position.structure);
                linkedClears += (other.mine - mine * forcedMine) / (1.0L - mine) <= 1e-10L;
            }
            undo(delta, forced, boxTail);
        }

        int order[9], count = 0;
        for (int digit = 0; digit <= 8; ++digit) {
            const long double p = observation.probability[digit];
            if (p == 0.0L) continue;
            int i = count++;
            while (i > 0 && observation.probability[order[i - 1]] < p) {
                order[i] = order[i - 1];
                --i;
            }
            order[i] = digit;
        }
        for (int i = 0; i < count; ++i) {
            const int digit = order[i];
            const Position::Delta delta = reveal(candidate.cell, Cell(digit));
            const vectorPool<long double>::vector boxTail = probabilities.boxProbabilities.push_back();
            const Position::ProbabilityResult child = Position().analyzeProbability(position, positions, probabilities);
            ++result.nodes;
            const Outcome outcome = scoreOutcome(position, child, probabilities, remainingSafe);
            contributions[digit] = observation.probability[digit] * outcome.safety;
            if (mine != 0.0L && outcome.knownSafe > linkedClears) progressByDigit[digit] = observation.probability[digit];
            evaluated |= 1u << digit;
            undo(delta, child, boxTail);
            if constexpr (Details) record(candidate.cell, digit, outcome.safety);
            if constexpr (!Details)
                if (bestScore >= 0.0L && score() < bestScore) return {0.0L, false};
        }
        return {mine == 0.0L ? 1.0L + compensation : score(), true};
    };

    for (int i = 0; i < (int)selected.candidates.size; ++i) {
        const Selection::Candidate &candidate = selected.candidates[i];
        const auto [row, col] = board.pos(candidate.cell);
        const long double compensation = longTermRisk[row][col];
        const Evaluation evaluated = evaluate(candidate, compensation);
        if (!evaluated.valid) continue;
        result.candidates.push_back({candidate.cell, evaluated.value});
        if constexpr (!Details) bestScore = (std::max)(bestScore, evaluated.value);
    }
    result.recommend();
    return result;
}

SuggestResult JavaEval::analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                                int remainingSafe, const Config &config, Compensation mode) {
    return analyzeImpl<false>(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, config, mode,
                              [](ObservedBoard::CellId, int, long double) {});
}

SuggestResult JavaEval::analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                                int remainingSafe, const Config &config, SuggestResult::Details &details, Compensation mode) {
    details.clear();
    return analyzeImpl<true>(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, config, mode,
                             [&](ObservedBoard::CellId cell, int digit, long double safety) { details.outcomes.push_back({cell, digit, safety}); });
}

} // 命名空间 mss
