#include "algo/ioe_engine/ref_dom/ref_dom.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

#include "algo/ioe_engine/ref_dom/frontier.h"
#include "core/algorithm/neighbors.h"
#include "core/datastructure/bit_mask.h"

namespace mss::ioe::ref_dom {

void Scopes::clear() {
    parts.size = 0;
    values.data.size = 0;
}

void Scopes::append(span<const int> items) {
    vectorPool<int>::vector part{values.data.size, (size_t)items.size};
    values.data.resize(part.offset + part.count);
    if (items.size) std::memcpy(values.data.data + part.offset, items.data, part.count * sizeof(int));
    parts.push_back(part);
}

template <int Rows, int Cols> struct RefDom::Engine {
    static constexpr int cells = Rows * Cols;
    using Model = BoardModel<cells>;
    using Neighbors = BoardNeighbors<cells>;
    using BoardBits = bitMask<(cells + 63) / 64>;
    using MineBits = bitMask<(Cols == 30 ? 2 : 1)>;

    using StateMask = std::conditional_t<cells == ref_dom::cells, Mask, BoardBits>;

    Neighbors makeNeighbors() {
        Neighbors result{};
        for (int cell = 0; cell < cells; ++cell) {
            typename Neighbors::Row &row = result.rows[cell];
            const int r = cell / Cols, c = cell % Cols;
            forEachAdjacent(r, c, Rows, Cols, [&](int nr, int nc) { row.items[row.count++] = nr * Cols + nc; });
        }
        return result;
    }

    bool partialOpening(const Model &model, const StateMask &shown, const Neighbors &adjacent) {
        for (int i = 0; i < model.zeros.size(); ++i)
            for (int zero : model.zeros[i])
                if (shown.test(zero))
                    for (int next : adjacent[zero])
                        if (!shown.test(next)) return true;
        return false;
    }

    int remainingClicks(const Model &layout, const StateMask &shown, const StateMask &flags, const Neighbors &adjacent) {
        Workspace &workspace = getWorkspace();
        int wrongFlags = 0;
        for (int cell = 0; cell < cells; ++cell)
            if (flags.test(cell) && !layout.mines[cell]) ++wrongFlags;
        const Model *model = &layout;
        if (partialOpening(layout, shown, adjacent)) {
            // 错旗可能挡住展开。已揭开的零格不重复展开，按仍盖住的零格重算连通区。
            Model &partial = workspace.model.partial;
            std::copy_n(layout.mines, cells, partial.mines);
            build(shown, adjacent, partial);
            model = &partial;
        }
        Model &remaining = workspace.model.remaining;
        Model (&reduced)[2] = workspace.model.reduced;
        Model &ordered = workspace.model.ordered;
        int paidSeed;
        remainingModel(*model, shown, flags, adjacent, remaining, paidSeed);
        const Model &input = reduce(remaining, reduced);
        chooseOrder(input, ordered);
        return wrongFlags + Frontier().solve(ordered) - paidSeed;
    }

    int remainingClicks(const Grid<char> &layout, span<const int> shownCells, span<const int> flagCells) {
        Workspace &workspace = getWorkspace();
        Model &model = workspace.model.layout;
        const Neighbors adjacent = makeNeighbors();
        for (int cell = 0; cell < cells; ++cell) model.mines[cell] = layout[0][cell];
        build({}, adjacent, model);
        StateMask shown{}, flags{};
        for (int cell : shownCells) shown.set(cell);
        for (int cell : flagCells) flags.set(cell);
        return remainingClicks(model, shown, flags, adjacent);
    }

    void build(const StateMask &shown, const Neighbors &adjacent, Model &model) {
        Workspace &workspace = getWorkspace();
        const uint8_t (&mineCells)[cells] = model.mines;
        model.zeros.clear();
        model.mine_cells.size = 0;
        model.base_descriptions.size = 0;
        clearGraph(model);

        bool (&unseen)[cells] = workspace.build.unseen;
        bool (&adjacentToZero)[cells] = workspace.build.adjacentToZero;
        for (int cell = 0; cell < cells; ++cell) {
            int number = -1;
            if (!mineCells[cell]) {
                number = 0;
                for (int other : adjacent[cell]) number += mineCells[other];
            }
            model.numbers[cell] = (int8_t)number;
            unseen[cell] = number == 0 && !shown.test(cell);
            adjacentToZero[cell] = false;
        }

        int (&component)[cells] = workspace.build.component;
        int (&stack)[cells] = workspace.build.stack;
        for (int start = 0; start < cells; ++start) {
            if (!unseen[start]) continue;
            unseen[start] = false;
            int top = 0, count = 0;
            stack[top++] = start;
            while (top) {
                const int cell = stack[--top];
                component[count++] = cell;
                for (int other : adjacent[cell]) {
                    if (!mineCells[other]) adjacentToZero[other] = true;
                    if (unseen[other]) {
                        unseen[other] = false;
                        stack[top++] = other;
                    }
                }
            }
            // 每个零区从下标最小的格开始，因此生成顺序即为零区顺序。
            std::sort(component, component + count);
            model.zeros.append({component, count});
        }

        for (int cell = 0; cell < cells; ++cell) {
            if (model.numbers[cell] > 0) {
                model.candidate_index[cell] = (int)model.candidates.size;
                model.candidates.push_back(cell);
            } else model.candidate_index[cell] = -1;
            if (mineCells[cell]) model.mine_cells.push_back(cell);
        }

        int (&scope)[cells] = workspace.build.scope;
        for (size_t i = 0; i < model.candidates.size; ++i) {
            const int count = adjacentCandidates(model, adjacent[model.candidates[i]], scope);
            model.graph.append({scope, count});
        }
        for (int z = 0; z < model.zeros.size(); ++z) {
            BoardBits boundary{};
            const span<const int> opening = model.zeros[z];
            for (int zero : opening) {
                for (int cell : adjacent[zero]) {
                    const int id = model.candidate_index[cell];
                    if (id >= 0) boundary.set(id);
                }
            }
            const int count = unpack(boundary, scope);
            model.zero_scopes.append({scope, count});
            model.base_scopes.append({scope, count});
            model.base_descriptions.push_back({BaseDescription::Kind::Zero, opening[0]});
        }
        for (size_t i = 0; i < model.mine_cells.size; ++i) {
            const int count = adjacentCandidates(model, adjacent[model.mine_cells[i]], scope);
            model.mine_scopes.append({scope, count});
        }
        for (int cell = 0; cell < cells; ++cell) {
            if (model.numbers[cell] <= 0 || shown.test(cell) || adjacentToZero[cell]) continue;
            const int count = adjacentCandidates(model, adjacent[cell], scope);
            scope[count] = model.candidate_index[cell];
            std::sort(scope, scope + count + 1);
            model.base_scopes.append({scope, count + 1});
            model.base_descriptions.push_back({BaseDescription::Kind::Single, cell});
        }
    }

    void remainingModel(const Model &model, const StateMask &shown, const StateMask &flags, const Neighbors &adjacent,
                        Model &remaining, int &paidSeed) {
        Workspace &workspace = getWorkspace();
        int (&anchors)[cells] = workspace.restriction.anchors;
        int (&remap)[cells] = workspace.restriction.remap;
        bool (&keep)[cells] = workspace.restriction.keep;
        int anchorCount = 0;
        for (int i = 0; i < (int)model.candidates.size; ++i) {
            const int cell = model.candidates[i];
            bool hasHiddenNeighbor = false;
            for (int next : adjacent[cell]) {
                if (!shown.test(next) && !model.mines[next]) {
                    hasHiddenNeighbor = true;
                    break;
                }
            }
            keep[i] = hasHiddenNeighbor;
            if (hasHiddenNeighbor && shown.test(cell)) anchors[anchorCount++] = i;
        }
        restrictCandidates(model, keep, remap, remaining);
        const auto addOpening = [&](span<const int> scope) {
            appendRestricted(remaining.zero_scopes, scope, remap);
            remaining.base_scopes.append(remaining.zero_scopes[remaining.zero_scopes.size() - 1]);
        };
        for (int i = 0; i < model.zeros.size(); ++i)
            if (!shown.test(model.zeros[i][0])) addOpening(model.zero_scopes[i]);
        paidSeed = anchorCount > 0;
        if (paidSeed) addOpening({anchors, anchorCount});
        for (int i = model.zeros.size(); i < model.base_scopes.size(); ++i)
            if (!shown.test(model.base_descriptions[i].representative)) appendRestricted(remaining.base_scopes, model.base_scopes[i], remap);
        for (int i = 0; i < (int)model.mine_cells.size; ++i) {
            if (flags.test(model.mine_cells[i])) remaining.mine_scopes.append({});
            else appendRestricted(remaining.mine_scopes, model.mine_scopes[i], remap);
        }
    }

    const Model &reduce(const Model &original, Model (&models)[2]) {
        Workspace &workspace = getWorkspace();
        Relations &relations = workspace.reduction.relations;
        bool (&keep)[cells] = workspace.restriction.keep;
        int (&remap)[cells] = workspace.restriction.remap;
        const Model *current = &original;
        int next = 0;
        for (;;) {
            const Model &model = *current;
            const int q = (int)model.candidates.size;
            calculateRelations(model, relations);
            std::fill_n(keep, q, true);
            int removed = 0;
            for (int victim = 0; victim < q; ++victim) {
                relations.propagation[victim].forEachSetBit([&](int replacement) {
                    if (keep[victim] && keep[replacement] && swapDominated(relations, victim, replacement)) {
                        keep[victim] = false;
                        ++removed;
                    }
                });
            }
            if (!removed) {
                if (!removeLeftClickDominated(model, relations, keep)) return model;
            }
            // 删除保持替代关系，或只缩小邻域，批内其余删除仍有效。
            Model &output = models[next];
            restrictCandidates(model, keep, remap, output);
            restrictScopes(model.zero_scopes, remap, output.zero_scopes);
            restrictScopes(model.mine_scopes, remap, output.mine_scopes);
            restrictScopes(model.base_scopes, remap, output.base_scopes);
            current = &output;
            next ^= 1;
        }
    }

    // 单行或单列扫掠沿用参考版的评分和并列顺序。
    int sweepRank(int cell, int sweep) {
        const int row = cell / Cols;
        const int col = cell % Cols;
        const int line = sweep < 2 ? col : row;
        const int along = sweep < 2 ? row : col;
        const int lines = sweep < 2 ? Cols : Rows;
        const int length = sweep < 2 ? Rows : Cols;
        return ((sweep & 1) ? lines - 1 - line : line) * length + along;
    }

    void chooseOrder(const Model &model, Model &output) {
        Workspace &workspace = getWorkspace();
        const int q = (int)model.candidates.size;
        int (&bestOrder)[cells] = workspace.order.bestOrder;
        int (&byRank)[cells] = workspace.order.byRank;
        int (&order)[cells] = workspace.order.order;
        int (&inverse)[cells] = workspace.order.inverse;
        int (&graphDelta)[cells] = workspace.order.graphDelta;
        int (&factorDelta)[cells] = workspace.order.factorDelta;
        struct Score {
            int weightedWidth;
            int totalWidth;
            uint64_t work;
            bool operator<(const Score &other) const {
                if (weightedWidth != other.weightedWidth) return weightedWidth < other.weightedWidth;
                if (totalWidth != other.totalWidth) return totalWidth < other.totalWidth;
                return work < other.work;
            }
        };
        Score best;
        // 每个扫掠位置最多同时活跃三条线上的旗和独立数字格。高级按列扫掠，留在单字因子掩码内。
        constexpr int sweepCount = 3 * Cols > 64 ? 2 : 4;
        for (int sweep = 0; sweep < sweepCount; ++sweep) {
            std::fill_n(byRank, cells, -1);
            for (int i = 0; i < q; ++i) byRank[sweepRank(model.candidates[i], sweep)] = i;
            int n = 0;
            for (int old : byRank) {
                if (old < 0) continue;
                inverse[old] = n;
                order[n++] = old;
            }
            std::fill_n(graphDelta, q, 0);
            std::fill_n(factorDelta, q, 0);
            for (int i = 0; i < q; ++i) {
                int last = inverse[i];
                for (int other : model.graph[i]) last = std::max(last, inverse[other]);
                ++graphDelta[inverse[i]];
                --graphDelta[last];
            }
            const auto addScope = [&](span<const int> scope, int *delta) {
                int first = q, last = -1;
                for (int candidate : scope) {
                    first = std::min(first, inverse[candidate]);
                    last = std::max(last, inverse[candidate]);
                }
                if (first < last) {
                    ++delta[first];
                    --delta[last];
                }
            };
            for (int i = 0; i < model.zero_scopes.size(); ++i) addScope(model.zero_scopes[i], graphDelta);
            for (int i = 0; i < model.mine_scopes.size(); ++i) addScope(model.mine_scopes[i], factorDelta);
            for (int i = model.zero_scopes.size(); i < model.base_scopes.size(); ++i)
                addScope(model.base_scopes[i], factorDelta);
            int graph = 0, factors = 0, maxGraph = 0, maxTotal = 0;
            uint64_t work = 0;
            for (int i = 0; i + 1 < q; ++i) {
                graph += graphDelta[i];
                factors += factorDelta[i];
                maxGraph = std::max(maxGraph, graph);
                maxTotal = std::max(maxTotal, graph + factors);
                const uint64_t step = uint64_t{1} << std::min(graph + factors, 60);
                work = std::min(work, std::numeric_limits<uint64_t>::max() - step) + step;
            }
            const Score score{maxTotal + maxGraph, maxTotal, work};
            if (sweep == 0 || score < best) {
                best = score;
                std::copy_n(order, q, bestOrder);
            }
        }
        orderModel(model, bestOrder, output);
    }


private:
    struct Relations {
        BoardBits propagation[cells], baseFactors[cells];
        MineBits mineFactors[cells];
    };

    struct Workspace {
        struct {
            Model layout, partial, remaining, reduced[2], ordered;
        } model;

        // 危险：各阶段复用缓冲，仅本次写入的棋盘或下标范围有效。
        struct {
            bool unseen[cells], adjacentToZero[cells];
            int component[cells], stack[cells], scope[cells];
        } build;
        struct {
            int anchors[cells], remap[cells];
            bool keep[cells];
        } restriction;
        struct {
            Relations relations;
            int privateMines[cells], adjacent[cells], baseIds[cells];
            uint64_t localEdges[cells], localBaseHits[cells];
        } reduction;
        struct {
            int bestOrder[cells], byRank[cells], order[cells], inverse[cells];
            int graphDelta[cells], factorDelta[cells];
        } order;
    };

    __declspec(noinline) Workspace &getWorkspace() {
        static thread_local Workspace workspace;
        return workspace;
    }

    int adjacentCandidates(const Model &model, span<const int> adjacent, int *output) {
        int count = 0;
        for (int other : adjacent) {
            const int id = model.candidate_index[other];
            if (id >= 0) output[count++] = id;
        }
        return count;
    }

    void clearGraph(Model &model) {
        model.candidates.size = 0;
        model.graph.clear();
        model.zero_scopes.clear();
        model.mine_scopes.clear();
        model.base_scopes.clear();
    }

    void appendRestricted(Scopes &out, span<const int> source, const int *remap) {
        vectorPool<int>::vector part = out.values.push_back();
        vector<int> &values = out.values.data;
        values.resize(part.offset + source.size);
        for (int at : source) {
            const int mapped = remap[at];
            if (mapped >= 0) values[part.offset + part.count++] = mapped;
        }
        // 保留空区间，因子编号依赖各区间的位置。
        values.size = part.offset + part.count;
        out.parts.push_back(part);
    }

    void restrictScopes(const Scopes &source, const int *remap, Scopes &output) {
        for (int i = 0; i < source.size(); ++i) appendRestricted(output, source[i], remap);
    }

    void restrictCandidates(const Model &model, const bool *keep, int *remap, Model &output) {
        clearGraph(output);
        for (int old = 0; old < (int)model.candidates.size; ++old) {
            if (keep[old]) {
                remap[old] = (int)output.candidates.size;
                output.candidates.push_back(model.candidates[old]);
            } else remap[old] = -1;
        }
        for (int old = 0; old < (int)model.candidates.size; ++old)
            if (keep[old]) appendRestricted(output.graph, model.graph[old], remap);
    }

    void orderModel(const Model &model, const int *order, Model &output) {
        Workspace &workspace = getWorkspace();
        clearGraph(output);
        int (&inverse)[cells] = workspace.order.inverse;
        const int n = (int)model.candidates.size;
        for (int i = 0; i < n; ++i) {
            inverse[order[i]] = i;
            output.candidates.push_back(model.candidates[order[i]]);
        }
        const auto append = [&](Scopes &to, span<const int> source) {
            vector<int> &values = to.values.data;
            const vectorPool<int>::vector part{values.size, (size_t)source.size};
            values.resize(part.offset + part.count);
            for (int i = 0; i < source.size; ++i) values[part.offset + i] = inverse[source[i]];
            if (part.count > 1) std::sort(values.data + part.offset, values.data + part.offset + part.count);
            to.parts.push_back(part);
        };
        for (int i = 0; i < n; ++i) append(output.graph, model.graph[order[i]]);
        for (int i = 0; i < model.zero_scopes.size(); ++i) append(output.zero_scopes, model.zero_scopes[i]);
        for (int i = 0; i < model.mine_scopes.size(); ++i) append(output.mine_scopes, model.mine_scopes[i]);
        for (int i = 0; i < model.base_scopes.size(); ++i) append(output.base_scopes, model.base_scopes[i]);
    }

    void calculateRelations(const Model &model, Relations &result) {
        const int q = (int)model.candidates.size;
        std::fill_n(result.propagation, q, BoardBits{});
        std::fill_n(result.mineFactors, q, MineBits{});
        std::fill_n(result.baseFactors, q, BoardBits{});
        for (int i = 0; i < q; ++i)
            for (int other : model.graph[i]) result.propagation[i].set(other);
        // 同一零区的相邻双击中心属于同一个连通分量。
        for (int i = 0; i < model.zero_scopes.size(); ++i) {
            const span<const int> scope = model.zero_scopes[i];
            BoardBits unionMask{};
            for (int candidate : scope) unionMask.set(candidate);
            for (int candidate : scope) result.propagation[candidate] |= unionMask;
        }
        for (int i = 0; i < q; ++i) result.propagation[i].reset(i);
        for (int f = 0; f < model.mine_scopes.size(); ++f)
            for (int candidate : model.mine_scopes[f]) result.mineFactors[candidate].set(f);
        for (int f = 0; f < model.base_scopes.size(); ++f)
            for (int candidate : model.base_scopes[f]) result.baseFactors[candidate].set(f);
    }

    // 传播邻居要保留已有的覆盖和连通关系，同时不增加插旗要求。
    bool swapDominated(const Relations &relation, int victim, int by) {
        BoardBits missingBases = relation.baseFactors[victim];
        missingBases &= ~relation.baseFactors[by];
        if (missingBases.any()) return false;
        MineBits extraMines = relation.mineFactors[by];
        extraMines &= ~relation.mineFactors[victim];
        if (extraMines.any()) return false;
        BoardBits needed = relation.propagation[victim];
        needed.reset(by);
        needed &= ~relation.propagation[by];
        return !needed.any();
    }

    int unpack(const BoardBits &bits, int *ids) {
        int count = 0;
        bits.forEachSetBit([&](int bit) { ids[count++] = bit; });
        return count;
    }

    // 删除中心后，各分量取一个代表，代表构成独立集。
    // 最大的 |独立集| 减其覆盖数，与空集的点击上界合起来用于证明删除安全。
    bool removeLeftClickDominated(const Model &model, const Relations &relation, bool *keep) {
        Workspace &workspace = getWorkspace();
        int (&privateMines)[cells] = workspace.reduction.privateMines;
        int (&adjacent)[cells] = workspace.reduction.adjacent;
        int (&baseIds)[cells] = workspace.reduction.baseIds;
        uint64_t (&localEdges)[cells] = workspace.reduction.localEdges;
        uint64_t (&localBaseHits)[cells] = workspace.reduction.localBaseHits;
        const int q = (int)model.candidates.size;
        std::fill_n(privateMines, q, 0);
        for (int i = 0; i < model.mine_scopes.size(); ++i) {
            const span<const int> scope = model.mine_scopes[i];
            if (scope.size == 1) ++privateMines[scope[0]];
        }
        bool removed = false;
        for (int candidate = 0; candidate < q; ++candidate) {
            const int degree = unpack(relation.propagation[candidate], adjacent);
            const int bases = unpack(relation.baseFactors[candidate], baseIds);
            if (degree > 63 || bases > 63) continue;
            const int threshold = 2 + privateMines[candidate] - bases;
            if (threshold < 0) continue;

            for (int i = 0; i < degree; ++i) {
                uint64_t edges = 0, hits = 0;
                for (int j = 0; j < degree; ++j)
                    if (relation.propagation[adjacent[i]].test(adjacent[j])) edges |= uint64_t{1} << j;
                for (int b = 0; b < bases; ++b)
                    if (relation.baseFactors[adjacent[i]].test(baseIds[b])) hits |= uint64_t{1} << b;
                localEdges[i] = edges;
                localBaseHits[i] = hits;
            }
            int best = 0;
            if (!exceedsBound((uint64_t{1} << degree) - 1, 0, 0, threshold, best, localEdges, localBaseHits)) {
                keep[candidate] = false;
                removed = true;
            }
        }
        return removed;
    }

    bool exceedsBound(uint64_t available, int chosen, uint64_t covered, int threshold, int &best,
                      const uint64_t *edges, const uint64_t *baseHits) {
        const FrontierWord word;
        const int hits = word.populationCount(covered);
        best = std::max(best, chosen - hits);
        if (best > threshold) return true;
        if (chosen + word.populationCount(available) - hits <= best) return false;

        int pivot = 0, degree = -1;
        for (uint64_t scan = available; scan; scan &= scan - 1) {
            const int vertex = std::countr_zero(scan);
            const int current = word.populationCount(edges[vertex] & available);
            if (current > degree) {
                pivot = vertex;
                degree = current;
            }
        }
        available &= ~(uint64_t{1} << pivot);
        return exceedsBound(available & ~edges[pivot], chosen + 1, covered | baseHits[pivot], threshold, best, edges, baseHits)
            || exceedsBound(available, chosen, covered, threshold, best, edges, baseHits);
    }

};

Neighbors RefDom::makeNeighbors() {
    return Engine<side, side>().makeNeighbors();
}

void RefDom::buildModel(Mask layout, const Neighbors &adjacent, Model &model) {
    for (int cell = 0; cell < cells; ++cell) model.mines[cell] = layout.has(cell);
    Engine<side, side>().build({}, adjacent, model);
}

bool RefDom::partialOpening(const Model &model, Mask shown, const Neighbors &adjacent) {
    return Engine<side, side>().partialOpening(model, shown, adjacent);
}

int RefDom::remainingClicks(const Model &layout, Mask shown, Mask playerFlags, const Neighbors &adjacent) {
    return Engine<side, side>().remainingClicks(layout, shown, playerFlags, adjacent);
}

int RefDom::remainingClicks(const Grid<char> &layout, span<const int> shown, span<const int> playerFlags) {
    if (layout.rows() == side) return Engine<side, side>().remainingClicks(layout, shown, playerFlags);
    if (layout.cols() == 16) return Engine<16, 16>().remainingClicks(layout, shown, playerFlags);
    return Engine<16, 30>().remainingClicks(layout, shown, playerFlags);
}

} // 命名空间 mss::ioe::ref_dom
