#pragma once

#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "algo/winrate_engine/types.h"
#include "pseudo_helper.h"

namespace mss {

struct LongTermRisk {
    struct Config {
        long double influenceThreshold;
        long double eqEps;
        Config(long double influenceThreshold, long double eqEps) : influenceThreshold(influenceThreshold), eqEps(eqEps) {}
    };

  private:
    friend struct JavaEval;
    friend struct MoveEval;
    friend struct AnytimeSearch;

  public:
    struct Result {
        RawGrid<long double> influences;

      private:
        friend struct LongTermRisk;
        friend struct JavaEval;
        friend struct MoveEval;
        std::vector<long double> enablers;
        std::vector<winrate::CellId> pseudos;
        Pseudo::Kind pseudoKind;
        Result() : pseudoKind(Pseudo::Kind::None) {}
    };

    struct Workspace {
      public:
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : basic(basic), structure(structure), probability(probability) {}

      private:
        friend struct LongTermRisk;
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        // 横、竖、方块的单格查询缓存；只读取 ready 置位的槽。
        std::array<std::vector<long double>, 3> values;
        std::array<std::vector<unsigned char>, 3> ready;
        ObservedBoard::Delta boardDelta;
        Basic::Delta basicDelta;
        Structure::Delta structureDelta;
        std::vector<winrate::CellId> changedCells;
        // winrate::Probability 不允许空构造；首次以真实分支构造，以后复用输出容量。
        std::optional<winrate::Probability> forcedProbability;
    };

    // 返回原全盘扫描的 tiles + enablers，单位为布局数，非单格查询的最大值与钳制结果。
    // 输入来自同一有效盘面且候选数 > 0；临时修改三个结果并在返回前逆序恢复。
    // Result 独立持有影响网格、enabler 和 pseudo；评分不依赖扫描工作区的历史。
    // 池保留临时分支的缓存；同一工作区及其引用的下层工作区不可并发或重入。
    static Result analyze(const winrate::Input &input, winrate::Cells dead, const Config &cfg, Workspace &workspace);
    // 全盘覆写 Result；全局雷数耦合使本层没有可局部撤回的 Delta。
    static void update(Result &out, const winrate::Input &input, winrate::Cells dead, const Config &cfg, Workspace &workspace);
  private:
    // 每次评分开始时重置占用标记；缓存只在该次评分内复用，丢弃不改变结果。
    static void resetCache(Workspace &workspace, int size);
    // 单格查询读取同盘面扫描结果；工作区缓存仅避免本次评分内重复计数。
    static long double influence(winrate::CellId cell, const winrate::Input &input, const Result &full, const Config &cfg, Workspace &workspace);
    // mines/safes 不相交、不重复，均为 Hidden；矛盾假设计数为 0，返回前恢复输入。
    static long double countWithForces(const winrate::Input &input, winrate::Cells mines, winrate::Cells safes, Workspace &workspace);

    struct CellList {
        std::array<winrate::CellId, 8> cells;
        int count;

        // 危险：固定容量避免热路径堆分配；只读取 [0, count)，push_back 首次写入，复制只处理有效项。
        CellList() : count(0) {}
        CellList(const CellList &other);
        CellList &operator=(const CellList &other);
        void push_back(winrate::CellId cell);
        const winrate::CellId *begin() const;
        const winrate::CellId *end() const;
        winrate::Cells span() const;
    };
    enum class TallyKind : unsigned char { Horizontal, Vertical, Box };

    template <typename Compute>
    static long double cachedTally(Workspace &workspace, TallyKind kind, int index, Compute &&compute) {
        const int slot = (int)kind;
        if (workspace.ready[slot][index]) return workspace.values[slot][index];
        const long double value = compute();
        workspace.values[slot][index] = value;
        workspace.ready[slot][index] = 1;
        return value;
    }

    static std::vector<winrate::CellId> influencedTiles(const Result &risk, long double threshold, const winrate::View &input, winrate::Cells dead);
    static bool contains(winrate::Cells cells, winrate::CellId cell);
    static bool hidden(const winrate::Board &board, const winrate::Basic &basic, int x, int y);
    static CellList missingMines(const winrate::View &input, std::span<const std::pair<int, int>> source, long double eqEps);
    static bool hasRevealedSource(const winrate::Board &board, std::span<const std::pair<int, int>> source);
    static long double boxTally(winrate::CellId cell, const winrate::View &input);
    static long double candidateTally(const winrate::Input &input, winrate::Cells tiles, std::span<const std::pair<int, int>> source, int mineCount, int maxMissing, int minesLeft, long double eqEps, TallyKind kind, int index, Workspace &workspace);
};

} // 命名空间 mss
