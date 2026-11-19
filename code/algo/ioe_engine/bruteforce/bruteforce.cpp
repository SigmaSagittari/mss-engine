#include "algo/ioe_engine/bruteforce/bruteforce.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <tuple>

#include "algo/ioe_engine/ref_dom/ref_dom.h"
#include "core/assert.h"
#include "core/datastructure/flat_hashtable.h"
#include "game/position.h"

namespace mss::ioe {

using ref_dom::BaseDescription;
using ref_dom::Mask;
using ref_dom::Model;
using ref_dom::Neighbors;
using ref_dom::RefDom;
using ref_dom::cells;
using ref_dom::mines;
using ref_dom::side;
using Cell = ObservedBoard::CellState;
using Phase = IoeBruteForce::Phase;
using Status = IoeBruteForce::Status;
using Action = IoeBruteForce::Action;
using Ways = vector<uint64_t>;

constexpr int maxWinningClicks = (cells - mines) / 2;
constexpr Position::Config positionConfig{12, ShapeSolverConfig::Order::Adjacent, 0, nullptr};

struct Observation {
    Cell board[cells];
    int clicks;
    Mask flags;

    Observation(const ObservedBoard::Result &observed, const Grid<bool> &playerFlags, int clicks) : clicks(clicks), flags{} {
        std::copy_n(observed.cells[0], cells, board);
        for (int cell = 0; cell < cells; ++cell)
            if (playerFlags[0][cell]) flags.set(cell);
    }

    // 危险：哈希表预分配空槽。插入时先写完盘面、点击数和旗，再标记占用。
    Observation() = default;

    bool operator==(const Observation &other) const {
        return clicks == other.clicks && flags == other.flags && std::memcmp(board, other.board, sizeof(board)) == 0;
    }

    bool covered(int cell) const { return board[cell] == Cell::Hidden || board[cell] == Cell::ForcedSafe; }

    Mask revealed() const {
        Mask shown{};
        for (int cell = 0; cell < cells; ++cell)
            if (ObservedBoard().isNumber(board[cell])) shown.set(cell);
        return shown;
    }

    bool finished() const {
        int shown = 0;
        for (Cell cell : board) shown += ObservedBoard().isNumber(cell);
        return shown == cells - mines;
    }
};

struct ObservationHash {
    size_t operator()(const Observation &state) const {
        U128Hasher hasher;
        hasher.mixBytes(state.board, sizeof(state.board));
        hasher.mix(state.flags.low, state.flags.high, state.clicks);
        return U128Hash()(hasher.finalize());
    }
};

struct ProgressReporter {
    const std::function<void(const IoeBruteForce::Progress &)> &publish;
    IoeBruteForce::Progress progress;
    std::chrono::steady_clock::time_point last;

    void report(bool phaseChanged) {
        if (!publish) return;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (!phaseChanged && now - last < std::chrono::milliseconds(100)) return;
        last = now;
        publish(progress);
    }
};

// 只有必雷和必安参与枚举约束。玩家旗独立保存在 Observation 中。
struct Enumerator {
    struct Clue { int need; int unknown; };
    struct Membership { int ids[8]; int size; };
    struct Constraints {
        Membership memberships[cells];
        Clue clues[cells];
        int unknown[cells];
        int clueCount;
        int unknownCount;

        // 危险：只访问已录入的约束和未知格。enumerate 写入有效前缀，visit 递归修改。
        Constraints() : memberships{}, clueCount(0), unknownCount(0) {}
    };

    void visit(Constraints &constraints, int at, int left, Mask map, vector<Mask> &layouts, ProgressReporter &reporter) {
        if (at == constraints.unknownCount) {
            layouts.push_back(map);
            if ((layouts.size & 511) == 0) {
                reporter.progress.enumerated = layouts.size;
                reporter.report(false);
            }
            return;
        }
        const int cell = constraints.unknown[at];
        const Membership &membership = constraints.memberships[cell];
        bool safe = left < constraints.unknownCount - at;
        bool mine = left > 0;
        for (int i = 0; i < membership.size; ++i) {
            Clue &clue = constraints.clues[membership.ids[i]];
            safe &= clue.need < clue.unknown;
            mine &= clue.need > 0;
            --clue.unknown;
        }
        if (safe) visit(constraints, at + 1, left, map, layouts, reporter);
        if (mine) {
            map.set(cell);
            for (int i = 0; i < membership.size; ++i) --constraints.clues[membership.ids[i]].need;
            visit(constraints, at + 1, left - 1, map, layouts, reporter);
            for (int i = 0; i < membership.size; ++i) ++constraints.clues[membership.ids[i]].need;
        }
        for (int i = 0; i < membership.size; ++i) ++constraints.clues[membership.ids[i]].unknown;
    }

    void enumerate(const Observation &state, const Neighbors &adjacent, vector<Mask> &layouts, ProgressReporter &reporter) {
        Constraints constraints;
        Mask fixed{};
        int remaining = mines;
        for (int cell = 0; cell < cells; ++cell) {
            const Cell value = state.board[cell];
            if (value == Cell::ForcedMine) {
                fixed.set(cell);
                --remaining;
            } else if (value == Cell::Hidden) {
                constraints.unknown[constraints.unknownCount++] = cell;
            } else if (ObservedBoard().isNumber(value)) {
                Clue clue{ObservedBoard().numberValue(value), 0};
                for (int next : adjacent[cell]) {
                    if (state.board[next] == Cell::ForcedMine) --clue.need;
                    if (state.board[next] != Cell::Hidden) continue;
                    Membership &membership = constraints.memberships[next];
                    membership.ids[membership.size++] = constraints.clueCount;
                    ++clue.unknown;
                }
                constraints.clues[constraints.clueCount++] = clue;
            }
        }
        std::sort(constraints.unknown, constraints.unknown + constraints.unknownCount, [&](int left, int right) {
            const int a = constraints.memberships[left].size;
            const int b = constraints.memberships[right].size;
            return a != b ? a > b : left < right;
        });
        visit(constraints, 0, remaining, fixed, layouts, reporter);
    }
};

struct RootFilter {
    bool exceedsClicks(const Model &model, const Mask &shown, const Mask &flags, int budget);
    bool exceedsFlags(const Model &model, Mask shown, Mask flags, int budget);
};

bool RootFilter::exceedsClicks(const Model &model, const Mask &shown, const Mask &flags, int budget) {
    if (budget < 0) return true;
    // 1 到 8 的最小公倍数。每颗雷最多毗邻 8 个双击中心，分摊无需浮点数。
    constexpr int click = 840;
    struct Unit {
        span<const int> centers;
        int ownCenter;
        int width;
        int count;
    };
    Unit units[cells];
    int coverage[cells]{};
    int size = 0;
    for (int i = 0; i < model.base_scopes.size(); ++i) {
        const BaseDescription &base = model.base_descriptions[i];
        if (shown.has(base.representative)) continue;
        const int own = base.kind == BaseDescription::Kind::Single
            ? model.candidate_index[base.representative] : -1;
        const span<const int> scope = model.base_scopes[i];
        units[size++] = {scope, own, 0, scope.size - (own >= 0)};
        // 双击中心自身必须先翻开，不能计作这次双击打开的单格。
        for (int center : scope) {
            if (center != own) ++coverage[center];
        }
    }

    int capacity[cells];
    std::fill_n(capacity, cells, click);
    for (size_t i = 0; i < model.mine_cells.size; ++i) {
        if (flags.has(model.mine_cells[i])) continue;
        const span<const int> scope = model.mine_scopes[i];
        int users = 0;
        for (int center : scope) users += coverage[center] > 0;
        if (!users) continue;
        for (int center : scope) {
            if (coverage[center]) capacity[center] += click / users;
        }
    }

    int order[cells];
    std::iota(order, order + size, 0);
    for (int i = 0; i < size; ++i) {
        Unit &unit = units[i];
        for (int center : unit.centers) {
            if (center != unit.ownCenter) unit.width += coverage[center] - 1;
        }
    }
    for (int pass = 0; pass < 5; ++pass) {
        if (pass == 1) std::reverse(order, order + size);
        if (pass >= 2) {
            const auto key = [&](int i) {
                const Unit &unit = units[i];
                if (pass == 2) return std::tuple{unit.width, unit.count, i};
                return std::tuple{unit.count, pass == 3 ? unit.width : -unit.width, i};
            };
            std::sort(order, order + size, [&](int a, int b) { return key(a) < key(b); });
        }
        int left[cells];
        std::copy_n(capacity, cells, left);
        int total = 0;
        for (int i = 0; i < size; ++i) {
            const Unit &unit = units[order[i]];
            int weight = click;
            for (int center : unit.centers) {
                if (center != unit.ownCenter) weight = std::min(weight, left[center]);
            }
            for (int center : unit.centers) {
                if (center != unit.ownCenter) left[center] -= weight;
            }
            total += weight;
            if (total > budget * click) return true;
        }
    }
    return false;
}

struct CoverageProfile {
    Mask covered;
    int counts[cells + 1];
    int largest;

    CoverageProfile() : covered{}, counts{}, largest(0) {}

    void add(Mask units, int gain) {
        if (gain == 0) return;
        covered.low |= units.low;
        covered.high |= units.high;
        ++counts[gain];
        largest = std::max(gain, largest);
    }

    bool fits(int units, int budget) const {
        if (units <= budget) return true;
        const int total = std::popcount(covered.low) + std::popcount(covered.high);
        int chords = 0, sum = 0;
        for (int gain = largest; gain > 0 && sum < total; --gain) {
            for (int left = counts[gain]; left && sum < total; --left) {
                if (++chords > budget) return false;
                sum += gain;
                if (chords + units - std::min(sum, total) <= budget) return true;
            }
        }
        return false;
    }
};

bool RootFilter::exceedsFlags(const Model &model, Mask shown, Mask flags, int budget) {
    Mask covers[cells]{};
    int gains[cells]{};
    unsigned flagMasks[cells]{};
    int units = 0;
    for (int i = 0; i < model.base_scopes.size(); ++i) {
        const BaseDescription &base = model.base_descriptions[i];
        if (shown.has(base.representative)) continue;
        const int own = base.kind == BaseDescription::Kind::Single
            ? model.candidate_index[base.representative] : -1;
        for (int center : model.base_scopes[i]) {
            if (center == own) continue;
            covers[center].set(units);
            ++gains[center];
        }
        ++units;
    }
    if (units <= budget) return false;
    unsigned unpaid = 0;
    for (size_t m = 0; m < model.mine_cells.size; ++m) {
        if (flags.has(model.mine_cells[m])) continue;
        const unsigned bit = 1u << m;
        unpaid |= bit;
        for (int center : model.mine_scopes[m]) flagMasks[center] |= bit;
    }
    const int centers = (int)model.candidates.size;
    const int maxFlags = std::min(budget, std::popcount(unpaid));
    for (int k = 0; k <= maxFlags; ++k) {
        CoverageProfile plain;
        int shares[mines]{};
        for (int center = 0; center < centers; ++center) {
            const unsigned needs = flagMasks[center];
            if (std::popcount(needs) > k) continue;
            plain.add(covers[center], gains[center]);
            if (gains[center] <= 1) continue;
            for (unsigned left = needs; left; left &= left - 1)
                shares[std::countr_zero(left)] += gains[center] - 1;
        }
        if (!plain.fits(units, budget - k)) continue;
        // 对两个影响最大的旗分别考虑插与不插，避免把互斥的双击覆盖算在一起。
        unsigned selected = 0;
        for (int i = 0; i < 2; ++i) {
            int *best = std::max_element(shares, shares + mines);
            if (!*best) break;
            selected |= 1u << (best - shares);
            *best = 0;
        }
        if (!selected) return false;
        const unsigned others = unpaid & ~selected;
        for (unsigned subset = selected;; subset = (subset - 1) & selected) {
            const int otherCount = k - std::popcount(subset);
            if (otherCount >= 0 && otherCount <= std::popcount(others)) {
                CoverageProfile profile;
                for (int center = 0; center < centers; ++center) {
                    const unsigned needs = flagMasks[center];
                    if ((needs & selected & ~subset) || std::popcount(needs & others) > otherCount) continue;
                    profile.add(covers[center], gains[center]);
                }
                if (profile.fits(units, budget - k)) return false;
            }
            if (!subset) break;
        }
    }
    return true;
}

struct Reward {
    long double points;
    size_t wins;
    size_t above200;

    struct Points {
        long double values[cells - mines + 1][maxWinningClicks + 1];

        Points() {
            for (int bv = 0; bv <= cells - mines; ++bv) {
                values[bv][0] = bv ? std::numeric_limits<long double>::infinity() : 1;
                for (int clicks = 1; clicks <= maxWinningClicks; ++clicks)
                    values[bv][clicks] = bv == 2 * clicks ? 1 : std::exp2(20.0L * (bv - 2 * clicks) / clicks);
            }
        }
    };

    long double pointsFor(int bv, int clicks) const {
        static const Points table;
        return table.values[bv][clicks];
    }

    Reward() : points(0), wins(0), above200(0) {}
    Reward(int threeBv, int clicks)
        : points(pointsFor(threeBv, clicks)), wins(1), above200(threeBv > 2 * clicks) {}

    void add(const Reward &other) {
        points += other.points;
        wins += other.wins;
        above200 += other.above200;
    }

    bool canImprove(const Reward &best) const {
        if (!above200 && !best.above200) return wins > best.wins;
        // 加权分数接近时继续搜索，避免舍入误差导致剪枝。
        const long double tolerance = 32 * std::numeric_limits<long double>::epsilon()
            * std::max(wins, best.wins) * std::max(points, best.points);
        return points + tolerance >= best.points;
    }
};

struct Move {
    int cell;
    Action action;
    unsigned chordFlags;
};

// 一张摆法共用根部预算内的双击组合。剩余点击随已揭格和已放旗重新计算。
struct ChordPlans {
    struct Plan {
        uint64_t selected;
        Mask singles;
        uint32_t start; // components 中的下标。
        uint16_t flags;
        uint16_t count;
    };
    struct NextMoves {
        uint64_t chords[maxWinningClicks + 1];
        Mask bestOpens;
    };

    // 危险：opening 由 build 填满后才读。其他数组从空集合开始累加。
    uint64_t blocks[cells];
    uint64_t users[mines];
    int minesAt[mines];
    uint8_t opening[cells];
    uint8_t centerNumber[cells];
    Mask blockCells;
    Mask wrongAtRoot;
    vector<Plan> plans;
    vector<uint32_t> components;
    vector<Mask> uniqueComponents;
    int rootTotal;
    int words;

    ChordPlans(int rootTotal) : blocks{}, users{}, minesAt{}, centerNumber{}, blockCells{}, wrongAtRoot{},
                                rootTotal(rootTotal), words(0) {}

    uint64_t bit(int index) const { return uint64_t{1} << index; }
    Mask without(Mask a, Mask b) const { return {a.low & ~b.low, a.high & ~b.high}; }
    bool overlaps(Mask a, Mask b) const { return (a.low & b.low) || (a.high & b.high); }
    int count(Mask mask) const { return std::popcount(mask.low) + std::popcount(mask.high); }

    int distance(Mask shown, Mask flags, int lower, int cap, const uint64_t *parent, uint64_t *save,
                 NextMoves *next, int minimum) const {
        const Mask hidden = without(blockCells, shown);
        uint64_t undone = 0;
        for (uint64_t bits = hidden.low; bits; bits &= bits - 1) undone |= blocks[std::countr_zero(bits)];
        for (uint64_t bits = hidden.high; bits; bits &= bits - 1) undone |= blocks[64 + std::countr_zero(bits)];
        const int wrong = count({flags.low & wrongAtRoot.low, flags.high & wrongAtRoot.high});
        lower -= wrong;
        unsigned paidFlags = 0;
        for (int m = 0; m < mines; ++m)
            if (flags.has(minesAt[m])) paidFlags |= 1U << m;
        int best = cap + 1 - wrong;
        for (int word = 0; word < words; ++word) {
            uint64_t candidates = parent ? parent[word] : UINT64_MAX;
            if (word + 1 == words && plans.size % 64) candidates &= bit(plans.size % 64) - 1;
            for (; candidates; candidates &= candidates - 1) {
                const Plan &plan = plans[word * 64 + std::countr_zero(candidates)];
                const uint64_t future = plan.selected & undone;
                int cost = std::popcount(future) + count(without(plan.singles, shown));
                if (cost >= best) continue;
                unsigned missing = plan.flags & ~paidFlags;
                if (future == plan.selected) cost += std::popcount(missing);
                else for (; missing; missing &= missing - 1)
                    cost += (users[std::countr_zero(missing)] & future) != 0;
                for (unsigned i = 0; i < plan.count; ++i)
                    cost += !overlaps(uniqueComponents[components[plan.start + i]], shown);
                if (save) {
                    if (cost >= best) continue;
                    save[word] |= candidates & -candidates;
                    next->chords[cost + wrong] |= future;
                    if (cost + wrong != minimum) continue;
                    next->bestOpens.low |= plan.singles.low;
                    next->bestOpens.high |= plan.singles.high;
                    for (unsigned i = 0; i < plan.count; ++i) {
                        const Mask component = uniqueComponents[components[plan.start + i]];
                        if (overlaps(component, shown)) continue;
                        next->bestOpens.low |= component.low;
                        next->bestOpens.high |= component.high;
                    }
                } else {
                    best = std::min(best, cost);
                    if (best == lower) return best + wrong;
                }
            }
        }
        return best + wrong;
    }
};

struct ChordPlanBuilder {
    struct Connection {
        vectorPool<uint64_t>::vector groups; // Storage::groups 中的切片。
        int next[2];
        int closed[2];
        int closureLower;
        int sameHash;
    };
    struct Row {
        uint64_t stay;
        uint64_t touch;
        uint64_t mines;
        uint64_t ends;
        uint64_t contacts;
        uint64_t mandatoryComponents;
        uint8_t joinedByFuture[64];
        vector<uint64_t> openings;
        FlatHashTable<uint64_t, int> connections;
        vector<Connection> conn;
        vector<uint8_t> costs;
        FlatHashTable<uint64_t, int> sparse;
    };
    struct Failure {
        uint64_t state;
        int row;
        int id;

        bool operator==(const Failure &) const = default;
        struct Hash {
            size_t operator()(const Failure &key) const {
                return SplitMix64Hash()(key.state ^ ((uint64_t)key.id << 32) ^ key.row);
            }
        };
    };
    struct ComponentHash {
        size_t operator()(const Mask &mask) const { return SplitMix64Hash()(mask.low ^ (mask.high * 0x9e3779b97f4a7c15ULL)); }
    };
    struct Storage {
        Row rows[65];
        vectorPool<uint64_t> groups;
        FlatHashTable<Failure, int, Failure::Hash> failures;
        FlatHashTable<Mask, uint32_t, ComponentHash> components;

        void reset(int count) {
            groups.data.resize(0);
            failures.clear();
            components.clear();
            for (int i = 0; i <= count; ++i) {
                Row &row = rows[i];
                row.stay = row.touch = row.mines = row.ends = row.contacts = row.mandatoryComponents = 0;
                row.openings.resize(0);
                row.connections.clear();
                row.conn.resize(0);
                row.costs.resize(0);
                row.sparse.clear();
            }
        }
    };

    ChordPlans &output;
    Mask rootShown;
    int budget;
    bool tightBudget;
    bool ready;
    int enumerated;
    int states;
    int n;
    int nz;
    unsigned initialFlags;
    Mask covers[64];
    Mask singles;
    Mask units;
    int centers[64];
    int zeroCells[cells];
    uint64_t links[64];
    uint64_t zeros[64];
    uint64_t covering[cells];
    unsigned needs[64];

    ChordPlanBuilder(ChordPlans &output, Mask shown, int allowance, bool tightBudget)
        : output(output), rootShown(shown), budget(allowance), tightBudget(tightBudget), ready(true),
          enumerated(0), states(0), n(0), nz(0), initialFlags(0), covers{}, singles{}, units{}, links{}, zeros{}, covering{}, needs{} {}

    bool build(const Model &model, const Neighbors &adjacent, Mask flags, Storage &storage);
    bool prepare(Storage &storage, int &constant);
    int closureLower(int i, Connection &connection, const Row &row, const vectorPool<uint64_t> &groups);
    int best(int i, uint64_t state, Row *rows);
    int intern(Row &row, const uint64_t *groups, int count, vectorPool<uint64_t> &pool);
    void connect(int i, int id, Row *rows, vectorPool<uint64_t> &pool);
    bool enumerate(int i, int id, uint64_t state, int limit, uint64_t selected, unsigned used, Mask uncovered, Storage &storage);
    void record(uint64_t selected, unsigned flags, Mask uncovered, Storage &storage);

    int find(const int *parent, int i) const {
        while (parent[i] != i) i = parent[i];
        return i;
    }
    void join(int *parent, int a, int b) const { parent[find(parent, a)] = find(parent, b); }
};

void ChordPlanBuilder::record(uint64_t selected, unsigned flags, Mask uncovered, Storage &storage) {
    int parent[cells];
    Mask anchors[cells]{};
    for (int i = 0; i < n + nz; ++i) parent[i] = i;
    for (uint64_t bits = selected; bits; bits &= bits - 1) {
        const int i = std::countr_zero(bits);
        for (uint64_t near = links[i] & selected; near; near &= near - 1) join(parent, i, std::countr_zero(near));
        for (uint64_t near = zeros[i]; near; near &= near - 1) join(parent, i, n + std::countr_zero(near));
    }
    for (uint64_t bits = selected; bits; bits &= bits - 1) {
        const int i = std::countr_zero(bits);
        anchors[find(parent, i)].set(centers[i]);
    }
    for (int i = 0; i < nz; ++i) anchors[find(parent, n + i)].set(zeroCells[i]);
    const Mask remainingSingles{uncovered.low & singles.low, uncovered.high & singles.high};
    int left = budget - std::popcount(selected) - std::popcount(flags & ~initialFlags) - output.count(remainingSingles);
    int indices[cells], count = 0;
    for (int i = 0; i < n + nz; ++i) {
        if (!(anchors[i].low || anchors[i].high) || output.overlaps(rootShown, anchors[i])) continue;
        if (--left < 0) return;
        indices[count++] = i;
    }
    // 表超过固定上限时，由精确 DOM 回答这张摆法的距离查询。
    if (output.plans.size == 10'000) {
        ready = false;
        return;
    }
    const uint32_t start = output.components.size;
    FlatHashTable<Mask, uint32_t, ComponentHash> &components = storage.components;
    for (int i = 0; i < count; ++i) {
        const Mask component = anchors[indices[i]];
        if (const uint32_t *found = components.find(component)) output.components.push_back(*found);
        else {
            output.components.push_back(output.uniqueComponents.size);
            components.emplace(component, output.uniqueComponents.size);
            output.uniqueComponents.push_back(component);
        }
    }
    output.plans.push_back({selected, remainingSingles, start, (uint16_t)flags, (uint16_t)count});
}

bool ChordPlanBuilder::prepare(Storage &storage, int &constant) {
    Row *rows = storage.rows;
    int occupiedUntil[64];
    std::fill_n(occupiedUntil, 64, -1);
    uint64_t scope[cells + mines];
    bool mine[cells + mines]{};
    int count = 0;
    for (int i = 0; i < cells; ++i) {
        if (!singles.has(i)) continue;
        if (covering[i]) scope[count++] = covering[i];
        else ++constant;
    }
    for (int m = 0; m < mines; ++m) {
        if ((initialFlags & (1U << m)) || !output.users[m]) continue;
        scope[count] = output.users[m];
        mine[count++] = true;
    }
    const auto opening = [&](uint64_t group) {
        if (!group) {
            ++constant;
            return;
        }
        rows[std::countr_zero(group)].openings.push_back(group);
        for (uint64_t bits = group; bits; bits &= bits - 1) rows[std::countr_zero(bits)].contacts |= bits & (bits - 1);
    };
    for (int z = 0; z < nz; ++z) opening(covering[zeroCells[z]]);
    uint64_t anchors = 0;
    for (int i = 0; i < n; ++i) {
        if (rootShown.has(centers[i])) anchors |= output.bit(i);
        for (uint64_t bits = links[i]; bits; bits &= bits - 1) rows[std::countr_zero(bits)].contacts |= output.bit(i);
    }
    if (anchors) {
        --constant; // 已揭开的中心共用一个无需点击的起点。
        opening(anchors);
    }
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < count; ++j) {
            if (std::countr_zero(scope[j]) != i) continue;
            const int last = 63 - std::countl_zero(scope[j]);
            int bit = 0;
            while (bit < 64 && occupiedUntil[bit] >= i) ++bit;
            if (bit == 64) return false;
            occupiedUntil[bit] = last;
            const uint64_t flag = output.bit(bit);
            for (uint64_t bits = scope[j]; bits; bits &= bits - 1) {
                Row &row = rows[std::countr_zero(bits)];
                row.touch |= flag;
                if (mine[j]) row.mines |= flag;
            }
            for (int k = i; k < last; ++k) rows[k].stay |= flag;
            if (!mine[j]) rows[last].ends |= flag;
        }
        const int width = i ? std::bit_width(rows[i - 1].stay) : 0;
        if (width <= 14) {
            vector<uint8_t> &costs = rows[i].costs;
            costs.resize(1U << width);
            std::fill_n(costs.data, costs.size, UINT8_MAX); // 尚未求过该覆盖状态的最小花费。
        }
    }
    if (!tightBudget) return true;
    int parent[64];
    for (int i = 0; i < n; ++i) parent[i] = i;
    uint64_t compulsory = 0;
    // 允许所有余下的中心合并，得到每个已有分量能接到的最宽范围。
    for (int i = n - 1; i >= 0; --i) {
        Row &row = rows[i];
        for (uint64_t bits = row.contacts; bits; bits &= bits - 1) join(parent, i, std::countr_zero(bits));
        for (size_t j = 0; j < row.openings.size; ++j) compulsory |= output.bit(std::countr_zero(row.openings[j]));
        for (int j = i; j < n; ++j) row.joinedByFuture[j] = find(parent, j);
        for (uint64_t bits = compulsory; bits; bits &= bits - 1)
            row.mandatoryComponents |= output.bit(row.joinedByFuture[std::countr_zero(bits)]);
    }
    return true;
}

int ChordPlanBuilder::closureLower(int i, Connection &connection, const Row &row, const vectorPool<uint64_t> &groups) {
    if (i == n) return 0;
    if (connection.closureLower >= 0) return connection.closureLower;
    uint64_t combined[cells + 1], attached = 0;
    int count = 0;
    for (size_t index = 0; index < connection.groups.count; ++index) {
        uint64_t connected = 0;
        for (uint64_t bits = groups.data[connection.groups.offset + index]; bits; bits &= bits - 1)
            connected |= output.bit(row.joinedByFuture[std::countr_zero(bits)]);
        attached |= connected;
        for (int j = 0; j < count;) {
            if (connected & combined[j]) {
                connected |= combined[j];
                combined[j] = combined[--count];
            } else ++j;
        }
        combined[count++] = connected;
    }
    // 即使允许所有剩余中心参与合并，每个仍分离的分量也至少需要一个起点。
    return connection.closureLower = count + std::popcount(row.mandatoryComponents & ~attached);
}

int ChordPlanBuilder::best(int i, uint64_t state, Row *rows) {
    if (i == n) return 0;
    Row &row = rows[i];
    if (row.costs.size) {
        if (row.costs[state] != UINT8_MAX) return row.costs[state];
    } else if (const int *found = row.sparse.find(state)) return *found;
    if (!ready || ++states > 300'000) {
        ready = false;
        return cells;
    }
    const uint64_t hit = state | row.touch;
    const int left = std::popcount(row.ends & ~state) + best(i + 1, state & row.stay, rows);
    const int right = 1 + std::popcount(row.mines & ~state) + std::popcount(row.ends & ~hit) + best(i + 1, hit & row.stay, rows);
    const int cost = std::min(left, right);
    if (ready) {
        if (row.costs.size) row.costs[state] = cost;
        else row.sparse.emplace(state, cost);
    }
    return cost;
}

int ChordPlanBuilder::intern(Row &row, const uint64_t *groups, int count, vectorPool<uint64_t> &pool) {
    uint64_t hash = count;
    for (int i = 0; i < count; ++i) hash ^= groups[i] + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    int *head = row.connections.find(hash);
    for (int id = head ? *head : -1; id >= 0; id = row.conn[id].sameHash) {
        const vectorPool<uint64_t>::vector slice = row.conn[id].groups;
        if (slice.count == (size_t)count && (!count || !std::memcmp(pool.data.data + slice.offset, groups, count * sizeof(uint64_t))))
            return id;
    }
    vectorPool<uint64_t>::vector slice = pool.push_back();
    for (int i = 0; i < count; ++i) pool.push_back(slice, groups[i]);
    const int id = row.conn.size;
    // -1 表示连接尚未展开、下界尚未计算，或没有下一个同哈希条目。
    row.conn.push_back({slice, {-1, -1}, {0, 0}, -1, head ? *head : -1});
    if (head) *head = id;
    else row.connections.emplace(hash, id);
    return id;
}

void ChordPlanBuilder::connect(int i, int id, Row *rows, vectorPool<uint64_t> &pool) {
    const Row &row = rows[i];
    Connection &conn = rows[i].conn[id];
    if (conn.next[0] >= 0) return;
    uint64_t previous[cells + 1];
    int size = 0;
    for (size_t j = 0; j < conn.groups.count; ++j) previous[size++] = pool.data[conn.groups.offset + j];
    for (size_t j = 0; j < row.openings.size; ++j) previous[size++] = row.openings[j];
    const uint64_t bit = output.bit(i);
    for (int take = 0; take < 2; ++take) {
        uint64_t groups[cells + 1], merged = take ? row.contacts : 0;
        int count = 0, closed = 0;
        for (int j = 0; j < size; ++j) {
            const uint64_t rest = previous[j] & ~bit;
            if (take && (previous[j] & bit)) merged |= rest;
            else if (rest) groups[count++] = rest;
            else ++closed;
        }
        if (take) {
            if (merged) groups[count++] = merged;
            else ++closed;
        }
        std::sort(groups, groups + count);
        conn.next[take] = intern(rows[i + 1], groups, count, pool);
        conn.closed[take] = closed;
    }
}

bool ChordPlanBuilder::enumerate(int i, int id, uint64_t state, int limit, uint64_t selected, unsigned used,
                               Mask uncovered, Storage &storage) {
    if (!ready) return false;
    Row *rows = storage.rows;
    const int lower = best(i, state, rows);
    if (lower > limit) return false;
    Row &row = rows[i];
    Connection &conn = row.conn[id];
    if (tightBudget && lower + closureLower(i, conn, row, storage.groups) > limit) return false;
    if (++enumerated > 1'000'000) {
        ready = false;
        return false;
    }
    if (i == n) {
        record(selected, used, uncovered, storage);
        return true;
    }
    const Failure key{state, i, id};
    FlatHashTable<Failure, int, Failure::Hash> &failures = storage.failures;
    if (const int *found = failures.find(key))
        if (*found >= limit) return false;
    connect(i, id, rows, storage.groups);
    bool any = enumerate(i + 1, conn.next[0], state & row.stay, limit - std::popcount(row.ends & ~state) - conn.closed[0],
                         selected, used, uncovered, storage);
    const uint64_t hit = state | row.touch;
    any |= enumerate(i + 1, conn.next[1], hit & row.stay, limit - 1 - std::popcount(row.mines & ~state) -
                     std::popcount(row.ends & ~hit) - conn.closed[1], selected | output.bit(i), used | needs[i],
                     output.without(uncovered, covers[i]), storage);
    if (!any) failures[key] = limit;
    return any;
}

bool ChordPlanBuilder::build(const Model &model, const Neighbors &adjacent, Mask flags, Storage &storage) {
    for (int cell = 0, m = 0; cell < cells; ++cell) {
        output.opening[cell] = cell;
        if (model.mines[cell]) {
            output.minesAt[m] = cell;
            if (flags.has(cell)) initialFlags |= 1U << m;
            ++m;
        } else if (flags.has(cell)) {
            output.wrongAtRoot.set(cell);
            --budget;
        }
    }
    int cellToZero[cells];
    std::fill_n(cellToZero, cells, -1);
    for (int i = 0; i < model.zeros.size(); ++i) {
        const span<const int> opening = model.zeros[i];
        const int rep = opening[0];
        for (int cell : opening) {
            output.opening[cell] = rep;
            // 有错旗挡住零区时，后续可能出现部分展开。该摆法沿用可重建零区的 DOM。
            if (output.wrongAtRoot.has(cell)) return false;
            for (int next : adjacent[cell])
                if (output.wrongAtRoot.has(next) || (rootShown.has(cell) && !rootShown.has(next))) return false;
            if (!rootShown.has(rep)) cellToZero[cell] = nz;
        }
        if (!rootShown.has(rep)) {
            zeroCells[nz++] = rep;
            units.set(rep);
        }
    }
    for (int i = model.zeros.size(); i < model.base_scopes.size(); ++i) {
        const int cell = model.base_descriptions[i].representative;
        if (rootShown.has(cell)) continue;
        singles.set(cell);
        units.set(cell);
    }
    for (int cell = 0; cell < cells; ++cell) {
        if (model.numbers[cell] <= 0) continue;
        Mask hidden{};
        for (int near : adjacent[cell])
            if (!model.mines[near] && !rootShown.has(near)) hidden.set(near);
        if (output.count(hidden) < 2) continue;
        if (n == 64) return false;
        const uint64_t bit = output.bit(n);
        centers[n] = cell;
        output.centerNumber[cell] = n + 1;
        output.blocks[cell] |= bit;
        output.blockCells.set(cell);
        for (int near : adjacent[cell]) {
            if (!hidden.has(near)) continue;
            output.blocks[near] |= bit;
            output.blockCells.set(near);
        }
        ++n;
    }
    if (n + nz > cells || nz > 63) return false;
    for (int i = 0; i < n; ++i) {
        const int cell = centers[i];
        if (singles.has(cell)) covers[i].set(cell);
        for (int near : adjacent[cell]) {
            if (singles.has(near)) covers[i].set(near);
            if (cellToZero[near] >= 0) {
                zeros[i] |= output.bit(cellToZero[near]);
                covers[i].set(zeroCells[cellToZero[near]]);
            }
            if (model.mines[near]) {
                for (int m = 0; m < mines; ++m) {
                    if (output.minesAt[m] != near) continue;
                    needs[i] |= 1U << m;
                    output.users[m] |= output.bit(i);
                }
            }
            const int center = output.centerNumber[near] - 1;
            if (center >= 0 && center < i) links[i] |= output.bit(center);
        }
        for (int j = 0; j < cells; ++j)
            if (covers[i].has(j)) covering[j] |= output.bit(i);
    }
    storage.reset(n);
    int constant = 0;
    if (!prepare(storage, constant)) return false;
    intern(storage.rows[0], nullptr, 0, storage.groups);
    enumerate(0, 0, 0, budget - constant, 0, 0, units, storage);
    output.words = (output.plans.size + 63) / 64;
    return ready;
}

struct Search {
    struct DistanceKey {
        Mask shown;
        Mask flags;

        bool operator==(const DistanceKey &other) const = default;

        struct Hash {
            size_t operator()(const DistanceKey &key) const {
                U128Hasher hasher;
                hasher.mix(key.shown.low, key.shown.high, key.flags.low, key.flags.high);
                return U128Hash()(hasher.finalize());
            }
        };
    };

    using DistanceId = uint32_t;

    struct PlanView {
        struct Range {
            size_t offset;
            size_t summary;
        };
        vector<Range> ranges;
        vector<uint64_t> bits;
        vector<ChordPlans::NextMoves> summaries;
        const int *optimum;
        PlanView *previous;
        Mask shown;
        Mask flags;
        int paid;

        void reset(const Observation &state, const int *clicks, size_t count, PlanView *parent) {
            optimum = clicks;
            previous = parent;
            shown = state.revealed();
            flags = state.flags;
            paid = state.clicks;
            bits.resize(0);
            summaries.resize(0);
            ranges.resize(count);
            // summary 为 0 表示该摆法尚未扫描，其他值为当前 summaries 的下标加 1。
            std::fill_n(ranges.data, ranges.size, Range{});
        }

        const uint64_t *get(size_t id, const ChordPlans &plans, int budget) {
            Range &range = ranges[id];
            if (!range.summary) {
                const uint64_t *prior = previous ? previous->get(id, plans, budget) : nullptr;
                range = {bits.size, summaries.size + 1};
                bits.resize(bits.size + plans.words);
                std::fill_n(bits.data + range.offset, plans.words, uint64_t{0});
                summaries.push_back({});
                // 后继局面的任何达标组合在已执行的这条路径上也有足够预算。
                plans.distance(shown, flags, 0, budget - paid, prior, bits.data + range.offset,
                               &summaries[range.summary - 1], optimum[id] - paid);
            }
            return bits.data + range.offset;
        }
    };

    struct Layout {
        Model model;
        FlatHashTable<DistanceId, int> distance;
        ChordPlans *plans;
        bool plansBuilt;
        int rootDistance;
        int threeBv;
        uint8_t neighborMines[cells];
        int winningClicks;

        Layout(const Model &source, int rootDistance)
            : model(source), plans(nullptr), plansBuilt(false), rootDistance(rootDistance), threeBv(source.three_bv()), winningClicks(0) {}
        ~Layout() { delete plans; }

        void discardDomGraph() {
            // 完整计划表负责后续距离，揭示和猜旗仍读取 model 中的格子数据。
            const auto release = []<typename T>(vector<T> &items) { vector<T>().swap(items); };
            const auto releaseScopes = [&](ref_dom::Scopes &scopes) {
                release(scopes.parts);
                release(scopes.values.data);
            };
            release(model.candidates);
            release(model.mine_cells);
            release(model.base_descriptions);
            releaseScopes(model.graph);
            releaseScopes(model.zero_scopes);
            releaseScopes(model.mine_scopes);
            releaseScopes(model.base_scopes);
            releaseScopes(model.zeros);
        }
    };

    struct QualifiedLayout {
        size_t id;
        int clicks;
        size_t previous; // 父调用存放的达标摆法，0 表示当前分支的列表结束。
    };

    struct Outcome {
        Observation state;
        Reward upper;
        bool settled;
        DistanceId distanceId; // 0 表示尚未登记当前可见状态。
        size_t winTrace;
        size_t order;
        size_t lastQualified;
    };

    struct Trial {
        size_t layout;
        size_t outcome;
        long double remainingPoints;
        size_t remainingAbove;
        uint8_t bv;
        uint8_t clicks;
    };

    struct Choice {
        Move move;
        vectorPool<Outcome>::vector outcomes;
        vectorPool<Trial>::vector trials;
        Reward upper;
        Reward checked;
        long double largest;
        size_t next;
        int planMove;
    };

    struct Value {
        Reward best;
        long double upper;
        bool exact;
        size_t winTrace;
    };

    const Neighbors &adjacent;
    ProgressReporter &reporter;
    const size_t domLimit;
    size_t domCalls;
    bool domLimited;
    Move rootMove;

    Search(const Neighbors &adjacent, ProgressReporter &reporter, size_t domLimit)
        : adjacent(adjacent), reporter(reporter), domLimit(domLimit), domCalls(0), domLimited(false),
          rootMove{0, Action::None, 0}, rootShown{}, rootFlags{}, rootClicks(0), plansEnabled(false), tolerance(0) {}

    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;

    ~Search() {
        for (size_t i = 0; i < layouts.size; ++i) delete layouts[i];
    }

    IoeBruteForce::RootResult filterRoot(const ObservedBoard::Result &board, const Observation &root, size_t candidateLimit);
    void prepareLayouts(const Observation &root);
    Value solve(const Observation &state, bool root, long double need, const QualifiedLayout *qualified, size_t lastQualified, PlanView *ancestor);
    void summarizeWins(const Observation &root, size_t trace, IoeBruteForce::Result &result);

private:
    struct Workspace;
    Workspace &getWorkspace();

    // 数据的有效期覆盖这一次根部筛选和后续搜索。
    vector<Layout *> layouts;
    FlatHashTable<DistanceKey, DistanceId, DistanceKey::Hash> distanceIds;
    vector<DistanceKey> distanceKeys;
    Mask rootShown;
    Mask rootFlags;
    int rootClicks;
    bool plansEnabled;
    Ways seen[cells][10]; // 下标 9 用于雷格。
    Ways all;
    FlatHashTable<Observation, Value, ObservationHash> values;
    vector<size_t> winBranches;
    long double tolerance;

    enum : size_t { clickBuckets = maxWinningClicks + 1, histogramSize = (cells - mines + 1) * clickBuckets };

    // 0 表示无胜局。偶数编码摆法编号和实际点击数，奇数索引以 0 结尾的子分支。
    size_t winLeaf(size_t id, int clicks) const { return 2 * (id * clickBuckets + clicks); }
    size_t recordWins(const Outcome *outcomes, size_t processed);
    void collectWins(size_t trace);
    enum : int { anyMove = 2 * cells };
    DistanceId internDistance(const DistanceKey &key);
    int afterMove(const Layout &layout, size_t id, int move, PlanView &parent);
    int distance(size_t id, DistanceId key, int paid, int lower, int move, PlanView &parent);
    bool below(long double upper, long double need) const { return upper + tolerance < need; }
    struct Small {
        // 危险：只查有效的 count 个编号。digitGroups 写满数字和雷的掩码后才置 prepared。
        size_t ids[64];
        size_t count;
        bool prepared[cells];
        uint64_t digits[cells][10];

        Small() : count(0), prepared{} {}
        uint64_t all() const { return count == 64 ? UINT64_MAX : (uint64_t{1} << count) - 1; }
    };
    const uint64_t *digitGroups(Small &small, int cell);
    void actions(const Observation &state, const Ways &viable, Small &small, vector<Move> &result);
    template <typename Each> void outcome(Observation &state, const vector<int> &optimum, Choice &choice,
        vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials, Each visit);
    void revealSmall(Observation &state, Mask pending, Small &small, uint64_t support,
                     const vector<int> &optimum, Choice &choice, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials);
    void reveal(Observation &state, Mask pending, const Ways &support, const vector<int> &optimum,
                Choice &choice, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials, int depth);
    Choice observe(const Observation &state, Move move, PlanView &view, const Ways &viable, Small &small, const vector<int> &optimum,
                   vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials);
    void refine(Choice &choice, vectorPool<Outcome> &outcomes, const vectorPool<Trial> &trials, vector<QualifiedLayout> &qualified, PlanView &view);

    template <typename Func> void each(const Ways &ways, Func fn) {
        for (size_t i = 0; i < ways.size; ++i)
            for (uint64_t word = ways[i]; word; word &= word - 1) fn(i * 64 + std::countr_zero(word));
    }

    bool intersect(const Ways &left, const Ways &right, Ways &result) {
        result.resize(left.size);
        uint64_t any = 0;
        for (size_t i = 0; i < left.size; ++i) {
            result[i] = left[i] & right[i];
            any |= result[i];
        }
        return any != 0;
    }
};

struct Search::Workspace {
    struct Frame {
        PlanView planView;
        vector<int> optimum;
        Ways viable;
        vector<Move> actions;
        vector<Choice> choices;
        vectorPool<Outcome> outcomes;
        vectorPool<Trial> trials;
        vector<QualifiedLayout> qualified;
        vector<size_t> pending;
        vector<long double> remaining;
    };

    vector<Mask> enumerated;
    Model candidate;

    Frame frames[maxWinningClicks + 1];
    Ways revealSupport[cells + 1];
    ChordPlanBuilder::Storage planStorage;

    Combination::Pool combinations;
    Position::Pool positions;

    Workspace() : positions(combinations) {}
};

__declspec(noinline) Search::Workspace &Search::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

IoeBruteForce::RootResult Search::filterRoot(const ObservedBoard::Result &board, const Observation &root, size_t candidateLimit) {
    Workspace &workspace = getWorkspace();
    Position::Pool &positions = workspace.positions;
    positions.clear();
    Position position;
    Position::CheckedResult checked = position.analyzeChecked(board, positions, positionConfig, positions.probabilities);
    if (checked.error != Position::CheckError::None) return {Status::InvalidBoard, 0, 0, 0};
    const long double candidates = position.countWays(checked.result, positions);
    if (candidates > candidateLimit) return {Status::CandidateLimit, candidates, 0, 0};

    IoeBruteForce::RootResult result{Status::Exact, candidates, 0, 0};
    if (root.clicks > maxWinningClicks) return result;
    reporter.progress.candidates = (size_t)candidates;
    reporter.report(true);
    vector<Mask> &enumerated = workspace.enumerated;
    enumerated.resize((size_t)candidates);
    enumerated.resize(0);
    Enumerator().enumerate(root, adjacent, enumerated, reporter);
    assert_(enumerated.size == (size_t)candidates, "IOE 枚举与局面的摆法计数不符");

    const Mask shown = root.revealed();
    reporter.progress.phase = Phase::Root;
    reporter.progress.enumerated = enumerated.size;
    reporter.report(true);
    Model &model = workspace.candidate;
    RefDom dom;
    RootFilter filter;
    const auto analyzeLayout = [&](Mask mines) {
        dom.buildModel(mines, adjacent, model);
        const int budget = model.three_bv() / 2;
        const int wrongFlags = std::popcount(root.flags.low & ~mines.low) + std::popcount(root.flags.high & ~mines.high);
        if (root.clicks + wrongFlags > budget) return;
        const int remainingBudget = budget - root.clicks - wrongFlags;
        if (!dom.partialOpening(model, shown, adjacent) &&
            (filter.exceedsClicks(model, shown, root.flags, remainingBudget) ||
             filter.exceedsFlags(model, shown, root.flags, remainingBudget))) return;
        ++result.domCalls;
        const int distance = dom.remainingClicks(model, shown, root.flags, adjacent);
        if (root.clicks + distance > budget) return;
        layouts.push_back(new Layout(model, distance));
        ++result.eligible;
    };
    for (size_t i = 0; i < enumerated.size; ++i) {
        analyzeLayout(enumerated[i]);
        if ((++reporter.progress.rootAnalyzed & 63) == 0) {
            reporter.progress.rootEligible = result.eligible;
            reporter.progress.rootDomCalls = result.domCalls;
            reporter.report(false);
        }
    }
    reporter.progress.rootEligible = result.eligible;
    reporter.progress.rootDomCalls = result.domCalls;
    return result;
}

void Search::prepareLayouts(const Observation &root) {
    rootShown = root.revealed();
    rootFlags = root.flags;
    rootClicks = root.clicks;
    plansEnabled = true;
    for (Cell value : root.board)
        if (value != Cell::Hidden && !ObservedBoard().isNumber(value)) plansEnabled = false;
    all.resize((layouts.size + 63) / 64);
    std::fill_n(all.data, all.size, 0);
    for (int cell = 0; cell < cells; ++cell)
        for (int digit = 0; digit < 10; ++digit) {
            Ways &words = seen[cell][digit];
            words.resize(all.size);
            std::fill_n(words.data, words.size, 0);
        }
    for (size_t id = 0; id < layouts.size; ++id) {
        const uint64_t bit = uint64_t{1} << (id % 64);
        all[id / 64] |= bit;
        const Model &model = layouts[id]->model;
        for (int cell = 0; cell < cells; ++cell) {
            const int digit = model.mines[cell] ? 9 : model.numbers[cell];
            seen[cell][digit][id / 64] |= bit;
            const span<const int> neighbors = adjacent[cell];
            unsigned pattern = 0;
            for (int i = 0; i < neighbors.size; ++i)
                if (model.mines[neighbors[i]]) pattern |= 1U << i;
            layouts[id]->neighborMines[cell] = pattern;
        }
    }
}

size_t Search::recordWins(const Outcome *outcomes, size_t processed) {
    size_t first = 0;
    const size_t offset = winBranches.size;
    for (size_t i = 0; i < processed; ++i) {
        const size_t trace = outcomes[i].winTrace;
        if (!trace) continue;
        if (!first) {
            first = trace;
            continue;
        }
        if (winBranches.size == offset) winBranches.push_back(first);
        winBranches.push_back(trace);
    }
    if (winBranches.size == offset) return first;
    winBranches.push_back(0);
    return 2 * offset + 1;
}

void Search::collectWins(size_t trace) {
    if (!trace) return;
    const size_t index = trace / 2;
    if (!(trace & 1)) {
        layouts[index / clickBuckets]->winningClicks = index % clickBuckets;
        return;
    }
    for (size_t i = index; winBranches[i]; ++i) collectWins(winBranches[i]);
}

void Search::summarizeWins(const Observation &root, size_t trace, IoeBruteForce::Result &result) {
    collectWins(trace);
    size_t histogram[histogramSize][2]{};
    size_t wins = 0;
    for (size_t id = 0; id < layouts.size; ++id) {
        const Layout *layout = layouts[id];
        const bool won = layout->winningClicks != 0;
        const int clicks = won ? layout->winningClicks : root.clicks + layout->rootDistance;
        const int threeBv = layout->threeBv;
        const int divisor = std::gcd(threeBv, clicks);
        ++histogram[(threeBv / divisor) * clickBuckets + clicks / divisor][!won];
        wins += won;
    }
    assert_(wins == result.wins, "IOE 分布的摆法数与胜出摆法数不符");
    for (size_t i = 1; i < histogramSize; ++i)
        if (histogram[i][0] || histogram[i][1])
            result.ioeDistribution.push_back({(int)(i / clickBuckets), (int)(i % clickBuckets), histogram[i][0], histogram[i][1]});
}

Search::DistanceId Search::internDistance(const DistanceKey &key) {
    if (const DistanceId *found = distanceIds.find(key)) return *found;
    assert_(distanceKeys.size < UINT32_MAX, "距离缓存编号超过 32 位");
    distanceKeys.push_back(key);
    const DistanceId id = distanceKeys.size;
    distanceIds.emplace(key, id);
    return id;
}

int Search::afterMove(const Layout &layout, size_t id, int move, PlanView &parent) {
    const ChordPlans &plans = *layout.plans;
    const int budget = layout.threeBv / 2;
    parent.get(id, plans, budget);
    const ChordPlans::NextMoves &next = parent.summaries[parent.ranges[id].summary - 1];
    if (move < cells) return parent.optimum[id] + !next.bestOpens.has(plans.opening[move]);
    const int center = plans.centerNumber[move - cells];
    if (center) {
        const uint64_t bit = uint64_t{1} << (center - 1);
        for (int cost = parent.optimum[id] - parent.paid; cost <= budget - parent.paid; ++cost)
            if (next.chords[cost] & bit) return cost + parent.paid;
    }
    return budget + 1;
}

int Search::distance(size_t id, DistanceId keyId, int paid, int lower, int move, PlanView &parent) {
    Layout &layout = *layouts[id];
    const int budget = layout.threeBv / 2;
    const int cap = budget - paid;
    int *old = layout.distance.find(keyId);
    // 负数记录已证明失败的预算，放宽预算后仍可在同一缓存项补求精确值。
    if (old && (*old >= 0 || -*old - 1 >= cap)) return *old >= 0 ? *old : cap + 1;
    if (!old && domCalls >= domLimit) {
        domLimited = true;
        return cells;
    }
    if (!old) ++domCalls;
    const DistanceKey &key = distanceKeys[keyId - 1];
    if (plansEnabled && !layout.plansBuilt) {
        layout.plansBuilt = true;
        const int allowance = budget - rootClicks;
        ChordPlans *plans = new ChordPlans(rootClicks + layout.rootDistance);
        ChordPlanBuilder builder(*plans, rootShown, allowance, layout.rootDistance == allowance);
        ChordPlanBuilder::Storage &storage = getWorkspace().planStorage;
        if (builder.build(layout.model, adjacent, rootFlags, storage)) {
            layout.plans = plans;
            layout.discardDomGraph();
        } else delete plans;
    }
    int remaining;
    if (!layout.plans) remaining = RefDom().remainingClicks(layout.model, key.shown, key.flags, adjacent);
    else if (move != anyMove) remaining = afterMove(layout, id, move, parent) - paid;
    else {
        const ChordPlans &plans = *layout.plans;
        remaining = plans.distance(key.shown, key.flags, std::max(lower, plans.rootTotal - paid), cap,
                                   parent.get(id, plans, budget), nullptr, nullptr, 0);
    }
    const int entry = layout.plans && remaining > cap ? -cap - 1 : remaining;
    if (old) *old = entry;
    else layout.distance.emplace(keyId, entry);
    if ((domCalls & 63) == 0) {
        reporter.progress.searchDomCalls = domCalls;
        reporter.report(false);
    }
    return remaining;
}

const uint64_t *Search::digitGroups(Small &small, int cell) {
    uint64_t *groups = small.digits[cell];
    if (!small.prepared[cell]) {
        std::fill_n(groups, 10, uint64_t{0});
        for (size_t i = 0; i < small.count; ++i) {
            const int value = layouts[small.ids[i]]->model.numbers[cell];
            groups[value < 0 ? 9 : value] |= uint64_t{1} << i;
        }
        small.prepared[cell] = true;
    }
    return groups;
}

void Search::actions(const Observation &state, const Ways &viable, Small &small, vector<Move> &result) {
    result.resize(0);
    for (int cell = 0; cell < cells; ++cell) {
        if (state.covered(cell)) {
            if (!state.flags.has(cell)) result.push_back({cell, Action::Open, 0});
            else {
                // 确认始终是真旗的格子无需拔旗。
                bool mayBeSafe = false;
                if (small.count) mayBeSafe = (small.all() & ~digitGroups(small, cell)[9]) != 0;
                else for (size_t word = 0; word < viable.size; ++word) mayBeSafe |= (viable[word] & ~seen[cell][9][word]) != 0;
                if (mayBeSafe) result.push_back({cell, Action::Unflag, 0});
            }
            continue;
        }
        const Cell value = state.board[cell];
        if (!ObservedBoard().isNumber(value) || value == Cell::Num0) continue;
        const span<const int> neighbors = adjacent[cell];
        unsigned hidden = 0;
        for (int i = 0; i < neighbors.size; ++i)
            if (!ObservedBoard().isNumber(state.board[neighbors[i]])) hidden |= 1U << i;
        if (std::popcount(hidden) <= ObservedBoard().numberValue(value)) continue;
        bool choices[256]{};
        const auto mark = [&](size_t id) { choices[layouts[id]->neighborMines[cell] & hidden] = true; };
        if (small.count) for (size_t i = 0; i < small.count; ++i) mark(small.ids[i]);
        else each(viable, mark);
        for (unsigned flags = 0; flags < 256; ++flags)
            if (choices[flags]) result.push_back({cell, Action::Chord, flags});
    }
}

template <typename Each>
void Search::outcome(Observation &state, const vector<int> &optimum, Choice &choice, vectorPool<Outcome> &outcomes,
                     vectorPool<Trial> &trials, Each visit) {
    const Value *cached = values.find(state);
    const size_t index = choice.outcomes.count;
    if (cached && cached->exact) {
        const Reward exact = cached->best;
        outcomes.push_back(choice.outcomes, {state, exact, true, 0, cached->winTrace, index, 0});
        choice.checked.add(exact);
        choice.largest = std::max(choice.largest, exact.points);
        return;
    }
    const bool terminal = state.finished();
    outcomes.push_back(choice.outcomes, {state, {}, terminal, 0, 0, index, 0});
    visit([&](size_t id) {
        // 走一手后的透视最优点击数至少是原局面的最优点击数。
        const int minimum = std::max(optimum[id], state.clicks + !terminal);
        const int threeBv = layouts[id]->threeBv;
        if (minimum <= threeBv / 2) trials.push_back(choice.trials, {id, index, 0, 0, (uint8_t)threeBv, (uint8_t)minimum});
    });
}

void Search::revealSmall(Observation &state, Mask pending, Small &small, uint64_t support, const vector<int> &optimum,
                         Choice &choice, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials) {
    if (!support) return;
    const auto pop = [](Mask &mask) {
        if (mask.low) {
            const int cell = std::countr_zero(mask.low);
            mask.low &= mask.low - 1;
            return cell;
        }
        const int cell = 64 + std::countr_zero(mask.high);
        mask.high &= mask.high - 1;
        return cell;
    };
    if (!(support & (support - 1))) {
        const size_t id = small.ids[std::countr_zero(support)];
        const Model &model = layouts[id]->model;
        Observation only = state;
        while (pending.low || pending.high) {
            const int cell = pop(pending);
            if (!only.covered(cell) || only.flags.has(cell)) continue;
            const int value = model.numbers[cell];
            if (value < 0) return;
            only.board[cell] = (Cell)value;
            if (!value) for (int neighbor : adjacent[cell]) pending.set(neighbor);
        }
        outcome(only, optimum, choice, outcomes, trials, [&]<typename Visit>(const Visit &visit) { visit(id); });
        return;
    }
    while (pending.low || pending.high) {
        const int cell = pop(pending);
        if (!state.covered(cell) || state.flags.has(cell)) continue;
        const uint64_t *groups = digitGroups(small, cell);
        const Cell previous = state.board[cell];
        for (int value = 0; value < 9; ++value) {
            const uint64_t branch = groups[value] & support;
            if (!branch) continue;
            Mask next = pending;
            if (!value) for (int neighbor : adjacent[cell]) next.set(neighbor);
            state.board[cell] = (Cell)value;
            revealSmall(state, next, small, branch, optimum, choice, outcomes, trials);
        }
        state.board[cell] = previous;
        return;
    }
    outcome(state, optimum, choice, outcomes, trials, [&]<typename Visit>(const Visit &visit) {
        for (uint64_t bits = support; bits; bits &= bits - 1) visit(small.ids[std::countr_zero(bits)]);
    });
}

void Search::reveal(Observation &state, Mask pending, const Ways &support, const vector<int> &optimum,
                    Choice &choice, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials, int depth) {
    int cell = 0;
    while (cell < cells && (!pending.has(cell) || !state.covered(cell) || state.flags.has(cell))) ++cell;
    if (cell == cells) {
        outcome(state, optimum, choice, outcomes, trials, [&]<typename Visit>(const Visit &visit) { each(support, visit); });
        return;
    }
    pending.clear(cell);
    Ways &branch = getWorkspace().revealSupport[depth + 1];
    for (int digit = 0; digit < 9; ++digit) {
        const Ways &matches = seen[cell][digit];
        size_t count = 0;
        branch.resize(support.size);
        for (size_t i = 0; i < support.size; ++i) {
            branch[i] = support[i] & matches[i];
            count += std::popcount(branch[i]);
        }
        if (!count) continue;
        Mask next = pending;
        if (digit == 0)
            for (int neighbor : adjacent[cell]) next.set(neighbor);
        const Cell previous = state.board[cell];
        state.board[cell] = (Cell)digit;
        if (count <= 64) {
            Small sub;
            each(branch, [&](size_t id) { sub.ids[sub.count++] = id; });
            revealSmall(state, next, sub, sub.all(), optimum, choice, outcomes, trials);
        } else reveal(state, next, branch, optimum, choice, outcomes, trials, depth + 1);
        state.board[cell] = previous;
    }
}

Search::Choice Search::observe(const Observation &state, Move move, PlanView &view, const Ways &viable, Small &small,
                               const vector<int> &optimum, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials) {
    Observation next = state;
    ++next.clicks;
    Mask pending{};
    Ways &support = getWorkspace().revealSupport[0];
    uint64_t local = small.all();
    if (!small.count) support = viable;
    if (move.action == Action::Unflag) {
        next.flags.clear(move.cell);
    } else if (move.action == Action::Open) {
        pending.set(move.cell);
    } else {
        const span<const int> neighbors = adjacent[move.cell];
        for (int i = 0; i < neighbors.size; ++i) {
            const int cell = neighbors[i];
            if (ObservedBoard().isNumber(state.board[cell])) continue;
            const bool marked = (move.chordFlags & (1U << i)) != 0;
            if (marked != state.flags.has(cell)) ++next.clicks;
            if (marked) {
                next.flags.set(cell);
                if (small.count) local &= digitGroups(small, cell)[9];
                else intersect(support, seen[cell][9], support);
            } else {
                next.flags.clear(cell);
                if (state.covered(cell)) pending.set(cell);
            }
        }
    }
    Choice result{move, outcomes.push_back(), trials.push_back(), {}, {}, 0, 0, anyMove};
    if (move.action == Action::Open) result.planMove = move.cell;
    if (move.action == Action::Chord && std::popcount(pending.low) + std::popcount(pending.high) >= 2) result.planMove = cells + move.cell;
    if (result.planMove != anyMove) {
        const auto payable = [&](size_t id) {
            const Layout &layout = *layouts[id];
            const int budget = layout.threeBv / 2;
            if (next.clicks > budget) return false;
            return !layout.plans || afterMove(layout, id, result.planMove, view) <= budget;
        };
        // 只有所有安全摆法都已证明超预算时，才省去这一步的揭示和分组。
        bool possible = false;
        if (small.count) {
            uint64_t candidates = local;
            if (move.action == Action::Open) candidates &= ~digitGroups(small, move.cell)[9];
            for (; candidates && !possible; candidates &= candidates - 1) possible = payable(small.ids[std::countr_zero(candidates)]);
        } else {
            for (size_t word = 0; word < support.size && !possible; ++word) {
                uint64_t candidates = support[word];
                if (move.action == Action::Open) candidates &= ~seen[move.cell][9][word];
                for (; candidates && !possible; candidates &= candidates - 1) possible = payable(word * 64 + std::countr_zero(candidates));
            }
        }
        if (!possible) return result;
    }
    if (small.count) revealSmall(next, pending, small, local, optimum, result, outcomes, trials);
    else reveal(next, pending, support, optimum, result, outcomes, trials, 0);
    if (result.trials.count) {
        Trial *first = trials.data.data + result.trials.offset;
        std::sort(first, first + result.trials.count, [](const Trial &a, const Trial &b) {
            const Reward reward;
            const long double left = reward.pointsFor(a.bv, a.clicks), right = reward.pointsFor(b.bv, b.clicks);
            if (left != right) return left > right;
            if (a.outcome != b.outcome) return a.outcome < b.outcome;
            return a.layout < b.layout;
        });
        for (size_t i = result.trials.count; i > 0; --i) {
            Trial &trial = first[i - 1];
            trial.remainingPoints = result.upper.points;
            trial.remainingAbove = result.upper.above200;
            result.upper.add(Reward(trial.bv, trial.clicks));
        }
    }
    result.upper.add(result.checked);
    return result;
}

void Search::refine(Choice &choice, vectorPool<Outcome> &outcomes, const vectorPool<Trial> &trials,
                    vector<QualifiedLayout> &qualified, PlanView &view) {
    const Trial &trial = trials.data[choice.trials.offset + choice.next++];
    Outcome &outcome = outcomes.data[choice.outcomes.offset + trial.outcome];
    const int threeBv = trial.bv;
    // 已缓存精确结果的分支不生成试算项。此处已确定的分支只有通关局面。
    int clicks = outcome.state.clicks;
    if (!outcome.settled) {
        if (!outcome.distanceId) outcome.distanceId = internDistance({outcome.state.revealed(), outcome.state.flags});
        clicks += distance(trial.layout, outcome.distanceId, clicks, trial.clicks - clicks, choice.planMove, view);
    }
    if (domLimited) return;
    if (clicks <= threeBv / 2) {
        const Reward actual(threeBv, clicks);
        choice.checked.add(actual);
        if (!outcome.upper.wins) outcome.winTrace = winLeaf(trial.layout, clicks);
        outcome.upper.add(actual);
        if (!outcome.settled) {
            qualified.push_back({trial.layout, clicks, outcome.lastQualified});
            outcome.lastQualified = qualified.size;
        }
        choice.largest = std::max(choice.largest, outcome.upper.points);
    }
    choice.upper = choice.checked;
    choice.upper.points += trial.remainingPoints;
    choice.upper.wins += choice.trials.count - choice.next;
    choice.upper.above200 += trial.remainingAbove;
}

Search::Value Search::solve(const Observation &state, bool root, long double need, const QualifiedLayout *qualified, size_t lastQualified, PlanView *ancestor) {
    Workspace &workspace = getWorkspace();
    Reward best;
    size_t bestTrace = 0;
    long double previousUpper = std::numeric_limits<long double>::infinity();
    if (const Value *found = values.find(state)) {
        if (found->exact || below(found->upper, need)) return *found;
        best = found->best;
        bestTrace = found->winTrace;
        previousUpper = found->upper;
    }
    const auto save = [&](const Value &value) {
        values[state] = value;
        return value;
    };

    // 子调用至少多一次点击，且只有上界仍支持胜出的招法才会进入子调用。
    Workspace::Frame &frame = workspace.frames[state.clicks];
    vector<int> &optimum = frame.optimum;
    optimum.resize(layouts.size);
    Ways &viable = frame.viable;
    Reward ceiling;
    if (root) {
        viable = all;
        each(viable, [&](size_t id) {
            const Layout &layout = *layouts[id];
            optimum[id] = state.clicks + layout.rootDistance;
            ceiling.add(Reward(layout.threeBv, optimum[id]));
        });
        tolerance = 64 * std::numeric_limits<long double>::epsilon() * layouts.size * ceiling.points;
    } else {
        viable.resize(all.size);
        std::fill_n(viable.data, viable.size, 0);
        for (size_t at = lastQualified; at; at = qualified[at - 1].previous) {
            const QualifiedLayout &layout = qualified[at - 1];
            viable[layout.id / 64] |= uint64_t{1} << (layout.id % 64);
            optimum[layout.id] = layout.clicks;
        }
        // 保持按摆法编号累加，浮点分数与从头筛选一致。
        each(viable, [&](size_t id) { ceiling.add(Reward(layouts[id]->threeBv, optimum[id])); });
    }
    if ((!root && ceiling.wins <= 1) || !ceiling.wins || state.finished()) {
        size_t trace = 0;
        each(viable, [&](size_t id) { trace = winLeaf(id, optimum[id]); });
        return save({ceiling, ceiling.points, true, trace});
    }
    const long double stateUpper = std::min(ceiling.points, previousUpper);
    if (below(stateUpper, need)) return save({best, stateUpper, false, bestTrace});

    vector<Choice> &choices = frame.choices;
    vector<Move> &moves = frame.actions;
    vectorPool<Outcome> &outcomes = frame.outcomes;
    vectorPool<Trial> &trials = frame.trials;
    vector<QualifiedLayout> &childQualified = frame.qualified;
    childQualified.resize(0);
    choices.resize(0);
    outcomes.data.resize(0);
    trials.data.resize(0);
    PlanView &view = frame.planView;
    view.reset(state, optimum.data, layouts.size, ancestor);
    Small small;
    if (ceiling.wins <= 64) each(viable, [&](size_t id) { small.ids[small.count++] = id; });
    actions(state, viable, small, moves);
    for (size_t i = 0; i < moves.size; ++i) {
        const Choice choice = observe(state, moves[i], view, viable, small, optimum, outcomes, trials);
        if (choice.upper.wins) choices.push_back(choice);
        else {
            outcomes.pop_back(choice.outcomes);
            trials.pop_back(choice.trials);
        }
    }

    const auto less = [&](size_t left, size_t right) {
        const Choice &a = choices[left];
        const Choice &b = choices[right];
        if (a.upper.points != b.upper.points) return a.upper.points < b.upper.points;
        const bool aReady = a.next == a.trials.count;
        const bool bReady = b.next == b.trials.count;
        if (aReady != bReady) return !aReady;
        if (a.largest != b.largest) return a.largest > b.largest;
        return left > right;
    };
    vector<size_t> &pending = frame.pending;
    pending.resize(choices.size);
    if (pending.size) {
        std::iota(pending.data, pending.data + pending.size, 0);
        std::make_heap(pending.data, pending.data + pending.size, less);
    }
    long double unresolved = 0;
    while (pending.size) {
        std::pop_heap(pending.data, pending.data + pending.size, less);
        const size_t index = pending[--pending.size];
        Choice &choice = choices[index];
        const bool beatsBest = choice.upper.canImprove(best);
        if (!beatsBest || below(choice.upper.points, need)) {
            if (beatsBest) unresolved = std::max(unresolved, choice.upper.points);
            continue;
        }
        if (choice.next < choice.trials.count) {
            refine(choice, outcomes, trials, childQualified, view);
            if (domLimited) return {best, stateUpper, false, bestTrace};
            pending.push_back(index);
            std::push_heap(pending.data, pending.data + pending.size, less);
            continue;
        }

        Outcome *first = outcomes.data.data + choice.outcomes.offset;
        const size_t count = choice.outcomes.count;
        std::sort(first, first + count, [](const Outcome &a, const Outcome &b) {
            return a.upper.points != b.upper.points ? a.upper.points > b.upper.points : a.order < b.order;
        });
        vector<long double> &remaining = frame.remaining;
        remaining.resize(count + 1);
        remaining[count] = 0;
        for (size_t i = count; i > 0; --i) {
            const Outcome &outcome = first[i - 1];
            long double upper = outcome.upper.points;
            if (const Value *cached = values.find(outcome.state)) upper = std::min(upper, cached->upper);
            remaining[i - 1] = remaining[i] + upper;
        }
        Reward score;
        long double upper = remaining[0];
        bool complete = true;
        const long double target = std::max(need, best.points);
        size_t i = 0;
        for (; i < count && remaining[i] > 0; ++i) {
            upper = score.points + remaining[i];
            if (below(upper, target)) {
                complete = false;
                break;
            }
            Outcome &outcome = first[i];
            if (outcome.settled || outcome.upper.wins <= 1) {
                score.add(outcome.upper);
                continue;
            }
            // 假定其余揭示分支都取到上界，计算本分支至少还要贡献多少。
            const long double requested = std::max(0.0L, target - score.points - remaining[i + 1] - 2 * tolerance);
            const Value child = solve(outcome.state, false, requested, childQualified.data, outcome.lastQualified, &view);
            if (domLimited) return {best, stateUpper, false, bestTrace};
            if (!child.exact) {
                assert_(below(child.upper, requested), "未完成分支已达到请求的分数下限");
                upper = score.points + child.upper + remaining[i + 1];
                complete = false;
                break;
            }
            outcome.winTrace = child.winTrace;
            score.add(child.best);
        }
        if (!complete && !below(upper, best.points)) unresolved = std::max(unresolved, upper);
        if (complete && score.points > best.points) {
            best = score;
            bestTrace = recordWins(first, i);
            if (root) rootMove = choice.move;
        }
        if (!ceiling.canImprove(best)) break;
    }
    if (!unresolved || below(unresolved, best.points)) return save({best, best.points, true, bestTrace});
    const Value bounded{best, std::max(best.points, std::min(unresolved, stateUpper)), false, bestTrace};
    if (!below(bounded.upper, need)) {
        assert_(need != 0, "精确搜索仍有未解决的招法");
        // 浮点舍入使上下界贴近门槛时，回到常规搜索求精确值。
        save(bounded);
        return solve(state, root, 0, qualified, lastQualified, ancestor);
    }
    return save(bounded);
}

} // 命名空间 mss::ioe

namespace mss {

IoeBruteForce::RootResult IoeBruteForce::analyzeRoot(const ObservedBoard::Result &board, const Grid<bool> &playerFlags, int clicks, size_t candidateLimit) {
    const ioe::Neighbors adjacent = ioe::RefDom().makeNeighbors();
    const std::function<void(const Progress &)> publish;
    ioe::ProgressReporter reporter{publish, {Phase::Enumeration, 0, 0, 0, 0, 0, 0, 0}, {}};
    return ioe::Search(adjacent, reporter, 0).filterRoot(board, ioe::Observation(board, playerFlags, clicks), candidateLimit);
}

IoeBruteForce::Result IoeBruteForce::solve(const ObservedBoard::Result &board, const Grid<bool> &playerFlags, int clicks,
                                         size_t candidateLimit, size_t domLimit, const std::function<void(const Progress &)> &publish) {
    const ioe::Neighbors adjacent = ioe::RefDom().makeNeighbors();
    const ioe::Observation root(board, playerFlags, clicks);
    ioe::ProgressReporter reporter{publish, {Phase::Enumeration, 0, 0, 0, 0, 0, 0, domLimit}, {}};
    ioe::Search search(adjacent, reporter, domLimit);
    const RootResult screened = search.filterRoot(board, root, candidateLimit);
    Result result(screened);
    if (!screened.eligible) {
        if (reporter.progress.phase == Phase::Root) reporter.report(true);
        return result;
    }
    reporter.progress.phase = Phase::Search;
    reporter.report(true);
    search.prepareLayouts(root);
    const ioe::Search::Value value = search.solve(root, true, 0, nullptr, 0, nullptr);
    assert_(value.exact || search.domLimited, "IOE 搜索未完成最优性证明");
    if (search.domLimited) result.status = Status::DomLimit;
    result.searchDomCalls = search.domCalls;
    result.wins = value.best.wins;
    result.expectedScore = value.best.points / result.candidates;
    if (value.winTrace || !search.domLimited) search.summarizeWins(root, value.winTrace, result);
    const ioe::Move move = search.rootMove;
    if (move.action == Action::None) return result;
    result.action = move.action;
    result.cell = move.cell;
    if (move.action == Action::Chord) {
        const span<const int> neighbors = adjacent[move.cell];
        for (int i = 0; i < neighbors.size; ++i) {
            const int cell = neighbors[i];
            const bool next = (move.chordFlags & (1U << i)) != 0;
            const bool old = root.flags.has(cell);
            if (next && !old) result.addedFlags[result.addedCount++] = cell;
            if (!next && old) result.removedFlags[result.removedCount++] = cell;
        }
    }
    return result;
}

} // 命名空间 mss
