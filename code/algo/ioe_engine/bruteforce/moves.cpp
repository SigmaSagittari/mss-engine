#include "algo/ioe_engine/bruteforce/moves.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mss {

struct IoeMoves::Workspace {
    struct Footprint { int base; IoeCells opens, flags; };
    vector<int> filtered, sorted;
    vector<Footprint> footprints;
    vector<std::uint8_t> removed;
};

__declspec(noinline) IoeMoves::Workspace &IoeMoves::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

int IoeMoves::clicks(const IoeMove &move, const IoeView &view) {
    if (move.kind != IoeMove::Kind::Chord) return 1;
    return 1 + !view.revealed.test(move.cell) + move.addFlags.popcount() + move.removeFlags.popcount();
}

IoeMoves::Targets IoeMoves::targets(const IoeMove &move, const IoeView &view) {
    Targets result{view.flags, {}};
    if (move.kind == IoeMove::Kind::Unflag) result.flags.reset(move.cell);
    else if (move.kind == IoeMove::Kind::Open) result.cells.set(move.cell);
    else {
        result.flags = (result.flags | move.addFlags) & ~move.removeFlags;
        result.cells = IoeBoard().neighbors()[move.cell] & ~(view.revealed | result.flags);
        if (!view.revealed.test(move.cell)) result.cells.set(move.cell);
    }
    return result;
}

bool IoeMoves::play(const IoeMove &move, const IoeView &view, const IoeStrategyBoard &board, IoeCells &revealed) {
    const IoeCells *adjacent = IoeBoard().neighbors();
    const Targets target = targets(move, view);
    if ((target.cells & board.mines).any()) return false;
    if (move.kind == IoeMove::Kind::Chord && ((adjacent[move.cell] & board.mines) != (adjacent[move.cell] & target.flags))) return false;
    revealed = view.revealed;
    IoeCells pending = target.cells;
    while (pending.any()) {
        const int cell = pending.firstSetBit();
        pending.reset(cell);
        revealed.set(cell);
        if (board.digits[cell] == 0) pending |= adjacent[cell] & ~(revealed | target.flags);
    }
    return true;
}

void IoeMoves::group(const IoeMove &move, const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, Result &result) {
    const IoeCells *adjacent = IoeBoard().neighbors();
    const Targets target = targets(move, view);
    const bool chord = move.kind == IoeMove::Kind::Chord;
    const IoeCells neighbors = chord ? adjacent[move.cell] : IoeCells{};
    const IoeCells needed = neighbors & target.flags;
    result.flags = target.flags;
    result.groups.size = 0;
    result.layouts.data.size = 0;
    Workspace &workspace = getWorkspace();
    vector<int> &filtered = workspace.filtered, &sorted = workspace.sorted;
    filtered.resize(layouts.size);
    int accepted = 0;
    for (int id : layouts) {
        const IoeCells &mines = boards[id].mines;
        if ((target.cells & mines).any() || (chord && (mines & neighbors) != needed)) continue;
        filtered[accepted++] = id;
    }
    if (!accepted) return;
    sorted.resize(accepted);
    result.layouts.data.resize(accepted);

    auto partition = [&](auto &self, int begin, int end, IoeCells shown, IoeCells pending) -> void {
        while (pending.any()) {
            const int cell = pending.firstSetBit();
            pending.reset(cell);
            shown.set(cell);
            const int first = boards[filtered[begin]].digits[cell];
            int split = begin + 1;
            while (split < end && boards[filtered[split]].digits[cell] == first) ++split;
            const IoeCells expanded = pending | (adjacent[cell] & ~(shown | target.flags));
            if (split == end) {
                if (!first) pending = expanded;
                continue;
            }

            int count[9]{};
            count[first] = split - begin;
            for (int i = split; i < end; ++i) ++count[boards[filtered[i]].digits[cell]];
            int next[9], start = begin;
            for (int digit = 0; digit < 9; ++digit) {
                next[digit] = start;
                start += count[digit];
            }
            for (int i = begin; i < end; ++i) sorted[next[boards[filtered[i]].digits[cell]]++] = filtered[i];
            std::memcpy(filtered.data + begin, sorted.data + begin, sizeof(int) * (end - begin));
            for (int digit = 0, at = begin; digit < 9; ++digit) {
                if (count[digit]) self(self, at, next[digit], shown, digit == 0 ? expanded : pending);
                at = next[digit];
            }
            return;
        }
        std::memcpy(result.layouts.data.data + begin, filtered.data + begin, sizeof(int) * (end - begin));
        result.groups.push_back({shown, {(std::size_t)begin, (std::size_t)(end - begin)}});
    };
    partition(partition, 0, accepted, view.revealed, target.cells);
}

void IoeMoves::moves(const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, bool root, vector<IoeMove> &moves) {
    const IoeCells *adjacent = IoeBoard().neighbors();
    constexpr int cells = IoeBoard::cells;
    IoeCells everyMine(cells), anyMine;
    for (int id : layouts) {
        everyMine &= boards[id].mines;
        anyMine |= boards[id].mines;
    }
    moves.size = 0;
    const IoeStrategyBoard &representative = boards[layouts[0]];
    auto chords = [&](int cell, int digit, bool compound) {
        const IoeCells hidden = adjacent[cell] & ~view.revealed;
        if (hidden.popcount() <= digit + !root) return;
        int first[256];
        std::fill_n(first, 256, std::numeric_limits<int>::max());
        int nearby[8], count = 0;
        hidden.forEachSetBit([&](int other) { nearby[count++] = other; });
        for (int id : layouts) {
            unsigned pattern = 0;
            for (int i = 0; i < count; ++i) pattern |= (unsigned)boards[id].mines.test(nearby[i]) << i;
            first[pattern] = std::min(first[pattern], id);
        }
        int patterns[256], variants = 0;
        for (int pattern = 0; pattern < (1 << count); ++pattern)
            if (first[pattern] != std::numeric_limits<int>::max()) patterns[variants++] = pattern;
        if (compound)
            std::sort(patterns, patterns + variants, [&](int a, int b) { return first[a] < first[b]; });
        for (int k = 0; k < variants; ++k) {
            const int pattern = patterns[k];
            IoeCells flags;
            for (int i = 0; i < count; ++i)
                if (pattern & (1 << i)) flags.set(nearby[i]);
            moves.push_back({IoeMove::Kind::Chord, cell, flags & ~view.flags, hidden & view.flags & ~flags});
        }
    };

    for (int cell = 0; cell < cells; ++cell) {
        if (view.revealed.test(cell)) {
            const int digit = representative.digits[cell];
            if (digit != 0) chords(cell, digit, false);
        } else if (!everyMine.test(cell)) {
            moves.push_back({view.flags.test(cell) ? IoeMove::Kind::Unflag : IoeMove::Kind::Open, cell});
        }
    }
    if (root) return;

    const bool flagsAreMines = !(view.flags & ~everyMine).any();
    IoeCells covered;
    if (flagsAreMines) {
        for (std::size_t i = 0; i < moves.size; ++i) {
            const IoeMove &move = moves[i];
            if (move.kind == IoeMove::Kind::Chord && !move.addFlags.any() && !move.removeFlags.any())
                covered |= adjacent[move.cell] & ~view.flags;
        }
    }

    int compound[cells], composites = 0;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < moves.size; ++i) {
        const IoeMove move = moves[i];
        if (move.kind == IoeMove::Kind::Open) {
            if (covered.test(move.cell)) continue;
            const int digit = representative.digits[move.cell];
            bool same = digit > 0 && digit < 9 && !anyMine.test(move.cell);
            for (int id : layouts) same &= boards[id].digits[move.cell] == digit;
            if (same) {
                compound[composites++] = move.cell;
                continue;
            }
        }
        moves[kept++] = move;
    }
    moves.size = kept;
    for (int i = 0; i < composites; ++i) chords(compound[i], representative.digits[compound[i]], true);
    if (flagsAreMines) removeDominated(view, boards, layouts, moves);
    if (moves.size > 1) postponeChords(view, boards, layouts, moves);
}

void IoeMoves::removeDominated(const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, vector<IoeMove> &moves) {
    const IoeCells *adjacent = IoeBoard().neighbors();
    // 基础点击以外只比较多插的旗和仍须单开的格。相同代价与覆盖保留较早生成的一手。
    Workspace &workspace = getWorkspace();
    vector<Workspace::Footprint> &footprints = workspace.footprints;
    vector<std::uint8_t> &removed = workspace.removed;
    footprints.resize(moves.size);
    removed.resize(moves.size);
    int supply[IoeBoard::cells]{};
    for (std::size_t i = 0; i < moves.size; ++i) {
        const IoeMove &move = moves[i];
        Workspace::Footprint &step = footprints[i];
        step = {1, {}, move.addFlags};
        removed[i] = 0;
        if (move.kind == IoeMove::Kind::Chord) {
            step.opens = targets(move, view).cells;
            step.base = 1 + !view.revealed.test(move.cell);
            step.opens.forEachSetBit([&](int cell) { ++supply[cell]; });
        } else if (move.kind == IoeMove::Kind::Open) step.opens.set(move.cell);
    }
    for (std::size_t b = 0; b < moves.size; ++b) {
        const IoeMove &replaced = moves[b];
        const Workspace::Footprint &from = footprints[b];
        if (replaced.kind == IoeMove::Kind::Unflag || !from.opens.any()) continue;
        int choices[2]{-1, -1};
        from.opens.forEachSetBit([&](int cell) {
            if (choices[0] < 0 || supply[cell] < supply[choices[0]]) {
                choices[1] = choices[0];
                choices[0] = cell;
            } else if (choices[1] < 0 || supply[cell] < supply[choices[1]]) choices[1] = cell;
        });
        for (int d = 0; d < from.base && choices[d] >= 0 && !removed[b]; ++d) {
            for (int a = (int)moves.size - 1; a >= 0; --a) {
                if (moves[a].kind != IoeMove::Kind::Chord || a == (int)b) continue;
                const Workspace::Footprint &to = footprints[a];
                if (!to.opens.test(choices[d])) continue;
                const IoeCells extra = to.flags & ~from.flags;
                if ((from.opens & ~to.opens).popcount() + extra.popcount() > from.base - to.base) continue;
                if (extra.any()) {
                    bool pay = true;
                    const IoeCells around = adjacent[replaced.cell];
                    const IoeCells required = targets(replaced, view).flags & around;
                    for (int id : layouts)
                        if ((boards[id].mines & around) == required && (extra & ~boards[id].mines).any()) pay = false;
                    if (!pay) continue;
                }
                if (a < (int)b || to.base < from.base || to.opens != from.opens || to.flags != from.flags) {
                    removed[b] = 1;
                    break;
                }
            }
        }
    }
    std::size_t kept = 0;
    for (std::size_t i = 0; i < moves.size; ++i)
        if (!removed[i]) moves[kept++] = moves[i];
    moves.size = kept;
}

void IoeMoves::postponeChords(const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, vector<IoeMove> &moves) {
    const IoeCells *adjacent = IoeBoard().neighbors();
    const IoeStrategyBoard &first = boards[layouts[0]];
    IoeCells postponed;
    std::size_t count = 0;
    for (std::size_t i = 0; i < moves.size; ++i) {
        const IoeMove &move = moves[i];
        if (move.kind != IoeMove::Kind::Chord || !view.revealed.test(move.cell) || move.addFlags.any() || move.removeFlags.any()) continue;
        const IoeCells neighbors = adjacent[move.cell] & ~view.revealed;
        const IoeCells flags = neighbors & view.flags;
        const IoeCells reveals = neighbors & ~flags;
        const IoeCells hidden = ~(view.revealed | reveals);
        bool same = true;
        reveals.forEachSetBit([&](int cell) {
            if (!same) return;
            const int digit = first.digits[cell];
            if (!digit || (adjacent[cell] & hidden).popcount() > digit + 1) { same = false; return; }
            for (int id : layouts)
                if (boards[id].digits[cell] != digit) { same = false; break; }
        });
        if (!same) continue;
        for (int id : layouts)
            if ((boards[id].mines & neighbors) != flags) { same = false; break; }
        if (same) {
            postponed.set(move.cell);
            ++count;
        }
    }
    if (!count || count == moves.size) return;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < moves.size; ++i)
        if (moves[i].kind != IoeMove::Kind::Chord || !postponed.test(moves[i].cell)) moves[kept++] = moves[i];
    moves.size = kept;
}

} // 命名空间 mss
