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

GameControl::Position::Position(ObservedBoard::Result board, Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo)
    : observedBoard(std::move(board)), orderAlgo_(orderAlgo), workspace_(workspace) {
    initialize(shapes, distributions);
}

void GameControl::Position::initialize(Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions) {
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

    basic_ = Basic::analyze(observedBoard, workspace_.basic);
    structure_ = Structure::analyze(observedBoard, basic_, shapes, workspace_.structure);
    Probability::analyze(observedBoard, basic_, structure_, shapes, distributions, probability_, workspace_.probability, orderAlgo_);
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

void GameControl::Position::update(ObservedBoard::Delta &updates, Structure::Pool &shapes) {
    if (error.failed || updates.changes.empty())
        return;
    for (const ObservedBoard::Delta::Change &change : updates.changes)
        if (change.next < ObservedBoard::CellState::Hidden)
            --remainingSafe_;
    ObservedBoard::update(observedBoard, updates);
    Basic::update(basic_, basicDelta_, observedBoard, updates, workspace_.basic);
    probabilityDirty_ = true;
    Structure::update(structure_, structureDelta_, observedBoard, basic_, shapes, updates, workspace_.structure);
    for (const Basic::Delta::Change &change : basicDelta_.changes) {
        const ObservedBoard::CellId cell = change.cell;
        const std::pair<int, int> position = observedBoard.pos(cell);
        if (!basicSafeSeen_[cell] && observedBoard.board[position.first][position.second] == ObservedBoard::CellState::Hidden && basic_.marks[position.first][position.second] == Basic::Mark::S) {
            basicSafeCells_.push_back(cell);
            basicSafeSeen_[cell] = 1;
        }
    }
}

const Probability::Result &GameControl::Position::probability(Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions) {
    if (error.failed)
        return probability_;
    if (probabilityDirty_) {
        Probability::analyze(observedBoard, basic_, structure_, shapes, distributions, probability_, workspace_.probability, orderAlgo_);
        probabilityDirty_ = false;
        if (probability_.candidates() == 0.0L) {
            error.failed = true;
            error.reason = "观测盘面没有合法的全盘雷分布";
        }
    }
    return probability_;
}

const std::vector<ObservedBoard::CellId> &GameControl::Position::safeMoves(Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions) {
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
        const Structure::Instance &instance = shapes.getInstance(structure_.components[component]);
        const Structure::Shape &shape = shapes.getShape(instance.shape);
        const ShapeSolver::DistributionId distributionId = ShapeSolver::analyze(shape, shapes, distributions, workspace_.shapeSolver, orderAlgo_);
        const ShapeSolver::DistributionResult &distribution = distributions.get(distributionId);
        const std::vector<long double> &ways = distribution.ways();
        if (ways.empty())
            continue;
        const std::span<const int> offsets = instance.boxes.boxOf.span(shapes.boxOf);
        const std::span<const ObservedBoard::CellId> cells = instance.boxes.cells.span(shapes.cells);
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

    const Probability::Result &currentProbability = probability(shapes, distributions);
    if (error.failed)
        return safeCells_;
    currentProbability.frontierCells(structure_, shapes, [&](ObservedBoard::CellId cell, long double risk) {
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
    : mineBoard_(std::make_unique<MineBoard>(std::move(mineBoard))), shapes_(std::make_unique<Structure::Pool>()), distributions_(std::make_unique<ShapeSolver::DistributionPool>()),
      position(std::move(observedBoard), *shapes_, *distributions_, workspace, orderAlgo) {
    if (!mineBoard_->matches(position.observedBoard)) {
        position.error.failed = true;
        position.error.reason = "观测盘面与实际雷盘不一致";
    }
}

GameControl::Game::Game(int rows, int cols, int mines, U128 seed, Workspace &workspace, const ObservedBoard::Result *observedBoard, ShapeSolver::OrderAlgo orderAlgo)
    : mineBoard_(std::make_unique<MineBoard>()), shapes_(std::make_unique<Structure::Pool>()), distributions_(std::make_unique<ShapeSolver::DistributionPool>()),
      position(observedBoard == nullptr ? ObservedBoard::Result(rows, cols, mines) : *observedBoard, *shapes_, *distributions_, workspace, orderAlgo) {
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
    shapes_->clear();
    distributions_->clear();
    position.observedBoard = ObservedBoard::Result(rows, cols, mines);
    position.initialize(*shapes_, *distributions_);
}

void GameControl::Game::update(ObservedBoard::Delta &updates) {
    position.update(updates, *shapes_);
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

const Probability::Result &GameControl::Game::probability() {
    return position.probability(*shapes_, *distributions_);
}

Observe::Result GameControl::Game::observe(ObservedBoard::CellId cell) {
    const Probability::Result &currentProbability = probability();
    return Observe::observe(position.observedBoard, position.basic_, position.structure_, *shapes_, currentProbability, *distributions_, cell, position.workspace_.probability, position.orderAlgo_);
}

long double GameControl::Game::mineProbability(ObservedBoard::CellId cell) {
    return probability().mineProbability(cell, position.observedBoard, position.basic_, position.structure_);
}

const Structure::Pool &GameControl::Game::shapePool() const {
    return *shapes_;
}

std::vector<ObservedBoard::CellId> GameControl::Game::suggest(Position::SuggestAlgo algo) {
    if (algo != Position::SuggestAlgo::Java && algo != Position::SuggestAlgo::LowRisk) {
        assert_(false, "Game::suggest: 无效的建议算法");
#if defined(_MSC_VER)
        __assume(0);
#else
        __builtin_unreachable();
#endif
    }
    const std::vector<ObservedBoard::CellId> &knownSafe = position.safeMoves(*shapes_, *distributions_);
    if (!knownSafe.empty() || position.error.failed)
        return knownSafe;
    if (position.remainingSafe_ == 0)
        return {};

    const Probability::Result &currentProbability = position.probability(*shapes_, *distributions_);
    if (position.error.failed)
        return {};
    ObservedBoard::Delta forcedMines;
    currentProbability.frontierCells(position.structure_, *shapes_, [&](ObservedBoard::CellId cell, long double risk) {
        if (risk == 1.0L)
            forcedMines.changes.push_back({cell, ObservedBoard::CellState::ForcedMine});
    });
    if (currentProbability.tCellProbability() == 1.0L && position.basic_.unknownSum != 0)
        for (int row = 0; row < position.observedBoard.board.rows(); ++row)
            for (int col = 0; col < position.observedBoard.board.cols(); ++col)
                if (position.observedBoard.board[row][col] == ObservedBoard::CellState::Hidden && position.basic_.marks[row][col] == Basic::Mark::T)
                    forcedMines.changes.push_back({position.observedBoard.id(row, col), ObservedBoard::CellState::ForcedMine});
    if (!forcedMines.changes.empty()) {
        update(forcedMines);
        const std::vector<ObservedBoard::CellId> &newlySafe = position.safeMoves(*shapes_, *distributions_);
        if (!newlySafe.empty() || position.error.failed)
            return newlySafe;
    }

    const Probability::Result &updatedProbability = position.probability(*shapes_, *distributions_);
    if (position.error.failed)
        return {};
    if (algo == Position::SuggestAlgo::Java) {
        if (updatedProbability.candidates() < 5000.0L) {
            const BruteForce::Result result = BruteForce::solve(position.observedBoard, position.basic_, position.structure_, *shapes_, {false, 1, BruteForce::Solver::Bitwise}, position.workspace_.bruteForce);
            if (!result.moves.empty())
                return {result.moves.front().cell};
        }
        const LongTermRiskReference::Config riskConfig{};
        const LongTermRiskReference::Influence risk = LongTermRiskReference::findInfluence(position.observedBoard, position.basic_, position.structure_, updatedProbability, *shapes_, *distributions_, {}, riskConfig, position.workspace_.longTermRisk);
        const JavaEvaluate::Config evaluateConfig{};
        const JavaEvaluate::Result result = JavaEvaluate::solve(position.observedBoard, position.basic_, position.structure_, updatedProbability, *shapes_, *distributions_, risk, {}, evaluateConfig, position.workspace_.javaEvaluate);
        if (result.x == 0 || result.y == 0)
            return {};
        return {position.observedBoard.id(result.x - 1, result.y - 1)};
    }
    ObservedBoard::CellId safest = -1;
    int safestRow = position.observedBoard.board.rows();
    int safestCol = -1;
    long double lowestRisk = 2.0L;
    auto consider = [&](ObservedBoard::CellId cell, long double risk) {
        const std::pair<int, int> location = position.observedBoard.pos(cell);
        if (risk < lowestRisk || (risk == lowestRisk && (location.first < safestRow || (location.first == safestRow && location.second > safestCol)))) {
            lowestRisk = risk;
            safestRow = location.first;
            safestCol = location.second;
            safest = cell;
        }
    };
    updatedProbability.frontierCells(position.structure_, *shapes_, [&](ObservedBoard::CellId cell, long double risk) { consider(cell, risk); });
    if (position.basic_.unknownSum != 0)
        for (int row = 0; row < position.observedBoard.board.rows(); ++row)
            for (int col = 0; col < position.observedBoard.board.cols(); ++col)
                if (position.observedBoard.board[row][col] == ObservedBoard::CellState::Hidden && position.basic_.marks[row][col] == Basic::Mark::T)
                    consider(position.observedBoard.id(row, col), updatedProbability.tCellProbability());
    assert_(safest >= 0, "Game::suggest: 未获胜但没有可建议的隐藏格");
    return {safest};
}

void GameControl::Game::playUntilNoSafe() {
    ObservedBoard::Delta updates;
    for (;;) {
        const std::vector<ObservedBoard::CellId> &safe = position.safeMoves(*shapes_, *distributions_);
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
