#pragma once

#include "algo/winrate_engine/move_policy.h"
#include "core/algorithm/rng.h"
#include "game/game.h"

namespace mss {

// 比较首招的完整对局胜率；有安全格时采样整局胜率。
struct MonteCarlo {
    struct Config {
        long double temperature;
        long double priorWeight;
        Config(long double temperature, long double priorWeight) : temperature(temperature), priorWeight(priorWeight) {}
    };

    struct Candidate {
        ObservedBoard::CellId cell;
        long long samples, wins;
        long double prior, mean, variance;
        // chanceSamples=-1 表示尚未计算；缓存只在样本数和目标均值相同时有效。
        long long chanceSamples;
        long double chanceTarget, chanceLog;

        Candidate(ObservedBoard::CellId cell, long double prior, const Config &config);
    };

    struct Result {
        bool winrate;
        // engine 为 BruteForce 时保存精确结果，此时不构造采样候选、不再采样。
        SuggestResult exact;
        long long samples, wins;
        vector<Candidate> candidates;
        vector<ObservedBoard::CellId> safeCells;
        Random random;
        ShapeSolver::AssignmentPool assignments;
        GameControl::SuggestPool suggestions;

        // 接管根层精确结果或安全格数组；统计只计实际完成的对局。
        explicit Result(SuggestResult &root);
        int best() const;
        ObservedBoard::CellId recommended() const;
    };

    struct Workspace {
        GameControl::Workspace game;
        AnytimeSearch::Workspace anytimeSearch;
        BruteForce::Workspace bruteForce;

        Workspace();
    };

    // temperature>0，priorWeight>=0；返回后保持同一根盘面和配置再 update。
    // fiftyPool 由调用方跨查询和模拟复用；暴力接管时直接返回精确结果。
    // cells 非空时比较指定首招，须互异、非空、未揭开且非必雷；有安全格时仍采整局胜率。
    static Result analyze(GameControl::Position &position, const Config &config, const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &pool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells = nullptr);
    static void update(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &pool, Workspace &workspace);

  private:
    static void estimate(Candidate &candidate, const Config &config);
    static long double logChance(long double won, long double lost, long double target);
    static int select(Result &result, const Config &config);
    static bool simulate(Result &result, PositionState &state, long long sequence, ObservedBoard::CellId first, const ShapeSolverConfig &shapeConfig, const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &pool, Workspace &workspace);
};

} // 命名空间 mss
