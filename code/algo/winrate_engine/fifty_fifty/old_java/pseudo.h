#pragma once

#include <deque>

#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// 旧 Java 死猜检测：从数字约束拼出交换边，再连接成链；只读 PositionState。
struct Pseudo {
    // Forced：封闭的二选一死猜；Semi：候选为雷时形成死猜，点击它可获得全额本体抵扣。
    enum class Kind { None, Forced, Semi };

    struct Result {
        Kind kind;
        vector<ObservedBoard::CellId> cells; // 这一组里可以作为首招的本体格，kind=None 时为空。

        Result() : kind(Kind::None) {}
    };

    // 一条待连接的交换边，以及两端尚未被雷墙挡住的信息位置。
    struct Link {
        ObservedBoard::CellId first;
        ObservedBoard::CellId second;
        bool closedFirst, closedSecond; // 该端独占的信息位是否已封口。
        bool deadFirst, deadSecond; // 调用方传入的死格集合是否包含该端。
        bool processed; // 拼链时是否已经使用这条边。
        bool pseudo; // 从较大的候选组中取出的 Semi 边。
        bool unavoidable; // 两端所有独占信息位都被封住，不需要再接边。
        int witnessSize; // 来源约束的候选格数；用于优先处理较小的结构。
        // 每条 link 最多两个格的邻居；固定容量避免热路径堆分配。
        ObservedBoard::CellId trouble[16];
        int troubleCount;

        Link();
        Link(ObservedBoard::CellId first, ObservedBoard::CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize);
        void reset(ObservedBoard::CellId first, ObservedBoard::CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize);
        void addTrouble(ObservedBoard::CellId cell);
    };

    struct Chain {
        vector<ObservedBoard::CellId> whole5050; // 已连入这条链的本体格。
        // -1 表示链已封口且未满足不可避免条件，不再参与后续连接。
        ObservedBoard::CellId openTile;
        bool pseudo; // 链中是否含有 Semi 边。
        vector<ObservedBoard::CellId> trouble; // 尚待后续连接平衡的信息格。

        Chain(ObservedBoard::CellId openTile, bool pseudo) : openTile(openTile), pseudo(pseudo) {}
    };

    // 链接和链保留容量；每次 update 从零计数，阶段间只传本次有效范围。
    struct Workspace {
        vector<Link> links;
        std::deque<Chain> chains;
        std::deque<vector<ObservedBoard::CellId>> witnesses;
        vector<ObservedBoard::CellId> cells;
        vector<ObservedBoard::CellId> combined;
        vector<ObservedBoard::CellId> trouble;
    };

    // 输入来自同一有效盘面，probability.candidates > 0；dead 使用同盘面编号。
    static Result analyze(const PositionState &state, span<const ObservedBoard::CellId> dead, Workspace &workspace);
    // 按当前全部约束刷新结果并保留容量；target=-1 选首组，否则只返回包含该格的结果。
    static Kind update(Result &result, const PositionState &state, span<const ObservedBoard::CellId> dead, Workspace &workspace, ObservedBoard::CellId target = -1);

  private:
    static void collectLinks(const PositionState &state, span<const ObservedBoard::CellId> dead, vector<Link> &links, int &linkCount, std::deque<vector<ObservedBoard::CellId>> &witnesses, int &witnessCount, Workspace &workspace);
    static Kind extendChains(Result &result, const PositionState &state, span<Link> links, std::deque<Chain> &chains, int &chainCount, ObservedBoard::CellId target);
    static Kind connectChains(Result &result, const PositionState &state, span<const ObservedBoard::CellId> dead, std::deque<Chain> &chains, int chainCount, const std::deque<vector<ObservedBoard::CellId>> &witnesses, int witnessCount, Workspace &workspace, ObservedBoard::CellId target);
    static bool pseudoContains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    static bool adjacent(const ObservedBoard::Result &board, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    static void assess(Link &link, const PositionState &state);
    static void findRootedLinks(span<const ObservedBoard::CellId> cells, const PositionState &state, span<const ObservedBoard::CellId> dead, int witnessSize, vector<Link> &links, int &linkCount);
    static bool noTrouble(const ObservedBoard::Result &board, span<const ObservedBoard::CellId> trouble, span<const ObservedBoard::CellId> chain);
    static bool linkLess(const Link &lhs, const Link &rhs);
};

} // 命名空间 mss
