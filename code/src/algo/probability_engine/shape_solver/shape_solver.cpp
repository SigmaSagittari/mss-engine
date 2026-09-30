#include "algo/probability_engine/shape_solver/shape_solver.h"

namespace mss {
namespace ShapeSolver {

DistributionId analyze(const Structure::Shape &shape, const Structure::Pool &shapes, DistributionPool &distPool, Workspace &workspace, const OrderAlgo &algo) {
    // 与 Structure::computeHash 保持字段顺序一致，使同一 Shape 使用相同的分布缓存键。
    U128Hasher hasher;
    for (const Structure::Shape::Box &box : shape.boxes.span(shapes.boxes))
        hasher.mix((std::uint64_t)(box.size));
    for (int i = 0; i < (int)(shape.constraintCount()); ++i) {
        const Structure::Shape::ConstraintView constraint = shape.constraint(shapes, i);
        hasher.mix((std::uint64_t)(constraint.sum));
        for (Structure::BoxId box : constraint.boxIds)
            hasher.mix((std::uint64_t)(box) + 0x9e3779b9ULL);
    }
    const U128 hash = hasher.finalize();
    const DistributionId cached = distPool.find(hash);
    if (cached >= 0)
        return cached;
    if (shape.boxes.size < graphThreshold)
        return distPool.insert(hash, DfsSolver::analyze(shape, shapes, workspace));
    return distPool.insert(hash, GraphSolver::analyze(shape, shapes, algo, workspace));
}

void observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace) {
    if (shape.boxes.size < graphThreshold)
        DfsSolver::observe(shape, shapes, adjacentBoxCells, xBox, result, workspace);
    else
        GraphSolver::observe(shape, shapes, adjacentBoxCells, xBox, result, workspace);
}

}
}
