#pragma once

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/long_term_risk/long_term_risk.h"
#include "algo/winrate_engine/pseudo/pseudo.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// Java 的候选生成：Pseudo 命中时只返回 pseudo 格；否则按前沿安全度、长期风险和空间大小筛选。
struct Selection {
    enum class Reason { Forced, Semi, ScoreWins, Specified };

    struct Workspace;
    struct Result {
        vector<ObservedBoard::CellId> cells;
        vector<Observe::Result> observations;
        Reason reason;
        // 仅普通非安全候选或显式候选在 useRisk=true 时持有本次长期风险结果。
        LongTermRisk::Result risk;

        Result() : reason(Reason::ScoreWins) {}
        // 只交换闲置存储，下一次 find 重建候选及所需的长期风险。
        void swapStorage(Workspace &workspace);
    };

    struct Config {
        Pseudo::Config pseudo;
        LongTermRisk::Config risk;
        long double selectionThreshold1;
        long double selectionThreshold2;
        int spaceThreshold;

        Config(Pseudo::Config pseudo, LongTermRisk::Config risk, long double selectionThreshold1, long double selectionThreshold2, int spaceThreshold) : pseudo(pseudo), risk(risk), selectionThreshold1(selectionThreshold1), selectionThreshold2(selectionThreshold2), spaceThreshold(spaceThreshold) {}
    };

  private:
    struct CellPosition {
        int row;
        int col;

        CellPosition(int row, int col) : row(row), col(col) {}
    };

    struct FrontierBox {
        long double safety;
        Structure::InstanceId instance;
        Structure::BoxId box;

        FrontierBox(long double safety, Structure::InstanceId instance, Structure::BoxId box) : safety(safety), instance(instance), box(box) {}
    };

  public:
    struct Workspace {
        Pseudo::Workspace pseudo;
        LongTermRisk::Workspace risk;
        vector<ObservedBoard::CellId> cellStorage;
        vector<Observe::Result> observationStorage;
        Grid<unsigned> visited;
        unsigned visit;
        vector<CellPosition> stack;
        vector<FrontierBox> boxes;
        vector<ObservedBoard::CellId> onEdge;
        vector<ObservedBoard::CellId> offEdgeCandidates;
        vector<ObservedBoard::CellId> offEdge;
        vector<ObservedBoard::CellId> witnesses;

        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : risk(basic, structure, probability), visit(0) {}
    };

    // 先取安全格，再检测 Pseudo；命中即返回，未命中才按 useRisk 计算长期风险并筛选。
    // cells 非空指针表示显式候选，保持输入顺序；格子必须互异且未揭开，空 vector 表示不搜索。
    // 这里只生成候选；removeDead 单独准备观测，显式候选不删减。
    static void find(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Result &result, Workspace &workspace, bool useRisk, const vector<ObservedBoard::CellId> *cells = nullptr);
    static void removeDead(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, Probability::Workspace &probabilityWorkspace);
    static bool offEdgeCandidate(const ObservedBoard::Result &board, ObservedBoard::CellId cell);

  private:
    static bool contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    static bool meetsSpaceThreshold(int startRow, int startCol, const PositionState &state, int threshold, Workspace &workspace);
    static void window(vector<ObservedBoard::CellId> &result, const PositionState &state, const LongTermRisk::Result *risk, span<const ObservedBoard::CellId> offEdge, span<const ObservedBoard::CellId> witnesses, ObservedBoard::CellId firstOffEdge, const Config &config, Workspace &workspace);
    static bool isDead(const Observe::Result &observation);
};

} // 命名空间 mss
