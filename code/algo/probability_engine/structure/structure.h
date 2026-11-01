#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct Structure {
    using ComponentId = int;
    using BoxId = int;
    using ShapeId = int;
    using InstanceId = int;
    using CellId = ObservedBoard::CellId;

    // 位置无关的形状，内容切片都存放在 Pool::Shapes 里；切片只在池存活期间有效。
    struct Shape {
        // 单条数字约束；boxIds 是 Pool::Shapes::boxIds 的绝对切片，不是相对 Shape 的偏移。
        struct Constraint {
            // 扣除相邻 F 后，仍需由相邻 H 格承担的雷数。
            int sum;
            // 本条约束涉及的 Box；单条数字约束最多 8 个 Box。
            vectorPool<BoxId>::vector boxIds;
        };

        // 每个 Box 的格子数；同一 Box 的 H 格共享非空数字邻接集合，因此最多 8 格。
        vectorPool<int>::vector boxSizes;
        vectorPool<Constraint>::vector constraints;
        vectorPool<BoxId>::vector boxIds;
        // 内容哈希同时作为池去重身份，不再做结构相等核验。
        U128 hash;
    };

    // Instance 把 Shape 的 Box 和数字约束映射回当前盘面。
    struct Instance {
        ShapeId shape;
        // 按 Box 连续排列的盘面格子，CellId 使用零起始编号。
        vectorPool<CellId>::vector cells;
        // Box 前缀和，长度 = Box 数 + 1；首项恒为 0，末项恒等于 cells.count。
        vectorPool<int>::vector boxOf;
        // 与 Shape::constraints 一一对应，保存每条数字约束所在的 CellId。
        vectorPool<CellId>::vector constraintCells;
    };

    struct Result {
        struct CellLocation {
            // components 下标；-1 表示该格不属于组件，数字约束格属于组件但不属于 Box。
            ComponentId component;
            // H 格指向所属 Box；数字约束格为 -1。
            BoxId box;

            CellLocation() : component(-1), box(-1) {}
            CellLocation(ComponentId component, BoxId box) : component(component), box(box) {}
        };

        // 每项是 Pool 中的 InstanceId；ComponentId 是此数组中的位置，不是 InstanceId。
        vector<InstanceId> components;
        // 按 CellId 索引，长度为 rows * cols。
        vector<CellLocation> cellLoc;
    };

    struct Delta {
        struct Change {
            ComponentId component;
            // 更新前占据该组件位置的 InstanceId。
            InstanceId previous;
        };

        // 被删除的旧组件按下标降序记录，逆序恢复时依赖此顺序。
        vectorPool<Change>::vector removed;
        // 按追加顺序保存实例 ID；撤回先弹出这些尾项。
        vectorPool<InstanceId>::vector added;
    };

    // 本层的数据、索引和 Delta 记录共用一个池，引用的 Basic 池须覆盖本池的使用期。
    // ID 只在本池未清空时有效，切片在池扩容后仍有效。
    struct Pool {
        Basic::Pool &basic;

        // Shape 内容、本体与去重索引
        struct Shapes {
            vectorPool<int> boxSizes;
            vectorPool<Shape::Constraint> constraints;
            vectorPool<BoxId> boxIds;
            vector<Shape> list;
            FlatHashTable<U128, ShapeId, U128Hash> index;

            // 按内容哈希复用已有形状；命中时退掉传入 Shape 的内容切片。
            ShapeId intern(Shape shape);
        } shapes;

        // Instance 内容、本体与去重索引
        struct Instances {
            vectorPool<CellId> cells;
            vectorPool<int> boxOf;
            vectorPool<CellId> constraintCells;
            vector<Instance> list;
            FlatHashTable<U128, InstanceId, U128Hash> index;

            // 按 Shape ID 和盘面布局哈希复用已有实例；命中时退掉传入 Instance 的内容切片。
            InstanceId intern(Instance instance);
        } instances;

        // Delta 记录
        struct Deltas {
            vectorPool<Delta::Change> removed;
            vectorPool<InstanceId> added;
        } deltas;

        Pool(Basic::Pool &basic) : basic(basic) {}
        // 只清空本层存储；调用方同步清空依赖这些 Shape/Instance 的缓存和结果。
        void clear();
    };

    // 输入必须尺寸相同、Basic 与此观测盘面一致；只为 H 和相邻数字建立组件，不收录 T。
    void analyze(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool);

    // changedCells 覆盖自 result 状态以来的全部观测变化；board/basic 是最终状态，Pool 与 result 匹配。
    // 只重建受影响组件，delta 记录回到调用前 result 所需的信息。
    Delta update(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool, const CellId *changedCells, int changedCount);

    // 正向时 result 对应更新前状态；reverse 时对应更新后状态且必须后进先出。池不得清空。
    void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    U128 cellSignature(int row, int col, const ObservedBoard::Result &board);
    void remapInstance(Structure::InstanceId id, Structure::ComponentId component, const Structure::Pool &pool, Structure::Result &result);
    void clearInstance(Structure::InstanceId id, const Structure::Pool &pool, Structure::Result &result);
    void collectComponent(Structure::CellId start, const ObservedBoard::Result &board, const Basic::Result &basic, Grid<char> &visited, vector<Structure::CellId> &cells);
    U128 computeShapeHash(vectorPool<int>::vector boxSizes, vectorPool<Structure::Shape::Constraint>::vector constraints, const Structure::Pool::Shapes &pool);
    U128 computeInstanceHash(const Structure::Instance &instance, const Structure::Pool::Instances &pool);
    InstanceId buildComponent(const vector<CellId> &componentCells, const ObservedBoard::Result &board, const Basic::Result &basic, Grid<int> &boxOfCell, Pool &pool);
};

} // 命名空间 mss
