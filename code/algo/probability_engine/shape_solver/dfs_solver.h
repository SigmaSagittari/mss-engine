#pragma once

#include <functional>

#include "algo/probability_engine/shape_solver/shape_solver_type.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 小 Shape 的完整枚举后端。结果直接使用 ShapeSolverType 的共享类型。
struct ShapeSolverDfs {
    struct Workspace {
        // Box -> 关联约束的边链表；每条约束的当前和、最大可贡献量和已分配容量。
        vector<int> boxHead;
        vector<int> constraintNext;
        vector<int> constraintIds;
        vector<int> constraintSum;
        vector<int> constraintMaxAdd;
        vector<int> currentSum;
        vector<int> assignedSize;
        // 当前枚举到的各 Box 雷数；只在回调期间有效。
        vector<char> assignment;
        struct Frame {
            // 下一层要分配的 Box 和当前 Box 下一种待试的雷数。
            int index;
            int nextMine;
            // 进入本帧时已应用的父层 Box 及其雷数；根帧为 -1 和 0。
            int appliedIndex;
            int appliedMine;
            long double ways;
        };
        vector<Frame> frames;
        // 随机布雷建赋值缓存：每个雷数档的赋值数、计数排序游标和累计权重。
        vector<int> groupCounts;
        vector<int> groupCursor;
        vector<long double> groupWays;
        // 普通分布的 ways 和按组件雷数行累计的矩。
        vector<long double> ways;
        Grid<long double> moments;
        // 观测累计表和卷积缓冲；每次调用覆写，只保留存储容量。
        vectorPool<long double> observedValues;
    };

    // 一条合法赋值：boxMines 池里的起点、本组内累计权重、组件总雷数。
    struct Assignment {
        long double cumulative;
        int boxMinesOffset;
        int mineCount;
    };

    // 一个 Shape 的全部赋值；entries 按枚举顺序，order 按 mineCount 稳定分组。
    struct Assignments {
        int boxCount;
        vectorPool<char>::vector boxMines;
        vectorPool<Assignment>::vector entries;
        vectorPool<int>::vector order;
        // 组 m 是 order[groupStart[m] .. groupStart[m + 1])，长度 = maxMines + 2。
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
    static ShapeSolverType::Distribution analyze(const Structure::Shape &shape, const Structure::Pool &shapes, vectorPool<long double> &values, Workspace &workspace);
    // adjacentBoxCells[b] 是第 b 个 Box 中与点击格相邻的格数；xBox 是点击格所在 Box 或 -1。
    // 结果完整覆写，values 由结果自己持有。
    static void observe(const Structure::Shape &shape, const Structure::Pool &shapes, span<const int> adjacentBoxCells, Structure::BoxId xBox, ShapeSolverType::Observed &result, Workspace &workspace);
    // 枚举全部合法赋值，回调只在调用期间有效。
    static void forEachAssignment(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace, const std::function<void(const vector<char> &assignment, long double ways)> &callback);
    // 随机布雷用：把全部合法赋值追加到 storage，返回切片。
    static Assignments buildAssignments(const Structure::Shape &shape, const Structure::Pool &shapes, AssignmentStorage &storage, Workspace &workspace);

  private:
    // 枚举循环只在 .cpp 内定义，回调借用 workspace.assignment。
    template <typename Callback> static void enumerateAssignments(const Structure::Shape &shape, const Structure::Pool &shapes, Workspace &workspace, Callback &&callback);
};

} // 命名空间 mss
