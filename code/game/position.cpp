#include "game/position.h"

#include <algorithm>
#include <utility>

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

U128 GameControl::Position::suggestionKey(const ObservedBoard::Result &board, int algorithm) {
    U128Hasher hasher;
    hasher.mix(board.cells.rows());
    hasher.mix(board.cells.cols());
    hasher.mix(board.totalMines);
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col)
            hasher.mix((int)board.cells[row][col]);
    hasher.mix(algorithm);
    return hasher.finalize();
}

U128 GameControl::Position::suggestionKey(const ObservedBoard::Result &board, int algorithm, const JavaEval::Config &config) {
    U128Hasher hasher;
    hasher.mix(board.cells.rows());
    hasher.mix(board.cells.cols());
    hasher.mix(board.totalMines);
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col)
            hasher.mix((int)board.cells[row][col]);
    hasher.mix(algorithm);
    config.hash(hasher);
    return hasher.finalize();
}

U128 GameControl::Position::suggestionKey(const ObservedBoard::Result &board, int algorithm, const JavaBrute::Config &config) {
    U128Hasher hasher;
    hasher.mix(board.cells.rows());
    hasher.mix(board.cells.cols());
    hasher.mix(board.totalMines);
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col)
            hasher.mix((int)board.cells[row][col]);
    hasher.mix(algorithm);
    config.hash(hasher);
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
    // Basic 已经给出安全格时不再算概率；和 1.5 一样提前返回。
    if (safeCells_.size != 0)
        return span<const ObservedBoard::CellId>(safeCells_.data, (int)safeCells_.size);

    updateStructure();
    for (int component = 0; component < (int)state_.structure.components.size; ++component) {
        const Structure::Instance &instance = state_.shapes.instances.list.data[state_.structure.components.data[component]];
        const Structure::Shape &shape = state_.shapes.shapes.list.data[instance.shape];
        const int boxCount = (int)shape.boxSizes.count;
        const int *offsets = state_.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        const Structure::CellId *cells = state_.shapes.instances.cells.data.data + instance.cells.offset;
        const ShapeSolver::DistributionId id = ShapeSolver::analyze(shape, state_.shapes, config_.shapeSolver, state_.distributions, workspace_.probability.shapeSolver);
        const ShapeSolver::Distribution &distribution = state_.distributions.list.data[id];
        const long double *ways = state_.distributions.values.data.data + distribution.ways.offset;
        const long double *expectations = state_.distributions.values.data.data + distribution.expectations.offset;
        for (int box = 0; box < boxCount; ++box) {
            bool alwaysZero = true;
            for (int i = 0; i < (int)distribution.ways.count; ++i)
                if (ways[i] != 0.0L && expectations[i * boxCount + box] != 0.0L) {
                    alwaysZero = false;
                    break;
                }
            if (!alwaysZero)
                continue;
            for (int i = offsets[box]; i < offsets[box + 1]; ++i)
                safeCells_.push_back(cells[i]);
        }
        // 找到一个有零雷 Box 的连通块就返回；和 1.5 一样不再扫后面的连通块。
        if (safeCells_.size != 0)
            return span<const ObservedBoard::CellId>(safeCells_.data, (int)safeCells_.size);
    }

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

ObservedBoard::CellId GameControl::Position::lowRiskMove() {
    const Probability::Result &currentProbability = probability();
    ObservedBoard::CellId safest = -1;
    int safestRow = state_.board.cells.rows();
    int safestCol = -1;
    long double lowestRisk = 2.0L;
    auto consider = [&](ObservedBoard::CellId cell, long double risk) {
        if (risk >= 1.0L)
            return;
        const std::pair<int, int> location = state_.board.pos(cell);
        if (risk < lowestRisk || (risk == lowestRisk && (location.first < safestRow || (location.first == safestRow && location.second > safestCol)))) {
            lowestRisk = risk;
            safestRow = location.first;
            safestCol = location.second;
            safest = cell;
        }
    };
    currentProbability.frontierCells(state_.structure, state_.shapes, consider);
    if (state_.basic.unknownSum != 0)
        for (int row = 0; row < state_.board.cells.rows(); ++row)
            for (int col = 0; col < state_.board.cells.cols(); ++col)
                if (state_.board.cells[row][col] == ObservedBoard::CellState::Hidden && state_.basic.marks[row][col] == Basic::Mark::T)
                    consider(state_.board.id(row, col), currentProbability.tCellProbability);
    // 合法局面只剩确定雷格时，没有可揭开的格，正常返回 -1。
    return safest;
}

void GameControl::Position::suggest(SuggestResult &result, SuggestPool &pool) {
    const span<const ObservedBoard::CellId> safe = safeMoves();
    result.suggest.resize(0);
    if (safe.size > 0) {
        for (int i = 0; i < safe.size; ++i)
            result.suggest.push_back(safe.data[i]);
        result.safeResult = true;
        return;
    }
    const U128 key = suggestionKey(state_.board, 0);
    if (ObservedBoard::CellId *cached = pool.find(key)) {
        result.suggest.push_back(*cached);
        result.safeResult = false;
        return;
    }
    const ObservedBoard::CellId cell = lowRiskMove();
    if (cell >= 0) {
        result.suggest.push_back(cell);
        pool.emplace(key, cell);
    }
    result.safeResult = false;
}

void GameControl::Position::suggestJavaBrute(const JavaBrute::Config &config, JavaBrute::Workspace &workspace, SuggestResult &result, SuggestPool &pool) {
    const span<const ObservedBoard::CellId> safe = safeMoves();
    result.suggest.resize(0);
    if (safe.size > 0) {
        for (int i = 0; i < safe.size; ++i)
            result.suggest.push_back(safe.data[i]);
        result.safeResult = true;
        return;
    }
    const U128 key = suggestionKey(state_.board, 2, config);
    if (ObservedBoard::CellId *cached = pool.find(key)) {
        result.suggest.push_back(*cached);
        result.safeResult = false;
        return;
    }
    probability();
    const JavaBrute::Result choice = JavaBrute::solve(state(), config_.shapeSolver, config, workspace);
    if (choice.cell >= 0) {
        result.suggest.push_back(choice.cell);
        pool.emplace(key, choice.cell);
    }
    result.safeResult = false;
}

void GameControl::Position::suggestJavaEval(const JavaEval::Config &config, JavaEval::Workspace &workspace, SuggestResult &result, SuggestPool &pool) {
    const span<const ObservedBoard::CellId> safe = safeMoves();
    result.suggest.resize(0);
    if (safe.size > 0) {
        for (int i = 0; i < safe.size; ++i)
            result.suggest.push_back(safe.data[i]);
        result.safeResult = true;
        return;
    }
    const U128 key = suggestionKey(state_.board, 1, config);
    if (ObservedBoard::CellId *cached = pool.find(key)) {
        result.suggest.push_back(*cached);
        result.safeResult = false;
        return;
    }
    probability();
    const JavaEval::Result evaluated = JavaEval::analyze(state(), config_.shapeSolver, config, workspace, nullptr);
    if (evaluated.candidates.size == 0) {
        result.safeResult = false;
        return;
    }
    int best = 0;
    for (int i = 1; i < (int)evaluated.candidates.size; ++i)
        if (evaluated.candidates.data[i].value > evaluated.candidates.data[best].value)
            best = i;
    result.suggest.push_back(evaluated.candidates.data[best].cell);
    pool.emplace(key, evaluated.candidates.data[best].cell);
    result.safeResult = false;
}


} // 命名空间 mss

