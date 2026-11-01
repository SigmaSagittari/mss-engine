#pragma once

#include <utility>

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
        struct Constraints {
            vector<int> constraintLast;
            vector<int> boxHead;
            vector<int> nextLink;
            vector<int> constraintIds;
        } constraints;
        struct Close {
            vector<int> closeStep;
            vector<int> closeHead;
            vector<int> closeNext;
        } close;
        struct Frontier {
            vector<int> slotOf;
            vector<Structure::BoxId> layout;
            vector<Structure::BoxId> layoutNext;
        } frontier;
        struct Plan {
            vector<int> gather;
            vector<Check> checks;
            vector<Closing> closings;
        } plan;
    };

    void buildAdjacency(const Structure::Shape &shape, const Structure::Pool &shapes, vector<int> &offsets, vector<Structure::BoxId> &adjacent);
    void makeOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, const ShapeSolverConfig &config, vector<Structure::BoxId> &order);
    void prepareSteps(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, Sequence &sequence);
    // 返回前完成下一步的布局更新；调用者消费完计划后才能生成下一步。
    StepPlan makeStep(const Structure::Shape &shape, const Structure::Pool &shapes, span<const Structure::BoxId> order, int step, Sequence &sequence);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    int shapeBoxCount(const Structure::Shape &shape);
    const int *shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes);
    const Structure::Shape::Constraint *shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes);
    const Structure::BoxId *constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes);
    span<const Structure::BoxId> neighbors(const vector<int> &offsets, const vector<Structure::BoxId> &adjacent, Structure::BoxId box);
    void copyOrder(span<const Structure::BoxId> source, vector<Structure::BoxId> &target);
    std::pair<int, int> orderScore(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, span<const Structure::BoxId> order);
    Structure::BoxId farthestBox(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, Structure::BoxId source);
    void makeGreedyOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, Structure::BoxId first, vector<Structure::BoxId> &order);
    void makeWindow3Order(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, vector<Structure::BoxId> &order);
    void makeSAOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, span<const Structure::BoxId> seed, const ShapeSolverConfig &config, vector<Structure::BoxId> &best);
    void makeAutoOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const ShapeSolverConfig &config, vector<Structure::BoxId> &order);
    void makeAutoSAOrder(const vector<int> &offsets, const vector<Structure::BoxId> &adjacentBoxes, const ShapeSolverConfig &config, vector<Structure::BoxId> &order);
};

} // 命名空间 mss
