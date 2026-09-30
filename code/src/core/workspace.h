#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/bruteforce/bruteforce.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/utility/combination.h"

namespace mss {

// 对外工作区按模块保存可复用临时状态；ShapeSolver 整体接收 shapeSolver 字段。
struct Workspace {
    Combination::Workspace combination;
    Basic::Workspace basic;
    ShapeSolver::Workspace shapeSolver;
    BruteForce::Workspace bruteForce{shapeSolver};
    Structure::Workspace structure;
    Probability::Workspace probability{shapeSolver, combination};
};

} // 命名空间 mss
