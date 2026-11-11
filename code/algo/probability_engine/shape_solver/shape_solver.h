#pragma once

#include "algo/probability_engine/logic_component/logic_component.h"
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

// 求解实例分布和观测结果，并把实例分布组合成逻辑块分布。
struct ShapeSolver {
    using DistributionId = int;
    using Config = ShapeSolverConfig;
    using Distribution = ShapeSolverType::Distribution;
    using Observed = ShapeSolverType::Observed;

    // 实例和逻辑块共用 list 和 values；DistributionId 在 clear 前有效。
    // 结构池清空时必须同步 clear；values 扩容后须重新取得系数地址。
    struct DistributionPool {
        const Structure::Pool &shapes;
        vectorPool<long double> values;
        vector<Distribution> list;
        FlatHashTable<Structure::ShapeId, DistributionId> byShape;
        FlatHashTable<U128, DistributionId, U128Hash> byComponent;

        DistributionPool(const Structure::Pool &shapes) : shapes(shapes) {}

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

    // instance 来自 pool.shapes.instances；同一 ShapeId 共用分布。
    DistributionId analyze(const Structure::Instance &instance, const Config &config, DistributionPool &pool);

    // component 的实例 ID 必须来自 pool.shapes.instances，切片来自 components。
    // 缓存身份包含实例分布 ID 的多重集合及 T 格数。返回的分布保留 T 格数，ways 只计前沿。
    DistributionId analyzeComponent(const LogicComponent::Result::Component &component, const LogicComponent::Pool &components,
                                    const Config &config, DistributionPool &pool);

    // tCount 非负。返回只含自由格的分布，与同样大小的纯 T 块共享缓存。
    DistributionId analyzeFreeCells(int tCount, DistributionPool &pool);

    // adjacentBoxCells 是点击格在本组件各 Box 中的相邻格数，长度必须等于 Box 数。
    // xBox 只在点击格属于本组件时非负：该 Box 的容量按点击格已知安全减 1；否则传 -1。
    // 多个组件各调一次，邻居雷数由调用方卷积；result 完整覆写。
    void observe(const Structure::Instance &instance, const Structure::Pool &shapes, const vector<int> &adjacentBoxCells, Structure::BoxId xBox, const Config &config, Observed &result);

    // 按 ShapeId 缓存 DFS 枚举的全部赋值；返回的引用在下一次 insert 后失效。
    const Assignments &assignments(const Structure::Instance &instance, AssignmentPool &pool);

    // componentMines 必须是 ways 里的非零档；unit 必须在 [0, 1) 内。
    // 返回借用 pool.storage.boxMines 的 boxCount 个字节。缓冲区扩容、清空或对应段截断后失效。
    span<const char> sampleAssignment(const AssignmentPool &pool, const Assignments &assignments, int componentMines, long double unit);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    DistributionId combine(const vector<DistributionId> &factors, int tCount, DistributionPool &pool);
};

} // 命名空间 mss
