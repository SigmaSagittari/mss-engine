#pragma once

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/utility/rng.h"
#include "core/workspace.h"
#include "game/game.h"

namespace test {

struct TimeBox {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point deadline;

    explicit TimeBox(double seconds)
        : start(std::chrono::steady_clock::now()), deadline(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds))) {
    }

    // 判断计时期限是否已到。
    bool expired() const {
        return std::chrono::steady_clock::now() >= deadline;
    }

    // 返回计时盒启动至今经过的秒数。
    double elapsedSeconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
};

struct RunSummary {
    long long games = 0;
    long long wins = 0;
    double elapsedSeconds = 0.0;
};

template <typename Fn>
inline bool generateGame(mss::GameControl::Game &game, mss::Random &rng, mss::ObservedBoard::Delta &updates, Fn &&perDecision) {
    std::vector<mss::ObservedBoard::CellId> moves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
    perDecision(game);
    const mss::ObservedBoard::CellId firstCell = game.position.observedBoard.id(0, game.position.observedBoard.board.cols() - 1);
    mss::ObservedBoard::CellId safeCell = firstCell;
    if (game.number(0, game.position.observedBoard.board.cols() - 1) == 9) {
        const int cellCount = game.position.observedBoard.board.rows() * game.position.observedBoard.board.cols();
        safeCell = -1;
        while (safeCell < 0) {
            const mss::ObservedBoard::CellId candidate = (mss::ObservedBoard::CellId)(rng.next() % (std::uint64_t)cellCount);
            if (candidate != firstCell && game.number(candidate / game.position.observedBoard.board.cols(), candidate % game.position.observedBoard.board.cols()) != 9)
                safeCell = candidate;
        }
    }
    game.makeFirstMoveSafe(firstCell, safeCell);
    for (;;) {
        for (int i = 0; i < (int)moves.size(); ++i) {
            const mss::ObservedBoard::CellId cell = moves[i];
            const std::pair<int, int> position = game.position.observedBoard.pos(cell);
            const int number = game.number(position.first, position.second);
            if (number == 9)
                return false;
            updates.clear();
            updates.changes.push_back({cell, (mss::ObservedBoard::CellState)(number)});
            game.update(updates);
            if (game.won())
                return true;
        }
        moves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
        perDecision(game);
    }
}

template <typename DecisionFn, typename GameFn>
inline RunSummary runGames(int rows, int cols, int mines, double seconds, DecisionFn &&perDecision, GameFn &&perGame) {
    mss::Random rng(0xC0FFEE12345ULL, 0xD1B54A32D192ED03ULL);
    mss::Workspace workspace;
    mss::ObservedBoard::Delta updates;
    mss::GameControl::Game game(rows, cols, mines, {rng.next(), rng.next()}, workspace);
    const std::vector<mss::ObservedBoard::CellId> initialMoves = game.suggest(mss::GameControl::Position::SuggestAlgo::LowRisk);
    mss::assert_(initialMoves.size() == 1 && initialMoves[0] == game.position.observedBoard.id(0, cols - 1), "test::runGames: 初始平局没有选择右上角");
    const TimeBox timebox(seconds);
    RunSummary summary;
    while (!timebox.expired()) {
        if (summary.games != 0)
            game.reset({rng.next(), rng.next()});
        if (generateGame(game, rng, updates, perDecision))
            ++summary.wins;
        ++summary.games;
        perGame(summary);
    }
    summary.elapsedSeconds = timebox.elapsedSeconds();
    return summary;
}

}
