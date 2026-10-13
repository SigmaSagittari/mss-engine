#pragma once

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

#include "common.h"

namespace test {

inline void performance(const Config &config, bool multiThread = false) {
    using Game = mss::GameControl::Game;
    const mss::U128 seed{0x123456789abcdef0ULL, 0xfedcba9876543210ULL};
    //constexpr std::array algorithms{Game::SuggestAlgo::LowRisk, Game::SuggestAlgo::JavaEval, Game::SuggestAlgo::JavaBrute};
    constexpr std::array algorithms{Game::SuggestAlgo::JavaBrute};
    for (Game::SuggestAlgo algorithm : algorithms) {
        const Config runConfig{config.cols, config.rows, config.mines, config.time, config.games, algorithm};
        const char *name = algorithm == Game::SuggestAlgo::LowRisk ? "LowRisk" : algorithm == Game::SuggestAlgo::JavaEval ? "JavaEval" : "JavaBrute";
        const int threadCount = multiThread ? (std::min)(engineConfig.global.threadLimit, config.games) : 1;
        // 每个线程结束前写自己的统计槽；全部 join 后才汇总，不共享 Game、Workspace、招法池或热路径计数。
        std::vector<std::pair<int, int>> results(threadCount);
        std::vector<std::jthread> threads;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        auto run = [&](int worker) {
            const Config workerConfig{config.cols, config.rows, config.mines, config.time, config.games / threadCount + (worker < config.games % threadCount), algorithm};
            // 0 号线程沿用单线程种子；其他线程使用各自固定的种子流。
            const mss::U128 workerSeed = worker == 0 ? seed : mss::U128{mss::splitmix64(seed.lo + worker), mss::splitmix64(seed.hi + worker)};
            int games = 0, wins = 0;
            mss::GameControl::SuggestPool suggestions;
            runGames(workerConfig, mss::ShapeSolver::OrderAlgo::Auto, workerSeed, suggestions, [&](const Game &game) {
                if (game.state() == Game::State::Running) return;
                ++games;
                wins += game.state() == Game::State::Won;
            });
            results[worker] = {games, wins};
        };
        for (int worker = 1; worker < threadCount; ++worker) threads.emplace_back(run, worker);
        run(0);
        for (std::jthread &thread : threads) thread.join();
        const long double elapsed = std::chrono::duration<long double>(std::chrono::steady_clock::now() - start).count();
        int games = 0, wins = 0;
        for (const std::pair<int, int> &result : results) {
            games += result.first;
            wins += result.second;
        }
        std::cout << std::fixed << std::setprecision(3) << name << " mode=" << (multiThread ? "multi" : "single") << " version=" << mss::version << " threads=" << threadCount << " thread_limit=" << engineConfig.global.threadLimit << ' ' << runConfig.cols << 'x' << runConfig.rows << " mines=" << runConfig.mines << " seed=" << seed.lo << ',' << seed.hi << " time_limit=" << runConfig.time << " games_limit=" << runConfig.games << " candidate_threshold=" << engineConfig.bruteForce.candidateThreshold << " games=" << games << " wins=" << wins << " losses=" << games - wins << " elapsed=" << elapsed << " games/s=" << games / elapsed << " winrate=" << 100.0L * wins / games << "%\n" << std::flush;
    }
}

}
