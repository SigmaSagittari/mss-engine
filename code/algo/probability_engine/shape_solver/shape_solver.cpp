#include "algo/probability_engine/shape_solver/shape_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "core/assert.h"

namespace mss {

struct ShapeSolver::Workspace {
    vector<DistributionId> factors;
};

__declspec(noinline) ShapeSolver::Workspace &ShapeSolver::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void ShapeSolver::DistributionPool::clear() {
    values.data.resize(0);
    list.resize(0);
    byShape.clear();
    byComponent.clear();
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

ShapeSolver::DistributionId ShapeSolver::analyze(const Structure::Instance &instance, const Config &config, DistributionPool &pool) {
    if (const DistributionId *cached = pool.byShape.find(instance.shape))
        return *cached;
    const Structure::Pool &shapes = pool.shapes;
    const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
    const ShapeSolverType::Boxes boxes(shapes.instances.boxes.data.data + instance.boxes.offset, (int)instance.boxes.count);
    const Distribution distribution = boxes.size < config.graphThreshold
        ? ShapeSolverDfs().analyze(shape, shapes.shapes, boxes, pool.values)
        : ShapeSolverGraph().analyze(shape, shapes.shapes, boxes, config, pool.values);
    const DistributionId id = (DistributionId)pool.list.size;
    pool.list.push_back(distribution);
    pool.byShape.emplace(instance.shape, id);
    return id;
}

ShapeSolver::DistributionId ShapeSolver::analyzeComponent(const LogicComponent::Result::Component &component, const LogicComponent::Pool &components,
                                                         const Config &config, DistributionPool &pool) {
    Workspace &workspace = getWorkspace();
    vector<DistributionId> &factors = workspace.factors;
    factors.resize(0);
    for (Structure::InstanceId instance : components.instanceIds.span(component.instanceIds))
        factors.push_back(analyze(pool.shapes.instances.list.data[instance], config, pool));

    // 排序让同一组因子共享缓存；重复 ID 对应不同实例，每次都参与卷积。
    if (factors.size > 1)
        std::sort(factors.data, factors.data + factors.size);
    return combine(factors, (int)component.tCells.count, pool);
}

ShapeSolver::DistributionId ShapeSolver::analyzeFreeCells(int tCount, DistributionPool &pool) {
    Workspace &workspace = getWorkspace();
    vector<DistributionId> &factors = workspace.factors;
    factors.resize(0);
    return combine(factors, tCount, pool);
}

ShapeSolver::DistributionId ShapeSolver::combine(const vector<DistributionId> &factors, int tCount, DistributionPool &pool) {
    U128Hasher hasher;
    hasher.mix(tCount);
    hasher.mix(factors.size);
    for (std::size_t i = 0; i < factors.size; ++i)
        hasher.mix(factors[i]);
    const U128 key = hasher.finalize();
    if (const DistributionId *cached = pool.byComponent.find(key))
        return *cached;

    vector<long double> &values = pool.values.data;
    vectorPool<long double>::vector ways{values.size, 1};
    int start = 0;
    for (std::size_t i = 0; i < factors.size; ++i) {
        const Distribution &dist = pool.list[factors[i]];
        if (dist.ways.count == 0) {
            ways.count = 0;
            start = 0;
            break;
        }
        ways.count += dist.ways.count - 1;
        start += dist.start;
    }

    values.resize(ways.offset + ways.count);
    if (ways.count != 0) {
        // 危险：新切片未初始化。每轮从高次向低次写，只读取上一轮已写入且次数不超过当前项的系数。
        values.data[ways.offset] = 1.0L;
        std::size_t count = 1;
        for (std::size_t factor = 0; factor < factors.size; ++factor) {
            const vectorPool<long double>::vector terms = pool.list[factors[factor]].ways;
            const std::size_t nextCount = count + terms.count - 1;
            for (std::size_t remaining = nextCount; remaining > 0; --remaining) {
                const std::size_t degree = remaining - 1;
                const std::size_t begin = degree >= terms.count ? degree - terms.count + 1 : 0;
                const std::size_t end = (std::min)(count, remaining);
                long double total = 0.0L;
                for (std::size_t i = begin; i < end; ++i)
                    total += values.data[ways.offset + i] * values.data[terms.offset + degree - i];
                values.data[ways.offset + degree] = total;
            }
            count = nextCount;
        }
    }
    const DistributionId id = (DistributionId)pool.list.size;
    pool.list.push_back({ways, pool.values.push_back(), start, 0, tCount});
    pool.byComponent.emplace(key, id);
    return id;
}

void ShapeSolver::observe(const Structure::Instance &instance, const Structure::Pool &shapes, const vector<int> &adjacentBoxCells, Structure::BoxId xBox, const Config &config, Observed &result) {
    const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
    const ShapeSolverType::Boxes boxes(shapes.instances.boxes.data.data + instance.boxes.offset, (int)instance.boxes.count);
    if (boxes.size < config.graphThreshold)
        ShapeSolverDfs().observe(shape, shapes.shapes, boxes, adjacentBoxCells, xBox, result);
    else
        ShapeSolverGraph().observe(shape, shapes.shapes, boxes, adjacentBoxCells, xBox, config, result);
}

const ShapeSolver::Assignments &ShapeSolver::assignments(const Structure::Instance &instance, AssignmentPool &pool) {
    if (const Assignments *cached = pool.find(instance.shape))
        return *cached;
    const Structure::Pool &shapes = pool.shapes;
    const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
    const ShapeSolverType::Boxes boxes(shapes.instances.boxes.data.data + instance.boxes.offset, (int)instance.boxes.count);
    const Assignments assignments = ShapeSolverDfs().buildAssignments(shape, shapes.shapes, boxes, pool.storage);
    return pool.insert(instance.shape, assignments);
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
