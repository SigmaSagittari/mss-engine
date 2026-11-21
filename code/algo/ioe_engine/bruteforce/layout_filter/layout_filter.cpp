#include "algo/ioe_engine/bruteforce/layout_filter/layout_filter.h"

#include "algo/ioe_engine/dom/dom.h"
#include "algo/ioe_engine/bruteforce/layout_filter/constraints.h"
#include "algo/ioe_engine/bruteforce/layout_filter/joint_filter.h"

namespace mss {

void IoeLayoutFilter::select(const ObservedBoard::Result &board, const Basic::Result &basic, const IoeView &view, int spent, int minIoePercent,
                             std::uint64_t memoryBytes, Result &result, IoeObserver<Progress> progress) {
    vector<IoeLayout> &matches = result.layouts;
    matches.size = result.domCalls = result.nodes = 0;
    if (spent > 100 * (IoeBoard::cells - IoeBoard::mines) / minIoePercent) return;

    const IoeConstraints::Result constraints = IoeConstraints().build(board, basic);
    auto reportNodes = [&](std::uint64_t nodes) { progress({0, 0, 0, nodes}); };
    if (const std::size_t legal = IoeJointFilter().select(constraints, view, spent, minIoePercent, memoryBytes, matches, result.nodes, reportNodes)) {
        progress({legal, matches.size, 0, result.nodes});
        return;
    }

    std::size_t count = 0;
    auto visit = [&](const IoeCells &mines) {
        ++count;
        const int bv = threeBV(mines);
        const int budget = 100 * bv / minIoePercent - spent;
        if (budget >= 0) {
            const IoeDom::Result minimum = IoeDom().minimum(mines, view, budget);
            result.domCalls += minimum.searched;
            if (minimum.clicks != IoeDom::overBudget) matches.push_back({mines, (std::uint8_t)bv, (std::uint8_t)minimum.clicks});
        }
        if ((count & 511) == 0) progress({count, matches.size, result.domCalls, result.nodes});
    };
    IoeConstraints().forEach(constraints, visit);
    progress({count, matches.size, result.domCalls, result.nodes});
}

int IoeLayoutFilter::threeBV(const IoeCells &mines) {
    constexpr int cells = IoeBoard::cells;
    const IoeCells *adjacent = IoeBoard().neighbors();
    IoeCells nearMines = mines;
    mines.forEachSetBit([&](int cell) { nearMines |= adjacent[cell]; });
    IoeCells zeros = IoeCells(cells) & ~nearMines;
    IoeCells covered = mines | zeros;
    int count = 0;
    while (zeros.any()) {
        ++count;
        IoeCells pending;
        pending.set(zeros.firstSetBit());
        do {
            zeros &= ~pending;
            IoeCells connected;
            pending.forEachSetBit([&](int cell) { connected |= adjacent[cell]; });
            covered |= connected;
            pending = connected & zeros;
        } while (pending.any());
    }
    return count + cells - covered.popcount();
}

} // 命名空间 mss
