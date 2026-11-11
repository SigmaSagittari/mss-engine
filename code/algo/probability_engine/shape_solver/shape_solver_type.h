#pragma once

#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// ShapeSolver 的跨文件结果类型；后端直接返回这些类型，上层不再翻译。
struct ShapeSolverType {
    // 外层视图借用 Structure::Pool::instances.boxes，每个元素切自 instances.cells。
    // boxes 池扩容、清空或截断对应段后，外层视图失效。
    using Boxes = span<const vectorPool<Structure::CellId>::vector>;

    // 两段切片均来自后端 analyze 的 values 参数，缓存中对应 ShapeSolver::DistributionPool::values。
    // 清池或截断对应段后失效。
    // ways[i] 对应 start + i 个前沿雷，另有 tCount 个自由 T 格。
    // 实例分布的 expectations 按 [雷数][Box] 排列；逻辑块分布仅提供计数，boxCount 为零。
    struct Distribution {
        vectorPool<long double>::vector ways;
        vectorPool<long double>::vector expectations;
        int start;
        int boxCount;
        int tCount;
    };

    // ways[i] 切自本结果的 values。i 是邻居雷数，第 j 项对应 start[i] + j 个组件雷。
    struct Observed {
        int start[9];
        vectorPool<long double>::vector ways[9];
        vectorPool<long double> values;
    };
};

} // 命名空间 mss
