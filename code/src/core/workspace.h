#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/bruteforce/bruteforce.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"
#include "algo/winrate_engine/java_transplant/java_evaluate.h"
#include "algo/winrate_engine/monte_carlo/monte_carlo.h"
#include "core/utility/combination.h"

namespace mss {

// 对外工作区按模块保存可复用临时状态；ShapeSolver 整体接收 shapeSolver 字段。
struct Workspace {
    Combination::Workspace combination;
    Basic::Workspace basic;
    ShapeSolver::Workspace shapeSolver;
    BruteForce::Workspace bruteForce;
    Structure::Workspace structure;
    Probability::Workspace probability;
    LongTermRisk::Workspace longTermRisk;
    Pseudo::Workspace pseudo;
    JavaEval::Workspace javaEval;
    MonteCarlo::Workspace monteCarlo;

    Workspace() : bruteForce(shapeSolver), probability(shapeSolver, combination), longTermRisk(basic, structure, probability), javaEval(basic, structure, probability, longTermRisk, pseudo), monteCarlo(probability, longTermRisk, javaEval, *this) {}
};

} // 命名空间 mss
