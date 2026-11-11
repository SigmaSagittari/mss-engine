#include "game/game.h"

#include <algorithm>

#include "core/algorithm/neighbors.h"
#include "core/algorithm/rng.h"

namespace mss {

struct Game::Workspace {
    vector<ObservedBoard::CellId> order, safe;
    vector<ObservedBoard::Delta::Change> changes;
};

__declspec(noinline) Game::Workspace &Game::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

Game::Game(int rows, int cols, int mines, U128 seed, const Position::Config &config, Combination::Pool &combinations)
    : mines_(rows, cols, 0), seed_(seed), generated_(false), remainingSafe_(rows * cols - mines), state_(State::Running),
      positions_(combinations), position_(Position().analyze(ObservedBoard::Result(rows, cols, mines), positions_, config)) {}

Game::Game(const Grid<char> &mines, const ObservedBoard::Result &board, const Position::Config &config, Combination::Pool &combinations)
    : mines_(mines), seed_(), generated_(true), remainingSafe_(board.cells.rows() * board.cells.cols() - board.totalMines),
      state_(State::Running), positions_(combinations), position_(Position().analyze(board, positions_, config)) {
    for (int row = 0; row < board.cells.rows(); ++row) for (int col = 0; col < board.cells.cols(); ++col)
        remainingSafe_ -= ObservedBoard().isNumber(board.cells[row][col]);
    if (remainingSafe_ == 0) state_ = State::Won;
}

void Game::reset(U128 seed) {
    const int rows = mines_.rows(), cols = mines_.cols(), mines = position_.board.totalMines;
    positions_.clear();
    position_ = Position().analyze(ObservedBoard::Result(rows, cols, mines), positions_, position_.config());
    mines_.fill(0);
    seed_ = seed;
    generated_ = false;
    remainingSafe_ = rows * cols - mines;
    state_ = State::Running;
}

Game::State Game::state() const { return state_; }
const Position::Result &Game::position() const { return position_; }

void Game::generate(int first) {
    const int cols = mines_.cols();
    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::CellId> &order = workspace.order;
    const int count = mines_.rows() * cols - 1;
    order.resize(count);
    for (int i = 0; i < count; ++i)
        order[i] = i + (i >= first);

    Random random(seed_.lo, seed_.hi);
    for (int i = count - 1; i > 0; --i) {
        const int j = random.next() % (i + 1);
        std::swap(order[i], order[j]);
    }
    for (int i = 0; i < position_.board.totalMines; ++i) {
        const int cell = order[i];
        mines_[cell / cols][cell % cols] = 1;
    }
    generated_ = true;
}

int Game::play(int x, int y) {
    const ObservedBoard::CellId cell = position_.board.id(y, x);
    play(&cell, 1);
    const ObservedBoard::CellState observed = position_.board.cells[y][x];
    return ObservedBoard().isNumber(observed) ? ObservedBoard().numberValue(observed) : number(x, y);
}

void Game::play(const ObservedBoard::CellId *cells, int count) {
    using Cell = ObservedBoard::CellState;
    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::Delta::Change> &changes = workspace.changes;
    changes.resize(0);
    const ObservedBoard::Result &board = position_.board;
    for (int i = 0; i < count && state_ == State::Running; ++i) {
        const auto [row, col] = board.pos(cells[i]);
        const Cell previous = board.cells[row][col];
        if (previous != Cell::Hidden && previous != Cell::ForcedSafe) continue;
        if (!generated_) generate(cells[i]);
        const int value = number(col, row);
        if (value == 9) {
            state_ = State::Lost;
            break;
        }
        changes.push_back({cells[i], previous, (Cell) value});
        if (--remainingSafe_ == 0) state_ = State::Won;
    }
    Position().update(position_, changes, positions_);
}

int Game::number(int x, int y) const {
    if (mines_[y][x])
        return 9;

    int number = 0;
    forEachAdjacent(y, x, mines_.rows(), mines_.cols(), [&](int row, int col) { number += mines_[row][col]; });
    return number;
}

void Game::playUntilNoSafe() {
    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::CellId> &safe = workspace.safe;
    while (state_ == State::Running) {
        safe.resize(0);
        auto collect = [&](ObservedBoard::CellId cell) { safe.push_back(cell); };
        position_.basic.safeCells(collect);
        if (safe.size == 0) {
            const Probability::Pool &probabilities = positions_.probabilities;
            const Position::ProbabilityResult probability = Position().analyzeProbability(position_, positions_);
            probability.boxes.frontierCells(probabilities, position_.structure, [&](ObservedBoard::CellId cell, long double mine) {
                if (mine == 0.0L) collect(cell);
            });
            if (position_.basic.unknownSum != 0 && probability.boxes.tCellProbability == 0.0L) position_.basic.tCells(collect);
            if (safe.size == 0) return;
        }
        play(safe.data, (int)safe.size);
    }
}

SuggestResult Game::suggest(const MovePolicy::Config &config, Suggest::Pool &pool, bool useCache) {
    if (state_ != State::Running) return {};
    bool basicSafe = false;
    if (useCache) position_.basic.safeCells([&](ObservedBoard::CellId) { basicSafe = true; });
    const bool cacheable = useCache && !basicSafe;
    U128 key;
    if (cacheable) {
        const ObservedBoard::Result &board = position_.board;
        U128Hasher hasher;
        hasher.mix(board.cells.rows(), board.cells.cols(), board.totalMines);
        hasher.mixBytes(board.cells[0], (std::size_t) board.cells.rows() * board.cells.cols());
        position_.config().hash(hasher);
        config.hash(hasher);
        key = hasher.finalize();
        if (const Suggest::Pool::Entry *cached = pool.cache.find(key)) {
            SuggestResult result(cached->engine, cached->reason);
            result.suggest.push_back(cached->cell);
            return result;
        }
    }
    SuggestResult result = MovePolicy().suggest(position_, positions_, config, pool, &remainingSafe_);
    if (cacheable && result.reason != SuggestResult::Reason::Safe && result.reason != SuggestResult::Reason::Finished)
        pool.cache.emplace(key, {result.suggest[0], result.engine, result.reason});
    return result;
}

} // 命名空间 mss
