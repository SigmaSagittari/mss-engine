#pragma once

#include "core/datastructure/grid.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "algo/winrate_engine/fifty_fifty/old_java/pseudo.h"
#include "game/position_state.h"

namespace mss {

// Java 长期风险：统计 2-tile/2x2 结构被强制标记后可消除的布局数。
// 只读 Pseudo 之外的信息；Result 不携带 pseudos，Pseudo 与本模块正交。
struct LongTermRisk {
    struct Config {
        long double influenceThreshold;
        long double eqEps;

        Config(long double influenceThreshold, long double eqEps) : influenceThreshold(influenceThreshold), eqEps(eqEps) {}
    };

    struct Workspace;
    // 单模板明细：本体和限制各取命中量最大的一个，不计算多模板联合分布。
    struct Details {
        ObservedBoard::CellId cell;
        long double body, restriction;
        explicit Details(ObservedBoard::CellId cell) : cell(cell), body(0.0L), restriction(0.0L) {}
    };
    struct Result {
        Grid<long double> influences;
        // 交换闲置存储；交换后必须 update 再查询，工作区不保留有效结果。
        void swapStorage(Workspace &workspace);

      private:
        friend struct LongTermRisk;
        // 点击该格能消除的风险量；单格查询使用。
        vector<long double> enablers;
        // 同一结果的单格查询缓存，slot 对应 TallyKind；不依赖工作区调用历史。
        Grid<long double> values;
        Grid<unsigned char> ready;
    };

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : basic(basic), structure(structure), probability(probability) {}

        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        vector<ObservedBoard::CellId> changedCells;
        Probability::Result forcedProbability;
        Grid<long double> influenceStorage;
        vector<long double> enablerStorage;
        Grid<long double> valueStorage;
        Grid<unsigned char> readyStorage;
    };

    // dead 使用同盘面编号；临时强制标记在返回前恢复。
    static Result analyze(PositionState &state, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Pseudo::Result *pseudo);
    // 全盘覆写 Result；同一 Workspace 串行复用。
    static void update(Result &result, PositionState &state, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Pseudo::Result *pseudo, Details *details = nullptr);
    // 单格查询；full 必须来自同一 state/config，工作区可替换或复用。
    static long double influence(ObservedBoard::CellId cell, PositionState &state, Result &full, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Details *details = nullptr);
    // 仅详情查询调用；cell 的雷率必须大于 0，返回假设该格为雷后的单模板命中概率。
    static Details describe(ObservedBoard::CellId cell, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);

  private:
    struct CellPosition {
        int row;
        int col;

        CellPosition(int row, int col) : row(row), col(col) {}
    };

    struct CellList {
        ObservedBoard::CellId cells[8];
        int count;

        CellList() : count(0) {}
        void push_back(ObservedBoard::CellId cell) { cells[count++] = cell; }
        span<const ObservedBoard::CellId> span() const { return {cells, count}; }
    };

    enum class TallyKind : unsigned char { Horizontal, Vertical, Box };

    static long double boxTally(ObservedBoard::CellId cell, const PositionState &state);
    static long double countWithForces(PositionState &state, span<const ObservedBoard::CellId> mines, span<const ObservedBoard::CellId> safes, const ShapeSolverConfig &shapeConfig, Workspace &workspace);
    static long double candidateTally(PositionState &state, span<const ObservedBoard::CellId> tiles, span<const CellPosition> source, int mineCount, int maxMissing, int minesLeft, const ShapeSolverConfig &shapeConfig, const Config &config, TallyKind kind, ObservedBoard::CellId cacheCell, Result &result, Workspace &workspace);
    static bool hasSafeSource(const ObservedBoard::Result &board, span<const CellPosition> source);
    static bool contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    static CellList missingMines(const PositionState &state, span<const CellPosition> source, long double eqEps);
    static bool hidden(const PositionState &state, int row, int col);
};

} // 命名空间 mss
