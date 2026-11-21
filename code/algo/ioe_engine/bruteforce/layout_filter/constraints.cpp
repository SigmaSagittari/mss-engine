#include "algo/ioe_engine/bruteforce/layout_filter/constraints.h"

#include <algorithm>

namespace mss {

struct IoeConstraints::Workspace {
    struct Ranked {
        IoeCells order;
        IoeLayout layout;
    };
    vector<Ranked> sorted;
};

__declspec(noinline) IoeConstraints::Workspace &IoeConstraints::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

IoeConstraints::Result IoeConstraints::build(const ObservedBoard::Result &board, const Basic::Result &basic) {
    constexpr int n = IoeBoard::cells;
    constexpr int side = IoeBoard::side;
    Result result;
    result.cellCount = 0;
    result.equationCount = 0;
    int degree[n]{};
    for (int cell = 0; cell < n; ++cell) {
        const Basic::Mark mark = basic.marks[cell / side][cell % side];
        if (mark == Basic::Mark::F) result.fixedMines.set(cell);
        if (mark == Basic::Mark::H || mark == Basic::Mark::T) {
            result.unknown.set(cell);
            result.cells[result.cellCount++] = cell;
        }
    }
    result.remainingMines = board.totalMines - basic.mineSum;
    const IoeCells *adjacent = IoeBoard().neighbors();
    for (int cell = 0; cell < n; ++cell) {
        const ObservedBoard::CellState state = board.cells[cell / side][cell % side];
        result.digits[cell] = ObservedBoard().isNumber(state) ? ObservedBoard().numberValue(state) : 9;
        if (!ObservedBoard().isNumber(state)) continue;
        const IoeCells unknown = adjacent[cell] & result.unknown;
        if (!unknown.any()) continue;
        result.equations[result.equationCount++] = {unknown, (std::uint8_t)(ObservedBoard().numberValue(state) - (adjacent[cell] & result.fixedMines).popcount())};
        unknown.forEachSetBit([&](int other) { ++degree[other]; });
    }
    std::sort(result.cells, result.cells + result.cellCount, [&](int a, int b) { return degree[a] != degree[b] ? degree[a] > degree[b] : a < b; });
    return result;
}

void IoeConstraints::forEach(const Result &constraints, IoeObserver<IoeCells> visit) {
    constexpr int n = IoeBoard::cells;
    // 危险：每个格只使用 count 标记的项，需求及余量在递归前一次填好。
    struct Variable { int count; std::uint8_t equations[8]; };
    Variable vars[n];
    int demand[n], left[n];
    for (int e = 0; e < constraints.equationCount; ++e) {
        demand[e] = constraints.equations[e].demand;
        left[e] = constraints.equations[e].unknown.popcount();
    }
    for (int i = 0; i < constraints.cellCount; ++i) {
        Variable &var = vars[i];
        var.count = 0;
        for (int e = 0; e < constraints.equationCount; ++e)
            if (constraints.equations[e].unknown.test(constraints.cells[i])) var.equations[var.count++] = e;
    }

    IoeCells mines = constraints.fixedMines;
    auto enumerate = [&](auto &self, int depth, int mineCount) -> void {
        if (depth == constraints.cellCount) {
            visit(mines);
            return;
        }
        const Variable &var = vars[depth];
        const int size = constraints.cellCount - depth - 1;
        int minimum = mineCount > size;
        int maximum = mineCount > 0;
        for (int i = 0; i < var.count; ++i) {
            const int eq = var.equations[i];
            --left[eq];
            if (demand[eq] > left[eq]) minimum = 1;
            if (!demand[eq]) maximum = 0;
        }
        if (!minimum) self(self, depth + 1, mineCount);
        if (maximum) {
            mines.set(constraints.cells[depth]);
            for (int i = 0; i < var.count; ++i) --demand[var.equations[i]];
            self(self, depth + 1, mineCount - 1);
            for (int i = 0; i < var.count; ++i) ++demand[var.equations[i]];
            mines.reset(constraints.cells[depth]);
        }
        for (int i = 0; i < var.count; ++i) ++left[var.equations[i]];
    };
    enumerate(enumerate, 0, constraints.remainingMines);
}

void IoeConstraints::sort(const Result &constraints, vector<IoeLayout> &layouts) {
    if (layouts.size < 2) return;
    int bit[IoeBoard::cells];
    for (int i = 0; i < constraints.cellCount; ++i) bit[constraints.cells[i]] = constraints.cellCount - 1 - i;
    Workspace &workspace = getWorkspace();
    vector<Workspace::Ranked> &sorted = workspace.sorted;
    sorted.resize(layouts.size);
    for (std::size_t i = 0; i < layouts.size; ++i) {
        IoeCells order;
        (layouts[i].mines & constraints.unknown).forEachSetBit([&](int cell) { order.set(bit[cell]); });
        sorted[i] = {order, layouts[i]};
    }
    std::sort(sorted.data, sorted.data + sorted.size, [](const Workspace::Ranked &a, const Workspace::Ranked &b) {
        return a.order.word(1) != b.order.word(1) ? a.order.word(1) < b.order.word(1) : a.order.word(0) < b.order.word(0);
    });
    for (std::size_t i = 0; i < sorted.size; ++i) layouts[i] = sorted[i].layout;
}

} // 命名空间 mss
