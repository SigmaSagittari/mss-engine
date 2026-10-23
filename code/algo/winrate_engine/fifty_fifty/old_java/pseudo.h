#pragma once

#include <deque>

#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// Java 的强制 50/50 与半 50/50 检测；只读 PositionState，不持有盘面或 pool。
struct Pseudo {
    struct Config {
        long double eqEps;
        explicit Config(long double eqEps) : eqEps(eqEps) {}
    };

    enum class Kind { None, Forced, Semi };

    struct Result {
        Kind kind;
        vector<ObservedBoard::CellId> cells;

        Result() : kind(Kind::None) {}
    };

    struct Link {
        ObservedBoard::CellId first;
        ObservedBoard::CellId second;
        bool closedFirst;
        bool closedSecond;
        bool deadFirst;
        bool deadSecond;
        bool processed;
        bool pseudo;
        bool unavoidable;
        int witnessSize;
        // 每条 link 最多两个格的邻居；固定容量避免热路径堆分配。
        ObservedBoard::CellId trouble[16];
        int troubleCount;

        Link();
        Link(ObservedBoard::CellId first, ObservedBoard::CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize);
        void reset(ObservedBoard::CellId first, ObservedBoard::CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize);
        void addTrouble(ObservedBoard::CellId cell);
    };

    struct Chain {
        vector<ObservedBoard::CellId> whole5050;
        // -1 表示链已封口且未满足不可避免条件，不再参与后续连接。
        ObservedBoard::CellId openTile;
        bool pseudo;
        vector<ObservedBoard::CellId> trouble;

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
    static Result analyze(const PositionState &state, span<const ObservedBoard::CellId> dead, const Config &config, Workspace &workspace);
    // 按当前全部约束刷新结果并保留容量；不提供局部 Delta。
    static Kind update(Result &result, const PositionState &state, span<const ObservedBoard::CellId> dead, const Config &config, Workspace &workspace);

  private:
    static void collectLinks(const PositionState &state, span<const ObservedBoard::CellId> dead, long double eqEps, vector<Link> &links, int &linkCount, std::deque<vector<ObservedBoard::CellId>> &witnesses, int &witnessCount, Workspace &workspace);
    static Kind extendChains(Result &result, const PositionState &state, span<Link> links, std::deque<Chain> &chains, int &chainCount);
    static Kind connectChains(Result &result, const PositionState &state, span<const ObservedBoard::CellId> dead, long double eqEps, std::deque<Chain> &chains, int chainCount, const std::deque<vector<ObservedBoard::CellId>> &witnesses, int witnessCount, Workspace &workspace);
    static bool pseudoContains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    static bool adjacent(const ObservedBoard::Result &board, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    static bool isMine(const PositionState &state, ObservedBoard::CellId cell, long double eqEps);
    static void assess(Link &link, const PositionState &state, long double eqEps);
    static void findRootedLinks(span<const ObservedBoard::CellId> cells, const PositionState &state, span<const ObservedBoard::CellId> dead, int witnessSize, long double eqEps, vector<Link> &links, int &linkCount);
    static bool noTrouble(const ObservedBoard::Result &board, span<const ObservedBoard::CellId> trouble, span<const ObservedBoard::CellId> chain);
    static bool linkLess(const Link &lhs, const Link &rhs);
};

} // 命名空间 mss
