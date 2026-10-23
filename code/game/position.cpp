#include "game/position.h"

#include "algo/winrate_engine/basic/lowrisk/lowrisk.h"

#include <algorithm>
#include <utility>

#include "core/assert.h"

namespace mss {

void GameControl::Position::initialize() {
    error_.clear();
    state_.probabilityValid = false;
    state_.structureLazyUpdate.resize(0);
    safeCells_.resize(0);
    state_.shapes = Structure::Pool();
    state_.distributions.clear();
    state_.basicPool = Basic::Pool();
    Structure::analyze(state_.structure, state_.board, state_.basic, state_.shapes, workspace_.structure);
    basicSafeCells_.resize(0);
    basicSafeSeen_.resize(state_.board.cells.rows() * state_.board.cells.cols());
    std::fill(basicSafeSeen_.data, basicSafeSeen_.data + basicSafeSeen_.size, 0);
    for (int row = 0; row < state_.board.cells.rows(); ++row)
        for (int col = 0; col < state_.board.cells.cols(); ++col) {
            const ObservedBoard::CellState cell = state_.board.cells[row][col];
            if ((cell == ObservedBoard::CellState::Hidden || cell == ObservedBoard::CellState::ForcedSafe) && state_.basic.marks[row][col] == Basic::Mark::S) {
                const ObservedBoard::CellId id = state_.board.id(row, col);
                basicSafeCells_.push_back(id);
                basicSafeSeen_.data[id] = 1;
            }
        }
}

void GameControl::Position::reset(const ObservedBoard::Result &board) {
    state_.board = board;
    Basic::analyze(state_.basic, state_.board, workspace_.basic);
    initialize();
}

void GameControl::Position::update(ObservedBoard::Delta &delta) {
    ObservedBoard::apply(state_.board, delta, state_.basicPool.board);
    const Basic::Delta basicDelta = Basic::update(state_.basic, state_.board, delta, state_.basicPool, workspace_.basic);
    for (int i = 0; i < (int)delta.changes.count; ++i)
        state_.structureLazyUpdate.push_back(state_.basicPool.board.changes.data.data[delta.changes.offset + i].cell);
    for (int i = 0; i < (int)basicDelta.changes.count; ++i) {
        const ObservedBoard::CellId cell = state_.basicPool.changes.data.data[basicDelta.changes.offset + i].cell;
        const std::pair<int, int> location = state_.board.pos(cell);
        const ObservedBoard::CellState observed = state_.board.cells[location.first][location.second];
        if (!basicSafeSeen_.data[cell] && (observed == ObservedBoard::CellState::Hidden || observed == ObservedBoard::CellState::ForcedSafe) && state_.basic.marks[location.first][location.second] == Basic::Mark::S) {
            basicSafeSeen_.data[cell] = 1;
            basicSafeCells_.push_back(cell);
        }
    }
    state_.probabilityValid = false;
    state_.basicPool.changes.pop_back(basicDelta.changes);
}

void GameControl::Position::updateStructure() {
    if (state_.structureLazyUpdate.size == 0)
        return;
    const Structure::Delta delta = Structure::update(state_.structure, state_.board, state_.basic, state_.shapes, state_.structureLazyUpdate.data, (int)state_.structureLazyUpdate.size, workspace_.structure);
    state_.shapes.deltas.removed.pop_back(delta.removed);
    state_.shapes.deltas.added.pop_back(delta.added);
    state_.structureLazyUpdate.resize(0);
}

GameControl::Position::Position(const ObservedBoard::Result &board, Workspace &workspace, const Config &config, Check check)
    : state_(board), error_(), workspace_(workspace), config_(config) {
    Basic::analyze(state_.basic, state_.board, workspace_.basic);
    // trusted 盘面由 Game 自己从合法根局面和抽样雷盘构造，不再重复校验。
    if (check == Check::User) {
        if (!Basic::checkValid(state_.board, state_.basic)) {
            error_ = "基础逻辑冲突";
            return;
        }
    }
    initialize();
    if (check == Check::User) {
        for (int component = 0; component < (int)state_.structure.components.size; ++component) {
            const Structure::Instance &instance = state_.shapes.instances.list.data[state_.structure.components.data[component]];
            const Structure::Shape &shape = state_.shapes.shapes.list.data[instance.shape];
            const ShapeSolver::DistributionId id = ShapeSolver::analyze(shape, state_.shapes, config_.shapeSolver, state_.distributions, workspace_.probability.shapeSolver);
            if (state_.distributions.list.data[id].ways.count == 0) {
                error_ = "某连通块无解";
                return;
            }
        }
        if (probability().candidates == 0.0L)
            error_ = "连通块都有解，却与雷数冲突";
    }
}

GameControl::Position GameControl::Position::fromUserInput(const ObservedBoard::Result &board, Workspace &workspace, const Config &config) {
    return Position(board, workspace, config, Check::User);
}

U128 GameControl::Position::suggestionKey(const ObservedBoard::Result &board, const MovePolicy &policy) {
    U128Hasher hasher;
    hasher.mix(board.cells.rows()); hasher.mix(board.cells.cols()); hasher.mix(board.totalMines);
    for (int row = 0; row < board.cells.rows(); ++row) for (int col = 0; col < board.cells.cols(); ++col) hasher.mix((int)board.cells[row][col]);
    policy.hash(hasher);
    return hasher.finalize();
}

GameControl::Position GameControl::Position::assumeLegal(const ObservedBoard::Result &board, Workspace &workspace, const Config &config) {
    return Position(board, workspace, config, Check::Trusted);
}

const ObservedBoard::Result &GameControl::Position::observedBoard() const { return state_.board; }

const std::string &GameControl::Position::error() const { return error_; }

bool GameControl::Position::valid() const { return error_.empty(); }

const Basic::Result &GameControl::Position::basic() const { return state_.basic; }

const Structure::Result &GameControl::Position::structure() {
    updateStructure();
    return state_.structure;
}

const Structure::Pool &GameControl::Position::shapePool() {
    updateStructure();
    return state_.shapes;
}

const Probability::Result &GameControl::Position::probability() {
    if (!state_.probabilityValid) {
        updateStructure();
        Probability::analyze(state_.board, state_.basic, state_.structure, state_.shapes, config_.shapeSolver, state_.distributions, state_.probability, workspace_.probability);
        state_.probabilityValid = true;
    }
    return state_.probability;
}

PositionState &GameControl::Position::state() {
    probability();
    return state_;
}

long double GameControl::Position::mineProbability(ObservedBoard::CellId cell) {
    const std::pair<int, int> location = state_.board.pos(cell);
    const Basic::Mark mark = state_.basic.marks[location.first][location.second];
    if (mark == Basic::Mark::S)
        return 0.0L;
    if (mark == Basic::Mark::F)
        return 1.0L;
    return Probability::mineProbability(probability(), cell, state_.board, state_.basic, state_.structure);
}

void GameControl::Position::observe(ObservedBoard::CellId cell, Observe::Result &result) {
    const Probability::Result &currentProbability = probability();
    Observe::observe(state_.board, state_.basic, state_.structure, state_.shapes, currentProbability, state_.distributions, config_.shapeSolver, cell, result, workspace_.probability);
}

span<const ObservedBoard::CellId> GameControl::Position::safeMoves() {
    safeCells_.resize(0);
    for (int i = 0; i < (int)basicSafeCells_.size; ++i) {
        const ObservedBoard::CellId cell = basicSafeCells_.data[i];
        const std::pair<int, int> location = state_.board.pos(cell);
        const ObservedBoard::CellState observed = state_.board.cells[location.first][location.second];
        if ((observed == ObservedBoard::CellState::Hidden || observed == ObservedBoard::CellState::ForcedSafe) && state_.basic.marks[location.first][location.second] == Basic::Mark::S)
            safeCells_.push_back(cell);
    }
    // Basic 已给出安全格时直接返回；其余统一检查全盘概率。
    if (safeCells_.size != 0)
        return span<const ObservedBoard::CellId>(safeCells_.data, (int)safeCells_.size);

    const Probability::Result &currentProbability = probability();
    currentProbability.frontierCells(state_.structure, state_.shapes, [&](ObservedBoard::CellId cell, long double risk) {
        if (risk == 0.0L)
            safeCells_.push_back(cell);
    });
    if (state_.basic.unknownSum != 0 && currentProbability.tCellProbability == 0.0L)
        for (int row = 0; row < state_.board.cells.rows(); ++row)
            for (int col = 0; col < state_.board.cells.cols(); ++col)
                if (state_.board.cells[row][col] == ObservedBoard::CellState::Hidden && state_.basic.marks[row][col] == Basic::Mark::T)
                    safeCells_.push_back(state_.board.id(row, col));
    return span<const ObservedBoard::CellId>(safeCells_.data, (int)safeCells_.size);
}

void GameControl::Position::suggest(const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &searchPool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, SuggestResult &result, const vector<ObservedBoard::CellId> *cells, AnytimeSearch::Result *search) {
    result = SuggestResult();
    if (search) *search = AnytimeSearch::Result();
    const span<const ObservedBoard::CellId> safe = safeMoves();
    if (safe.size != 0) {
        result.engine = SuggestResult::Engine::Safe; result.reason = SuggestResult::Reason::Safe;
        for (int i = 0; i < safe.size; ++i) { result.suggest.push_back(safe.data[i]); result.candidates.push_back(SuggestResult::Candidate(safe.data[i], 1)); }
        return;
    }
    suggest(state(), policy, fiftyPool, searchPool, workspace, bruteForce, result, cells, search);
}

void GameControl::Position::suggest(PositionState &current, const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &searchPool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, SuggestResult &result, const vector<ObservedBoard::CellId> *cells, AnytimeSearch::Result *search) {
    int remaining = current.board.cells.rows() * current.board.cells.cols() - current.board.totalMines;
    for (int row = 0; row < current.board.cells.rows(); ++row) for (int col = 0; col < current.board.cells.cols(); ++col)
        if (current.board.cells[row][col] <= ObservedBoard::CellState::Num8) --remaining;
    if (remaining == 0) return;
    if (policy.brute.limit != 0 && current.probability.candidates < policy.brute.limit) {
        BruteForce::Config config = *policy.brute.config;
        if (cells) config.checkAllMoves = true;
        BruteForce::solve(current.board, current.basic, current.structure, current.shapes, config, result, bruteForce);
        if (cells) {
            int count = 0;
            for (int i = 0; i < (int)result.candidates.size; ++i)
                if (std::find(cells->data, cells->data + cells->size, result.candidates.data[i].cell) != cells->data + cells->size) result.candidates.data[count++] = result.candidates.data[i];
            result.candidates.size = count; result.reason = SuggestResult::Reason::Specified; result.recommend();
        }
    } else if (policy.basic.algorithm == MovePolicy::Basic::Algorithm::LowRisk) {
        SuggestResult evaluated = LowRisk::analyze(current, cells);
        result.swap(evaluated);
    } else {
        const FiftyFifty::Config fifty(policy.fiftyFifty.algorithm == MovePolicy::FiftyFifty::Algorithm::OldJava ? policy.fiftyFifty.oldJava : nullptr, policy.fiftyFifty.algorithm == MovePolicy::FiftyFifty::Algorithm::Templates ? policy.fiftyFifty.templates : nullptr, policy.fiftyFifty.risk);
        if (policy.basic.algorithm == MovePolicy::Basic::Algorithm::JavaEval) {
            SuggestResult evaluated = JavaEval::analyze(current, config_.shapeSolver, *policy.basic.javaEval, fifty, fiftyPool, workspace.javaEval, cells);
            result.swap(evaluated);
        } else {
            const AnytimeSearch::Result searched = AnytimeSearch::analyze(current, searchPool, config_.shapeSolver, *policy.basic.anytime, fifty, fiftyPool, workspace, cells);
            SuggestResult evaluated = searched.snapshot(searchPool);
            result.swap(evaluated);
            if (search) *search = searched;
        }
    }
}

void GameControl::Position::suggest(const MovePolicy &policy, SuggestPool &pool, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &searchPool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, vector<ObservedBoard::CellId> &result) {
    result.resize(0);
    const span<const ObservedBoard::CellId> safe = safeMoves();
    if (safe.size != 0) { for (int i = 0; i < safe.size; ++i) result.push_back(safe.data[i]); return; }
    const U128 key = suggestionKey(state_.board, policy);
    if (ObservedBoard::CellId *cached = pool.cache.find(key)) { result.push_back(*cached); return; }
    SuggestResult evaluated;
    suggest(state(), policy, fiftyPool, searchPool, workspace, bruteForce, evaluated, nullptr, nullptr);
    result.swap(evaluated.suggest);
    if (result.size != 0) pool.cache.emplace(key, result.data[0]);
}

} // 命名空间 mss
