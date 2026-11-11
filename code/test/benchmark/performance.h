#pragma once

#include <iomanip>
#include <iostream>

#include "core/assert.h"
#include "core/config.h"
#include "test/common.h"

namespace test::benchmark {

struct Performance {
    using Config = test::Config;

    template <typename Callback>
    void run(const Config &config, const mss::MovePolicy::Config &policyConfig, Callback &&callback) {
        int games = 0, wins = 0;
        long long callbacks = 0;
        const long double elapsed = runGames(config, policyConfig, [&](const mss::Game &game) {
            ++callbacks;
            if (game.state() != mss::Game::State::Running) {
                ++games;
                wins += game.state() == mss::Game::State::Won;
            }
            callback(game);
        });
        // 每局的空盘和固定首点各触发一次回调，其余回调对应一次选招。
        const long long suggestions = callbacks - 2LL * games;
        mss::assert_(games > 0, "整局测试没有完成对局");
        std::cout << std::fixed << std::setprecision(6)
                  << (policyConfig.algorithm == mss::MovePolicy::Algorithm::LowRisk ? "LowRisk" :
                      policyConfig.algorithm == mss::MovePolicy::Algorithm::JavaEval ? "JavaEval" : "Anytime")
                  << " version=" << mss::version << " threads=1 " << config.cols << 'x' << config.rows << " mines=" << config.mines
                  << " seed=" << config.seed.lo << ',' << config.seed.hi << " time_limit=" << config.seconds << " games_limit=" << config.maxGames
                  << " stop=" << (games == config.maxGames ? "games_limit" : "time_limit")
                  << " games=" << games << " wins=" << wins << " losses=" << games - wins << " suggestions=" << suggestions
                  << " elapsed=" << elapsed << " games/s=" << games / elapsed << " winrate=" << 100.0L * wins / games << "%\n";
    }
};

} // 命名空间 test::benchmark
