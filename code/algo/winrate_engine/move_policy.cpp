#include "algo/winrate_engine/move_policy.h"

#include <algorithm>

#include "algo/winrate_engine/basic/lowrisk/lowrisk.h"

namespace mss {

void MovePolicy::Config::hash(U128Hasher &hasher) const {
    hasher.mix((int) algorithm);
    brute.hash(hasher);
    if (algorithm == Algorithm::LowRisk) return;
    selection->hash(hasher);
    fiftyFifty->hash(hasher, selection->longTermRisk);
    if (algorithm == Algorithm::JavaEval) javaEval->hash(hasher);
    else anytime->hash(hasher);
}

struct MovePolicy::Workspace {
    FiftyFifty::Result risk;
    LongTermRisk::SemiResult oldJavaSemi;
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
                                  Suggest::Pool &suggestions, const int *knownRemainingSafe,
                                  span<const ObservedBoard::CellId> requested, AnytimeSearch::Result *search) {
    using CellId = ObservedBoard::CellId;
    const ObservedBoard::Result &board = position.board;
    const Basic::Result &basic = position.basic;
    const bool specified = requested.size != 0;
    AnytimeSearch::Pool &searchPool = suggestions.anytime;
    auto resetSearch = [&]() {
        searchPool.clear(positions);
        if (search) *search = {};
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

    int remainingSafe = knownRemainingSafe ? *knownRemainingSafe : board.cells.rows() * board.cells.cols() - board.totalMines;
    if (!knownRemainingSafe)
        for (int row = 0; row < board.cells.rows(); ++row) for (int col = 0; col < board.cells.cols(); ++col)
            remainingSafe -= ObservedBoard().isNumber(board.cells[row][col]);
    if (remainingSafe == 0) {
        resetSearch();
        return {};
    }

    if (config.brute.limit != 0 && probability.totalWays < config.brute.limit) {
        resetSearch();
        const Position::LogicComponentResult &components = Position().analyzeLogicComponents(position, positions);
        BruteForce::Config brute = *config.brute.config;
        if (specified) brute.checkAllMoves = true;
        SuggestResult result;
        BruteForce().solve(board, basic, components.result.components, components.pool, positions.structure,
                           board.totalMines - basic.mineSum, brute, result);
        if (specified) {
            int count = 0;
            for (int i = 0; i < (int)result.candidates.size; ++i)
                if (std::find(requested.begin(), requested.end(), result.candidates[i].cell) != requested.end())
                    result.candidates[count++] = result.candidates[i];
            result.candidates.resize(count);
            result.recommend();
        }
        return result;
    }

    if (config.algorithm == Algorithm::LowRisk) {
        resetSearch();
        if (!specified) return LowRisk().analyze(board, basic, position.structure, probability.boxes, probabilities);
        Workspace &workspace = getWorkspace();
        vector<CellId> &cells = workspace.detected;
        cells.resize(requested.size);
        for (int i = 0; i < requested.size; ++i) cells[i] = requested[i];
        return LowRisk().analyze(board, basic, position.structure, probability.boxes, probabilities, &cells);
    }

    Workspace &workspace = getWorkspace();
    const FiftyFifty::Config &fiftyFifty = *config.fiftyFifty;
    FiftyFifty::Pool &fiftyPool = suggestions.fiftyFifty;
    FiftyFifty::Result &risk = workspace.risk;
    LongTermRisk::SemiResult &oldJavaSemi = workspace.oldJavaSemi;
    vector<CellId> &detected = workspace.detected;
    Grid<long double> &longTermRisk = workspace.longTermRisk;
    const bool java = config.algorithm == Algorithm::JavaEval;
    const bool oldJava = fiftyFifty.algorithm == FiftyFifty::Algorithm::OldJava;
    Selection::Config selection = *config.selection;
    Semi::Kind semi = Semi::Kind::None;
    bool riskPrepared = false;
    if (specified || java) {
        selection.interchangeable = false;
        selection.equivalence = false;
        selection.dominance = false;
    }
    if (!specified) {
        oldJavaSemi.cells.resize(0);
        semi = FiftyFifty().detect(detected, position, probability, probabilities, fiftyFifty, fiftyPool, oldJavaSemi.cells);
        if (semi != Semi::Kind::None) {
            requested = {detected.data, (int)detected.size};
        } else if (selection.longTermRisk && oldJava) {
            LongTermRisk().update(risk.oldJava, position, positions, probability, probabilities, *fiftyFifty.oldJava, &oldJavaSemi);
            if (oldJavaSemi.kind != LongTermRisk::SemiResult::Kind::None) {
                semi = oldJavaSemi.kind == LongTermRisk::SemiResult::Kind::Forced ? Semi::Kind::Forced : Semi::Kind::Semi;
                requested = {oldJavaSemi.cells.data, (int)oldJavaSemi.cells.size};
            } else riskPrepared = true;
        }
    }

    const SuggestResult::Reason reason = semi == Semi::Kind::Forced ? SuggestResult::Reason::Forced :
                                         semi == Semi::Kind::Semi ? SuggestResult::Reason::Semi : SuggestResult::Reason::ScoreWins;
    if (java && semi == Semi::Kind::Forced && requested.size < 4) {
        resetSearch();
        SuggestResult result(SuggestResult::Engine::JavaEval, reason);
        for (CellId cell : requested) result.candidates.push_back({cell, 1.0L});
        result.recommend();
        return result;
    }

    const bool compensate = selection.longTermRisk && semi == Semi::Kind::None;
    if (compensate && !riskPrepared)
        FiftyFifty().prepareRisk(risk, position, positions, probability, probabilities, fiftyFifty, fiftyPool,
                                 !specified && !oldJava ? &longTermRisk : nullptr);
    const Grid<long double> *influences = nullptr;
    if (compensate && !specified) influences = oldJava ? &risk.oldJava.influences : &longTermRisk;
    Selection::Result &selected = workspace.selected;
    Selection().select(selected, position, positions, probability, probabilities, selection, requested, influences, oldJava);
    longTermRisk.resize(board.cells.rows(), board.cells.cols(), 0.0L);
    if (compensate)
        for (int i = 0; i < (int)selected.candidates.size; ++i) {
            const Selection::Candidate &candidate = selected.candidates[i];
            const long double mine = candidate.observation.probability[9];
            if (mine == 0.0L || mine == 1.0L) continue;
            const auto [row, col] = board.pos(candidate.cell);
            longTermRisk[row][col] = FiftyFifty().influence(candidate.cell, risk, position, positions, probability, probabilities, fiftyFifty, fiftyPool);
        }

    if (java) {
        resetSearch();
        const JavaEval::Config &javaEval = *config.javaEval;
        const JavaEval::Compensation mode = oldJava ? JavaEval::Compensation::ScaleWithProgress : JavaEval::Compensation::Additive;
        SuggestResult::Details &details = workspace.details;
        SuggestResult result = specified
            ? JavaEval().analyze(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, javaEval, details, mode)
            : JavaEval().analyze(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, javaEval, mode);
        result.reason = reason;
        return result;
    }

    const AnytimeSearch::Config &anytime = *config.anytime;
    AnytimeSearch::Result searched = AnytimeSearch().analyze(position, positions, probability, probabilities, selected, longTermRisk,
                                                             remainingSafe, anytime, searchPool, oldJava);
    if (semi != Semi::Kind::None) searched.reason = reason;
    SuggestResult result = searched.snapshot(searchPool);
    if (search) *search = searched;
    else searchPool.clear(positions);
    return result;
}

} // 命名空间 mss
