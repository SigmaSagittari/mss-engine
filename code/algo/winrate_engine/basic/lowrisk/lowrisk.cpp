#include "algo/winrate_engine/basic/lowrisk/lowrisk.h"

namespace mss {

SuggestResult LowRisk::analyze(const PositionState &state, const vector<ObservedBoard::CellId> *cells) {
    SuggestResult result(SuggestResult::Engine::LowRisk, SuggestResult::Reason::ScoreWins);
    if (cells) {
        for (int i = 0; i < (int)cells->size; ++i) {
            const ObservedBoard::CellId cell = cells->data[i];
            const long double probability = Probability::mineProbability(state.probability, state.probabilityPool, cell, state.board, state.basic, state.structure);
            if (probability != 1) result.candidates.push_back(SuggestResult::Candidate(cell, 1 - probability));
        }
        result.recommend();
        return result;
    }
    ObservedBoard::CellId safest = -1;
    long double lowestRisk = 1;
    auto consider = [&](ObservedBoard::CellId cell, long double probability) {
        if (probability < lowestRisk) { lowestRisk = probability; safest = cell; }
    };
    state.probability.frontierCells(state.probabilityPool, state.structure, consider);
    // T 格概率相同，仅在优于前沿时定位其中一个。
    if (state.basic.unknownSum != 0 && state.probability.tCellProbability < lowestRisk) {
        ObservedBoard::CellId cell = 0;
        const int cols = state.board.cells.cols();
        while (state.basic.marks[cell / cols][cell % cols] != Basic::Mark::T) ++cell;
        consider(cell, Probability::mineProbability(state.probability, state.probabilityPool, cell, state.board, state.basic, state.structure));
    }
    if (safest >= 0) result.candidates.push_back(SuggestResult::Candidate(safest, 1 - lowestRisk));
    result.recommend();
    return result;
}

} // 命名空间 mss
