#pragma once

#include "algo/probability_engine/shape_solver/dfs_solver.h"
#include "algo/probability_engine/shape_solver/graph_solver/graph_solver.h"
#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/shape_solver/shape_solver_type.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// Shape 的分布求解与观测：分派到两个后端，直接使用共享结果类型。
struct ShapeSolver {
    using DistributionId = int;
    using Config = ShapeSolverConfig;
    using Distribution = ShapeSolverType::Distribution;
    using Observed = ShapeSolverType::Observed;

    // 按 shape.hash 缓存分布；ID 在 clear 前有效。
    // 与 Structure::Pool 同生命周期：Shape 池清空时必须同步 clear。
    struct DistributionPool {
        const Structure::Pool &shapes;
        vectorPool<long double> values;
        vector<Distribution> list;
        FlatHashTable<U128, DistributionId, U128Hash> index;

        DistributionPool(const Structure::Pool &shapes) : shapes(shapes) {}

        // 未命中返回 -1。
        DistributionId find(U128 hash) const;
        // hash 必须未命中；返回新 ID。
        DistributionId insert(U128 hash, Distribution distribution);
        void clear();
    };

    using Assignment = ShapeSolverDfs::Assignment;
    using Assignments = ShapeSolverDfs::Assignments;

    // 按 ShapeId 缓存 DFS 枚举的全部赋值；随机布雷用。
    // 与 Structure::Pool 同生命周期：ShapeId 只在 Shape 池未清空时有效。
    struct AssignmentPool {
        const Structure::Pool &shapes;
        ShapeSolverDfs::AssignmentStorage storage;
        vector<Assignments> list;
        FlatHashTable<Structure::ShapeId, int, SplitMix64Hash> index;

        AssignmentPool(const Structure::Pool &shapes) : shapes(shapes) {}

        const Assignments *find(Structure::ShapeId shape) const;
        const Assignments &insert(Structure::ShapeId shape, Assignments assignments);
        void clear();
    };

    // 树形工作区：两个后端各持自己的临时区，上层按值汇总。
    struct Workspace {
        ShapeSolverDfs::Workspace dfs;
        ShapeSolverGraph::Workspace graph;
    };

    // shape 必须来自 pool.shapes；命中 shape.hash 时返回池中已有 ID。
    static DistributionId analyze(const Structure::Shape &shape, const Config &config, DistributionPool &pool, Workspace &workspace);

    // adjacentBoxCells 是点击格在本组件各 Box 中的相邻格数，长度必须等于 Box 数。
    // xBox 只在点击格属于本组件时非负：该 Box 的容量按点击格已知安全减 1；否则传 -1。
    // 多个组件各调一次，邻居雷数由调用方卷积；result 完整覆写。
    static void observe(const Structure::Shape &shape, const Structure::Pool &shapes, span<const int> adjacentBoxCells, Structure::BoxId xBox, const Config &config, Observed &result, Workspace &workspace);

    // 按 ShapeId 缓存 DFS 枚举的全部赋值；返回的引用在下一次 insert 后失效。
    static const Assignments &assignments(Structure::ShapeId shapeId, const Structure::Shape &shape, AssignmentPool &pool, Workspace &workspace);

    // componentMines 必须是 ways 里的非零档；unit 必须在 [0, 1) 内。
    // 返回 boxCount 个字节的赋值视图，指向 pool 内，pool 清空前有效。
    static span<const char> sampleAssignment(const AssignmentPool &pool, const Assignments &assignments, int componentMines, long double unit);
};

} // 命名空间 mss
