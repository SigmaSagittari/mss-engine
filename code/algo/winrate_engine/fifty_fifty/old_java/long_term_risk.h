#pragma once

#include "core/datastructure/grid.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "algo/winrate_engine/fifty_fifty/old_java/semi.h"
#include "game/position_state.h"

namespace mss {

// 旧 Java 长期风险：尝试相邻两格和 2x2 交换，统计补上封口雷后能抵扣的布局数。
// 先 update 扫全盘，再按需 influence 补算单格；均临时修改 state，返回前恢复。
struct LongTermRisk {
    struct Config {
        long double influenceThreshold; // 全盘扫描时，单方向模板概率超过此值才计入风险。
        Config(long double influenceThreshold) : influenceThreshold(influenceThreshold) {}
    };

    struct Workspace;
    // describe 输出“已知本格为雷”的条件命中概率，本体和限制各取最大的一个模板。
    // update/influence 的可选明细参数只作 describe 内部累计，此时保存的还是布局数。
    struct Details {
        ObservedBoard::CellId cell;
        long double body, restriction;
        Details(ObservedBoard::CellId cell) : cell(cell), body(0.0L), restriction(0.0L) {}
    };
    struct Result {
        Grid<long double> influences; // [行][列] 的扫描补偿布局数；不是百分比。

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
        Grid<long double> influenceStorage;
        vector<long double> enablerStorage;
        Grid<long double> valueStorage;
        Grid<unsigned char> readyStorage;
    };

    // dead 使用同盘面编号；semi 非空时补找已成立的死猜，并在找到首组时停止扫描。
    static Result analyze(PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Semi::Result *semi);
    // 全盘覆写 Result；同一 Workspace 串行复用。
    static void update(Result &result, PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Semi::Result *semi, Details *details = nullptr);
    // 单格查询返回补偿布局数；full 必须来自同一 state，工作区可替换或复用。
    static long double influence(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, Result &full, const ShapeSolverConfig &shapeConfig, Workspace &workspace, Details *details = nullptr);
    // 仅详情查询调用；cell 的雷率必须大于 0，返回假设该格为雷后的单模板命中概率。
    static Details describe(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);

  private:
    static constexpr long double countTolerance = 1e-9L;
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

    static long double boxTally(ObservedBoard::CellId cell, const PositionState &state, const Probability::Pool &probabilities);
    static long double countWithForces(PositionState &state, span<const ObservedBoard::CellId> mines, span<const ObservedBoard::CellId> safes, const ShapeSolverConfig &shapeConfig, Workspace &workspace);
    static long double candidateTally(PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> tiles, span<const CellPosition> source, int mineCount, int maxMissing, int minesLeft, const ShapeSolverConfig &shapeConfig, TallyKind kind, ObservedBoard::CellId cacheCell, Grid<long double> &values, Grid<unsigned char> &ready, Workspace &workspace);
    static void update(Grid<long double> &influences, vector<long double> &enablers, Grid<long double> &values, Grid<unsigned char> &ready, PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Semi::Result *semi, Details *details);
    static long double influence(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, const vector<long double> &enablers, Grid<long double> &values, Grid<unsigned char> &ready, const ShapeSolverConfig &shapeConfig, Workspace &workspace, Details *details);
    static bool hasSafeSource(const ObservedBoard::Result &board, span<const CellPosition> source);
    static bool contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    static CellList missingMines(const PositionState &state, const Probability::Pool &probabilities, span<const CellPosition> source);
    static bool hidden(const PositionState &state, int row, int col);
};

} // 命名空间 mss
