#include "algo/ioe_engine/move_proof/move_proof.h"

namespace mss {

IoeMove IoeMoveProof::prove(const IoeView &view, const std::uint8_t *digits, const Facts &facts) {
    constexpr int cells = IoeBoard::cells;
    const IoeCells *adjacent = IoeBoard().neighbors();
    const IoeCells hidden = IoeCells(cells) & ~(view.revealed | facts.mines);
    IoeCells zeros = facts.possibleZeros & hidden;
    IoeCells centers = hidden;
    for (int cell = 0; cell < cells; ++cell) {
        if (view.revealed.test(cell) && digits[cell] != 0) centers.set(cell);
        if ((adjacent[cell] & facts.mines).any()) zeros.reset(cell);
    }

    if (!(view.flags & ~facts.mines).any()) {
        const IoeCells unpaidMines = facts.mines & ~view.flags;
        for (int cell = 0; cell < cells; ++cell) {
            if (!view.revealed.test(cell) || digits[cell] == 0) continue;
            const IoeCells mines = adjacent[cell] & facts.mines;
            if (mines.popcount() != digits[cell]) continue;
            const IoeCells added = mines & unpaidMines;
            if (added.popcount() > 1) continue;
            const IoeCells targets = adjacent[cell] & hidden;
            if ((targets & zeros).any() || targets.popcount() < 1 + added.popcount()) continue;

            IoeCells removableFlags = added;
            if (added.any()) {
                unpaidMines.forEachSetBit([&](int mine) {
                    bool onlyInside = true;
                    (adjacent[mine] & centers).forEachSetBit([&](int center) {
                        if ((adjacent[center] & hidden & ~targets).any()) onlyInside = false;
                    });
                    if (onlyInside) removableFlags.set(mine);
                });
            }
            // 每个证人必须保证原策略为揭开它支付的一击可省。
            struct Witness { IoeCells opens, chords; };
            Witness witnesses[8];
            int count = 0;
            bool proved = false;
            targets.forEachSetBit([&](int target) {
                if (proved || (adjacent[target] & zeros).any()) return;
                Witness found;
                found.opens.set(target);
                bool valid = true;
                (adjacent[target] & centers).forEachSetBit([&](int center) {
                    if (!valid) return;
                    found.chords.set(center);
                    const IoeCells outside = adjacent[center] & hidden & ~targets;
                    if (!outside.any()) return;
                    if (!targets.test(center)) {
                        valid = false;
                        return;
                    }
                    found.opens.set(center);
                    (adjacent[center] & centers).forEachSetBit([&](int other) {
                        IoeCells covered = adjacent[other];
                        covered.set(other);
                        if (!(outside & ~covered).any()) return;
                        if (zeros.test(other) || (adjacent[other] & hidden & ~targets).any()) valid = false;
                        else found.chords.set(other);
                    });
                });
                if (!valid) return;
                if (!added.any()) {
                    proved = true;
                    return;
                }
                for (int i = 0; i < count; ++i) {
                    const Witness &prior = witnesses[i];
                    if ((prior.opens & found.opens).any()) continue;
                    bool shareFlag = true;
                    (prior.chords & found.chords).forEachSetBit([&](int chord) {
                        if (!(adjacent[chord] & removableFlags).any()) shareFlag = false;
                    });
                    if (shareFlag) proved = true;
                }
                witnesses[count++] = found;
            });
            if (proved) return {IoeMove::Kind::Chord, cell, added};
        }
    }

    const IoeCells safe = facts.safe & hidden & ~view.flags & ~zeros;
    for (int cell = 0; cell < cells; ++cell) {
        if (!safe.test(cell) || (adjacent[cell] & zeros).any()) continue;
        bool single = true;
        (adjacent[cell] & centers).forEachSetBit([&](int center) {
            if ((adjacent[center] & hidden).popcount() > 1) single = false;
        });
        if (single) return {IoeMove::Kind::Open, cell};
    }
    return {};
}

} // 命名空间 mss
