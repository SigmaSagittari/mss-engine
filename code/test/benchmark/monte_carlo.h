#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "common.h"
#include "test/boards.h"
#include "core/assert.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

namespace test::benchmark {

// 固定盘面为 9x9/10；配置提供本次采样时限、样本上限和后续策略。
inline void monteCarlo(const Config &config = {9, 9, 10, 10.0L, 1000000, Algo::JavaEval, 0}) {
    mss::assert_(config.algorithm != Algo::JavaSearch, "test::monteCarlo: 后续策略只支持 LowRisk、JavaEval 和 JavaBrute");
    const mss::winrate::SuggestAlgo algorithm = config.algorithm == Algo::LowRisk ? mss::winrate::SuggestAlgo::LowRisk : config.algorithm == Algo::JavaEval ? mss::winrate::SuggestAlgo::JavaEval : mss::winrate::SuggestAlgo::JavaBrute;
    mss::Workspace workspace;
    mss::ObservedBoard::Result board = monteCarloBoard();
    const std::chrono::steady_clock::time_point rootStart = std::chrono::steady_clock::now();
    mss::Basic::Result basic = mss::Basic::analyze(board, workspace.basic);
    mss::Structure::Pool shapes;
    mss::Structure::Result structure = mss::Structure::analyze(board, basic, shapes, workspace.structure);
    mss::ShapeSolver::DistributionPool distributions;
    const mss::Probability::Result probability = mss::Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability);
    const long double rootMilliseconds = std::chrono::duration<long double, std::milli>(std::chrono::steady_clock::now() - rootStart).count();
    // 四个角各在三个邻格中放一个雷，其余六个雷在 65 个自由格中放置：3^4 * C(65,6)。
    std::vector<mss::ObservedBoard::CellId> targets;
    // D4 的一个基本区域为 0 <= row <= col <= 4；去掉已揭开的 (0,0)，得到 14 个非对称代表格。
    for (int row = 0; row <= board.board.rows() / 2; ++row)
        for (int col = row; col <= board.board.cols() / 2; ++col)
            if (board.board[row][col] == mss::ObservedBoard::CellState::Hidden)
                targets.push_back(board.id(row, col));
    const mss::winrate::Input root(board, basic, structure, probability, shapes, distributions);
    const std::chrono::steady_clock::time_point prepareStart = std::chrono::steady_clock::now();
    mss::MonteCarlo::Result result = mss::MonteCarlo::analyze(root, algorithm, engineConfig, workspace.monteCarlo, targets);
    const long double prepareMilliseconds = std::chrono::duration<long double, std::milli>(std::chrono::steady_clock::now() - prepareStart).count();
    const char *strategy = config.algorithm == Algo::LowRisk ? "LowRisk" : config.algorithm == Algo::JavaEval ? "JavaEval" : "JavaBrute";
    std::cout << "test/monte-carlo: " << board.board.cols() << 'x' << board.board.rows() << " mines=" << board.totalMines << " corners=1 targets=" << targets.size() << " strategy=" << strategy << " time_limit=" << config.time << " samples_limit=" << config.games << " layouts=" << probability.candidates() << "\n";
    std::cout << "  seed_bases=0x123456789ABCDEF,0xF123456789ABCDE0 scheduler_seed=0xC0FFEE12345\n";
    for (int row = 0; row < board.board.rows(); ++row) {
        std::cout << "  ";
        for (int col = 0; col < board.board.cols(); ++col)
            std::cout << (board.board[row][col] == mss::ObservedBoard::CellState::Num1 ? '1' : row <= col && col <= board.board.cols() / 2 ? '*' : '?');
        std::cout << '\n';
    }
    std::cout << std::flush;
    std::vector<long long> previousSamples(targets.size(), 0);
    std::uint64_t trace = 0;
    constexpr long long traceLimit = 1000;
    long long samples = 0, previousWins = 0;
    const std::chrono::steady_clock::time_point sampleStart = std::chrono::steady_clock::now();
    while (samples < config.games && std::chrono::duration<long double>(std::chrono::steady_clock::now() - sampleStart).count() < config.time) {
        mss::assert_(mss::MonteCarlo::update(result, root, engineConfig, workspace.monteCarlo), "test::monteCarlo: 未取消的采样没有完成");
        ++samples;
        if (samples <= traceLimit) {
            const long long wins = result.wins();
            for (int i = 0; i < (int)result.candidates.size(); ++i)
                if (result.candidates[i].samples != previousSamples[i]) {
                    trace = mss::splitmix64(trace ^ result.candidates[i].cellId);
                    trace = mss::splitmix64(trace ^ result.candidates[i].samples);
                    trace = mss::splitmix64(trace ^ (wins - previousWins));
                    previousSamples[i] = result.candidates[i].samples;
                }
            previousWins = wins;
        }
    }
    const long double elapsed = std::chrono::duration<long double>(std::chrono::steady_clock::now() - sampleStart).count();
    std::cout << std::fixed << std::setprecision(3) << "  root_ms=" << rootMilliseconds << " prepare_ms=" << prepareMilliseconds << " samples=" << samples << " wins=" << result.wins() << " losses=" << samples - result.wins() << " elapsed=" << elapsed << " samples/s=" << samples / elapsed << " capped=" << std::boolalpha << (samples == config.games) << " trace_steps=" << (std::min)(samples, traceLimit) << " trace=" << trace << '\n';
    const mss::ObservedBoard::CellId recommended = result.recommended();
    const std::pair<int, int> best = board.pos(recommended);
    std::cout << "  recommended=(" << best.first + 1 << ',' << best.second + 1 << ")\n";
    for (const mss::MonteCarlo::Candidate &candidate : result.candidates) {
        const std::pair<int, int> position = board.pos(candidate.cellId);
        std::cout << "  cell=(" << position.first + 1 << ',' << position.second + 1 << ") samples=" << candidate.samples << " wins=" << candidate.wins << " mine_probability=" << 100.0L * probability.mineProbability(candidate.cellId, board, basic, structure) << "% winrate=";
        if (candidate.samples != 0)
            std::cout << 100.0L * candidate.wins / candidate.samples << '%';
        else
            std::cout << "unsampled";
        std::cout << (candidate.cellId == recommended ? " recommended" : "") << '\n';
    }
}

} // 命名空间 test
