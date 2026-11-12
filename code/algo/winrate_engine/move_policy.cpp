#include "algo/winrate_engine/move_policy.h"

#include <algorithm>

#include "algo/winrate_engine/basic/lowrisk/lowrisk.h"

namespace mss {

void Suggest::Pool::clear(Position::Pool &positions) {
    anytime.clear(positions);
    fiftyFifty = {};
    cache.clear();
}

void MovePolicy::Config::hash(U128Hasher &hasher) const {
    hasher.mix((int) algorithm);
    hasher.mix(brute ? brute->limit : 0);
    if (brute) brute->config.hash(hasher);
    if (algorithm == Algorithm::LowRisk) return;
    guess->selection.hash(hasher);
    hasher.mix(guess->longTermRisk);
    guess->fiftyFifty.hash(hasher, guess->longTermRisk);
    guess->exchange.hash(hasher);
    guess->anytime->hash(hasher);
}

struct MovePolicy::Workspace {
    TemplateLongTermRisk::Result risk;
    vector<ObservedBoard::CellId> detected;
    Selection::Result selected;
    Grid<long double> longTermRisk;
    SuggestResult::Details details;
};

__declspec(noinline) MovePolicy::Workspace &MovePolicy::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

SuggestResult MovePolicy::suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                                  Suggest::Pool &suggestions, int remainingSafe) {
    AnytimeSearch::Result search;
    return suggest<false>(position, positions, config, suggestions, remainingSafe, {}, search, getWorkspace().details);
}

SuggestResult MovePolicy::suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                                  Suggest::Pool &suggestions, const vector<ObservedBoard::CellId> &requested,
                                  AnytimeSearch::Result &search, SuggestResult::Details &details) {
    return suggest<true>(position, positions, config, suggestions, 0, requested, search, details);
}

template <bool Details>
SuggestResult MovePolicy::suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                                  Suggest::Pool &suggestions, int remainingSafe, const vector<ObservedBoard::CellId> &requested,
                                  AnytimeSearch::Result &search, SuggestResult::Details &details) {
    using CellId = ObservedBoard::CellId;
    const ObservedBoard::Result &board = position.board;
    const Basic::Result &basic = position.basic;
    const bool specified = requested.size != 0;
    if constexpr (Details) details.clear();
    AnytimeSearch::Pool &searchPool = suggestions.anytime;
    auto resetSearch = [&]() {
        searchPool.clear(positions);
        if constexpr (Details) search = {};
    };
    SuggestResult safe(SuggestResult::Engine::Safe, SuggestResult::Reason::Safe);
    auto addSafe = [&](CellId cell) {
        safe.candidates.push_back({cell, 1.0L});
        safe.suggest.push_back(cell);
    };
    basic.safeCells(addSafe);
    if (safe.suggest.size != 0) {
        resetSearch();
        return safe;
    }

    Probability::Pool &probabilities = positions.probabilities;
    const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
    const long double tMine = probability.boxes.tCellProbability;
    probability.boxes.frontierCells(probabilities, position.structure, [&](CellId cell, long double mine) {
        if (mine == 0.0L) addSafe(cell);
    });
    if (basic.unknownSum != 0 && tMine == 0.0L) basic.tCells(addSafe);
    if (safe.suggest.size != 0) {
        resetSearch();
        return safe;
    }

    if constexpr (Details) {
        remainingSafe = board.cells.rows() * board.cells.cols() - board.totalMines;
        for (int row = 0; row < board.cells.rows(); ++row) for (int col = 0; col < board.cells.cols(); ++col)
            remainingSafe -= ObservedBoard().isNumber(board.cells[row][col]);
    }
    if (remainingSafe == 0) {
        resetSearch();
        return {};
    }

    if (config.brute && probability.totalWays < config.brute->limit) {
        resetSearch();
        const Position::LogicComponentResult &components = Position().analyzeLogicComponents(position, positions);
        BruteForce::Config brute = config.brute->config;
        if (specified) brute.checkAllMoves = true;
        SuggestResult result;
        BruteForce().solve(board, basic, components.result.components, components.pool, positions.structure,
                           board.totalMines - basic.mineSum, brute, result);
        if (specified) {
            int count = 0;
            for (int i = 0; i < (int)result.candidates.size; ++i)
                if (std::find(requested.data, requested.data + requested.size, result.candidates[i].cell) != requested.data + requested.size)
                    result.candidates[count++] = result.candidates[i];
            result.candidates.resize(count);
            result.recommend();
        }
        return result;
    }

    if (config.algorithm == Algorithm::LowRisk) {
        resetSearch();
        return LowRisk().analyze(board, basic, position.structure, probability.boxes, probabilities, requested);
    }

    Workspace &workspace = getWorkspace();
    const Config::Guess &guess = *config.guess;
    const FiftyFifty::Config &fiftyFifty = guess.fiftyFifty;
    FiftyFifty::Pool &fiftyPool = suggestions.fiftyFifty;
    TemplateLongTermRisk::Result &risk = workspace.risk;
    vector<CellId> &detected = workspace.detected;
    Grid<long double> &longTermRisk = workspace.longTermRisk;
    const Selection::Config selection{guess.selection, specified ? Selection::Exchange{} : guess.exchange};
    const FiftyFiftyKind semi = specified ? FiftyFiftyKind::None :
        FiftyFifty().detect(detected, position, probability, probabilities, fiftyFifty, fiftyPool);

    const SuggestResult::Reason reason = semi == FiftyFiftyKind::Forced ? SuggestResult::Reason::Forced :
                                         semi == FiftyFiftyKind::Semi ? SuggestResult::Reason::Semi : SuggestResult::Reason::ScoreWins;
    const vector<CellId> &candidates = semi != FiftyFiftyKind::None ? detected : requested;

    const bool compensate = guess.longTermRisk && semi == FiftyFiftyKind::None;
    const Grid<long double> *influences = compensate && !specified ? &longTermRisk : nullptr;
    const FiftyFiftyData::Pool *templates = compensate
        ? &FiftyFifty().prepareRisk(risk, position, positions, probability, probabilities, fiftyFifty, fiftyPool,
                                   specified ? nullptr : &longTermRisk)
        : nullptr;
    Selection::Result &selected = workspace.selected;
    Selection().select(selected, position, positions, probability, probabilities, selection, candidates, influences);
    longTermRisk.resize(board.cells.rows(), board.cells.cols(), 0.0L);
    if (compensate)
        for (int i = 0; i < (int)selected.candidates.size; ++i) {
            const Selection::Candidate &candidate = selected.candidates[i];
            const long double mine = candidate.observation.probability[9];
            if (mine == 0.0L || mine == 1.0L) continue;
            const auto [row, col] = board.pos(candidate.cell);
            longTermRisk[row][col] = TemplateLongTermRisk().influence(candidate.cell, risk, position, positions, probability, probabilities, *templates).compensation;
        }

    const AnytimeSearch::Config &anytime = *guess.anytime;
    AnytimeSearch::Result searched = Details
        ? AnytimeSearch().analyze(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, anytime, searchPool, details)
        : AnytimeSearch().analyze(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, anytime, searchPool);
    if (semi != FiftyFiftyKind::None) searched.reason = reason;
    SuggestResult result = searched.snapshot(searchPool);
    if constexpr (Details) search = searched;
    else searchPool.clear(positions);
    return result;
}

} // 命名空间 mss
