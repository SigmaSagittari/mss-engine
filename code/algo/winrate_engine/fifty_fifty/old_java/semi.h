#pragma once

#include <deque>

#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "game/position.h"

namespace mss {

// 旧 Java 死猜检测：从数字约束拼出交换边，再连接成链；只读当前局面。
struct Semi {
    // Forced：封闭的二选一死猜；Semi：候选为雷时形成死猜，点击它可获得全额本体抵扣。
    enum class Kind { None, Forced, Semi };

    struct Result {
        Kind kind;
        vector<ObservedBoard::CellId> cells; // 这一组里可以作为首招的本体格，kind=None 时为空。

        Result() : kind(Kind::None) {}
    };

    // 输入来自同一有效盘面，probability.totalWays > 0，dead 使用同盘面编号。
    Result analyze(const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<ObservedBoard::CellId> &dead);
    // 按当前全部约束刷新结果并保留容量。target=-1 选首组，否则只返回包含该格的结果。
    Kind update(vector<ObservedBoard::CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<ObservedBoard::CellId> &dead, ObservedBoard::CellId target = -1);

  private:
    struct Link;
    struct Chain;
    struct Workspace;
    Workspace &getWorkspace();
    bool extendChain(const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, span<Semi::Link> links, Semi::Chain &chain, std::deque<Semi::Chain> &chains, int &chainCount, ObservedBoard::CellId target);
    void collectLinks(const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<ObservedBoard::CellId> &dead, vector<Link> &links, int &linkCount, std::deque<vector<ObservedBoard::CellId>> &witnesses, int &witnessCount);
    Kind extendChains(vector<ObservedBoard::CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, span<Link> links, std::deque<Chain> &chains, int &chainCount, ObservedBoard::CellId target);
    Kind connectChains(vector<ObservedBoard::CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<ObservedBoard::CellId> &dead, std::deque<Chain> &chains, int chainCount, const std::deque<vector<ObservedBoard::CellId>> &witnesses, int witnessCount, ObservedBoard::CellId target);
    bool semiContains(const vector<ObservedBoard::CellId> &cells, ObservedBoard::CellId cell);
    bool adjacent(const ObservedBoard::Result &board, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    void assess(Link &link, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities);
    void findRootedLinks(const vector<ObservedBoard::CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<ObservedBoard::CellId> &dead, int witnessSize, vector<Link> &links, int &linkCount);
    bool noTrouble(const ObservedBoard::Result &board, const vector<ObservedBoard::CellId> &trouble, const vector<ObservedBoard::CellId> &chain);
    bool linkLess(const Link &lhs, const Link &rhs);
};

} // 命名空间 mss
