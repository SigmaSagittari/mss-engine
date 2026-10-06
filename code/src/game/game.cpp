#include "game/game.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "core/utility/neighbors.h"
#include "core/utility/rng.h"
#include "core/workspace.h"

namespace mss {

GameControl::MineBoard::MineBoard(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed) {
    generate(rows, cols, mines, firstRow, firstCol, seed);
}

void GameControl::MineBoard::generate(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed) {
    board_.resize(rows, cols, 0);
    const int cellCount = rows * cols;
    const int firstCell = firstRow * cols + firstCol;
    std::vector<int> cells(cellCount - 1);
    int index = 0;
    for (int cell = 0; cell < cellCount; ++cell)
        if (cell != firstCell)
            cells[index++] = cell;

    Random random(seed.lo, seed.hi);
    for (int i = (int)cells.size() - 1; i > 0; --i) {
        const int j = (int)(random.next() % (std::uint64_t)(i + 1));
        std::swap(cells[i], cells[j]);
    }
    for (int i = 0; i < mines; ++i) {
        const int cell = cells[i];
        board_[cell / cols][cell % cols] = 1;
    }
}

int GameControl::MineBoard::number(int row, int col) const {
    if (board_[row][col])
        return 9;
    int result = 0;
    forEachAdjacent(row, col, board_.rows(), board_.cols(), [&](int neighborRow, int neighborCol) { result += board_[neighborRow][neighborCol]; });
    return result;
}

void GameControl::Position::initialize() {
    error_ = {};
    safeCells_.clear();
    basicSafeCells_.clear();
    basicSafeSeen_.assign(observedBoard_.board.rows() * observedBoard_.board.cols(), 0);
    result_.probabilityDirty = true;
    delta_.basic.changes.clear();
    delta_.structure.removed.clear();
    delta_.structure.addedCount = 0;
    structureLazyUpdate_.clear();
    pool_.shapes.clear();
    pool_.distributions.clear();
    result_.structure = Structure::analyze(observedBoard_, result_.basic, pool_.shapes, workspace_.structure);
    for (int row = 0; row < observedBoard_.board.rows(); ++row)
        for (int col = 0; col < observedBoard_.board.cols(); ++col)
            if ((observedBoard_.board[row][col] == ObservedBoard::CellState::Hidden || observedBoard_.board[row][col] == ObservedBoard::CellState::ForcedSafe) && result_.basic.marks[row][col] == Basic::Mark::S) {
                const ObservedBoard::CellId cell = observedBoard_.id(row, col);
                basicSafeCells_.push_back(cell);
                basicSafeSeen_[cell] = 1;
            }
}

void GameControl::Position::reset(ObservedBoard::Result board) {
    observedBoard_ = std::move(board);
    result_.basic = Basic::analyze(observedBoard_, workspace_.basic);
    initialize();
}

void GameControl::Position::update(ObservedBoard::Delta &updates) {
    ObservedBoard::update(observedBoard_, updates);
    Basic::update(result_.basic, delta_.basic, observedBoard_, updates, workspace_.basic);
    for (const ObservedBoard::Delta::Change &change : updates.changes)
        structureLazyUpdate_.push_back(change.cell);
    result_.probabilityDirty = true;
    for (const Basic::Delta::Change &change : delta_.basic.changes) {
        const ObservedBoard::CellId cell = change.cell;
        const std::pair<int, int> position = observedBoard_.pos(cell);
        const ObservedBoard::CellState state = observedBoard_.board[position.first][position.second];
        if (!basicSafeSeen_[cell] && (state == ObservedBoard::CellState::Hidden || state == ObservedBoard::CellState::ForcedSafe) && result_.basic.marks[position.first][position.second] == Basic::Mark::S) {
            basicSafeCells_.push_back(cell);
            basicSafeSeen_[cell] = 1;
        }
    }
}

void GameControl::Position::updateStructure() const {
    if (structureLazyUpdate_.empty())
        return;
    Structure::update(result_.structure, delta_.structure, observedBoard_, result_.basic, pool_.shapes, structureLazyUpdate_, workspace_.structure);
    structureLazyUpdate_.clear();
}

GameControl::Position::Position(ObservedBoard::Result board, Workspace &workspace, const ShapeSolver::OrderAlgo &algo)
    : observedBoard_(std::move(board)), result_(Basic::analyze(observedBoard_, workspace.basic)), workspace_(workspace) {
    // 人工输入先检查局部数字和总雷数范围，不能用全盘候选数代替此检查：无前沿组件的矛盾数字也可能产生非零候选数。
    if (!Basic::checkValid(observedBoard_, result_.basic)) {
        error_.failed = true;
        error_.reason = "观测盘面的数字或已知雷位约束相互矛盾";
        return;
    }
    initialize();
    if (probability(algo).candidates() == 0.0L) {
        error_.failed = true;
        error_.reason = "观测盘面没有合法的全盘雷分布";
    }
}

GameControl::Position::Position(Workspace &workspace, ObservedBoard::Result board)
    : observedBoard_(std::move(board)), result_(Basic::analyze(observedBoard_, workspace.basic)), workspace_(workspace) {
    initialize();
}

const ObservedBoard::Result &GameControl::Position::observedBoard() const { return observedBoard_; }

const GameControl::Position::Error &GameControl::Position::error() const { return error_; }

const Basic::Result &GameControl::Position::basic() const { return result_.basic; }

const Structure::Result &GameControl::Position::structure() const { updateStructure(); return result_.structure; }

const Structure::Pool &GameControl::Position::shapePool() const { updateStructure(); return pool_.shapes; }

const Probability::Result &GameControl::Position::probability(const ShapeSolver::OrderAlgo &algo) const {
    if (result_.probabilityDirty) {
        updateStructure();
        if (result_.probability)
            Probability::analyze(observedBoard_, result_.basic, result_.structure, pool_.shapes, pool_.distributions, *result_.probability, workspace_.probability, algo);
        else
            result_.probability.emplace(Probability::analyze(observedBoard_, result_.basic, result_.structure, pool_.shapes, pool_.distributions, workspace_.probability, algo));
        result_.probabilityDirty = false;
    }
    return *result_.probability;
}

winrate::Input GameControl::Position::input(const ShapeSolver::OrderAlgo &algo) {
    const Probability::Result &currentProbability = probability(algo);
    return {observedBoard_, result_.basic, result_.structure, currentProbability, pool_.shapes, pool_.distributions};
}

long double GameControl::Position::mineProbability(ObservedBoard::CellId cell, const ShapeSolver::OrderAlgo &algo) const {
    const std::pair<int, int> location = observedBoard_.pos(cell);
    const Basic::Mark mark = result_.basic.marks[location.first][location.second];
    if (mark == Basic::Mark::S)
        return 0.0L;
    if (mark == Basic::Mark::F)
        return 1.0L;
    const Probability::Result &currentProbability = probability(algo);
    return currentProbability.mineProbability(cell, observedBoard_, result_.basic, result_.structure);
}

Observe::Result GameControl::Position::observe(ObservedBoard::CellId cell, const ShapeSolver::OrderAlgo &algo) const {
    const Probability::Result &currentProbability = probability(algo);
    return Observe::observe(observedBoard_, result_.basic, result_.structure, pool_.shapes, currentProbability, pool_.distributions, cell, workspace_.probability, algo);
}

const std::vector<ObservedBoard::CellId> &GameControl::Position::safeMoves(const ShapeSolver::OrderAlgo &algo) {
    safeCells_.clear();
    for (int i = 0; i < (int)basicSafeCells_.size(); ++i) {
        const ObservedBoard::CellId cell = basicSafeCells_[i];
        const std::pair<int, int> position = observedBoard_.pos(cell);
        const ObservedBoard::CellState state = observedBoard_.board[position.first][position.second];
        if ((state == ObservedBoard::CellState::Hidden || state == ObservedBoard::CellState::ForcedSafe) && result_.basic.marks[position.first][position.second] == Basic::Mark::S)
            safeCells_.push_back(cell);
    }
    basicSafeCells_ = safeCells_;
    if (!safeCells_.empty())
        return safeCells_;

    updateStructure();
    for (int component = 0; component < (int)result_.structure.components.size(); ++component) {
        const Structure::Instance &instance = pool_.shapes.getInstance(result_.structure.components[component]);
        const Structure::Shape &shape = pool_.shapes.getShape(instance.shape);
        const ShapeSolver::DistributionId distributionId = ShapeSolver::analyze(shape, pool_.shapes, pool_.distributions, workspace_.shapeSolver, algo);
        const ShapeSolver::DistributionResult &distribution = pool_.distributions.get(distributionId);
        const std::vector<long double> &ways = distribution.ways();
        const std::span<const int> offsets = instance.boxes.boxOf.span(pool_.shapes.boxOf);
        const std::span<const ObservedBoard::CellId> cells = instance.boxes.cells.span(pool_.shapes.cells);
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

    const Probability::Result &currentProbability = probability(algo);
    currentProbability.frontierCells(result_.structure, pool_.shapes, [&](ObservedBoard::CellId cell, long double risk) {
        if (risk == 0.0L)
            safeCells_.push_back(cell);
    });
    if (currentProbability.tCellProbability() == 0.0L && result_.basic.unknownSum != 0)
        for (int row = 0; row < observedBoard_.board.rows(); ++row)
            for (int col = 0; col < observedBoard_.board.cols(); ++col)
                if (observedBoard_.board[row][col] == ObservedBoard::CellState::Hidden && result_.basic.marks[row][col] == Basic::Mark::T)
                    safeCells_.push_back(observedBoard_.id(row, col));
    return safeCells_;
}

GameControl::Position::SuggestResult GameControl::Position::suggest(const ShapeSolver::OrderAlgo &algo, SuggestAlgo suggestAlgo, const Config &cfg) {
    const std::vector<ObservedBoard::CellId> &safe = safeMoves(algo);
    if (!safe.empty())
        return {safe, true};

    const Probability::Result &currentProbability = *result_.probability;
    if (suggestAlgo == SuggestAlgo::JavaBrute && currentProbability.candidates() < cfg.bruteForce.candidateThreshold) {
        const BruteForce::Result evaluated = BruteForce::solve(observedBoard_, result_.basic, result_.structure, pool_.shapes, cfg.bruteForce, workspace_.bruteForce);
        // 全候选模式按候选顺序返回；推荐须取胜局数最大项。无可点击格时返回空列表。
        if (evaluated.moves.empty())
            return {};
        const BruteForce::Result::Move *best = &evaluated.moves.front();
        for (int i = 1; i < (int)evaluated.moves.size(); ++i)
            if (evaluated.moves[i].wins > best->wins)
                best = &evaluated.moves[i];
        return {{best->cell}, false};
    }
    if (suggestAlgo == SuggestAlgo::JavaEval || suggestAlgo == SuggestAlgo::JavaBrute) {
        const winrate::Input input(observedBoard_, result_.basic, result_.structure, currentProbability, pool_.shapes, pool_.distributions);
        // Position 没有单独的根死格列表；JavaEval 仍按原数字分支判断推迟猜测的候选。
        const LongTermRisk::Result risk = LongTermRisk::analyze(input, {}, cfg.longTermRisk, workspace_.longTermRisk);
        const JavaEval::Result evaluated = JavaEval::analyze(input, risk, {}, cfg.javaEval, cfg.longTermRisk, cfg.pseudo, workspace_.javaEval);
        // 自动评估首项就是推荐格；无可点击格的合法局面返回空列表。
        if (evaluated.empty())
            return {};
        return {{evaluated.front().cellId}, false};
    }

    ObservedBoard::CellId safest = -1;
    int safestRow = observedBoard_.board.rows();
    int safestCol = -1;
    long double lowestRisk = 2.0L;
    auto consider = [&](ObservedBoard::CellId cell, long double risk) {
        if (risk == 1.0L)
            return;
        const std::pair<int, int> location = observedBoard_.pos(cell);
        if (risk < lowestRisk || (risk == lowestRisk && (location.first < safestRow || (location.first == safestRow && location.second > safestCol)))) {
            lowestRisk = risk;
            safestRow = location.first;
            safestCol = location.second;
            safest = cell;
        }
    };
    currentProbability.frontierCells(result_.structure, pool_.shapes, [&](ObservedBoard::CellId cell, long double risk) { consider(cell, risk); });
    if (result_.basic.unknownSum != 0)
        for (int row = 0; row < observedBoard_.board.rows(); ++row)
            for (int col = 0; col < observedBoard_.board.cols(); ++col)
                if (observedBoard_.board[row][col] == ObservedBoard::CellState::Hidden && result_.basic.marks[row][col] == Basic::Mark::T)
                    consider(observedBoard_.id(row, col), currentProbability.tCellProbability());
    // Position 没有 Game 的胜负状态；合法局面只剩确定雷格时，没有可揭开的安全格，正常返回空建议。
    if (safest < 0)
        return {};
    return {{safest}, false};
}

GameControl::Game::Game(MineBoard mineBoard, ObservedBoard::Result observedBoard, Workspace &workspace)
    : mineBoard_(std::move(mineBoard)), remainingSafe_(countRemainingSafe(observedBoard)), state_(remainingSafe_ == 0 ? State::Won : State::Running), position_(workspace, std::move(observedBoard)) {}

GameControl::Game::Game(int rows, int cols, int mines, U128 seed, Workspace &workspace)
    : pendingSeed_(seed), remainingSafe_(rows * cols - mines), state_(State::Running), position_(workspace, ObservedBoard::Result(rows, cols, mines)) {}

int GameControl::Game::countRemainingSafe(const ObservedBoard::Result &observedBoard) {
    int remainingSafe = observedBoard.board.rows() * observedBoard.board.cols() - observedBoard.totalMines;
    for (int row = 0; row < observedBoard.board.rows(); ++row)
        for (int col = 0; col < observedBoard.board.cols(); ++col)
            if (observedBoard.board[row][col] < ObservedBoard::CellState::Hidden)
                --remainingSafe;
    return remainingSafe;
}

void GameControl::Game::reset(U128 seed) {
    const int rows = position_.observedBoard_.board.rows();
    const int cols = position_.observedBoard_.board.cols();
    const int mines = position_.observedBoard_.totalMines;
    pendingSeed_ = seed;
    remainingSafe_ = rows * cols - mines;
    state_ = State::Running;
    playDelta_.clear();
    position_.reset(ObservedBoard::Result(rows, cols, mines));
}

const GameControl::Position &GameControl::Game::position() const {
    return position_;
}

GameControl::Game::SuggestResult GameControl::Game::suggest(const ShapeSolver::OrderAlgo &algo, SuggestAlgo suggestAlgo, const Config &cfg) {
    return position_.suggest(algo, suggestAlgo, cfg);
}

void GameControl::Game::play(const std::vector<ObservedBoard::CellId> &cells) {
    playDelta_.clear();
    int safeCount = 0;
    for (int i = 0; i < (int)cells.size(); ++i) {
        const std::pair<int, int> location = position_.observedBoard_.pos(cells[i]);
        const ObservedBoard::CellState previous = position_.observedBoard_.board[location.first][location.second];
        if (previous != ObservedBoard::CellState::Hidden)
            continue;
        if (pendingSeed_) {
            const ObservedBoard::Result &board = position_.observedBoard_;
            if (mineBoard_)
                mineBoard_->generate(board.board.rows(), board.board.cols(), board.totalMines, location.first, location.second, *pendingSeed_);
            else
                mineBoard_.emplace(board.board.rows(), board.board.cols(), board.totalMines, location.first, location.second, *pendingSeed_);
            pendingSeed_.reset();
        }
        const int value = mineBoard_->number(location.first, location.second);
        if (value == 9) {
            state_ = State::Lost;
            break;
        }
        playDelta_.changes.push_back({cells[i], previous, (ObservedBoard::CellState)value});
        ++safeCount;
        if (safeCount == remainingSafe_)
            break;
    }
    if (!playDelta_.changes.empty())
        position_.update(playDelta_);
    remainingSafe_ -= safeCount;
    if (remainingSafe_ == 0)
        state_ = State::Won;
}

int GameControl::Game::number(int row, int col) const {
    return mineBoard_->number(row, col);
}

GameControl::Game::State GameControl::Game::state() const {
    return state_;
}

void GameControl::Game::playUntilNoSafe(const ShapeSolver::OrderAlgo &algo) {
    while (state_ == State::Running) {
        const std::vector<ObservedBoard::CellId> &safe = position_.safeMoves(algo);
        if (safe.empty())
            return;
        play(safe);
    }
}

} // namespace mss
