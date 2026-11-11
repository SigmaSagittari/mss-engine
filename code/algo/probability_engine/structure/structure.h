#pragma once

#include <cstddef>
#include <cstdint>

#include "algo/probability_engine/basic/basic.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct Structure {
    using CellId = ObservedBoard::CellId;
    using BoxId = int;
    using ShapeId = int;
    using InstanceId = int;

    // 清池或截断对应段后，切片失效。
    struct Shape {
        struct Constraint {
            int sum; // 数字扣掉相邻的已知雷后剩下的雷数。
            vectorPool<BoxId>::vector boxIds; // 本条约束的 Box 编号，来自 Pool::shapes.boxIds。
        };

        vectorPool<Constraint>::vector constraints; // 来自 Pool::shapes.constraints。
        vectorPool<BoxId>::vector boxIds; // 本形状所有约束的 Box 编号整段，来自 Pool::shapes.boxIds。
    };

    // shape 索引 Pool::shapes.list。清池或截断对应段后，坐标切片失效。
    struct Instance {
        ShapeId shape;
        // 外层切片属于 Pool::instances.boxes，内层格号切片属于 Pool::instances.cells。
        vectorPool<vectorPool<CellId>::vector>::vector boxes;
        // 来自 Pool::instances.constraintCells，顺序与 Shape::constraints 一致。
        vectorPool<CellId>::vector constraintCells;
    };

    struct Result {
        struct CellLocation {
            int instanceIndex; // instanceIds 下标，-1 表示该格不在任何实例中。
            BoxId box; // 数字格和实例之外的格子为 -1。

            CellLocation() : instanceIndex(-1), box(-1) {}
            CellLocation(int instanceIndex, BoxId box) : instanceIndex(instanceIndex), box(box) {}
        };

        // 指向 Pool::instances，清池后失效。
        vector<InstanceId> instanceIds;
        vector<CellLocation> cellLoc;
        // 位 0 到 7 依次为左上、上、右上、左、右、左下、下、右下。
        Grid<std::uint8_t> adjacentH; // 已揭数字的邻接 H 掩码，其余位置为零。
        // H 的邻接数字按行跨度 5 编码，中心位为 6。其他格子的掩码为零。
        Grid<int> adjacentDigits;
    };

    struct Delta {
        struct Change {
            int instanceIndex;
            InstanceId previous;
        };

        // 清池或截断对应段后，切片失效。
        vectorPool<Change>::vector removed; // 来自 Pool::deltas.removed，按原实例下标降序删除。
        vectorPool<InstanceId>::vector added; // 来自 Pool::deltas.added，按追加顺序记录实例 ID。
    };

    struct Pool {
        using BoxPool = vectorPool<vectorPool<CellId>::vector>;

        struct Shapes {
            vectorPool<Shape::Constraint> constraints;
            vectorPool<BoxId> boxIds;
            vector<Shape> list;
            FlatHashTable<U128, ShapeId, U128Hash> index;

            // boxes 属于 boxPool，Box 大小取自其成员切片的 count。
            // 复用已有形状时退掉本次追加的约束和 Box 编号。
            ShapeId intern(Shape shape, const BoxPool &boxPool, BoxPool::vector boxes);
        } shapes;

        struct Instances {
            vectorPool<CellId> cells;
            BoxPool boxes;
            vectorPool<CellId> constraintCells;
            vector<Instance> list;
            FlatHashTable<U128, InstanceId, U128Hash> index;

            // Box 格号按顺序连续追加，命中已有实例时退掉本次追加的数据。
            InstanceId intern(Instance instance);
        } instances;

        struct Deltas {
            vectorPool<Delta::Change> removed;
            vectorPool<InstanceId> added;
        } deltas;

        void clear();
    };

    void analyze(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool);
    // board 和 basic 已更新。两段切片分别来自 basicPool.board 和 basicPool，覆盖上次同步后的全部正向变更。
    // 三份输入不在同一时刻：cellLoc 是旧的划分，board/basic 是最终盘面，两段切片是中间发生的变化。
    // 只作废受影响的块，共三种变动：H 不再是候选格，所在块少了一格；数字旁的已知雷数变了，约束
    // 跟着变；新揭的数字挨着旧块，几个块被它连成一片。
    // 新 H 只会由新揭的数字带入，所以新块里总有「作废块里的 H」或「本批变化的格子」：其余 H 的旧块
    // 没作废，它们仍在原来那块里。以这两类格子为种子重建，就能走全每个新块。
    Delta update(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool,
                 const ObservedBoard::Delta &observed, const Basic::Delta &marks, const Basic::Pool &basicPool);
    // 正向时 result 是改动前的划分，reverse 时是改动后的划分。
    void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);

  private:
    struct Workspace;
    // 取当前线程复用的构建工作区。
    Workspace &getWorkspace();
    // 扩展标记数组，只清零新增槽位。
    void growFlags(vector<std::uint8_t> &flags, std::size_t count);
    int neighborDirection(int deltaRow, int deltaCol);
    // 从一个 H 格出发构建实例，同时写入邻接掩码和格子索引。
    InstanceId buildInstance(CellId start, Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool);
    // 更新实例的格子索引，instanceIndex 为 -1 时同时清除对应的邻接掩码。
    void setLocations(Result &result, const Pool &pool, InstanceId id, int instanceIndex);
    // 应用增量时，从加入的实例恢复邻接掩码。
    void setInstanceMasks(Result &result, const Pool &pool, InstanceId id);
};

} // 命名空间 mss
