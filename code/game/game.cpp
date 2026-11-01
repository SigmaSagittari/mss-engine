#include "game/game.h"

#include <utility>

#include "core/assert.h"

namespace mss {

GameControl::Game::Game(const MineBoard &mineBoard, const ObservedBoard::Result &observedBoard, const Config &config, SuggestPool &suggestions)
    : mineBoard_(mineBoard), hasMineBoard_(true), pendingSeed_(), hasPendingSeed_(false), remainingSafe_(countRemainingSafe(observedBoard)), state_(remainingSafe_ == 0 ? State::Won : State::Running), position_(observedBoard, config, Position::Check::Trusted), suggestions_(suggestions) {}

GameControl::Game::Game(int rows, int cols, int mines, U128 seed, const Config &config, SuggestPool &suggestions)
    : mineBoard_(), hasMineBoard_(false), pendingSeed_(seed), hasPendingSeed_(true), remainingSafe_(rows * cols - mines), state_(State::Running), position_(ObservedBoard::Result(rows, cols, mines), config, Position::Check::Trusted), suggestions_(suggestions) {}

int GameControl::Game::countRemainingSafe(const ObservedBoard::Result &observedBoard) const {
    int remainingSafe = observedBoard.cells.rows() * observedBoard.cells.cols() - observedBoard.totalMines;
    for (int row = 0; row < observedBoard.cells.rows(); ++row)
        for (int col = 0; col < observedBoard.cells.cols(); ++col)
            if ((int)observedBoard.cells[row][col] <= (int)ObservedBoard::CellState::Num8)
                --remainingSafe;
    return remainingSafe;
}

void GameControl::Game::reset(U128 seed) {
    const int rows = position_.observedBoard().cells.rows();
    const int cols = position_.observedBoard().cells.cols();
    const int mines = position_.observedBoard().totalMines;
    pendingSeed_ = seed;
    hasPendingSeed_ = true;
    hasMineBoard_ = false;
    remainingSafe_ = rows * cols - mines;
    state_ = State::Running;
    position_.reset(ObservedBoard::Result(rows, cols, mines));
}

const GameControl::Position &GameControl::Game::position() const { return position_; }
GameControl::Position &GameControl::Game::position() { return position_; }

void GameControl::Game::suggest(const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &pool, vector<ObservedBoard::CellId> &result) {
    position_.suggest(policy, suggestions_, fiftyPool, pool, result);
}

void GameControl::Game::play(span<const ObservedBoard::CellId> cells) {
    ObservedBoard::Delta delta;
    delta.changes = position_.state_.basicPool.board.changes.push_back();
    int safeCount = 0;
    for (int i = 0; i < cells.size; ++i) {
        const ObservedBoard::CellId cell = cells.data[i];
        const std::pair<int, int> location = position_.state_.board.pos(cell);
        const ObservedBoard::CellState previous = position_.state_.board.cells[location.first][location.second];
        if (previous != ObservedBoard::CellState::Hidden)
            continue;
        if (hasPendingSeed_) {
            const int rows = position_.state_.board.cells.rows();
            const int cols = position_.state_.board.cells.cols();
            mineBoard_.generate(rows, cols, position_.state_.board.totalMines, location.first, location.second, pendingSeed_);
            hasPendingSeed_ = false;
            hasMineBoard_ = true;
        }
        const int value = mineBoard_.number(location.first, location.second);
        if (value == 9) {
            state_ = State::Lost;
            break;
        }
        position_.state_.basicPool.board.changes.push_back(delta.changes, {cell, previous, (ObservedBoard::CellState)value});
        ++safeCount;
        if (safeCount == remainingSafe_)
            break;
    }
    if (delta.changes.count != 0)
        position_.update(delta);
    position_.state_.basicPool.board.changes.pop_back(delta.changes);
    remainingSafe_ -= safeCount;
    if (remainingSafe_ == 0)
        state_ = State::Won;
}

int GameControl::Game::number(int row, int col) const {
    assert_(hasMineBoard_, "GameControl::Game::number: 当前局尚未布雷");
    return mineBoard_.number(row, col);
}

GameControl::Game::State GameControl::Game::state() const { return state_; }

void GameControl::Game::playUntilNoSafe() {
    while (state_ == State::Running) {
        const span<const ObservedBoard::CellId> safe = position_.safeMoves();
        if (safe.size == 0)
            return;
        play(safe);
    }
}

} // 命名空间 mss

