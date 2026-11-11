#include "algo/winrate_engine/basic/lowrisk/lowrisk.h"

namespace mss {

SuggestResult LowRisk::analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                               const Probability::BoxResult &boxes, const Probability::Pool &pool, const vector<ObservedBoard::CellId> *cells) {
    SuggestResult result(SuggestResult::Engine::LowRisk, SuggestResult::Reason::ScoreWins);
    if (cells) {
        for (int i = 0; i < (int)cells->size; ++i) {
            const ObservedBoard::CellId cell = cells->data[i];
            const long double risk = Probability().mineProbability(boxes, pool, cell, board, basic, structure);
            if (risk != 1.0L)
                result.candidates.push_back(SuggestResult::Candidate(cell, 1.0L - risk));
        }
    } else {
        ObservedBoard::CellId safest = -1;
        long double lowestRisk = 1.0L;
        auto consider = [&](ObservedBoard::CellId cell, long double risk) {
            if (risk < lowestRisk) {
                lowestRisk = risk;
                safest = cell;
            }
        };
        boxes.frontierCells(pool, structure, consider);
        if (basic.unknownSum != 0 && boxes.tCellProbability < lowestRisk)
            consider(basic.firstT(), boxes.tCellProbability);
        if (safest >= 0)
            result.candidates.push_back(SuggestResult::Candidate(safest, 1.0L - lowestRisk));
    }
    result.recommend();
    return result;
}

} // 命名空间 mss
