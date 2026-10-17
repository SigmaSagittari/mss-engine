#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "core/utility/flat_hashtable.h"
#include "core/utility/grid.h"
#include "core/utility/hash.h"
#include "core/utility/vector_pool.h"

namespace mss {

struct Structure {
    using ComponentId = int;
    using BoxId = int;
    using ShapeId = int;
    using InstanceId = int;

    // Shape 描述位置无关的约束形状；Instance 再把 Box 和数字约束映射回当前盘面。
    struct Pool;

    struct Shape {
      public:
        struct Box {
            // 同一 Box 的 H 格共享非空数字邻接集合，因此最多 8 格；无邻数字的 H 格单独成组件。
            int size;

            Box(int size) : size(size) {}
        };

        struct ConstraintView {
            int sum;
            std::span<const BoxId> boxIds;

            ConstraintView(int sum, std::span<const BoxId> boxIds) : sum(sum), boxIds(boxIds) {}
        };

        // 这些 vectorPool::vector 只是池内数组切片，必须配合同一个 Pool 读取。
        vectorPool<Box>::vector boxes;
        // 内容哈希同时作为池去重身份，不再做结构相等核验。
        U128 hash;

        // 返回 Shape 中数字约束的数量。
        std::size_t constraintCount() const;
        // 返回的视图引用 Pool 存储；Pool 后续写入或清空后，不得继续使用该视图。
        // pool 必须是保存此 Shape 的池，i 必须小于 constraintCount()。
        ConstraintView constraint(const Pool &pool, std::size_t i) const;

      private:
        friend struct Structure;
        friend struct Pool;

        struct Constraint {
            // sum 是扣除相邻 F 后，仍需由相邻 H 格承担的雷数。
            int sum;
            // boxIds_ 中本条约束的起点和 Box 数；单条数字约束最多涉及 8 个 Box。
            std::uint32_t offset;
            std::uint8_t count;

            Constraint(int sum, std::uint32_t offset, std::uint8_t count) : sum(sum), offset(offset), count(count) {}
        };

        vectorPool<Constraint>::vector constraints_;
        vectorPool<BoxId>::vector boxIds_;

        Shape(vectorPool<Box>::vector boxes, U128 hash, vectorPool<Constraint>::vector constraints, vectorPool<BoxId>::vector boxIds) : boxes(boxes), hash(hash), constraints_(constraints), boxIds_(boxIds) {}
    };

    struct Instance {
        ShapeId shape;
        struct Boxes {
            // 按 Box 连续排列的真实盘面格子；CellId 使用零起始编号。
            vectorPool<ObservedBoard::CellId>::vector cells;
            // Box 的前缀偏移；第 b 个 Box 的格子范围是 [boxOf[b], boxOf[b + 1])。
            vectorPool<int>::vector boxOf;

            // boxOf 含首尾哨兵，Box 数量为偏移数减一。
            std::size_t count() const;
            // pool 必须保存此 Instance，box 必须小于 count()；返回该 Box 的格子数。
            std::size_t cellCount(const Pool &pool, std::size_t box) const;
        };

        Boxes boxes;
        // 顺序与 Shape::constraints_ 一一对应，保存每条数字约束所在的 CellId。
        vectorPool<ObservedBoard::CellId>::vector constraintCells;

        Instance(ShapeId shape, Boxes boxes, vectorPool<ObservedBoard::CellId>::vector constraintCells) : shape(shape), boxes(boxes), constraintCells(constraintCells) {}
    };

    struct Result {
        struct CellLocation {
            // component 是此 Result::components 下标；只在当前 Result 状态内有效，更新时可能因紧凑搬移而变化。
            ComponentId component;
            // H 格指向所属 Box；数字约束格为 -1。
            BoxId box;

            // -1 表示该格不属于组件；数字约束格属于组件但不属于 Box。
            CellLocation() : component(-1), box(-1) {}
            CellLocation(ComponentId component, BoxId box) : component(component), box(box) {}
        };

        // 每项是 Pool 中的 InstanceId；ComponentId 是此数组中的位置，不是 InstanceId。
        std::vector<InstanceId> components;
        // 按 0-based CellId 索引，长度为 rows * cols。
        std::vector<CellLocation> cellLoc;
    };

    struct Delta {
        struct Change {
            ComponentId component;
            // 更新前占据该组件位置的 InstanceId。
            InstanceId previous;

            Change(ComponentId component, InstanceId previous) : component(component), previous(previous) {}
        };

        // 被删除的旧组件按下标降序记录；逆序恢复时依赖此顺序。
        vectorPool<Change>::vector removed;
        // 按追加顺序保存实例 ID；重放复用池内实例，撤回先弹出这些尾项。
        vectorPool<InstanceId>::vector added;
        Delta(vectorPool<Change>::vector removed, vectorPool<InstanceId>::vector added) : removed(removed), added(added) {}
    };

    // 池拥有 Shape、Instance 及其切片的存储；ID 只在池未 clear 时有效。
    struct Pool {
        vectorPool<Shape::Box> boxes;
        vectorPool<Shape::Constraint> constraints;
        vectorPool<BoxId> boxIds;
        vectorPool<ObservedBoard::CellId> cells;
        vectorPool<int> boxOf;
        vectorPool<ObservedBoard::CellId> constraintCells;
        // Delta 与 Shape、Instance 共用本池生命周期；退尾只回收对应的 Delta 记录。
        vectorPool<Delta::Change> removed;
        vectorPool<InstanceId> added;

        // 按 Shape 内容哈希复用已有形状，哈希命中不再逐项比对。
        ShapeId internShape(Shape shape);
        // 按 Shape ID 和盘面布局哈希复用已有实例，哈希命中不再逐项比对。
        InstanceId internInstance(Instance instance);
        // id 必须来自此池；引用和 Shape 中的 span 在池写入扩容或 clear 后可能失效。
        const Shape &getShape(ShapeId id) const;
        // 1.4 的现有调用点仍使用此名称；与 getShape 返回同一对象。
        const Shape &get(ShapeId id) const;
        // id 必须来自此池；池写入扩容或 clear 后不得继续使用返回的引用。
        const Instance &getInstance(InstanceId id) const;
        // 返回已缓存 Shape 数量，不包括 Instance。
        std::size_t size() const;
        // 清空后所有既有 ShapeId、InstanceId 和相关视图均失效；底层容量保留。
        void clear();

      private:
        static U128 computeInstanceHash(const Instance &instance, const Pool &pool);

        std::vector<Shape> shapes_;
        FlatHashTable<U128, ShapeId, U128Hash> shapeIndex_;
        std::vector<Instance> instances_;
        FlatHashTable<U128, InstanceId, U128Hash> instanceIndex_;
    };

    // 临时空间可串行复用；同一工作区不能被并发的 analyze/update 共用。
    struct Workspace {
        struct Analyze {
            RawGrid<char> visited;
            RawGrid<U128> cellHash;
            std::vector<ObservedBoard::CellId> cells;
        } analyze;

        struct Update {
            RawGrid<char> visited;
            RawGrid<U128> cellHash;
            std::vector<ObservedBoard::CellId> cells;
            RawGrid<char> dirty;
            std::vector<ObservedBoard::CellId> dirtyCells;
            std::vector<char> removed;
        } update;

        FlatHashTable<U128, BoxId, U128Hash> hashBox;
        std::vector<std::array<ObservedBoard::CellId, 9>> buckets;
        std::vector<char> boxUsed;
    };

  private:
    // Num0~Num8 的底层值连续为 0~8；判断和取值都依赖此编码约定。
    static bool isNumber(ObservedBoard::CellState state);
    static int numberValue(ObservedBoard::CellState state);
    // 为一个 H 格收集相邻数字的坐标种子，签名相同的 H 格归入同一个 Box。
    static U128 cellSignature(int row, int col, const ObservedBoard::Result &board);
    // 在 Result::CellLocation 中登记或清除一个实例的 H 格与数字约束格。
    static void remapInstance(InstanceId instance, ComponentId component, const Pool &pool, std::vector<Result::CellLocation> &cellLoc);
    static void clearInstance(InstanceId instance, const Pool &pool, std::vector<Result::CellLocation> &cellLoc);
    // 沿“数字—相邻 H 格”二部关系收集一个连通组件；T 格不参与遍历。
    static void collectComponent(ObservedBoard::CellId start, const ObservedBoard::Result &board, const Basic::Result &basic,
                                 RawGrid<char> &visited, std::vector<ObservedBoard::CellId> &cells);
    // 按数字邻接签名合并 H 格为 Box，并构造扣除相邻 F 后的约束。
    static InstanceId buildComponent(const std::vector<ObservedBoard::CellId> &cells, const ObservedBoard::Result &board,
                                     const Basic::Result &basic, RawGrid<U128> &cellHash, Pool &pool, Workspace &workspace);
    // 对 Box 大小和数字约束序列生成 Shape 的内容身份。
    static U128 computeHash(vectorPool<Shape::Box>::vector boxes, vectorPool<Shape::Constraint>::vector constraints, vectorPool<BoxId>::vector boxIds, const Pool &pool);

  public:
    // 输入必须尺寸相同、Basic 与此观测盘面一致且满足其约束；只为 H 和相邻数字建立组件，不收录 T。
    static Result analyze(const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool, Workspace &workspace);
    // changedCells 覆盖自 result 状态以来的全部观测变化；board/basic 是经正常 Basic::update 得到的最终状态，尺寸相同，Pool 与 result 匹配。
    // 允许合并多轮变化；新增连接位于变化邻域，Basic 的远端传播由命中的整组旧组件覆盖，重建只读取最终状态。
    // 只重建受影响组件，delta 记录回到调用前 result 所需的信息。
    static Delta update(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic, Pool &pool,
                       std::span<const ObservedBoard::CellId> changedCells, Workspace &workspace);
    // 正向时 result 对应更新前状态；reverse 时对应更新后状态且必须后进先出。切片沿 Delta 的记录顺序且只在调用期间借用；池不得清空，重放组件及格子映射，不改动 board/basic。
    static void applyDelta(Result &result, const Pool &pool, const Delta &delta, bool reverse = false);
};

} // 命名空间 mss
