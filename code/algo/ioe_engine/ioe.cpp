#include "algo/ioe_engine/ioe.h"

#include "algo/ioe_engine/move_proof/move_proof.h"

namespace mss {

struct Ioe::Workspace {
    Combination::Pool combinations;
    Position::Pool position;
    IoeLayoutFilter::Result filtered;

    Workspace() : position(combinations) {}
};

__declspec(noinline) Ioe::Workspace &Ioe::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

Position::CheckedResult Ioe::count(const ObservedBoard::Result &board) {
    Workspace &workspace = getWorkspace();
    Position::Pool &pool = workspace.position;
    pool.clear();
    return Position().analyzeChecked(board, pool, defaultPositionConfig, pool.probabilities);
}

Ioe::FilterResult Ioe::filterLayouts(const ObservedBoard::Result &board, const IoeCells &flags, int spent, const FilterConfig &config) {
    const Position::CheckedResult checked = count(board);
    FilterResult result{Status::Exact, checked.totalWays, {}};
    if (checked.error != Position::CheckError::None) result.status = Status::InvalidBoard;
    else if (checked.totalWays > config.maxLayouts) result.status = Status::TooManyLayouts;
    else {
        IoeView view{{}, flags};
        for (int cell = 0; cell < IoeBoard::cells; ++cell)
            if (ObservedBoard().isNumber(board.cells[cell / IoeBoard::side][cell % IoeBoard::side])) view.revealed.set(cell);
        IoeLayoutFilter().select(board, checked.result.basic, view, spent, config.minIoePercent, config.jointMemory, result.filtered);
    }
    return result;
}

Ioe::Result Ioe::analyze(Position::CheckedResult &checked, Position::Pool &positions, const IoeCells &flags, int spent,
                         const Config &config, IoeObserver<Progress> progress) {
    constexpr int cells = IoeBoard::cells, side = IoeBoard::side;
    Result result{Status::Exact, Reason::None, {}, checked.totalWays, 0, 0, 0, {}};
    if (checked.error != Position::CheckError::None) {
        result.status = Status::InvalidBoard;
        return result;
    }

    Position::Result &position = checked.result;
    const ObservedBoard::Result &board = position.board;
    IoeView view{{}, flags};
    // 危险：证优只读取已揭格的数字。
    std::uint8_t digits[cells];
    for (int cell = 0; cell < cells; ++cell) {
        const int row = cell / side, col = cell % side;
        if (ObservedBoard().isNumber(board.cells[row][col])) {
            view.revealed.set(cell);
            digits[cell] = ObservedBoard().numberValue(board.cells[row][col]);
        }
    }
    if (spent < 100 * (cells - IoeBoard::mines) / config.filter.minIoePercent) {
        const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
        IoeMoveProof::Facts facts{{}, {}, IoeCells(cells)};
        const IoeCells hidden = IoeCells(cells) & ~view.revealed;
        hidden.forEachSetBit([&](int cell) {
            const long double mine = Probability().mineProbability(probability.boxes, positions.probabilities, cell, position.board,
                                                                     position.basic, position.structure);
            if (mine == 1.0L) facts.mines.set(cell);
            else if (mine == 0.0L) facts.safe.set(cell);
        });
        result.move = IoeMoveProof().prove(view, digits, facts);
    }
    if (result.move.kind != IoeMove::Kind::None) {
        result.reason = Reason::Proved;
        if (config.moveOnlyIfProved) {
            result.status = Status::ProofOnly;
            return result;
        }
    }
    if (checked.totalWays > config.filter.maxLayouts) {
        result.status = Status::TooManyLayouts;
        return result;
    }

    IoeLayoutFilter::Progress scanned{};
    progress({Progress::Phase::Enumeration, checked.totalWays, scanned, 0});
    auto reportFilter = [&](const IoeLayoutFilter::Progress &state) {
        scanned = state;
        progress({Progress::Phase::Filtering, checked.totalWays, state, 0});
    };

    IoeLayoutFilter::Result &filtered = getWorkspace().filtered;
    IoeLayoutFilter().select(board, position.basic, view, spent, config.filter.minIoePercent, config.filter.jointMemory, filtered, reportFilter);
    result.eligible = filtered.layouts.size;
    result.rootDomCalls = filtered.domCalls;
    result.rootNodes = filtered.nodes;
    auto reportSearch = [&](std::size_t queries) { progress({Progress::Phase::Search, checked.totalWays, scanned, queries}); };
    result.move = IoeStrategy().solve(filtered.layouts, view, spent, result.move, config.filter.minIoePercent, config.maxQueries, result.strategy, reportSearch);
    if (result.strategy.exhausted) result.status = Status::QueryLimit;
    if (result.move.kind != IoeMove::Kind::None && result.reason == Reason::None) {
        if (result.strategy.reachedUpperBound) result.reason = Reason::Proved;
        else if (!result.strategy.exhausted) result.reason = Reason::Searched;
    }
    return result;
}

} // 命名空间 mss
