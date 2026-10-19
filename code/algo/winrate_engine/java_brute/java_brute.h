#pragma once

#include "algo/winrate_engine/bruteforce/bruteforce.h"
#include "algo/winrate_engine/java_eval/java_eval.h"
#include "game/position_state.h"

namespace mss {

// JavaBrute：候选数低于阈值时走精确 BruteForce，否则走 JavaEval。
// 安全步由 Game 在进入前处理；本模块只返回一个建议格。
struct JavaBrute {
    struct Config {
        int candidateThreshold;
        BruteForce::Config bruteForce;
        JavaEval::Config javaEval;

        Config(int candidateThreshold, BruteForce::Config bruteForce, JavaEval::Config javaEval) : candidateThreshold(candidateThreshold), bruteForce(bruteForce), javaEval(javaEval) {}

        // 建议缓存身份：阈值、暴力后端参数和 JavaEval 参数都会改变招法。
        void hash(U128Hasher &hasher) const {
            hasher.mix(candidateThreshold);
            hasher.mix((int)bruteForce.checkAllMoves);
            hasher.mix((int)bruteForce.parallel);
            hasher.mix(bruteForce.threadCount);
            javaEval.hash(hasher);
        }
    };

    struct Result {
        ObservedBoard::CellId cell;
        bool brute;

        Result() : cell(-1), brute(false) {}
    };

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, ShapeSolver::Workspace &shapeSolver) : bruteForce(shapeSolver), javaEval(basic, structure, probability) {}

        BruteForce::Workspace bruteForce;
        JavaEval::Workspace javaEval;
    };

    // state 必须已同步且概率有效；无可点击格时返回 cell=-1。
    static Result solve(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);
};

} // 命名空间 mss
