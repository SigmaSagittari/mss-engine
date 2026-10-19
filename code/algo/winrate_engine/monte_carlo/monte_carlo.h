#pragma once

#include "algo/winrate_engine/java_brute/java_brute.h"
#include "core/algorithm/rng.h"
#include "game/game.h"

namespace mss {

// 比较首招的完整对局胜率；有安全格时采样整局胜率。
struct MonteCarlo {
    enum class PlayoutAlgo { LowRisk, JavaEval, JavaBrute };
    struct Config {
        PlayoutAlgo playout;
        JavaEval::Config javaEval;
        JavaBrute::Config javaBrute;
        long double temperature;
        long double priorWeight;

        Config(PlayoutAlgo playout, JavaEval::Config javaEval, JavaBrute::Config javaBrute, long double temperature, long double priorWeight) : playout(playout), javaEval(javaEval), javaBrute(javaBrute), temperature(temperature), priorWeight(priorWeight) {}
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
        long long samples, wins;
        vector<Candidate> candidates;
        vector<ObservedBoard::CellId> safeCells;
        Random random;
        ShapeSolver::AssignmentPool assignments;
        GameControl::SuggestPool suggestions;

        // 接管安全格数组，清空原所有者；统计只计实际完成的对局。
        Result(bool winrate, vector<ObservedBoard::CellId> &safeCells);
        int best() const;
        ObservedBoard::CellId recommended() const;
    };

    struct Workspace {
        GameControl::Workspace game;
        JavaEval::Workspace javaEval;
        JavaBrute::Workspace javaBrute;

        Workspace();
    };

    // state 概率有效，temperature>0，priorWeight>=0；返回后保持同一根盘面和配置再 update。
    // cells 非空时比较指定首招，须互异、非空、未揭开且非必雷；有安全格时仍采整局胜率。
    static Result analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, const vector<ObservedBoard::CellId> *cells = nullptr);
    static void update(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);

  private:
    static void estimate(Candidate &candidate, const Config &config);
    static long double logChance(long double won, long double lost, long double target);
    static int select(Result &result, const Config &config);
    static bool simulate(Result &result, PositionState &state, long long sequence, ObservedBoard::CellId first, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);
};

} // 命名空间 mss
