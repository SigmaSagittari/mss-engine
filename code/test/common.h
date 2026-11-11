#pragma once

#include <chrono>
#include <utility>

#include "core/algorithm/rng.h"
#include "core/assert.h"
#include "game/game.h"

namespace test {

struct Config {
    int cols, rows, mines;
    int maxGames;
    long double seconds;
    mss::U128 seed;
    mss::Position::Config position;
};

// 开局空盘和每次 play 后调用 callback(const Game &)，包括终局。引用只在回调中有效。
// 局数和时限在两局之间检查，返回从构造首局到最后一局结束的实际秒数。
template <typename Callback>
long double runGames(const Config &config, const mss::MovePolicy::Config &policyConfig, Callback &&callback) {
    using Clock = std::chrono::steady_clock;
    mss::Suggest::Pool suggestions;
    mss::Random random(config.seed.lo, config.seed.hi);
    const mss::ObservedBoard::CellId first = config.cols - 1;
    const Clock::time_point start = Clock::now();
    mss::Game game(config.rows, config.cols, config.mines, {random.next(), random.next()}, config.position, suggestions.combinations);
    for (int games = 0; games < config.maxGames && std::chrono::duration<long double>(Clock::now() - start).count() < config.seconds; ++games) {
        if (games != 0) game.reset({random.next(), random.next()});
        callback(std::as_const(game));
        game.play(&first, 1);
        for (;;) {
            callback(std::as_const(game));
            if (game.state() != mss::Game::State::Running) break;
            const mss::SuggestResult result = game.suggest(policyConfig, suggestions);
            mss::assert_(result.suggest.size != 0, "整局测试没有可点的建议格");
            game.play(result.suggest.data, (int) result.suggest.size);
        }
    }
    return std::chrono::duration<long double>(Clock::now() - start).count();
}

} // 命名空间 test
