#pragma once

#include <functional>

#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"

namespace mss {

// 大 Shape 建图、消元顺序和逐步消元计划；只被 ShapeSolverGraph 使用。
struct ShapeSolverGraphStep {
    struct Workspace;
    // 邻接图和消元顺序拥有自己的存储；工作区只交换已失效的存储容量。
    struct Adjacency {
        // Box b 的邻居是 adjacent[offsets[b] .. offsets[b + 1])，offsets 长度 = Box 数 + 1。
        vector<int> offsets;
        vector<Structure::BoxId> adjacent;
        vector<Structure::BoxId> order;

        void swapStorage(Workspace &workspace);
    };
    struct Check {
        // 约束要求的总雷数；尚未处理 Box 的最大可贡献雷数。
        int sum;
        int remainingSize;
        // 已处理 Box 在旧 frontier 的 slot；一条约束最多 8 个。
        int readSlots[8];
        int readCount;
    };
    struct Closing {
        // 本步闭合的 Box；当前正在分配的 Box 用 -1 表示旧 frontier slot。
        Structure::BoxId box;
        int oldSlot;
    };
    // 一步的消元计划；切片借用 Workspace 的 checks/gather/closings，只在回调期间有效。
    struct StepPlan {
        Structure::BoxId box;
        int boxSize;
        span<const Check> checks;
        span<const int> gather;
        span<const Closing> closings;
    };
    struct Workspace {
        // 邻接图的闲置存储；有效数据由交换存储后的 Adjacency 持有。
        vector<int> offsets;
        vector<Structure::BoxId> adjacent;
        // 建图暂存：head/to/next 是按边组织的链表，marked 做邻居去重。
        vector<int> head;
        vector<int> next;
        vector<Structure::BoxId> to;
        vector<char> marked;
        // order 只交换结果存储，其余是搜索临时副本。
        vector<Structure::BoxId> order;
        vector<Structure::BoxId> candidate;
        vector<Structure::BoxId> best;
        vector<Structure::BoxId> current;
        // SA 的初始顺序，与 current/candidate 分开。
        vector<Structure::BoxId> seed;
        // 打分与 BFS 暂存。
        vector<int> remaining;
        vector<char> selected;
        vector<int> distance;
        vector<Structure::BoxId> queue;
        // 逐步消元暂存。
        vector<int> position;
        vector<int> constraintLast;
        vector<int> boxHead;
        vector<int> nextLink;
        vector<int> constraintIds;
        vector<int> closeStep;
        vector<int> closeHead;
        vector<int> closeNext;
        vector<int> slotOf;
        vector<Structure::BoxId> layout;
        vector<Structure::BoxId> layoutNext;
        vector<int> gather;
        vector<Check> checks;
        vector<Closing> closings;
    };

    using StepCallback = std::function<void(const StepPlan &plan)>;

    static void buildAdjacency(const Structure::Shape &shape, const Structure::Pool &shapes, Adjacency &result, Workspace &workspace);
    static void makeOrder(const Adjacency &adjacency, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, Workspace &workspace);
    static void forEachStep(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, Workspace &workspace, const StepCallback &callback);
};

} // 命名空间 mss
