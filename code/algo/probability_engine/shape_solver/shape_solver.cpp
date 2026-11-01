#include "algo/probability_engine/shape_solver/shape_solver.h"

#include "core/assert.h"

namespace mss {

ShapeSolver::DistributionId ShapeSolver::DistributionPool::find(U128 hash) const {
    const DistributionId *found = index.find(hash);
    return found == nullptr ? -1 : *found;
}

ShapeSolver::DistributionId ShapeSolver::DistributionPool::insert(U128 hash, Distribution distribution) {
    const DistributionId id = (DistributionId)list.size;
    list.push_back(distribution);
    index.emplace(hash, id);
    return id;
}

void ShapeSolver::DistributionPool::clear() {
    values.data.resize(0);
    list.resize(0);
    index.clear();
}

const ShapeSolver::Assignments *ShapeSolver::AssignmentPool::find(Structure::ShapeId shape) const {
    const int *found = index.find(shape);
    return found == nullptr ? nullptr : &list.data[*found];
}

const ShapeSolver::Assignments &ShapeSolver::AssignmentPool::insert(Structure::ShapeId shape, Assignments assignments) {
    const int id = (int)list.size;
    list.push_back(assignments);
    index.emplace(shape, id);
    return list.data[id];
}

void ShapeSolver::AssignmentPool::clear() {
    storage.boxMines.data.resize(0);
    storage.entries.data.resize(0);
    storage.order.data.resize(0);
    storage.groupStart.data.resize(0);
    storage.ways.data.resize(0);
    list.resize(0);
    index.clear();
}

ShapeSolver::DistributionId ShapeSolver::analyze(const Structure::Shape &shape, const Config &config, DistributionPool &pool) {
    const Structure::Pool &shapes = pool.shapes;
    const DistributionId cached = pool.find(shape.hash);
    if (cached >= 0)
        return cached;
    Distribution distribution;
    if ((int)shape.boxSizes.count < config.graphThreshold)
        distribution = ShapeSolverDfs().analyze(shape, shapes, pool.values);
    else
        distribution = ShapeSolverGraph().analyze(shape, shapes, config, pool.values);
    return pool.insert(shape.hash, distribution);
}

void ShapeSolver::observe(const Structure::Shape &shape, const Structure::Pool &shapes, span<const int> adjacentBoxCells, Structure::BoxId xBox, const Config &config, Observed &result) {
    if ((int)shape.boxSizes.count < config.graphThreshold)
        ShapeSolverDfs().observe(shape, shapes, adjacentBoxCells, xBox, result);
    else
        ShapeSolverGraph().observe(shape, shapes, adjacentBoxCells, xBox, config, result);
}

const ShapeSolver::Assignments &ShapeSolver::assignments(Structure::ShapeId shapeId, const Structure::Shape &shape, AssignmentPool &pool) {
    const Structure::Pool &shapes = pool.shapes;
    if (const Assignments *cached = pool.find(shapeId))
        return *cached;
    const Assignments assignments = ShapeSolverDfs().buildAssignments(shape, shapes, pool.storage);
    return pool.insert(shapeId, assignments);
}

span<const char> ShapeSolver::sampleAssignment(const AssignmentPool &pool, const Assignments &assignments, int componentMines, long double unit) {
    const ShapeSolverDfs::AssignmentStorage &storage = pool.storage;
    const ShapeSolverDfs::Assignment *entries = storage.entries.data.data + assignments.entries.offset;
    const int *order = storage.order.data.data + assignments.order.offset;
    const int *groupStart = storage.groupStart.data.data + assignments.groupStart.offset;
    const long double total = storage.ways.data.data[assignments.ways.offset + componentMines];
    const long double threshold = unit * total;
    const int begin = groupStart[componentMines];
    const int end = groupStart[componentMines + 1];
    int low = begin;
    int high = end;
    while (low < high) {
        const int middle = (low + high) / 2;
        if (entries[order[middle]].cumulative <= threshold)
            low = middle + 1;
        else
            high = middle;
    }
    assert_(low < end, "ShapeSolver::sampleAssignment: 抽样未命中 Box 雷数赋值");
    const ShapeSolverDfs::Assignment &chosen = entries[order[low]];
    return span<const char>(storage.boxMines.data.data + assignments.boxMines.offset + chosen.boxMinesOffset, assignments.boxCount);
}

} // 命名空间 mss
