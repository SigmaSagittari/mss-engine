#pragma once

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// 候选生成、死格过滤及评分前三种剪枝；假设安全同时提供后续评分的推进基准。
struct Selection {
    struct Result {
        // Ordinary：按安全率和空间挑选；Safe：内层已有必安；Specified：调用方指定。
        // Forced：封闭 50-50；Semi：假设候选为雷后必然形成死猜。
        enum class Source { Ordinary, Safe, Specified, Forced, Semi };
        struct Prepared {
            int index; // 原始 cells/observations 中的下标，不是棋盘 CellId。
            int linkedClears; // 只知道本格安全时已有的必安数量，用作额外推进的比较基准。

            Prepared(int index, int linkedClears) : index(index), linkedClears(linkedClears) {}
        };
        vector<ObservedBoard::CellId> cells;
        vector<Observe::Result> observations; // removeDead 填写，与 cells 逐项对应。
        // 评分前按原候选顺序准备；只保留通过剪枝的候选及其推进基准。
        vector<Prepared> prepared;
        vector<U128> identities; // 本次已保留的等价类签名，支持剪枝中断后续算。
        vector<int> equivalent; // 已由先前假设安全确认等价的后续候选下标，无需再次求概率。
        FiftyFifty::Result fiftyFifty;
    };

    struct Config {
        // 优先保留与最高安全率相差不超过此值的候选。
        long double selectionThreshold1;
        // 前沿候选不足两格时允许放宽的安全率差距。
        long double selectionThreshold2;
        // 优先保留能连到至少这么多未知格的位置。
        int spaceThreshold;
        bool interchangeable; // 对换去重：两格独占邻位都是必雷或棋盘外。
        bool equivalence; // 等价类去重：假设安全后得到相同的必安候选集合。
        bool dominance; // 支配去重：假设本格安全后，有一个原本更安全的候选必安。

        Config(long double selectionThreshold1, long double selectionThreshold2, int spaceThreshold, bool interchangeable, bool equivalence, bool dominance) : selectionThreshold1(selectionThreshold1), selectionThreshold2(selectionThreshold2), spaceThreshold(spaceThreshold), interchangeable(interchangeable), equivalence(equivalence), dominance(dominance) {}
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
        FiftyFifty::Workspace fiftyFifty;
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        PositionState::Reveal reveal;
        Grid<unsigned> visited;
        unsigned visit;
        vector<CellPosition> stack;
        vector<FrontierBox> boxes;
        vector<ObservedBoard::CellId> onEdge;
        vector<ObservedBoard::CellId> offEdgeCandidates;
        vector<ObservedBoard::CellId> offEdge;
        vector<ObservedBoard::CellId> witnesses;

        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : fiftyFifty(basic, structure, probability), basic(basic), structure(structure), probability(probability), visit(0) {}
    };

    // useRisk=true 为根层，安全格已由 Position::suggest 处理；内层先取安全格，再检测 Semi。
    // Semi 命中即返回；OldJava 未命中时 useRisk 准备其 LTR 供候选筛选；Templates 不调用旧路线。
    // cells 非空指针表示显式候选，保持输入顺序；格子必须互异且未揭开，空 vector 表示不搜索。
    // 返回本次实际来源；这里只生成候选，removeDead 单独准备观测，显式候选不删减。
    static Result::Source find(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, Workspace &workspace, bool useRisk, const vector<ObservedBoard::CellId> *cells = nullptr);
    // 计算每格的数字/爆炸分布；只从 Ordinary 中删除死格，返回实际 Observe 次数，包含被删掉的格子。
    static int removeDead(Result &result, Result::Source source, PositionState &state, const ShapeSolverConfig &shapeConfig, Probability::Workspace &probabilityWorkspace);
    // removeDead 后从 index=0 顺序调用；根层和内层共用，返回 true 时末项是本格的评分输入。
    // isSafeMove 跳过假设安全；需猜格至多做一次，返回前恢复 state。必雷格没有安全分支。
    // nodes 累加实际的假设安全概率分析次数，被剪掉的候选也按实际计算计数。
    static bool prepare(Result &result, int index, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, int remainingSafe, int forcedSafes, bool isSafeMove, long long &nodes, Workspace &workspace);
    static bool offEdgeCandidate(const ObservedBoard::Result &board, ObservedBoard::CellId cell);

  private:
    static bool interchangeable(const PositionState &state, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    static bool contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    static bool meetsSpaceThreshold(int startRow, int startCol, const PositionState &state, int threshold, Workspace &workspace);
    static void selectOrdinary(vector<ObservedBoard::CellId> &result, const PositionState &state, const LongTermRisk::Result *risk, span<const ObservedBoard::CellId> offEdge, span<const ObservedBoard::CellId> witnesses, ObservedBoard::CellId firstOffEdge, const Config &config, Workspace &workspace);
    static bool isDead(const Observe::Result &observation);
};

} // 命名空间 mss
