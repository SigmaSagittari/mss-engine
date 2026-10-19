#include "algo/winrate_engine/java_brute/java_brute.h"

namespace mss {

JavaBrute::Result JavaBrute::solve(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace) {
    Result result;
    if (state.probability.candidates < config.candidateThreshold) {
        BruteForce::Result brute;
        BruteForce::solve(state.board, state.basic, state.structure, state.shapes, config.bruteForce, brute, workspace.bruteForce);
        if (brute.moves.size == 0)
            return result;
        int best = 0;
        for (int i = 1; i < (int)brute.moves.size; ++i)
            if (brute.moves.data[i].wins > brute.moves.data[best].wins)
                best = i;
        result.cell = brute.moves.data[best].cell;
        result.brute = true;
        return result;
    }

    const JavaEval::Result evaluated = JavaEval::analyze(state, shapeConfig, config.javaEval, workspace.javaEval, nullptr);
    if (evaluated.candidates.size == 0)
        return result;
    int best = 0;
    for (int i = 1; i < (int)evaluated.candidates.size; ++i)
        if (evaluated.candidates.data[i].value > evaluated.candidates.data[best].value)
            best = i;
    result.cell = evaluated.candidates.data[best].cell;
    return result;
}

} // 命名空间 mss
