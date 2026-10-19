#pragma once

#include <cstdint>

#include "algo/probability_engine/shape_solver/graph_solver/graph_step.h"
#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/shape_solver/shape_solver_type.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 大 Shape 的 frontier DP 后端。结果直接使用 ShapeSolverType 的共享类型。
struct ShapeSolverGraph {
    // 每个 frontier 状态的 Count 用 next 串成一条链，存在全局 counts 数组里。
    // 试过按状态单独分配数组和哈希表，链的局部性和零额外分配最快；警告后人不要随手替换。
    struct State {
        int wordOffset;
        int firstCount;
        int lastCount;
    };

    using StepGenerator = ShapeSolverGraphStep;

    struct Count {
        long double ways;
        int mineCount;
        // 本组矩在 momentValues 里的起点。
        int momentOffset;
        int next;
    };
    // 分布 DP 的一层 frontier；相邻两步在 current/next 之间轮换。
    struct Layer {
        vector<State> states;
        vector<Count> counts;
        vector<Structure::BoxId> momentBoxes;
        vector<long double> momentValues;
        // 每个 frontier slot 用 4 位存 Box 雷数；一个 Box 最多 8 格。
        vector<std::uint64_t> frontierWords;
        FlatHashTable<U128, int, U128Hash> index;

        int frontierValue(const State &state, int slot) const;
        void reset();
        void advance(const StepGenerator::StepPlan &plan, Layer &next) const;
    };

    struct ObservedCount {
        long double ways;
        int componentMines;
        int neighborMines;
        int next;
    };
    // 观测 DP 的一层 frontier，同一 frontier 下按组件雷数和邻居雷数累计。
    struct ObservedLayer {
        vector<State> states;
        vector<ObservedCount> counts;
        vector<char> frontierValues;
        FlatHashTable<U128, int, U128Hash> index;

        void reset();
        void advance(const StepGenerator::StepPlan &plan, ObservedLayer &next, const int *adjacentBoxCells, Structure::BoxId xBox) const;
        ObservedCount &findOrAddCount(State &state, int componentMines, int neighborMines);
    };

    struct Workspace {
        StepGenerator::Workspace steps;
        Layer current, next;
        // 归一化前的各 Box 加权雷数。
        vector<int> momentSlot;
        vector<long double> moments;
        ObservedLayer observedCurrent, observedNext;
    };

    static ShapeSolverType::Distribution analyze(const Structure::Shape &shape, const Structure::Pool &shapes, const ShapeSolverConfig &config, vectorPool<long double> &values, Workspace &workspace);
    // 结果完整覆写，values 由结果自己持有。
    static void observe(const Structure::Shape &shape, const Structure::Pool &shapes, span<const int> adjacentBoxCells, Structure::BoxId xBox, const ShapeSolverConfig &config, ShapeSolverType::Observed &result, Workspace &workspace);
};

} // 命名空间 mss
