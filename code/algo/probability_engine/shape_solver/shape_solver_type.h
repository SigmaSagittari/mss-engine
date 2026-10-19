#pragma once

#include "core/datastructure/vector_pool.h"

namespace mss {

// ShapeSolver 的跨文件结果类型；后端直接返回这些类型，上层不再翻译。
struct ShapeSolverType {
    // ways[i] 对应 start + i 个组件雷；expectations 行优先，[雷数][Box]。
    struct Distribution {
        vectorPool<long double>::vector ways;
        vectorPool<long double>::vector expectations;
        int start;
        int boxCount;
    };

    // 邻居雷数 i 对应 ways[i]，第 j 项是 start[i] + j 个组件雷；values 由结果自己持有。
    struct Observed {
        int start[9];
        vectorPool<long double>::vector ways[9];
        vectorPool<long double> values;
    };
};

} // 命名空间 mss
