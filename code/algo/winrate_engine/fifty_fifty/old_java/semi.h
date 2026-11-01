#pragma once

#include <deque>

#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// 旧 Java 死猜检测：从数字约束拼出交换边，再连接成链；只读 PositionState。
struct Semi {
    // Forced：封闭的二选一死猜；Semi：候选为雷时形成死猜，点击它可获得全额本体抵扣。
    enum class Kind { None, Forced, Semi };

    struct Result {
        Kind kind;
        vector<ObservedBoard::CellId> cells; // 这一组里可以作为首招的本体格，kind=None 时为空。

        Result() : kind(Kind::None) {}
    };

    struct Link;

    struct Chain;

    // 输入来自同一有效盘面，probability.candidates > 0；dead 使用同盘面编号。
    Result analyze(const PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead);
    // 按当前全部约束刷新结果并保留容量；target=-1 选首组，否则只返回包含该格的结果。
    Kind update(Result &result, const PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, ObservedBoard::CellId target = -1);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    const int *shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes);
    const Structure::Shape::Constraint *shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes);
    const Structure::BoxId *constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes);
    bool extendChain(const PositionState &state, const Probability::Pool &probabilities, span<Semi::Link> links, Semi::Chain &chain, std::deque<Semi::Chain> &chains, int &chainCount, ObservedBoard::CellId target);
    void collectLinks(const PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, vector<Link> &links, int &linkCount, std::deque<vector<ObservedBoard::CellId>> &witnesses, int &witnessCount);
    Kind extendChains(Result &result, const PositionState &state, const Probability::Pool &probabilities, span<Link> links, std::deque<Chain> &chains, int &chainCount, ObservedBoard::CellId target);
    Kind connectChains(Result &result, const PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, std::deque<Chain> &chains, int chainCount, const std::deque<vector<ObservedBoard::CellId>> &witnesses, int witnessCount, ObservedBoard::CellId target);
    bool semiContains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    bool adjacent(const ObservedBoard::Result &board, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    void assess(Link &link, const PositionState &state, const Probability::Pool &probabilities);
    void findRootedLinks(span<const ObservedBoard::CellId> cells, const PositionState &state, const Probability::Pool &probabilities, span<const ObservedBoard::CellId> dead, int witnessSize, vector<Link> &links, int &linkCount);
    bool noTrouble(const ObservedBoard::Result &board, span<const ObservedBoard::CellId> trouble, span<const ObservedBoard::CellId> chain);
    bool linkLess(const Link &lhs, const Link &rhs);
};

} // 命名空间 mss
