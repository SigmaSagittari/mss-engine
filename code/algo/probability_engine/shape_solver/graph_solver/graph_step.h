#pragma once

#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"

namespace mss {

// 大 Shape 建图、消元顺序和逐步消元计划；只被 ShapeSolverGraph 使用。
struct ShapeSolverGraphStep {
    struct Check {
        // 约束要求的总雷数；尚未处理 Box 的最大可贡献雷数。
        int sum;
        int remainingSize;
        // 已处理 Box 在旧 frontier 的 slot；一条约束最多 8 个。
        // 危险：复用槽位避免整条记录初始化；makeStep 只写入并读取前 readCount 项。
        int readSlots[8];
        int readCount;
    };
    struct Closing {
        // 本步闭合的 Box；当前正在分配的 Box 用 -1 表示旧 frontier slot。
        Structure::BoxId box;
        int oldSlot;
    };
    // 一步的消元计划；切片借用 Sequence，下一次 makeStep 后失效。
    struct StepPlan {
        Structure::BoxId box;
        int boxSize;
        span<const Check> checks;
        span<const int> gather;
        span<const Closing> closings;
    };
    // 一次消元序列拥有生成状态和计划缓冲；按 step 从 0 开始顺序使用。
    struct Sequence {
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
    struct Workspace {
        // 邻接图和消元顺序的数组由工作区直接持有并跨调用复用。
        vector<int> offsets;
        vector<Structure::BoxId> adjacent;
        // 建图暂存：head/to/next 是按边组织的链表，marked 做邻居去重。
        vector<int> head;
        vector<int> next;
        vector<Structure::BoxId> to;
        vector<char> marked;
        vector<Structure::BoxId> order;
        // 搜索临时副本。
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
        // 步骤计划数组跨调用复用。
        Sequence sequence;
    };

    static void buildAdjacency(const Structure::Shape &shape, const Structure::Pool &shapes, vector<int> &offsets, vector<Structure::BoxId> &adjacent, Workspace &workspace);
    static void makeOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, const ShapeSolverConfig &config, vector<Structure::BoxId> &order, Workspace &workspace);
    static void prepareSteps(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, Sequence &sequence);
    // 返回前完成下一步的布局更新；调用者消费完计划后才能生成下一步。
    static StepPlan makeStep(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, int step, Sequence &sequence);
};

} // 命名空间 mss
