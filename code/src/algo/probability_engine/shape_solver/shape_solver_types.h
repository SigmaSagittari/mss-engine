#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "algo/probability_engine/structure/structure.h"
#include "core/utility/flat_hashtable.h"
#include "core/utility/grid.h"
#include "core/utility/hash.h"

namespace mss {
namespace ShapeSolver {

using DistributionId = int;

struct DfsSolver;
struct GraphSolver;
struct GraphStepGenerator;

// ShapeSolver 的工作区整体传入；各求解器只访问自己的分区，观测暂存单独归类。
struct Workspace {
    struct DfsSolverWorkspace {
      private:
        friend struct DfsSolver;

        // 以下临时缓冲可跨调用顺序复用；同一 Workspace 不得供并发调用共享。
        struct Frame {
            int index; // 下一层要分配的 Box 下标。
            int nextMine; // 当前 Box 下一种待试的雷数。
            int appliedIndex; // 进入本帧时已应用的父层 Box；根帧为 -1。
            int appliedMine; // 父层 Box 对各约束和已分配容量的贡献。
            long double ways; // 到达本帧的具体布局权重。

            Frame(int index, int nextMine, int appliedIndex, int appliedMine, long double ways) : index(index), nextMine(nextMine), appliedIndex(appliedIndex), appliedMine(appliedMine), ways(ways) {}
        };

        // 枚举栈及 Box 到约束的反向索引。
        std::vector<int> boxHead;
        std::vector<int> constraintNext;
        std::vector<int> constraintIds;
        std::vector<int> constraintSum;
        std::vector<int> constraintMaxAdd;
        std::vector<int> currentSum;
        std::vector<int> assignedSize;
        std::vector<char> assignment;
        std::vector<Frame> frames;
        // 普通分布暂存：ways 按组件雷数累计布局权重，moments 同行累计各 Box 的加权雷数。
        std::vector<long double> ways;
        RawGrid<long double> moments;
    } dfs_solver;

    struct GraphSolverWorkspace {
      private:
        friend struct GraphSolver;
        friend struct GraphStepGenerator;

        // 建图结果及去重暂存；Graph 仅借用前两项，下一次建图会覆盖这些数组。
        struct Graph {
            // Box b 的邻居位于 adjacent[offsets[b]..offsets[b + 1])。
            std::vector<int> offsets;
            std::vector<Structure::BoxId> adjacent;
            // 建图时把邻接边暂存为链表；to 与 next 按边下标配对，marked 用于邻居去重。
            std::vector<int> head;
            std::vector<Structure::BoxId> to;
            std::vector<int> next;
            std::vector<char> marked;
        } graph;

        // 顺序打分和 BFS 的暂存容量跨调用复用；makeOrder 返回的顺序由调用方持有。
        struct OrderScratch {
            std::vector<int> remaining;
            std::vector<char> selected;
            std::vector<int> distance;
            std::vector<Structure::BoxId> queue;
        } order_scratch;

        // StepPlan 的变长字段只借用这些缓冲，单次回调期间有效。
        struct Step {
            struct Check {
                // 该约束要求的总雷数及尚未处理 Box 的最大可贡献雷数。
                int sum;
                int remainingSize;
                // 单条数字约束最多涉及周围 8 格，因此只需记录至多 8 个已处理 Box 的旧 slot。
                std::array<int, 8> readSlots;
                int readCount;

                // 危险：固定容量避免逐约束分配；只读取 [0, readCount)，建图时逐项首次写入，复制也只处理有效项。
                explicit Check(int sum) : sum(sum), remainingSize(0), readCount(0) {}
                Check(const Check &other) : sum(other.sum), remainingSize(other.remainingSize), readCount(other.readCount) {
                    for (int i = 0; i < readCount; ++i) readSlots[i] = other.readSlots[i];
                }
                Check &operator=(const Check &other) {
                    sum = other.sum;
                    remainingSize = other.remainingSize;
                    readCount = other.readCount;
                    for (int i = 0; i < readCount; ++i) readSlots[i] = other.readSlots[i];
                    return *this;
                }
            };

            struct Closing {
                // 已闭合 Box 的 ID；当前正在分配的 Box 用 -1 表示旧 frontier slot。
                Structure::BoxId box;
                int oldSlot;

                Closing(Structure::BoxId box, int oldSlot) : box(box), oldSlot(oldSlot) {}
            };

            std::vector<int> position;
            std::vector<int> constraintLast;
            std::vector<int> boxHead;
            std::vector<int> nextLink;
            std::vector<int> constraintIds;
            std::vector<int> closeStep;
            std::vector<int> closeHead;
            std::vector<int> closeNext;
            std::vector<int> slotOf;
            std::vector<Structure::BoxId> layout;
            std::vector<Structure::BoxId> nextLayout;
            std::vector<Check> checks; // 当前 Box 关联的约束检查项。
            std::vector<int> gather; // 新 frontier 每个 slot 的旧 slot 来源，-1 表示当前 Box。
            std::vector<Closing> closings; // 本步闭合的 Box 及其旧 frontier slot。
        } step;

        // Box 到矩槽位的反向表及归一化前的加权雷数矩，均跨调用保留容量。
        std::vector<int> momentSlot;
        std::vector<long double> moments;
        // 归一化缓冲；返回结果需独立持有数据，因此从此处复制。
        std::vector<long double> expectations;

        struct Layer {
            // State 只区分当前未闭合 Box 的赋值；同一 State 的 Count 按组件总雷数累计 ways 和已闭合 Box 的加权雷数。
            struct Count {
                int mineCount;
                long double ways;
                std::size_t momentOffset;
                int next;

                Count(int mineCount, long double ways, std::size_t momentOffset, int next) : mineCount(mineCount), ways(ways), momentOffset(momentOffset), next(next) {}
            };

            struct State {
                std::size_t frontierOffset;
                int firstCount;
                int lastCount;

                State(std::size_t frontierOffset, int firstCount, int lastCount) : frontierOffset(frontierOffset), firstCount(firstCount), lastCount(lastCount) {}
            };

            struct MomentValue {
                long double value;

                // 危险：矩数组很大，此构造函数避免 resize 清零；首次贡献写完每个 Count 的全部矩槽，之后才累加，扩容只搬移已写完的逻辑范围。
                MomentValue() {}
            };

            std::vector<State> states;
            std::vector<Count> counts;
            std::vector<Structure::BoxId> momentBoxes;
            std::vector<MomentValue> momentValues;
            // 每个 frontier slot 用 4 位存 Box 雷数；Structure 中一个 Box 最多含 8 个 H 格。
            std::vector<std::uint64_t> frontierWords;
            FlatHashTable<U128, std::size_t, U128Hash> index;

            std::uint64_t frontierValue(const State &state, int slot) const;
            void reset();
            template <typename Plan> void advance(const Plan &plan, Layer &nextLayer) const;
        } current, next;
    } graph_solver;

    struct ObservedWorkspace {
      private:
        friend struct DfsSolver;
        friend struct GraphSolver;

        // DFS 观测暂存；行是组件雷数，列是邻居雷数 0~8。
        std::vector<std::array<long double, 9>> dfsWays;

        struct GraphLayer {
            // 观测层在同一 frontier 赋值下，再按组件总雷数和邻居雷数分别累计权重。
            struct Count {
                int componentMines;
                int neighborMines;
                long double ways;
                int next;

                Count(int componentMines, int neighborMines, long double ways, int next) : componentMines(componentMines), neighborMines(neighborMines), ways(ways), next(next) {}
            };

            struct State {
                std::size_t frontierOffset;
                int firstCount;
                int lastCount;

                State(std::size_t frontierOffset, int firstCount, int lastCount) : frontierOffset(frontierOffset), firstCount(firstCount), lastCount(lastCount) {}
            };

            std::vector<State> states;
            std::vector<Count> counts;
            std::vector<char> frontierValues;
            FlatHashTable<U128, std::size_t, U128Hash> index;

            Count &findOrAddCount(State &state, int componentMines, int neighborMines);
            void reset();
            template <typename Plan> void advance(const Plan &plan, GraphLayer &nextLayer, const std::vector<int> &adjacentBoxCells, Structure::BoxId xBox) const;
        } graph_current, graph_next;
    } observed;
};

enum class OrderAlgo {
    Adjacent, // 只按相邻关系贪心，开销最低，但顺序质量较差。
    Window3, // 比较局部三步窗口，开销略高，通常能得到更好的顺序。
    SA, // 反复优化候选顺序，开销最高，顺序质量接近最优。
    Auto, // 组合 Adjacent 和 Window3；默认策略，足以应付所有随机盘面。
    AutoSA, // Auto 结果仍不理想时追加 SA，面向人工构造的极端盘面。
};

class DistributionResult {
  public:
    // ways 为空表示此 Shape 没有满足约束的赋值；上层将其作为零候选组件处理。
    // ways[i] 对应 start+i 个组件雷；expectationValues 按 [雷数索引][Box] 行优先连续存储。
    // 前置条件：boxCount 非负，expectationValues.size() 等于 ways.size() 乘以 boxCount。
    DistributionResult(int start, int boxCount, std::vector<long double> ways, std::vector<long double> expectationValues) : start_(start), boxCount_(boxCount), ways_(std::move(ways)), expectationValues_(std::move(expectationValues)) {}

    // ways[i] 对应的实际组件雷数为 start()+i。
    int start() const { return start_; }
    // 返回形状中的 Box 数。
    int boxCount() const { return boxCount_; }
    // 返回每个组件雷数对应的布局权重。
    const std::vector<long double> &ways() const { return ways_; }
    // i 必须小于 ways().size()；返回该组件雷数下各 Box 的期望雷数，视图在本对象移动或销毁后失效。
    std::span<const long double> perBoxExpectation(std::size_t i) const { return std::span<const long double>(expectationValues_).subspan(i * boxCount_, boxCount_); }

  private:
    int start_;
    int boxCount_;
    std::vector<long double> ways_;
    std::vector<long double> expectationValues_;
};

// 按 Shape 内容哈希复用分布；ID 在池清空前有效，哈希按项目约定作为内容身份。
struct DistributionPool {
    // 未命中返回 -1。
    DistributionId find(U128 hash) const {
        if (const DistributionId *found = index_.find(hash))
            return *found;
        return -1;
    }
    // id 必须来自此池；引用在后续插入或清空后失效。
    const DistributionResult &get(DistributionId id) const { return results_[id]; }
    // 仅在 find 未命中时调用；返回新结果的 ID。
    DistributionId insert(U128 hash, DistributionResult result) {
        const DistributionId id = results_.size();
        results_.push_back(std::move(result));
        index_.emplace(hash, id);
        return id;
    }
    // 清空分布缓存并使所有既有 ID 失效。
    void clear() {
        results_.clear();
        index_.clear();
    }

  private:
    std::vector<DistributionResult> results_;
    FlatHashTable<U128, DistributionId, U128Hash> index_;
};

// 仅随机生成使用；按 ShapeId 缓存 DFS 枚举的 Box 雷数赋值，与概率分布池分开。
struct AssignmentPool {
    struct Assignment {
        std::size_t offset;
        long double cumulative;
    };

    struct Result {
        int boxCount;
        std::vector<char> boxMines;
        // assignments[k] 中每项是总雷数为 k 的赋值，cumulative 是该档内的累积布局权重。
        std::vector<std::vector<Assignment>> assignments;
        std::vector<long double> ways;

        // 按 ShapeId 扩容时创建的空槽尚未缓存赋值；只有 boxCount 非负的槽可由 find 返回。
        Result() : boxCount(-1) {}
        explicit Result(int boxCount) : boxCount(boxCount) {}
    };

    const Result *find(Structure::ShapeId shape) const { return shape < (int)results_.size() && results_[shape].boxCount >= 0 ? &results_[shape] : nullptr; }
    const Result &insert(Structure::ShapeId shape, Result result) {
        if (shape >= (int)results_.size())
            results_.resize(shape + 1);
        results_[shape] = std::move(result);
        return results_[shape];
    }
    void clear() { results_.clear(); }

  private:
    std::vector<Result> results_;
};

class ObversedResult {
  public:
    // 九段均为空表示没有满足约束且点击格安全的赋值。
    struct Distribution {
        int start;
        std::size_t offset;
        std::size_t size;
    };

    struct View {
        int start;
        std::span<const long double> ways;
    };

    // distributions 按邻居雷数 0~8 排列；九段紧邻存放在 ways 中，段内允许有零项。
    // 危险：工作区复用结果容量，避免另建临时结果再搬入；描述符不初始化，DFS/Graph observe 首次完整写入九项前不得读取或复制。
    // 后续调用同样完整覆盖描述符，并用 assign 覆盖 ways；容量可跨组件复用。
    ObversedResult() {}

    // neighborMines 必须在 [0, 8]；返回 ways[i] 对应 start+i 个组件雷，视图在结果被再次覆写、移动或销毁后失效。
    View distribution(int neighborMines) const {
        const Distribution &distribution = distributions_[neighborMines];
        return {distribution.start, std::span<const long double>(ways_).subspan(distribution.offset, distribution.size)};
    }

  private:
    friend struct DfsSolver;
    friend struct GraphSolver;

    std::array<Distribution, 9> distributions_;
    std::vector<long double> ways_;
};

}
}
