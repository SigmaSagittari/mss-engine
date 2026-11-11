#pragma once

#include <functional>

#include "algo/probability_engine/shape_solver/shape_solver_type.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 小 Shape 的完整枚举后端。结果直接使用 ShapeSolverType 的共享类型。
struct ShapeSolverDfs {
    // 一条合法赋值，记录组内累计权重和组件总雷数。
    struct Assignment {
        long double cumulative;
        // 相对于 Assignments::boxMines 切片起点，从这里连续读 Assignments::boxCount 项即各 Box 的雷数。
        int boxMinesOffset;
        int mineCount;
    };

    // 一个 Shape 的全部赋值；entries 按枚举顺序，order 按 mineCount 稳定分组。
    // 五段切片分别来自传给 buildAssignments 的 AssignmentStorage 中的同名池。
    // 缓存中对应 ShapeSolver::AssignmentPool::storage，清池或截断对应段后失效。
    struct Assignments {
        int boxCount;
        vectorPool<char>::vector boxMines;
        vectorPool<Assignment>::vector entries;
        vectorPool<int>::vector order;
        // groupStart 长度为 maxMines + 2，组 m 对应 order[groupStart[m] .. groupStart[m + 1])。
        vectorPool<int>::vector groupStart;
        vectorPool<long double>::vector ways;
    };

    // 赋值缓存的实际存储；按 ShapeId 索引的记录由 ShapeSolver 持有。
    struct AssignmentStorage {
        vectorPool<char> boxMines;
        vectorPool<Assignment> entries;
        vectorPool<int> order;
        vectorPool<int> groupStart;
        vectorPool<long double> ways;
    };

    // 汇总合法赋值，权重和 Box 条件期望追加到 values，返回切片。
    ShapeSolverType::Distribution analyze(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, vectorPool<long double> &values);
    // adjacentBoxCells[b] 是第 b 个 Box 中与点击格相邻的格数；xBox 是点击格所在 Box 或 -1。
    // 结果完整覆写，values 由结果自己持有。
    void observe(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, const vector<int> &adjacentBoxCells, Structure::BoxId xBox, ShapeSolverType::Observed &result);
    // 枚举全部合法赋值；回调借用当前线程的枚举缓冲，回调内再次调用本后端会覆盖该缓冲。
    void forEachAssignment(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, const std::function<void(const vector<char> &assignment, long double ways)> &callback);
    // 随机布雷用：把全部合法赋值追加到 storage，返回切片。
    Assignments buildAssignments(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, AssignmentStorage &storage);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    const Structure::Shape::Constraint *shapeConstraints(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes);
    const Structure::BoxId *constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool::Shapes &shapes);
    // 枚举循环只在 .cpp 内定义，回调借用 workspace.assignment。
    template <typename Callback> void enumerateAssignments(const Structure::Shape &shape, const Structure::Pool::Shapes &shapes, ShapeSolverType::Boxes boxes, Callback &&callback);
};

} // 命名空间 mss
