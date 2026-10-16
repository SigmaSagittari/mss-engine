#pragma once

#include <array>
#include <span>
#include <vector>

#include "algo/winrate_engine/types.h"

namespace mss {

struct Pseudo {
    struct Config {
        long double eqEps;
        explicit Config(long double eqEps) : eqEps(eqEps) {}
    };

    enum class Kind { None, Forced, OrSafe };

    // 空集表示当前约束中没有找到伪 50/50；CellId 属于输入盘面。
    using Result = std::vector<winrate::CellId>;

  private:
    struct Link {
        winrate::CellId first;
        winrate::CellId second;
        bool closedFirst;
        bool closedSecond;
        bool deadFirst;
        bool deadSecond;
        bool processed;
        bool pseudo;
        bool unavoidable;     // Java 默认 true，发现"能单独提供信息"的邻格即 false
        std::vector<winrate::CellId> trouble; // 只监控到链中一格的格（会打破封闭性）
        int witnessSize;         // Java Link 排序的次级键（witness squares 数）
        Link(winrate::CellId first, winrate::CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize) : first(first), second(second), closedFirst(true), closedSecond(true), deadFirst(deadFirst), deadSecond(deadSecond), processed(false), pseudo(pseudo), unavoidable(true), witnessSize(witnessSize) {}
        void reset(winrate::CellId first, winrate::CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize);
    };
    struct Chain {
        std::vector<winrate::CellId> whole5050;  // 全部组成格
        // -1 表示链已封口且未满足不可避免条件，不再参与后续连接。
        winrate::CellId openTile;
        bool pseudo;
        std::vector<winrate::CellId> trouble;
        Chain(winrate::CellId openTile, bool pseudo) : openTile(openTile), pseudo(pseudo) {}
    };

  public:
    // 保留临时槽及其内部 vector 容量；每次 update 从零计数，阶段间只传本次有效范围。
    struct Workspace {
      private:
        friend struct Pseudo;
        std::vector<Link> links;
        std::vector<Chain> chains;
        std::vector<std::vector<winrate::CellId>> witnesses;
        std::vector<winrate::CellId> cells;
        std::vector<winrate::CellId> combined;
        std::vector<winrate::CellId> trouble;
    };

    // 输入来自同一有效盘面，probability.candidates() > 0；dead 使用同盘面编号。
    static Result analyze(const winrate::View &input, winrate::Cells dead, const Config &config, Workspace &workspace);
    // 按当前全部约束刷新结果并保留容量；链的封闭性可能受远端连接影响，不提供局部 Delta。
    static Kind update(Result &result, const winrate::View &input, winrate::Cells dead, const Config &config, Workspace &workspace);

  private:
    static void collectLinks(const winrate::View &input, winrate::Cells dead, long double eqEps, std::vector<Link> &links, int &linkCount, std::vector<std::vector<winrate::CellId>> &witnesses, int &witnessCount, Workspace &workspace);
    static Kind extendChains(Result &result, const winrate::Board &board, std::span<Link> links, std::vector<Chain> &chains, int &chainCount);
    static Kind connectChains(Result &result, const winrate::View &input, winrate::Cells dead, long double eqEps, std::span<Chain> chains, std::span<const std::vector<winrate::CellId>> witnesses, Workspace &workspace);
    static bool pseudoContains(winrate::Cells cells, winrate::CellId cell);
    static bool adjacent(const winrate::Board &board, winrate::CellId lhs, winrate::CellId rhs);
    static bool isMine(const winrate::View &input, winrate::CellId cell, long double eqEps);
    static void assess(Link &link, const winrate::View &input, long double eqEps);
    static void findRootedLinks(winrate::Cells cells, const winrate::View &input, winrate::Cells dead, int witnessSize, long double eqEps, std::vector<Link> &links, int &linkCount);
    static bool noTrouble(const winrate::Board &board, winrate::Cells trouble, winrate::Cells chain);
    static bool linkLess(const Link &lhs, const Link &rhs);
};

} // 命名空间 mss
