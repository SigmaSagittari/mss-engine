#pragma once

#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "core/assert.h"
#include "core/config.h"
#include "test/common.h"

namespace test::benchmark {

inline void performance(const Config &config, const mss::MovePolicy &policy) {
    using Game = mss::GameControl::Game;
    const mss::U128 seed{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    const char *name = policy.basic.algorithm == mss::MovePolicy::Basic::Algorithm::LowRisk ? "LowRisk" : policy.basic.algorithm == mss::MovePolicy::Basic::Algorithm::JavaEval ? "JavaEval" : "Anytime";
    // 每线程独占对局、池和工作区；join 后才读取统计槽。
    std::vector<std::pair<int, int>> results(config.threads);
    std::vector<std::jthread> threads;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    auto run = [&](int worker) {
        const Config workerConfig{config.cols, config.rows, config.mines, config.time, config.games / config.threads + (worker < config.games % config.threads), 1};
        const mss::U128 workerSeed = worker == 0 ? seed : mss::U128{mss::splitmix64(seed.lo + worker), mss::splitmix64(seed.hi + worker)};
        int games = 0, wins = 0;
        runGames(workerConfig, policy, workerSeed, [&](const Game &game) {
            if (game.state() == Game::State::Running) return;
            ++games;
            wins += game.state() == Game::State::Won;
        });
        results[worker] = {games, wins};
    };
    for (int worker = 1; worker < config.threads; ++worker) threads.emplace_back(run, worker);
    run(0);
    for (std::jthread &thread : threads) thread.join();
    const long double elapsed = std::chrono::duration<long double>(std::chrono::steady_clock::now() - start).count();
    int games = 0, wins = 0;
    for (const std::pair<int, int> &result : results) {
        games += result.first;
        wins += result.second;
    }
    mss::assert_(games > 0, "performance: 没有完成对局");
    std::cout << std::fixed << std::setprecision(3) << name << " version=" << mss::version << " threads=" << config.threads << ' ' << config.cols << 'x' << config.rows << " mines=" << config.mines << " seed=" << seed.lo << ',' << seed.hi << " time_limit=" << config.time << " games_limit=" << config.games;
    if (policy.basic.algorithm == mss::MovePolicy::Basic::Algorithm::Anytime) std::cout << " node_budget=" << policy.basic.anytime->nodeLimit;
    if (policy.basic.algorithm != mss::MovePolicy::Basic::Algorithm::LowRisk) {
        std::cout << " fifty_fifty=" << (policy.fiftyFifty.algorithm == mss::FiftyFifty::Algorithm::OldJava ? "OldJava" : "Templates");
        if (policy.fiftyFifty.algorithm == mss::FiftyFifty::Algorithm::Templates) std::cout << " prefix=" << policy.fiftyFifty.templates->prefix;
    }
    std::cout << " brute_limit=" << policy.brute.limit << " stop=" << (games == config.games ? "games_limit" : "time_limit") << " games=" << games << " wins=" << wins << " losses=" << games - wins << " elapsed=" << elapsed << " games/s=" << games / elapsed << " winrate=" << 100.0L * wins / games << "%\n" << std::flush;
}

}
