#pragma once

#include <cstdint>

#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"
#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/shape_solver/shape_solver_type.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 大 Shape 的 frontier DP 后端。结果直接使用 ShapeSolverType 的共享类型。
struct ShapeSolverGraph {
    using StepGenerator = ShapeSolverGraphStep;

    ShapeSolverType::Distribution analyze(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, const ShapeSolverConfig &config, vectorPool<long double> &values);
    // 结果完整覆写，values 由结果自己持有。
    void observe(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, const vector<int> &adjacentBoxCells, Structure::BoxId xBox, const ShapeSolverConfig &config, ShapeSolverType::Observed &result);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    struct State;
    struct Count;
    struct Layer;
    struct ObservedCount;
    struct ObservedLayer;
};

} // 命名空间 mss
