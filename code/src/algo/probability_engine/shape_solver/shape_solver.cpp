#include "algo/probability_engine/shape_solver/shape_solver.h"

#include <algorithm>

#include "core/assert.h"

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

const AssignmentPool::Result &assignments(Structure::ShapeId shapeId, const Structure::Shape &shape, const Structure::Pool &shapes, AssignmentPool &pool, Workspace &workspace) {
    if (const AssignmentPool::Result *cached = pool.find(shapeId))
        return *cached;
    AssignmentPool::Result result;
    result.boxCount = shape.boxes.size;
    int maxMines = 0;
    for (const Structure::Shape::Box &box : shape.boxes.span(shapes.boxes))
        maxMines += box.size;
    result.ways.assign(maxMines + 1, 0.0L);
    result.assignments.resize(maxMines + 1);
    DfsSolver::forEachAssignment(shape, shapes, workspace, [&](const std::vector<char> &boxMines, long double ways) {
        int mineCount = 0;
        for (char mines : boxMines)
            mineCount += mines;
        result.ways[mineCount] += ways;
        result.assignments[mineCount].push_back({result.boxMines.size(), result.ways[mineCount]});
        result.boxMines.insert(result.boxMines.end(), boxMines.begin(), boxMines.end());
    });
    return pool.insert(shapeId, std::move(result));
}

std::span<const char> sampleAssignment(const AssignmentPool::Result &result, int componentMines, long double unit) {
    const long double total = result.ways[componentMines];
    const std::vector<AssignmentPool::Assignment> &group = result.assignments[componentMines];
    const std::vector<AssignmentPool::Assignment>::const_iterator end = group.end();
    const std::vector<AssignmentPool::Assignment>::const_iterator chosen = std::lower_bound(group.begin(), end, unit, [total](const AssignmentPool::Assignment &assignment, long double value) { return assignment.cumulative / total <= value; });
    if (chosen == end)
        assert_(false, "ShapeSolver::sampleAssignment: 抽样未命中 Box 雷数赋值");
    return std::span<const char>(result.boxMines).subspan(chosen->offset, result.boxCount);
}

void observe(const Structure::Shape &shape, const Structure::Pool &shapes, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox, ObversedResult &result, Workspace &workspace) {
    if (shape.boxes.size < graphThreshold)
        DfsSolver::observe(shape, shapes, adjacentBoxCells, xBox, result, workspace);
    else
        GraphSolver::observe(shape, shapes, adjacentBoxCells, xBox, result, workspace);
}

}
}
