#include "game/game.h"

#include <cstdint>
#include <span>
#include <utility>

#include "core/assert.h"
#include "core/utility/neighbors.h"
#include "core/utility/rng.h"

namespace mss {

void GameControl::MineBoard::generate(int rows, int cols, int mines, U128 seed) {
    board_.resize(rows, cols, 0);
    const int cellCount = rows * cols;
    std::vector<int> cells(cellCount);
    for (int cell = 0; cell < cellCount; ++cell)
        cells[cell] = cell;

    Random random(seed.lo, seed.hi);
    for (int i = cellCount - 1; i > 0; --i) {
        const int j = (int)(random.next() % (std::uint64_t)(i + 1));
        std::swap(cells[i], cells[j]);
    }
    for (int i = 0; i < mines; ++i) {
        const int cell = cells[i];
        board_[cell / cols][cell % cols] = 1;
    }
}

void GameControl::MineBoard::makeSafe(int row, int col, int targetRow, int targetCol) {
    if (!board_[row][col])
        return;
    board_[row][col] = 0;
    board_[targetRow][targetCol] = 1;
}

int GameControl::MineBoard::number(int row, int col) const {
    if (board_[row][col])
        return 9;
    int result = 0;
    forEachAdjacent(row, col, board_.rows(), board_.cols(), [&](int neighborRow, int neighborCol) { result += board_[neighborRow][neighborCol]; });
    return result;
}

bool GameControl::MineBoard::matches(const ObservedBoard::Result &observedBoard) const {
    if (board_.rows() != observedBoard.board.rows() || board_.cols() != observedBoard.board.cols())
        return false;
    int mines = 0;
    for (int row = 0; row < board_.rows(); ++row)
        for (int col = 0; col < board_.cols(); ++col) {
            const bool mine = board_[row][col] != 0;
            const ObservedBoard::CellState state = observedBoard.board[row][col];
            if (mine)
                ++mines;
            if (state == ObservedBoard::CellState::Hidden)
                continue;
            if (state == ObservedBoard::CellState::ForcedMine) {
                if (!mine)
                    return false;
                continue;
            }
            if (state == ObservedBoard::CellState::ForcedSafe) {
                if (mine)
                    return false;
                continue;
            }
            if (mine || number(row, col) != (int)state)
                return false;
        }
    return mines == observedBoard.totalMines;
}

GameControl::Position::Position(ObservedBoard::Result board, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo)
    : observedBoard(std::move(board)), orderAlgo_(orderAlgo), workspace_(workspace) {
    initialize(Basic::analyze(observedBoard, workspace_.basic));
}

GameControl::Position::Position(ObservedBoard::Result board, Basic::Result basic, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo)
    : observedBoard(std::move(board)), orderAlgo_(orderAlgo), workspace_(workspace) {
    initialize(std::move(basic));
}

ShapeSolver::OrderAlgo GameControl::Position::orderAlgo() const {
    return orderAlgo_;
}

void GameControl::Position::setOrderAlgo(ShapeSolver::OrderAlgo orderAlgo) {
    if (orderAlgo_ == orderAlgo)
        return;
    orderAlgo_ = orderAlgo;
    distributions_.clear();
    probabilityDirty_ = true;
}

void GameControl::Position::initialize(Basic::Result basic) {
    shapes_.clear();
    distributions_.clear();
    error = {};
    safeCells_.clear();
    basicSafeCells_.clear();
    structure_ = Structure::Result{};
    probability_ = Probability::Result{};
    probabilityDirty_ = false;
    const int rows = observedBoard.board.rows();
    const int cols = observedBoard.board.cols();
    basicSafeSeen_.assign(rows * cols, 0);
    remainingSafe_ = rows * cols - observedBoard.totalMines;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (observedBoard.board[row][col] < ObservedBoard::CellState::Hidden)
                --remainingSafe_;

    basic_ = std::move(basic);
    structure_ = Structure::analyze(observedBoard, basic_, shapes_, workspace_.structure);
    Probability::analyze(observedBoard, basic_, structure_, shapes_, distributions_, probability_, workspace_.probability, orderAlgo_);
    if (probability_.candidates() == 0.0L) {
        error.failed = true;
        error.reason = "观测盘面没有合法的全盘雷分布";
        return;
    }
    probabilityDirty_ = false;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (observedBoard.board[row][col] == ObservedBoard::CellState::Hidden && basic_.marks[row][col] == Basic::Mark::S) {
                const ObservedBoard::CellId cell = observedBoard.id(row, col);
                basicSafeCells_.push_back(cell);
                basicSafeSeen_[cell] = 1;
            }
}

const Basic::Result &GameControl::Position::basic() const {
    return basic_;
}

const Structure::Result &GameControl::Position::structure() const {
    return structure_;
}

void GameControl::Position::update(ObservedBoard::Delta &updates) {
    if (error.failed || updates.changes.empty())
        return;
    for (const ObservedBoard::Delta::Change &change : updates.changes)
        if (change.next < ObservedBoard::CellState::Hidden)
            --remainingSafe_;
    ObservedBoard::update(observedBoard, updates);
    Basic::update(basic_, basicDelta_, observedBoard, updates, workspace_.basic);
    probabilityDirty_ = true;
    Structure::update(structure_, structureDelta_, observedBoard, basic_, shapes_, updates, workspace_.structure);
    for (const Basic::Delta::Change &change : basicDelta_.changes) {
        const ObservedBoard::CellId cell = change.cell;
        const std::pair<int, int> position = observedBoard.pos(cell);
        if (!basicSafeSeen_[cell] && observedBoard.board[position.first][position.second] == ObservedBoard::CellState::Hidden && basic_.marks[position.first][position.second] == Basic::Mark::S) {
            basicSafeCells_.push_back(cell);
            basicSafeSeen_[cell] = 1;
        }
    }
}

const Probability::Result &GameControl::Position::probability() {
    if (error.failed)
        return probability_;
    if (probabilityDirty_) {
        Probability::analyze(observedBoard, basic_, structure_, shapes_, distributions_, probability_, workspace_.probability, orderAlgo_);
        probabilityDirty_ = false;
        if (probability_.candidates() == 0.0L) {
            error.failed = true;
            error.reason = "观测盘面没有合法的全盘雷分布";
        }
    }
    return probability_;
}

const std::vector<ObservedBoard::CellId> &GameControl::Position::safeMoves() {
    safeCells_.clear();
    if (error.failed)
        return safeCells_;
    for (int i = 0; i < (int)basicSafeCells_.size(); ++i) {
        const ObservedBoard::CellId cell = basicSafeCells_[i];
        const std::pair<int, int> position = observedBoard.pos(cell);
        if (observedBoard.board[position.first][position.second] == ObservedBoard::CellState::Hidden && basic_.marks[position.first][position.second] == Basic::Mark::S)
            safeCells_.push_back(cell);
    }
    basicSafeCells_ = safeCells_;
    if (!safeCells_.empty())
        return safeCells_;

    for (int component = 0; component < (int)structure_.components.size(); ++component) {
        const Structure::Instance &instance = shapes_.getInstance(structure_.components[component]);
        const Structure::Shape &shape = shapes_.getShape(instance.shape);
        const ShapeSolver::DistributionId distributionId = ShapeSolver::analyze(shape, shapes_, distributions_, workspace_.shapeSolver, orderAlgo_);
        const ShapeSolver::DistributionResult &distribution = distributions_.get(distributionId);
        const std::vector<long double> &ways = distribution.ways();
        if (ways.empty())
            continue;
        const std::span<const int> offsets = instance.boxes.boxOf.span(shapes_.boxOf);
        const std::span<const ObservedBoard::CellId> cells = instance.boxes.cells.span(shapes_.cells);
        for (int box = 0; box < distribution.boxCount(); ++box) {
            bool safe = true;
            for (int i = 0; i < (int)ways.size(); ++i)
                if (ways[i] != 0.0L && distribution.perBoxExpectation(i)[box] != 0.0L) {
                    safe = false;
                    break;
                }
            if (!safe)
                continue;
            for (int i = offsets[box]; i < offsets[box + 1]; ++i)
                safeCells_.push_back(cells[i]);
        }
        if (!safeCells_.empty())
            return safeCells_;
    }

    const Probability::Result &currentProbability = probability();
    if (error.failed)
        return safeCells_;
    currentProbability.frontierCells(structure_, shapes_, [&](ObservedBoard::CellId cell, long double risk) {
        if (risk == 0.0L)
            safeCells_.push_back(cell);
    });
    if (currentProbability.tCellProbability() == 0.0L && basic_.unknownSum != 0)
        for (int row = 0; row < observedBoard.board.rows(); ++row)
            for (int col = 0; col < observedBoard.board.cols(); ++col)
                if (observedBoard.board[row][col] == ObservedBoard::CellState::Hidden && basic_.marks[row][col] == Basic::Mark::T)
                    safeCells_.push_back(observedBoard.id(row, col));
    return safeCells_;
}

GameControl::Game::Game(MineBoard mineBoard, ObservedBoard::Result observedBoard, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo)
    : mineBoard_(std::make_unique<MineBoard>(std::move(mineBoard))),
      position(std::move(observedBoard), workspace, orderAlgo) {
    if (!mineBoard_->matches(position.observedBoard)) {
        position.error.failed = true;
        position.error.reason = "观测盘面与实际雷盘不一致";
    }
}

GameControl::Game::Game(int rows, int cols, int mines, U128 seed, Workspace &workspace, const ObservedBoard::Result *observedBoard, ShapeSolver::OrderAlgo orderAlgo)
    : mineBoard_(std::make_unique<MineBoard>()),
      position(observedBoard == nullptr ? ObservedBoard::Result(rows, cols, mines) : *observedBoard, workspace, orderAlgo) {
    mineBoard_->generate(rows, cols, mines, seed);
    if (observedBoard != nullptr && !mineBoard_->matches(position.observedBoard)) {
        position.error.failed = true;
        position.error.reason = "观测盘面与实际雷盘不一致";
    }
}

void GameControl::Game::reset(U128 seed) {
    const int rows = position.observedBoard.board.rows();
    const int cols = position.observedBoard.board.cols();
    const int mines = position.observedBoard.totalMines;
    mineBoard_->generate(rows, cols, mines, seed);
    position.observedBoard = ObservedBoard::Result(rows, cols, mines);
    position.initialize(Basic::analyze(position.observedBoard, position.workspace_.basic));
}

void GameControl::Game::update(ObservedBoard::Delta &updates) {
    position.update(updates);
}

void GameControl::Game::makeFirstMoveSafe(ObservedBoard::CellId cell, ObservedBoard::CellId safeCell) {
    const std::pair<int, int> position = this->position.observedBoard.pos(cell);
    if (mineBoard_->number(position.first, position.second) == 9) {
        const std::pair<int, int> safePosition = this->position.observedBoard.pos(safeCell);
        mineBoard_->makeSafe(position.first, position.second, safePosition.first, safePosition.second);
    }
}

int GameControl::Game::number(int row, int col) const {
    return mineBoard_->number(row, col);
}

bool GameControl::Game::won() const {
    return position.remainingSafe_ == 0;
}

Observe::Result GameControl::Position::observe(ObservedBoard::CellId cell) {
    const Probability::Result &currentProbability = probability();
    return Observe::observe(observedBoard, basic_, structure_, shapes_, currentProbability, distributions_, cell, workspace_.probability, orderAlgo_);
}

long double GameControl::Position::mineProbability(ObservedBoard::CellId cell) {
    return probability().mineProbability(cell, observedBoard, basic_, structure_);
}

const Structure::Pool &GameControl::Position::shapePool() const {
    return shapes_;
}

GameControl::Position::SuggestResult GameControl::Position::suggest(const Config &config) {
    if (config.position.algo != Config::Position::Algo::Java && config.position.algo != Config::Position::Algo::LowRisk) {
        assert_(false, "Position::suggest: 无效的建议算法");
        std::exit(1);
    }
    SuggestResult suggestion;
    const std::vector<ObservedBoard::CellId> &knownSafe = safeMoves();
    if (!knownSafe.empty() || error.failed) {
        suggestion.cells = knownSafe;
        return suggestion;
    }
    if (remainingSafe_ == 0)
        return {};

    const Probability::Result &currentProbability = probability();
    if (error.failed)
        return {};
    ObservedBoard::Delta forcedMines;
    currentProbability.frontierCells(structure_, shapes_, [&](ObservedBoard::CellId cell, long double risk) {
        if (risk == 1.0L)
            forcedMines.changes.push_back({cell, ObservedBoard::CellState::ForcedMine});
    });
    if (currentProbability.tCellProbability() == 1.0L && basic_.unknownSum != 0)
        for (int row = 0; row < observedBoard.board.rows(); ++row)
            for (int col = 0; col < observedBoard.board.cols(); ++col)
                if (observedBoard.board[row][col] == ObservedBoard::CellState::Hidden && basic_.marks[row][col] == Basic::Mark::T)
                    forcedMines.changes.push_back({observedBoard.id(row, col), ObservedBoard::CellState::ForcedMine});
    if (!forcedMines.changes.empty()) {
        update(forcedMines);
        const std::vector<ObservedBoard::CellId> &newlySafe = safeMoves();
        if (!newlySafe.empty() || error.failed) {
            suggestion.cells = newlySafe;
            return suggestion;
        }
    }

    const Probability::Result &updatedProbability = probability();
    if (error.failed)
        return {};
    if (config.position.algo == Config::Position::Algo::Java) {
        if (updatedProbability.candidates() <= config.position.bruteForceThreshold) {
            suggestion.bruteForce = BruteForce::solve(observedBoard, basic_, structure_, shapes_, config, workspace_.bruteForce);
            if (!suggestion.bruteForce.moves.empty()) {
                const BruteForce::Result::Move *best = &suggestion.bruteForce.moves.front();
                for (const BruteForce::Result::Move &move : suggestion.bruteForce.moves)
                    if (move.wins > best->wins)
                        best = &move;
                suggestion.engine = SuggestResult::Engine::BruteForce;
                suggestion.cells = {best->cell};
                return suggestion;
            }
        }
        const LongTermRiskReference::Influence risk = LongTermRiskReference::findInfluence(observedBoard, basic_, structure_, updatedProbability, shapes_, distributions_, {}, config, workspace_.longTermRisk);
        const JavaEvaluate::Result result = JavaEvaluate::solve(observedBoard, basic_, structure_, updatedProbability, shapes_, distributions_, risk, {}, config, workspace_.javaEvaluate);
        suggestion.engine = SuggestResult::Engine::JavaEval;
        if (result.x == 0 || result.y == 0)
            return suggestion;
        suggestion.cells = {observedBoard.id(result.x - 1, result.y - 1)};
        for (const JavaEvaluate::Candidate &candidate : result.candidates)
            suggestion.candidates.push_back(observedBoard.id(candidate.x - 1, candidate.y - 1));
        return suggestion;
    }
    ObservedBoard::CellId safest = -1;
    int safestRow = observedBoard.board.rows();
    int safestCol = -1;
    long double lowestRisk = 2.0L;
    auto consider = [&](ObservedBoard::CellId cell, long double risk) {
        const std::pair<int, int> location = observedBoard.pos(cell);
        if (risk < lowestRisk || (risk == lowestRisk && (location.first < safestRow || (location.first == safestRow && location.second > safestCol)))) {
            lowestRisk = risk;
            safestRow = location.first;
            safestCol = location.second;
            safest = cell;
        }
    };
    updatedProbability.frontierCells(structure_, shapes_, [&](ObservedBoard::CellId cell, long double risk) { consider(cell, risk); });
    if (basic_.unknownSum != 0)
        for (int row = 0; row < observedBoard.board.rows(); ++row)
            for (int col = 0; col < observedBoard.board.cols(); ++col)
                if (observedBoard.board[row][col] == ObservedBoard::CellState::Hidden && basic_.marks[row][col] == Basic::Mark::T)
                    consider(observedBoard.id(row, col), updatedProbability.tCellProbability());
    assert_(safest >= 0, "Position::suggest: 未获胜但没有可建议的隐藏格");
    suggestion.engine = SuggestResult::Engine::LowRisk;
    suggestion.cells = {safest};
    return suggestion;
}

void GameControl::Game::playUntilNoSafe() {
    ObservedBoard::Delta updates;
    for (;;) {
        const std::vector<ObservedBoard::CellId> &safe = position.safeMoves();
        if (safe.empty())
            return;
        updates.clear();
        for (ObservedBoard::CellId cell : safe) {
            const std::pair<int, int> location = position.observedBoard.pos(cell);
            updates.changes.push_back({cell, (ObservedBoard::CellState)mineBoard_->number(location.first, location.second)});
        }
        update(updates);
    }
}

} // namespace mss
