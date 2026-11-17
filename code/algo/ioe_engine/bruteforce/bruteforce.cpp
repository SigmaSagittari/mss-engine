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

    Reward() : points(0), wins(0), above200(0) {}
    Reward(int threeBv, int clicks)
        : points(threeBv == 2 * clicks ? 1 : std::exp2(20.0L * (threeBv - 2 * clicks) / clicks)), wins(1), above200(threeBv > 2 * clicks) {}

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

    struct Layout {
        Model model;
        FlatHashTable<DistanceKey, int, DistanceKey::Hash> distance;
    };

    struct Outcome {
        Observation state;
        Reward upper;
        bool settled;
        size_t winTrace;
        size_t order;
    };

    struct Trial {
        size_t layout;
        size_t outcome;
        Reward estimate;
        Reward remaining;
    };

    struct Choice {
        Move move;
        vectorPool<Outcome>::vector outcomes;
        vectorPool<Trial>::vector trials;
        Reward upper;
        Reward checked;
        long double largest;
        size_t next;
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
          rootMove{0, Action::None, 0}, tolerance(0) {}

    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;

    ~Search() {
        for (size_t i = 0; i < layouts.size; ++i) delete layouts[i];
    }

    IoeBruteForce::RootResult filterRoot(const ObservedBoard::Result &board, const Observation &root, size_t candidateLimit);
    void prepareLayouts();
    Value solve(const Observation &state, bool root, long double need);
    void summarizeWins(size_t trace, IoeBruteForce::Result &result);

private:
    struct Workspace;
    Workspace &getWorkspace();

    // 数据的有效期覆盖这一次根部筛选和后续搜索。
    vector<Layout *> layouts;
    Ways seen[cells][10]; // 下标 9 用于雷格。
    Ways all;
    FlatHashTable<Observation, Value, ObservationHash> values;
    vector<size_t> winBranches;
    long double tolerance;

    // 0 表示无胜局，小于 firstBranch 的值编码 3BV 和点击数，其余值索引以 0 结尾的子分支。
    enum : size_t { clickBuckets = maxWinningClicks + 1, firstBranch = (cells - mines + 1) * clickBuckets };

    size_t winIndex(int threeBv, int clicks) const { return threeBv * clickBuckets + clicks; }
    size_t recordWins(const Outcome *outcomes, size_t processed);
    void accumulateWins(size_t trace, size_t *histogram);
    int distance(size_t id, const DistanceKey &key);
    bool below(long double upper, long double need) const { return upper + tolerance < need; }
    void qualifying(const Observation &state, vector<int> &optimum, Ways &viable, Reward &upper);
    void actions(const Observation &state, const Ways &viable, vector<Move> &result);
    void reveal(Observation &state, Mask pending, const Ways &support, unsigned digits, const vector<int> &optimum,
                Choice &choice, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials, int depth);
    Choice observe(const Observation &state, Move move, const Ways &viable, unsigned digits, const vector<int> &optimum,
                   vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials);
    void refine(Choice &choice, vectorPool<Outcome> &outcomes, const vectorPool<Trial> &trials);

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
        vector<int> optimum;
        Ways viable;
        vector<Move> actions;
        vector<Choice> choices;
        vectorPool<Outcome> outcomes;
        vectorPool<Trial> trials;
        vector<size_t> pending;
        vector<long double> remaining;
    };

    vector<Mask> enumerated;
    Model candidate;

    Frame frames[maxWinningClicks + 1];
    Ways revealSupport[cells + 1];

    ObservedBoard::Result board;
    Combination::Pool combinations;
    Position::Pool positions;

    Workspace() : board(side, side, mines), positions(combinations) {}
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
    if (checked.error) return {Status::InvalidBoard, 0, 0, 0};
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
    const DistanceKey distanceKey{shown, root.flags};
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
        Layout *layout = new Layout;
        layouts.push_back(layout);
        layout->model = model;
        layout->distance.emplace(distanceKey, distance);
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

void Search::prepareLayouts() {
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
    return firstBranch + offset;
}

void Search::accumulateWins(size_t trace, size_t *histogram) {
    if (!trace) return;
    if (trace < firstBranch) {
        const int threeBv = trace / clickBuckets;
        const int clicks = trace % clickBuckets;
        const int divisor = std::gcd(threeBv, clicks);
        ++histogram[winIndex(threeBv / divisor, clicks / divisor)];
        return;
    }
    for (size_t i = trace - firstBranch; winBranches[i]; ++i)
        accumulateWins(winBranches[i], histogram);
}

void Search::summarizeWins(size_t trace, IoeBruteForce::Result &result) {
    if (!trace) return;
    size_t histogram[firstBranch]{};
    accumulateWins(trace, histogram);
    size_t total = 0;
    for (size_t i = 1; i < firstBranch; ++i) {
        if (!histogram[i]) continue;
        result.winningIoe.push_back({(int)(i / clickBuckets), (int)(i % clickBuckets), histogram[i]});
        total += histogram[i];
    }
    assert_(total == result.wins, "IOE 分布的摆法数与胜出摆法数不符");
}

int Search::distance(size_t id, const DistanceKey &key) {
    Layout &layout = *layouts[id];
    if (const int *found = layout.distance.find(key)) return *found;
    if (domCalls >= domLimit) {
        domLimited = true;
        return cells;
    }
    ++domCalls;
    const int remaining = RefDom().remainingClicks(layout.model, key.shown, key.flags, adjacent);
    layout.distance.emplace(key, remaining);
    if ((domCalls & 63) == 0) {
        reporter.progress.searchDomCalls = domCalls;
        reporter.report(false);
    }
    return remaining;
}

void Search::qualifying(const Observation &state, vector<int> &optimum, Ways &viable, Reward &upper) {
    viable = all;
    for (int cell = 0; cell < cells; ++cell) {
        const Cell value = state.board[cell];
        if (ObservedBoard().isNumber(value)) intersect(viable, seen[cell][ObservedBoard().numberValue(value)], viable);
    }
    const DistanceKey key{state.revealed(), state.flags};
    each(viable, [&](size_t id) {
        if (domLimited) return;
        const int threeBv = layouts[id]->model.three_bv();
        const int budget = threeBv / 2;
        if (state.clicks <= budget) {
            const int clicks = state.clicks + distance(id, key);
            if (clicks <= budget) {
                optimum[id] = clicks;
                upper.add(Reward(threeBv, clicks));
                return;
            }
        }
        viable[id / 64] &= ~(uint64_t{1} << (id % 64));
    });
}

void Search::actions(const Observation &state, const Ways &viable, vector<Move> &result) {
    result.resize(0);
    for (int cell = 0; cell < cells; ++cell) {
        if (state.covered(cell)) {
            if (!state.flags.has(cell)) result.push_back({cell, Action::Open, 0});
            else {
                // 确认始终是真旗的格子无需拔旗。
                bool mayBeSafe = false;
                for (size_t word = 0; word < viable.size; ++word) mayBeSafe |= (viable[word] & ~seen[cell][9][word]) != 0;
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
        each(viable, [&](size_t id) {
            unsigned mineSet = 0;
            for (int i = 0; i < neighbors.size; ++i)
                if (layouts[id]->model.mines[neighbors[i]]) mineSet |= 1U << i;
            choices[mineSet & hidden] = true;
        });
        for (unsigned flags = 0; flags < 256; ++flags)
            if (choices[flags]) result.push_back({cell, Action::Chord, flags});
    }
}

void Search::reveal(Observation &state, Mask pending, const Ways &support, unsigned digits, const vector<int> &optimum,
                    Choice &choice, vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials, int depth) {
    int cell = 0;
    while (cell < cells && (!pending.has(cell) || !state.covered(cell) || state.flags.has(cell))) ++cell;
    if (cell == cells) {
        const Value *cached = values.find(state);
        const size_t outcome = choice.outcomes.count;
        if (cached && cached->exact) {
            const Reward exact = cached->best;
            outcomes.push_back(choice.outcomes, {state, exact, true, cached->winTrace, outcome});
            choice.checked.add(exact);
            choice.largest = std::max(choice.largest, exact.points);
            return;
        }
        const bool terminal = state.finished();
        outcomes.push_back(choice.outcomes, {state, {}, terminal, 0, outcome});
        each(support, [&](size_t id) {
            // 走一手后的透视最优点击数至少是原局面的最优点击数。
            const int minimum = std::max(optimum[id], state.clicks + !terminal);
            const int threeBv = layouts[id]->model.three_bv();
            if (minimum <= threeBv / 2) trials.push_back(choice.trials, {id, outcome, Reward(threeBv, minimum), {}});
        });
        return;
    }
    pending.clear(cell);
    Ways &branch = getWorkspace().revealSupport[depth + 1];
    for (int digit = 0; digit < 9; ++digit) {
        if (!(digits & (1U << digit)) || !intersect(support, seen[cell][digit], branch)) continue;
        Mask next = pending;
        if (digit == 0)
            for (int neighbor : adjacent[cell]) next.set(neighbor);
        const Cell previous = state.board[cell];
        state.board[cell] = (Cell) digit;
        reveal(state, next, branch, 511, optimum, choice, outcomes, trials, depth + 1);
        state.board[cell] = previous;
    }
}

Search::Choice Search::observe(const Observation &state, Move move, const Ways &viable, unsigned digits, const vector<int> &optimum,
                               vectorPool<Outcome> &outcomes, vectorPool<Trial> &trials) {
    Observation next = state;
    ++next.clicks;
    Mask pending{};
    Workspace &workspace = getWorkspace();
    Ways &support = workspace.revealSupport[0];
    support = viable;
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
                intersect(support, seen[cell][9], support);
            } else {
                next.flags.clear(cell);
                if (state.covered(cell)) pending.set(cell);
            }
        }
    }
    Choice result{move, outcomes.push_back(), trials.push_back(), {}, {}, 0, 0};
    reveal(next, pending, support, digits, optimum, result, outcomes, trials, 0);
    if (result.trials.count) {
        Trial *first = trials.data.data + result.trials.offset;
        std::sort(first, first + result.trials.count, [](const Trial &a, const Trial &b) {
            if (a.estimate.points != b.estimate.points) return a.estimate.points > b.estimate.points;
            if (a.outcome != b.outcome) return a.outcome < b.outcome;
            return a.layout < b.layout;
        });
        for (size_t i = result.trials.count; i > 0; --i) {
            Trial &trial = first[i - 1];
            trial.remaining = result.upper;
            result.upper.add(trial.estimate);
        }
    }
    result.upper.add(result.checked);
    return result;
}

void Search::refine(Choice &choice, vectorPool<Outcome> &outcomes, const vectorPool<Trial> &trials) {
    const Trial &trial = trials.data[choice.trials.offset + choice.next++];
    Outcome &outcome = outcomes.data[choice.outcomes.offset + trial.outcome];
    const int threeBv = layouts[trial.layout]->model.three_bv();
    // 已缓存精确结果的分支不生成试算项。此处已确定的分支只有通关局面。
    const int clicks = outcome.state.clicks + (outcome.settled ? 0 : distance(trial.layout, {outcome.state.revealed(), outcome.state.flags}));
    if (domLimited) return;
    if (clicks <= threeBv / 2) {
        const Reward actual(threeBv, clicks);
        choice.checked.add(actual);
        if (!outcome.upper.wins) outcome.winTrace = winIndex(threeBv, clicks);
        outcome.upper.add(actual);
        choice.largest = std::max(choice.largest, outcome.upper.points);
    }
    choice.upper = choice.checked;
    choice.upper.add(trial.remaining);
}

Search::Value Search::solve(const Observation &state, bool root, long double need) {
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
    qualifying(state, optimum, viable, ceiling);
    if (domLimited) return {best, previousUpper, false, bestTrace};
    if (root) tolerance = 64 * std::numeric_limits<long double>::epsilon() * layouts.size * ceiling.points;
    if ((!root && ceiling.wins <= 1) || !ceiling.wins || state.finished()) {
        size_t trace = 0;
        each(viable, [&](size_t id) { trace = winIndex(layouts[id]->model.three_bv(), optimum[id]); });
        return save({ceiling, ceiling.points, true, trace});
    }
    const long double stateUpper = std::min(ceiling.points, previousUpper);
    if (below(stateUpper, need)) return save({best, stateUpper, false, bestTrace});

    vector<Choice> &choices = frame.choices;
    vector<Move> &moves = frame.actions;
    vectorPool<Outcome> &outcomes = frame.outcomes;
    vectorPool<Trial> &trials = frame.trials;
    choices.resize(0);
    outcomes.data.resize(0);
    trials.data.resize(0);
    actions(state, viable, moves);
    {
        ObservedBoard::Result &board = workspace.board;
        std::copy_n(state.board, cells, board.cells[0]);
        Position::Pool &positions = workspace.positions;
        positions.clear();
        Position position;
        Position::Result observed = position.analyze(board, positions, positionConfig);
        const Position::ProbabilityResult probabilities = position.analyzeProbability(observed, positions);
        for (size_t i = 0; i < moves.size; ++i) {
            const Move move = moves[i];
            unsigned digits = 511;
            if (move.action == Action::Open) {
                Observe::Result observedDigits;
                position.observe(observed, probabilities, positions.probabilities, move.cell, observedDigits);
                digits = 0;
                for (int digit = 0; digit < 9; ++digit)
                    if (observedDigits.probability[digit] > 0) digits |= 1U << digit;
            }
            const Choice choice = observe(state, move, viable, digits, optimum, outcomes, trials);
            if (choice.upper.wins) choices.push_back(choice);
            else {
                outcomes.pop_back(choice.outcomes);
                trials.pop_back(choice.trials);
            }
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
            refine(choice, outcomes, trials);
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
            const Value child = solve(outcome.state, false, requested);
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
        return solve(state, root, 0);
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
    search.prepareLayouts();
    const ioe::Search::Value value = search.solve(root, true, 0);
    assert_(value.exact || search.domLimited, "IOE 搜索未完成最优性证明");
    if (search.domLimited) result.status = Status::DomLimit;
    result.searchDomCalls = search.domCalls;
    result.wins = value.best.wins;
    result.expectedScore = value.best.points / result.candidates;
    search.summarizeWins(value.winTrace, result);
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
