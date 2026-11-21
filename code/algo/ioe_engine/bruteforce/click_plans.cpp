#include "algo/ioe_engine/bruteforce/click_plans.h"

#include <algorithm>
#include <bit>

#include "core/datastructure/flat_hashtable.h"

namespace mss {

struct IoeClickPlans::Workspace {
    static constexpr int centers = 64;
    struct Factors {
        std::uint64_t hits, flags, close, keep;
        int dense;
    };

    using Mask = std::uint64_t;
    struct Geometry {
        std::uint32_t next[2];
        int cost[2];
        int startups;
    };
    struct MemoKey {
        Mask hits;
        std::uint32_t geometry;
        int limit;
        bool operator==(const MemoKey &) const = default;
    };
    struct MemoHash {
        std::size_t operator()(const MemoKey &key) const {
            return splitmix64(key.hits ^ (key.geometry * 0x9e3779b97f4a7c15ULL) ^ (unsigned)key.limit);
        }
    };
    struct Choice {
        std::uint32_t next[2];
        int plans;
    };

    vectorPool<Mask> contacts;
    vector<vectorPool<Mask>::vector> geometries;
    FlatHashTable<std::uint64_t, std::uint32_t> byContacts;
    FlatHashTable<std::uint64_t, Geometry> transitions[centers];
    FlatHashTable<MemoKey, std::uint32_t, MemoHash> nodes[centers];
    FlatHashTable<MemoKey, int, MemoHash> failures[centers];
    vector<Choice> choices;
    vector<std::uint8_t> dense;
    FlatHashTable<std::uint64_t, int> sparse[centers];
};

__declspec(noinline) IoeClickPlans::Workspace &IoeClickPlans::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void IoeClickPlans::Pool::clear() {
    plans.data.size = components.data.size = 0;
}

IoeClickPlans::Result IoeClickPlans::build(const IoeStrategyBoard &layout, const IoeView &root, int optimum, int budget, Pool &pool) {
    using Mask = std::uint64_t;
    constexpr int cells = IoeBoard::cells;
    constexpr int capacity = Workspace::centers;
    const IoeCells *adjacent = IoeBoard().neighbors();
    const IoeCells safe = IoeCells(cells) & ~layout.mines;
    const IoeCells hidden = safe & ~root.revealed;
    const IoeCells wrong = safe & root.flags;
    const IoeCells uncoveredZeros = layout.zeros & hidden;
    vectorPool<Plan> &plans = pool.plans;
    vectorPool<IoeCells> &components = pool.components;
    Result result{Coverage::Unavailable, plans.push_back()};

    auto expand = [&](const IoeCells &set) {
        IoeCells neighbors;
        set.forEachSetBit([&](int cell) { neighbors |= adjacent[cell]; });
        return neighbors;
    };
    auto connected = [&](IoeCells &pending) {
        IoeCells component, frontier;
        frontier.set(pending.firstSetBit());
        do {
            pending &= ~frontier;
            component |= frontier;
            frontier = expand(frontier) & pending;
        } while (frontier.any());
        return component;
    };

    const IoeCells zeroBoundary = expand(layout.zeros);
    if ((wrong & (layout.zeros | zeroBoundary)).any() || (expand(layout.zeros & root.revealed) & hidden).any()) return result;
    const IoeCells independent = hidden & ~(layout.zeros | zeroBoundary);

    int center[cells], index[cells], count = 0;
    Mask anchored = 0;
    std::fill_n(index, cells, -1);
    (safe & ~layout.zeros).forEachSetBit([&](int cell) {
        if ((adjacent[cell] & hidden).popcount() < 2) return;
        index[cell] = count;
        center[count++] = cell;
    });
    if (count > capacity) return result;

    auto scope = [&](const IoeCells &set) {
        Mask result = 0;
        set.forEachSetBit([&](int cell) { if (index[cell] >= 0) result |= Mask{1} << index[cell]; });
        return result;
    };
    anchored = scope(root.revealed);
    int fixed = wrong.popcount();

    struct Factor {
        Mask members;
        bool flag;
    } factors[cells];
    int factorCount = 0;
    independent.forEachSetBit([&](int cell) {
        Mask members = scope(adjacent[cell]);
        if (index[cell] >= 0) members |= Mask{1} << index[cell];
        if (members) factors[factorCount++] = {members, false};
        else ++fixed;
    });
    (layout.mines & ~root.flags).forEachSetBit([&](int cell) {
        const Mask members = scope(adjacent[cell]);
        if (members) factors[factorCount++] = {members, true};
    });

    Mask neighbors[capacity], introductions[capacity]{};
    for (int i = 0; i < count; ++i) neighbors[i] = scope(adjacent[center[i]]);
    struct Opening {
        Mask scope;
        int next;
    } openings[capacity];
    int firstOpening[capacity], openingCount = 0;
    std::fill_n(firstOpening, count, -1);
    auto addOpening = [&](Mask members) {
        const int at = std::countr_zero(members);
        openings[openingCount] = {members, firstOpening[at]};
        firstOpening[at] = openingCount++;
        introductions[at] |= members;
        for (Mask scan = members; scan; scan &= scan - 1) neighbors[std::countr_zero(scan)] |= members & ~(scan & (~scan + 1));
    };

    IoeCells zeros = uncoveredZeros;
    while (zeros.any()) {
        const Mask members = scope(expand(connected(zeros)));
        if (members) addOpening(members);
        else ++fixed;
    }
    if (anchored) {
        addOpening(anchored);
        --fixed;
    }

    Workspace &workspace = getWorkspace();
    FlatHashTable<std::uint64_t, int> (&sparse)[capacity] = workspace.sparse;
    Workspace::Factors layers[capacity]{};
    Mask bits[cells], used = 0;
    int denseSize = 0;
    for (int i = 0; i < count; ++i) {
        Workspace::Factors &layer = layers[i];
        const int width = std::bit_width(used);
        layer.dense = width <= 14 ? denseSize : -1;
        if (layer.dense >= 0) denseSize += 1 << width;
        else sparse[i].clear();

        const Mask here = Mask{1} << i;
        for (int f = 0; f < factorCount; ++f) {
            const Factor &factor = factors[f];
            if (std::countr_zero(factor.members) == i) {
                if (used == ~Mask{}) return result;
                bits[f] = Mask{1} << std::countr_one(used);
                used |= bits[f];
            }
        }
        for (int f = 0; f < factorCount; ++f) {
            const Factor &factor = factors[f];
            if (!(factor.members & here)) continue;
            const Mask bit = bits[f];
            layer.hits |= bit;
            if (factor.flag) layer.flags |= bit;
            if (std::bit_width(factor.members) - 1 == i) {
                if (!factor.flag) layer.close |= bit;
                used &= ~bit;
            }
        }
        layer.keep = used;
    }
    vector<std::uint8_t> &dense = workspace.dense;
    dense.resize(denseSize);
    if (denseSize) std::fill_n(dense.data, denseSize, 255);
    int lowerStates = 0;
    auto lower = [&](auto &self, int i, Mask hits) -> int {
        if (i == count) return 0;
        const Workspace::Factors &here = layers[i];
        if (here.dense >= 0) {
            const int saved = dense[here.dense + hits];
            if (saved != 255) return saved;
        } else if (const int *saved = sparse[i].find(hits)) return *saved;
        if (++lowerStates > 300000) return 0;
        const Mask covered = hits | here.hits;
        const int pass = CountBits()(here.close & ~hits) + self(self, i + 1, hits & here.keep);
        const int take = 1 + CountBits()((here.flags & ~hits) | (here.close & ~covered)) + self(self, i + 1, covered & here.keep);
        const int value = std::min(pass, take);
        if (here.dense >= 0) dense[here.dense + hits] = value;
        else sparse[i].emplace(hits, value);
        return value;
    };
    lower(lower, 0, 0);
    if (lowerStates > 300000) return result;

    // 下界把所有未处理中心都选上，因此只保留最宽松的分量数。
    Mask label[capacity][capacity], scheduled[capacity]{};
    for (int i = 0; i < count; ++i) {
        const Mask remaining = (Mask{1} << i) - 1;
        Mask unvisited = (count == capacity ? ~Mask{} : (Mask{1} << count) - 1) & ~remaining;
        while (unvisited) {
            const int start = std::countr_zero(unvisited);
            const Mask id = Mask{1} << start;
            Mask pending = id;
            unvisited &= ~id;
            do {
                Mask next = 0;
                for (Mask group = pending; group; group &= group - 1) {
                    const int cell = std::countr_zero(group);
                    label[i][cell] = id;
                    next |= neighbors[cell];
                }
                pending = next & unvisited;
                unvisited &= ~pending;
            } while (pending);
        }
        for (int j = i; j < count; ++j)
            for (Mask pending = introductions[j]; pending; pending &= pending - 1)
                scheduled[i] |= label[i][std::countr_zero(pending)];
    }

    struct Frontier {
        Mask contacts[capacity];
        int size;

        Frontier() : size(0) {}
    };
    auto connectionBound = [&](int step, const Frontier &in) {
        Mask merged[capacity];
        int groups = 0;
        Mask used = 0;
        for (int i = 0; i < in.size; ++i) {
            Mask join = 0;
            for (Mask scan = in.contacts[i]; scan; scan &= scan - 1) join |= label[step][std::countr_zero(scan)];
            used |= join;
            for (int j = 0; j < groups; ) {
                if (!(merged[j] & join)) { ++j; continue; }
                join |= merged[j];
                merged[j] = merged[--groups];
            }
            merged[groups++] = join;
        }
        return groups + CountBits()(scheduled[step] & ~used);
    };

    vectorPool<Mask> &contacts = workspace.contacts;
    vector<vectorPool<Mask>::vector> &geometries = workspace.geometries;
    FlatHashTable<std::uint64_t, std::uint32_t> &byContacts = workspace.byContacts;
    FlatHashTable<std::uint64_t, Workspace::Geometry> (&transitions)[capacity] = workspace.transitions;
    contacts.data.size = geometries.size = 0;
    geometries.push_back({0, 0});
    byContacts.clear();
    for (int i = 0; i < count; ++i) transitions[i].clear();

    auto intern = [&](Frontier &frontier) {
        if (!frontier.size) return std::uint32_t{0};
        std::sort(frontier.contacts, frontier.contacts + frontier.size);
        std::uint64_t key = frontier.size;
        for (int i = 0; i < frontier.size; ++i) key = (key ^ frontier.contacts[i]) * 0x9e3779b185ebca87ULL;
        while (const std::uint32_t *id = byContacts.find(key)) {
            const vectorPool<Mask>::vector &old = geometries[*id];
            if (old.count == (std::size_t)frontier.size
                    && std::equal(frontier.contacts, frontier.contacts + frontier.size, contacts.data.data + old.offset)) return *id;
            key += 0xd1b54a32d192ed03ULL;
        }
        const std::uint32_t id = geometries.size;
        vectorPool<Mask>::vector part = contacts.push_back();
        for (int i = 0; i < frontier.size; ++i) contacts.push_back(part, frontier.contacts[i]);
        geometries.push_back(part);
        byContacts.emplace(key, id);
        return id;
    };

    auto advance = [&](int i, std::uint32_t id) {
        if (const Workspace::Geometry *cached = transitions[i].find(id)) return *cached;
        const Mask head = Mask{1} << i;
        Frontier in;
        const vectorPool<Mask>::vector part = geometries[id];
        if (part.count) std::copy_n(contacts.data.data + part.offset, part.count, in.contacts);
        in.size = part.count;
        Workspace::Geometry result{{0, 0}, {0, 0}, connectionBound(i, in)};
        for (int take = 0; take < 2; ++take) {
            Frontier next;
            Mask join = take ? neighbors[i] & ~(head - 1) & ~head : 0;
            auto transfer = [&](Mask frontier) {
                if (take && (frontier & head)) join |= frontier & ~head;
                else if (const Mask rest = frontier & ~head) next.contacts[next.size++] = rest;
                else ++result.cost[take];
            };
            for (int j = 0; j < in.size; ++j) transfer(in.contacts[j]);
            for (int j = firstOpening[i]; j >= 0; j = openings[j].next) transfer(openings[j].scope);
            if (take) {
                if (join) next.contacts[next.size++] = join;
                else ++result.cost[take];
            }
            result.next[take] = intern(next);
        }
        transitions[i].emplace(id, result);
        return result;
    };

    vector<Workspace::Choice> &choices = workspace.choices;
    FlatHashTable<Workspace::MemoKey, std::uint32_t, Workspace::MemoHash> (&nodes)[capacity] = workspace.nodes;
    FlatHashTable<Workspace::MemoKey, int, Workspace::MemoHash> (&failures)[capacity] = workspace.failures;
    const vectorPool<IoeCells>::vector start = components.push_back();
    auto buildThrough = [&](int maximum) {
        choices.resize(2);
        choices[0] = {{0, 0}, 0};
        choices[1] = {{0, 0}, 1};
        for (int i = 0; i < count; ++i) {
            nodes[i].clear();
            failures[i].clear();
        }
        int visits = 0;
        bool exhausted = false;
        auto countPlans = [&](auto &self, int i, int limit, Mask hits, std::uint32_t id) -> std::uint32_t {
            if (limit < 0 || exhausted) return 0;
            if (i == count) return 1;
            const int minimum = lower(lower, i, hits);
            if (minimum > limit) return 0;
            const Workspace::MemoKey base{hits, id, 0};
            if (const int *over = failures[i].find(base))
                if (*over >= limit) return 0;
            const Workspace::MemoKey key{hits, id, limit};
            if (const std::uint32_t *cached = nodes[i].find(key)) return *cached;
            if (++visits > 1000000) {
                exhausted = true;
                return 0;
            }
            const Workspace::Geometry geometry = advance(i, id);
            if (minimum + geometry.startups > limit) {
                failures[i][base] = limit;
                return 0;
            }

            const Workspace::Factors &layer = layers[i];
            Workspace::Choice next{{0, 0}, 0};
            for (int take = 0; take < 2 && next.plans <= 10000; ++take) {
                const Mask covered = take ? hits | layer.hits : hits;
                int cost = take + CountBits()(layer.close & ~covered) + geometry.cost[take];
                if (take) cost += CountBits()(layer.flags & ~hits);
                next.next[take] = self(self, i + 1, limit - cost, covered & layer.keep, geometry.next[take]);
                next.plans += choices[next.next[take]].plans;
            }
            if (!next.plans) {
                failures[i][base] = limit;
                return 0;
            }
            next.plans = std::min(next.plans, 10001);
            const std::uint32_t node = choices.size;
            choices.push_back(next);
            nodes[i].emplace(key, node);
            return node;
        };

        const std::uint32_t path = countPlans(countPlans, 0, maximum - fixed, 0, 0);
        if (exhausted || choices[path].plans > 10000) return false;
        auto collect = [&](auto &self, int i, std::uint32_t edge, IoeCells chords) -> void {
            if (!edge) return;
            if (i == count) {
                const IoeCells touching = expand(chords);
                Plan plan{chords, touching & layout.mines, independent & ~(chords | touching), components.push_back()};
                IoeCells pending = uncoveredZeros | chords;
                while (pending.any()) {
                    const IoeCells component = connected(pending);
                    if (!(component & root.revealed).any()) components.push_back(plan.components, component);
                }
                plans.push_back(result.plans, plan);
                return;
            }
            self(self, i + 1, choices[edge].next[0], chords);
            chords.set(center[i]);
            self(self, i + 1, choices[edge].next[1], chords);
        };
        collect(collect, 0, path, {});
        return true;
    };

    if (buildThrough(budget)) {
        result.coverage = Coverage::Complete;
        return result;
    }
    plans.pop_back(result.plans);
    result.plans.count = 0;
    components.pop_back(start);
    if (budget > optimum && buildThrough(optimum)) {
        result.coverage = Coverage::RootOptimum;
        return result;
    }
    plans.pop_back(result.plans);
    result.plans.count = 0;
    components.pop_back(start);
    return result;
}

IoeCells IoeClickPlans::activeChords(const IoeCells &hidden, const IoeCells *adjacent) {
    IoeCells active = hidden;
    hidden.forEachSetBit([&](int cell) { active |= adjacent[cell]; });
    return active;
}

IoeClickPlans::Cost IoeClickPlans::measure(const Plan &plan, span<const IoeCells> components, const IoeCells &hidden, const IoeCells &active,
                                        const IoeCells &revealed, const IoeCells &missing, const IoeCells *adjacent) {
    Cost result{(plan.independent & hidden).popcount(), plan.independent, plan.chords & active};
    for (const IoeCells &component : components) {
        if ((component & revealed).any()) continue;
        result.opens |= component;
        ++result.clicks;
    }
    IoeCells needed = plan.flags & missing;
    if (needed.any() && result.chords != plan.chords) {
        needed = {};
        result.chords.forEachSetBit([&](int center) { needed |= adjacent[center] & missing; });
    }
    result.clicks += result.chords.popcount() + needed.popcount();
    return result;
}

int IoeClickPlans::minimum(const IoeStrategyBoard &layout, const IoeView &view, const Result &plans, const Pool &pool, int budget) {
    if (!plans.plans.count) return overBudget;
    const IoeCells *adjacent = IoeBoard().neighbors();
    const IoeCells safe = IoeCells(IoeBoard::cells) & ~layout.mines;
    const IoeCells hidden = safe & ~view.revealed;
    const IoeCells active = activeChords(hidden, adjacent);
    const IoeCells missing = layout.mines & ~view.flags;
    const int wrong = (view.flags & safe).popcount();
    int best = budget - wrong + 1;
    for (const Plan &plan : pool.plans.span(plans.plans)) {
        const span<const IoeCells> parts{pool.components.span(plan.components).begin(), (int)plan.components.count};
        best = std::min(best, measure(plan, parts, hidden, active, view.revealed, missing, adjacent).clicks);
    }
    return best + wrong <= budget ? best + wrong : overBudget;
}

void IoeClickPlans::estimate(const IoeStrategyBoard &layout, const IoeView &view, const Result &plans, const Pool &pool,
                            int optimum, Estimate &result) {
    const IoeCells *adjacent = IoeBoard().neighbors();
    const IoeCells safe = IoeCells(IoeBoard::cells) & ~layout.mines;
    const IoeCells hidden = safe & ~view.revealed;
    const IoeCells active = activeChords(hidden, adjacent);
    const IoeCells missing = layout.mines & ~view.flags;
    const int wrong = (view.flags & safe).popcount();
    result.opens = result.bestChords = {};
    std::fill_n(result.chords, IoeBoard::cells, 255);
    for (const Plan &plan : pool.plans.span(plans.plans)) {
        const span<const IoeCells> parts{pool.components.span(plan.components).begin(), (int)plan.components.count};
        const Cost cost = measure(plan, parts, hidden, active, view.revealed, missing, adjacent);
        const int clicks = cost.clicks + wrong;
        if (clicks == optimum) {
            result.opens |= cost.opens;
            result.bestChords |= cost.chords;
        }
        cost.chords.forEachSetBit([&](int center) { result.chords[center] = std::min<int>(result.chords[center], clicks); });
    }
}

} // 命名空间 mss
