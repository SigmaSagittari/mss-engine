#include "algo/ioe_engine/bruteforce/strategy.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

#include "algo/ioe_engine/move_proof/move_proof.h"
#include "algo/ioe_engine/bruteforce/moves.h"
#include "core/assert.h"

namespace mss {

struct IoeStrategy::Candidate {
    int id;
    std::int16_t clickLowerBound; // 累计点击数下界，根部和已求出最少点击数的候选取精确值。
    std::int16_t exactAfterMove; // 仅在候选动作尚待查询时使用，-1 表示尚无精确结论。

    Candidate(int id, int clickLowerBound, int exact = -1) : id(id), clickLowerBound(clickLowerBound), exactAfterMove(exact) {}
};

struct IoeStrategy::Win {
    int id;
    int clicks;
};

struct IoeStrategy::Answer {
    IoeMove move;
    long double score;
    span<const Win> wins; // 借用当前帧或精确缓存；调用方在下次递归或缓存扩容前读取。
    bool exact;
    bool reachedUpper;
};

struct IoeStrategy::Cached {
    long double score;
    vectorPool<Win>::vector wins;

    Answer read(const vectorPool<Win> &traces) const {
        return {{}, score, {wins.count ? traces.data.data + wins.offset : nullptr, (int)wins.count}, true, false};
    }
};

struct IoeStrategy::Key {
    IoeCells revealed, flags;
    U128 ids;
    int spent;
    bool operator==(const Key &) const = default;
};

struct IoeStrategy::Hash {
    std::size_t operator()(const Key &key) const {
        return splitmix64(key.revealed.word(0) ^ (key.revealed.word(1) * 0x9e3779b97f4a7c15ULL)
                          ^ key.ids.lo ^ splitmix64(key.flags.word(0) ^ (key.flags.word(1) * 0xd1b54a32d192ed03ULL)
                                                   ^ key.ids.hi ^ key.spent));
    }
};

struct IoeStrategy::Rewards {
    // 危险：solve 按 3BV 填入各行的起点和点击上限，只存门槛内的分值。
    int offsets[IoeBoard::cells - IoeBoard::mines + 1];
    int limits[IoeBoard::cells - IoeBoard::mines + 1];
    vector<long double> values;
};

// 同一批雷图的预计算结果；boards 和 rank 均按 solve 输入的雷图编号访问。
struct IoeStrategy::Prepared {
    vector<IoeStrategyBoard> boards;
    vector<int> rank;
    Rewards rewards;
};

struct IoeStrategy::Root {
    IoeView view;
    int spent;
    IoeMove fixed;

    Root(const IoeView &view, int spent, const IoeMove &fixed) : view(view), spent(spent), fixed(fixed) {}
};

struct IoeStrategy::Expansion {
    struct Query {
        std::size_t limit;
        IoeObserver<std::size_t> progress;
        bool interrupted;

        Query(std::size_t limit, IoeObserver<std::size_t> progress) : limit(limit), progress(progress), interrupted(false) {}
    } query;

    std::size_t prefix;
    std::size_t previousPrefix;
    IoeMove previousMove;
    long double previousScore;
    long double slack;

    Expansion(std::size_t limit, IoeObserver<std::size_t> progress)
        : query(limit, progress), prefix(1), previousPrefix(0), previousScore(0), slack(0) {}
};

struct IoeStrategy::Node {
    IoeView view;
    int paid;
    // 借用根候选或父递归帧的候选切片，只在当前递归调用链有效。
    span<const Candidate> possible;
    int depth;
    long double need;

    Node(const IoeView &view, int paid, span<const Candidate> possible, int depth, long double need)
        : view(view), paid(paid), possible(possible), depth(depth), need(need) {}
};

struct IoeStrategy::Workspace {
    static constexpr int cells = IoeBoard::cells;
    static constexpr int safe = cells - IoeBoard::mines;

    struct Branch {
        IoeCells revealed;
        vectorPool<Candidate>::vector possible;
        long double upper;
        long double single;
        long double guaranteed;
        vectorPool<Win>::vector cachedWins;
        bool cached;
    };

    struct Pending {
        std::size_t candidate;
        std::size_t branch;
    };

    struct Action {
        struct Score {
            long double upper;
            long double singleEstimate;
            long double largestBranch;
            long double guaranteed;
            int possibleWins;

            Score() : upper(0), singleEstimate(0), largestBranch(0), guaranteed(0), possibleWins(0) {}
        } score;

        struct Query {
            vectorPool<Pending>::vector pending;
            std::size_t completed;
            bool precise;

            Query(vectorPool<Pending>::vector pending) : pending(pending), completed(0), precise(false) {}
        } query;

        IoeMove move;
        IoeCells flags;
        vectorPool<Branch>::vector branches;
        int spent;

        Action(const IoeMove &move, const IoeCells &flags, vectorPool<Branch>::vector branches, int spent,
               vectorPool<Pending>::vector pending)
            : query(pending), move(move), flags(flags), branches(branches), spent(spent) {}
    };

    struct Frame {
        IoeMoves::Result trial;
        vector<int> ids, heap, branchOrder;
        vector<IoeMove> moves;
        vector<Action> actions;
        vectorPool<Pending> pending;
        vector<IoeClickQuery::Estimate> estimates;
        vectorPool<Branch> branches;
        vectorPool<Candidate> candidates;
        vector<Win> trialWins, bestWins;
    };

    Prepared prepared;
    vector<Candidate> initial;
    vector<int> position, wonAt;
    vectorPool<Win> traces;
    FlatHashTable<Key, Cached, Hash> exact;
    vector<Win> completedWins;
    IoeClickQuery::Cache distances;
    // 每步揭开新格或拔去开局已有的旗。成功双击新插的旗在各剩余雷图中都是雷，搜索不会再拔。
    Frame frames[safe + cells + 1];
};

__declspec(noinline) IoeStrategy::Workspace &IoeStrategy::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

IoeMove IoeStrategy::solve(const vector<IoeLayout> &layouts, const IoeView &root, int spent, const IoeMove &fixed, int minIoePercent,
                           std::size_t queryLimit, Result &result, IoeObserver<std::size_t> progress) {
    constexpr int cells = Workspace::cells, safe = Workspace::safe;
    result.scoreSum = 0;
    result.completed = result.won = result.queries = 0;
    result.exhausted = result.reachedUpperBound = false;
    result.distribution.size = 0;
    result.winsByExtraClicks.data.size = 0;
    if (!layouts.size) return fixed;

    const Root start(root, spent, fixed);
    Workspace &workspace = getWorkspace();
    vector<IoeStrategyBoard> &boards = workspace.prepared.boards;
    vector<Candidate> &initial = workspace.initial;
    vector<int> &rank = workspace.prepared.rank;
    vectorPool<Win> &traces = workspace.traces;
    FlatHashTable<Key, Cached, Hash> &exact = workspace.exact;
    vector<Win> &completedWins = workspace.completedWins;
    IoeClickQuery::Cache &distances = workspace.distances;
    Rewards &rewards = workspace.prepared.rewards;
    boards.resize(layouts.size);
    initial.resize(layouts.size);
    rank.resize(layouts.size);
    const IoeCells *adjacent = IoeBoard().neighbors();
    for (int i = 0; i < (int)layouts.size; ++i) {
        IoeStrategyBoard &board = boards[i];
        board.mines = layouts[i].mines;
        board.zeros = {};
        for (int cell = 0; cell < cells; ++cell) {
            const int digit = board.mines.test(cell) ? 9 : (board.mines & adjacent[cell]).popcount();
            board.digits[cell] = digit;
            if (digit == 0) board.zeros.set(cell);
        }
        initial[i] = {i, spent + layouts[i].remaining};
        if ((i & 4095) == 0) progress(0);
    }
    int entries = 0;
    for (int bv = 0; bv <= safe; ++bv) {
        rewards.offsets[bv] = entries;
        rewards.limits[bv] = 100 * bv / minIoePercent;
        entries += rewards.limits[bv] + 1;
    }
    rewards.values.resize(entries);
    for (int bv = 0; bv <= safe; ++bv) {
        long double *scores = rewards.values.data + rewards.offsets[bv];
        scores[0] = 0;
        for (int t = 1; t <= rewards.limits[bv]; ++t) scores[t] = std::exp2(20.0L * (bv - 2 * t) / t);
    }

    auto betterRatio = [&](Candidate a, Candidate b) {
        const int left = layouts[a.id].threeBV * b.clickLowerBound, right = layouts[b.id].threeBV * a.clickLowerBound;
        return left != right ? left > right : a.id < b.id;
    };
    std::sort(initial.data, initial.data + initial.size, betterRatio);
    for (std::size_t i = 0; i < initial.size; ++i) rank[initial[i].id] = i;
    distances.clear(layouts.size);
    exact.clear();
    traces.data.size = 0;
    completedWins.size = 0;

    constexpr long double eps = std::numeric_limits<long double>::epsilon();
    Expansion expansion(queryLimit, progress);

    expansion.query.progress(0);
    const std::size_t count = initial.size;
    for (;;) {
        const std::size_t before = distances.queries();
        expansion.query.progress(before);
        long double upper = 0;
        for (std::size_t i = 0; i < expansion.prefix; ++i) upper += score(layouts[initial[i].id], initial[i].clickLowerBound, rewards);
        expansion.slack = upper * expansion.prefix * 64 * eps;
        // 扩批后，同一局面、旗位、候选集合和已用点击数的精确策略仍然有效。
        const Answer answer = search(layouts, workspace.prepared, distances, exact, traces, start, expansion,
                                     {start.view, start.spent, {initial.data, (int)expansion.prefix}, 0, 0});
        if (expansion.query.interrupted) break;
        expansion.previousMove = answer.move;
        expansion.previousScore = answer.score;
        result.reachedUpperBound = answer.reachedUpper;
        completedWins.resize(answer.wins.size);
        if (answer.wins.size) std::copy_n(answer.wins.data, answer.wins.size, completedWins.data);
        result.completed = expansion.prefix;
        if (expansion.prefix == count) break;
        std::size_t next = std::min(count, expansion.prefix * 2);
        const std::size_t queried = distances.queries();
        if (expansion.previousPrefix && next < count && queried > before) {
            // 查询额度与新增摆法数的乘积可能超过 Wasm 的 32 位 size_t。
            const unsigned long long remaining = expansion.query.limit - queried;
            const unsigned long long capacity = remaining * (expansion.prefix - expansion.previousPrefix) / (queried - before);
            if (capacity < next - expansion.prefix) {
                const std::size_t minimum = (expansion.prefix + 4) / 5;
                const std::size_t estimated = capacity / 2;
                next = expansion.prefix + std::max(minimum, estimated);
            }
        }
        expansion.previousPrefix = expansion.prefix;
        expansion.prefix = next;
    }
    result.exhausted = expansion.query.interrupted;
    result.scoreSum = expansion.previousScore;
    result.won = completedWins.size;
    result.queries = distances.queries();
    result.reachedUpperBound &= !expansion.query.interrupted;
    expansion.query.progress(result.queries);

    summarize(layouts, {initial.data, (int)initial.size}, {completedWins.data, (int)completedWins.size}, result);
    return fixed.kind == IoeMove::Kind::None ? expansion.previousMove : fixed;
}

long double IoeStrategy::score(const IoeLayout &layout, int total, const Rewards &rewards) {
    const int bv = layout.threeBV;
    return total <= rewards.limits[bv] ? rewards.values[rewards.offsets[bv] + total] : 0.0L;
}

vectorPool<IoeStrategy::Win>::vector IoeStrategy::store(vectorPool<Win> &traces, const vector<Win> &wins) {
    const vectorPool<Win>::vector result{traces.data.size, wins.size};
    traces.data.resize(result.offset + result.count);
    if (wins.size) std::memcpy(traces.data.data + result.offset, wins.data, wins.size * sizeof(Win));
    return result;
}

bool IoeStrategy::provePair(const vector<IoeStrategyBoard> &boards, IoeClickQuery::Cache &distances,
                            const IoeView &start, int startSpent, Candidate a, Candidate b) {
    constexpr int cells = IoeBoard::cells, safe = cells - IoeBoard::mines;
    const IoeCells *adjacent = IoeBoard().neighbors();
    IoeClickQuery query;
    if (!distances.canEstimate(a.id) || !distances.canEstimate(b.id)) return false;
    const IoeStrategyBoard &boardA = boards[a.id], &boardB = boards[b.id];
    const IoeCells commonSafe = IoeCells(cells) & ~(boardA.mines | boardB.mines);
    IoeView view = start;
    int paid = startSpent;
    while (paid < std::min(a.clickLowerBound, b.clickLowerBound)) {
        const IoeCells hidden = commonSafe & ~view.revealed;
        const int remainingA = a.clickLowerBound - paid, remainingB = b.clickLowerBound - paid;
        IoeClickQuery::Estimate choiceA, choiceB;
        query.estimate(distances, a.id, boardA, view, remainingA, choiceA);
        query.estimate(distances, b.id, boardB, view, remainingB, choiceB);
        IoeView next;
        int nextPaid = 0, largest = 0;
        const IoeCells opens = choiceA.opens() & choiceB.opens() & hidden & ~view.flags;
        IoeCells candidates = opens | (choiceA.chords() & choiceB.chords() & view.revealed);
        while (candidates.any()) {
            const int cell = candidates.firstSetBit();
            candidates.reset(cell);
            IoeMove move;
            if (hidden.test(cell)) {
                move = {IoeMove::Kind::Open, cell};
            } else {
                if (!boardA.digits[cell] || (adjacent[cell] & hidden).popcount() < 2) continue;
                const IoeCells mines = adjacent[cell] & boardA.mines;
                if (mines != (adjacent[cell] & boardB.mines)) continue;
                move = {IoeMove::Kind::Chord, cell, mines & ~view.flags, adjacent[cell] & view.flags & ~mines};
            }
            const int total = paid + IoeMoves().clicks(move, view);
            if (total > a.clickLowerBound || total > b.clickLowerBound) continue;
            IoeCells shownA, shownB;
            if (!IoeMoves().play(move, view, boardA, shownA) || !IoeMoves().play(move, view, boardB, shownB)) continue;
            const IoeCells flags = (view.flags | move.addFlags) & ~move.removeFlags;
            const IoeCells opened = shownA & ~view.revealed;
            bool different = shownA != shownB;
            opened.forEachSetBit([&](int cell) { different |= boardA.digits[cell] != boardB.digits[cell]; });
            if (different) return true;
            const int count = opened.popcount();
            if (count > largest) {
                largest = count;
                next = {shownA, flags};
                nextPaid = total;
            }
        }
        if (!largest) return false;
        view = next;
        paid = nextPaid;
    }
    return view.revealed.popcount() == safe;
}

IoeStrategy::Answer IoeStrategy::search(const vector<IoeLayout> &layouts, const Prepared &prepared,
                                       IoeClickQuery::Cache &distances, FlatHashTable<Key, Cached, Hash> &exact,
                                       vectorPool<Win> &traces, const Root &root, Expansion &expansion, const Node &node) {
    using Action = Workspace::Action;
    using Branch = Workspace::Branch;
    using Pending = Workspace::Pending;
    constexpr int cells = Workspace::cells, safe = Workspace::safe;
    constexpr long double eps = std::numeric_limits<long double>::epsilon();
    Workspace &workspace = getWorkspace();
    vector<int> &position = workspace.position;
    position.resize(layouts.size);
    const vector<IoeStrategyBoard> &boards = prepared.boards;
    const vector<int> &rank = prepared.rank;
    const Rewards &rewards = prepared.rewards;
    IoeClickQuery query;
    auto score = [&](int id, int total) { return this->score(layouts[id], total, rewards); };
    auto sameScore = [&](long double a, long double b) { return std::abs(a - b) <= expansion.slack; };
    auto improves = [&](long double a, std::size_t aw, long double b, std::size_t bw) {
        if (a == (long double)aw && b == (long double)bw) return aw > bw;
        return a > b + 32 * eps * std::max(aw, bw) * std::max(a, b);
    };

    const bool atRoot = node.depth == 0;
    Workspace::Frame &frame = workspace.frames[node.depth];
    vector<Win> &trialWins = frame.trialWins, &bestWins = frame.bestWins;
    const Answer none{{}, 0, {}, true, false};
    if (!node.possible.size) return none;
    if (node.view.revealed.popcount() == safe || (!atRoot && node.possible.size == 1)) {
        bestWins.size = 0;
        long double upper = 0;
        for (Candidate entry : node.possible) {
            bestWins.push_back({entry.id, entry.clickLowerBound});
            upper += score(entry.id, entry.clickLowerBound);
        }
        return {{}, upper, {bestWins.data, (int)bestWins.size}, true, true};
    }

    long double nodeUpper = 0, extraUpper = 0, guarantee = atRoot ? expansion.previousScore : 0;
    U128Hasher identities;
    int old = 0;
    for (Candidate entry : node.possible) {
        const long double upper = score(entry.id, entry.clickLowerBound);
        nodeUpper += upper;
        guarantee = std::max(guarantee, upper);
        if (rank[entry.id] < (int)expansion.previousPrefix) ++old;
        else extraUpper += upper;
    }
    // group 和后续筛选都保持组内输入顺序，上一批的候选恰好是当前组的前 old 项。
    for (int i = 0; i < old; ++i) identities.mix(node.possible[i].id);
    const Key older{node.view.revealed, node.view.flags, identities.finalize(), node.paid};
    for (int i = old; i < node.possible.size; ++i) identities.mix(node.possible[i].id);
    const Key key{node.view.revealed, node.view.flags, identities.finalize(), node.paid};
    if (!atRoot) {
        if (const Cached *cached = exact.find(key)) return cached->read(traces);
        if (old && old != node.possible.size)
            if (const Cached *cached = exact.find(older)) {
                nodeUpper = std::min(nodeUpper, cached->score + extraUpper);
                guarantee = std::max(guarantee, cached->score);
            }
        if (nodeUpper + expansion.slack < node.need) return {{}, nodeUpper, {}, false, false};
        if (node.possible.size == 2 && provePair(boards, distances, node.view, node.paid, node.possible[0], node.possible[1])) {
            bestWins.size = 0;
            for (Candidate entry : node.possible) bestWins.push_back({entry.id, entry.clickLowerBound});
            exact.emplace(key, {nodeUpper, store(traces, bestWins)});
            return {{}, nodeUpper, {bestWins.data, (int)bestWins.size}, true, true};
        }
    }

    IoeMove forced = atRoot ? root.fixed : IoeMove{};
    if (!atRoot && node.possible.size <= 64) {
        IoeMoveProof::Facts facts{IoeCells(cells), IoeCells(cells), {}};
        for (Candidate entry : node.possible) {
            facts.mines &= boards[entry.id].mines;
            facts.safe &= ~boards[entry.id].mines;
            facts.possibleZeros |= boards[entry.id].zeros;
        }
        forced = IoeMoveProof().prove(node.view, boards[node.possible[0].id].digits, facts);
    }
    const bool proved = forced.kind != IoeMove::Kind::None && !atRoot;
    vector<IoeMove> &moves = frame.moves;
    vector<int> &ids = frame.ids;
    ids.resize(node.possible.size);
    for (int i = 0; i < node.possible.size; ++i) {
        ids[i] = node.possible[i].id;
        position[ids[i]] = i;
    }
    moves.size = 0;
    if (forced.kind != IoeMove::Kind::None) moves.push_back(forced);
    else IoeMoves().moves(node.view, boards, {ids.data, (int)ids.size}, atRoot, moves);
    vector<IoeClickQuery::Estimate> &estimates = frame.estimates;
    if (!proved) {
        estimates.resize(node.possible.size);
        for (int i = 0; i < node.possible.size; ++i) {
            const Candidate entry = node.possible[i];
            query.estimate(distances, entry.id, boards[entry.id], node.view, entry.clickLowerBound - node.paid, estimates[i]);
        }
    }

    vector<Action> &actions = frame.actions;
    vector<int> &heap = frame.heap;
    vectorPool<Branch> &branches = frame.branches;
    vectorPool<Candidate> &candidates = frame.candidates;
    vectorPool<Pending> &pending = frame.pending;
    actions.size = heap.size = branches.data.size = candidates.data.size = pending.data.size = 0;
    bestWins.size = 0;
    for (std::size_t i = 0; i < moves.size; ++i) {
        const IoeMove &move = moves[i];
        const int total = node.paid + IoeMoves().clicks(move, node.view);
        if (total > rewards.limits[safe]) continue;
        const bool opensCenter = move.kind == IoeMove::Kind::Open
            || (move.kind == IoeMove::Kind::Chord && !node.view.revealed.test(move.cell));
        IoeMoves::Result &trial = frame.trial;
        IoeMoves().group(move, node.view, boards, {ids.data, (int)ids.size}, trial);
        Action action(move, trial.flags, branches.push_back(), total, pending.push_back());
        for (std::size_t g = 0; g < trial.groups.size; ++g) {
            const IoeMoves::Group &group = trial.groups[g];
            Branch branch{group.revealed, candidates.push_back(), 0, 0, 0, {}, false};
            const int minFinal = total + (group.revealed.popcount() != safe);
            U128Hasher branchIds;
            for (int id : trial.layouts.span(group.layouts)) {
                const int index = position[id];
                const int lowerBound = node.possible[index].clickLowerBound;
                int minimum = std::max(lowerBound, minFinal);
                int known = -1;
                if (!proved) {
                    const IoeClickQuery::Bound bound = query.bound(estimates[index], move, opensCenter,
                                                                  lowerBound - node.paid, root.spent + layouts[id].remaining - node.paid);
                    minimum = std::max(minimum, node.paid + bound.clicks);
                    if (bound.exact) known = minimum - total;
                }
                const long double upper = score(id, minimum);
                if (!upper) continue;
                candidates.push_back(branch.possible, {id, minimum, known});
                branchIds.mix(id);
                branch.upper += upper;
                branch.single = std::max(branch.single, upper);
            }
            if (!branch.possible.count) continue;
            const Cached *known = exact.find({group.revealed, trial.flags, branchIds.finalize(), total});
            if (known) {
                branch.cached = true;
                branch.cachedWins = known->wins;
                branch.upper = branch.single = branch.guaranteed = known->score;
            } else if (group.revealed.popcount() == safe) branch.guaranteed = branch.single = branch.upper;
            else if (proved) branch.guaranteed = branch.single;
            else
                for (std::size_t i = branch.possible.offset; i < branch.possible.offset + branch.possible.count; ++i)
                    pending.push_back(action.query.pending, {i, branches.data.size});
            action.score.guaranteed += branch.guaranteed;
            branches.push_back(action.branches, branch);
            action.score.upper += branch.upper;
            action.score.singleEstimate += branch.single;
            action.score.largestBranch = std::max(action.score.largestBranch, branch.upper);
            action.score.possibleWins += branch.possible.count;
        }
        if (!action.score.possibleWins) continue;
        guarantee = std::max(guarantee, action.score.guaranteed);
        action.query.precise = action.query.pending.count == 0;
        if (!action.query.precise) {
            Pending *first = pending.data.data + action.query.pending.offset;
            std::sort(first, first + action.query.pending.count, [&](const Pending &a, const Pending &b) {
                const Candidate x = candidates.data[a.candidate], y = candidates.data[b.candidate];
                const int difference = layouts[x.id].threeBV * y.clickLowerBound - layouts[y.id].threeBV * x.clickLowerBound;
                if (difference) return difference > 0;
                return a.branch != b.branch ? a.branch < b.branch : x.id < y.id;
            });
        }
        heap.push_back(actions.size);
        actions.push_back(action);
    }

    auto worse = [&](int a, int b) {
        const Action &x = actions[a], &y = actions[b];
        if (x.score.upper != y.score.upper) return x.score.upper < y.score.upper;
        const bool xPreferred = atRoot && x.move == expansion.previousMove, yPreferred = atRoot && y.move == expansion.previousMove;
        if (xPreferred != yPreferred) return yPreferred;
        if (x.query.precise != y.query.precise) return y.query.precise;
        if (x.score.singleEstimate != y.score.singleEstimate) return x.score.singleEstimate < y.score.singleEstimate;
        if (x.score.largestBranch != y.score.largestBranch) return x.score.largestBranch > y.score.largestBranch;
        return a > b;
    };
    if (heap.size) std::make_heap(heap.data, heap.data + heap.size, worse);
    long double best = 0;
    IoeMove bestMove;
    bool anySkipped = false;
    while (heap.size) {
        const int index = heap[0];
        Action &action = actions[index];
        if (action.score.upper + expansion.slack < std::max({best, node.need, guarantee})) {
            anySkipped |= action.score.upper + expansion.slack < node.need;
            break;
        }
        std::pop_heap(heap.data, heap.data + heap.size, worse);
        --heap.size;
        if (bestWins.size && !improves(action.score.upper, action.score.possibleWins, best, bestWins.size)) continue;

        if (!action.query.precise) {
            const Pending item = pending.data[action.query.pending.offset + action.query.completed++];
            Candidate &entry = candidates.data[item.candidate];
            Branch &branch = branches.data[item.branch];
            const IoeView state{branch.revealed, action.flags};
            const IoeLayout &layout = layouts[entry.id];
            const int limit = rewards.limits[layout.threeBV];
            const int budget = limit - action.spent;
            const std::size_t before = distances.queries();
            const int remaining = query.minimum(distances, entry.id, boards[entry.id], {state, budget, entry.exactAfterMove, expansion.query.limit},
                                                {root.view, layout.remaining, limit - root.spent});
            if (remaining == IoeClickQuery::limitReached) {
                expansion.query.interrupted = true;
                return none;
            }
            if (distances.queries() != before && !(distances.queries() & 255)) expansion.query.progress(distances.queries());
            const int final = remaining < 0 ? limit + 1 : action.spent + remaining;
            const long double bound = score(entry.id, final), difference = bound - score(entry.id, entry.clickLowerBound);
            entry.clickLowerBound = final;
            action.score.possibleWins -= !bound;
            branch.upper += difference;
            action.score.upper += difference;
            if (bound > branch.guaranteed) {
                action.score.guaranteed += bound - branch.guaranteed;
                branch.guaranteed = bound;
                guarantee = std::max(guarantee, action.score.guaranteed);
            }
            if (action.score.upper + expansion.slack < std::max(guarantee, node.need)
                    || (bestWins.size && !improves(action.score.upper, action.score.possibleWins, best, bestWins.size))) {
                anySkipped |= action.score.upper + expansion.slack < node.need;
                continue;
            }

            branch.single = 0;
            for (Candidate value : candidates.span(branch.possible)) branch.single = std::max(branch.single, score(value.id, value.clickLowerBound));
            action.score.singleEstimate = action.score.largestBranch = 0;
            for (const Branch &group : branches.span(action.branches)) {
                action.score.singleEstimate += group.single;
                action.score.largestBranch = std::max(action.score.largestBranch, group.upper);
            }
            if (action.query.completed == action.query.pending.count) {
                for (Branch &group : branches.span(action.branches)) {
                    std::size_t kept = 0;
                    for (Candidate value : candidates.span(group.possible))
                        if (score(value.id, value.clickLowerBound)) candidates.data[group.possible.offset + kept++] = value;
                    group.possible.count = kept;
                }
                action.query.precise = true;
            }
            heap.push_back(index);
            std::push_heap(heap.data, heap.data + heap.size, worse);
            continue;
        }
        if (!improves(action.score.upper, action.score.possibleWins, best, bestWins.size) && bestWins.size) continue;

        vector<int> &branchOrder = frame.branchOrder;
        branchOrder.size = 0;
        for (std::size_t g = action.branches.offset; g < action.branches.offset + action.branches.count; ++g)
            if (branches.data[g].possible.count) branchOrder.push_back(g);
        std::sort(branchOrder.data, branchOrder.data + branchOrder.size, [&](int a, int b) {
            const Branch &x = branches.data[a], &y = branches.data[b];
            return x.upper != y.upper ? x.upper > y.upper : a < b;
        });

        trialWins.size = 0;
        long double actual = 0, left = action.score.upper;
        bool complete = true;
        for (std::size_t g = 0; g < branchOrder.size; ++g) {
            const Branch &branch = branches.data[branchOrder[g]];
            left -= branch.upper;
            const long double target = std::max(0.0L, std::max({best, node.need, guarantee}) - actual - left - 2 * expansion.slack);
            const Answer answer = branch.cached ? Cached{branch.upper, branch.cachedWins}.read(traces)
                : search(layouts, prepared, distances, exact, traces, root, expansion,
                         {{branch.revealed, action.flags}, action.spent,
                          {candidates.data.data + branch.possible.offset, (int)branch.possible.count}, node.depth + 1, target});
            if (expansion.query.interrupted) return none;
            if (!answer.exact) { complete = false; break; }
            actual += answer.score;
            if (answer.wins.size) {
                const std::size_t offset = trialWins.size;
                trialWins.resize(offset + answer.wins.size);
                std::copy_n(answer.wins.data, answer.wins.size, trialWins.data + offset);
            }
            if (actual + left + expansion.slack < std::max({best, node.need, guarantee})) { complete = false; break; }
        }
        if (!complete) {
            anySkipped |= actual + left + expansion.slack < node.need || node.need > best;
            continue;
        }
        if (improves(actual, trialWins.size, best, bestWins.size)) {
            best = actual;
            guarantee = std::max(guarantee, best);
            bestWins = trialWins;
            bestMove = action.move;
            if (sameScore(best, nodeUpper)) break;
        }
    }

    if (anySkipped && best + expansion.slack < node.need) return {{}, nodeUpper, {}, false, false};
    if (!atRoot && exact.size() < (1U << 19)) exact.emplace(key, {best, store(traces, bestWins)});
    return {bestMove, best, {bestWins.data, (int)bestWins.size}, true, sameScore(best, nodeUpper)};
}

void IoeStrategy::summarize(const vector<IoeLayout> &layouts, span<const Candidate> initial, span<const Win> wins, Result &result) {
    Workspace &workspace = getWorkspace();
    const std::size_t count = initial.size;
    vector<int> &wonAt = workspace.wonAt;
    wonAt.resize(count);
    std::fill_n(wonAt.data, count, 0);
    for (const Win &win : wins) wonAt[win.id] = win.clicks;
    vector<Result::Ratio> &distribution = result.distribution;
    vectorPool<int> &winsByExtraClicks = result.winsByExtraClicks;
    // initial 已按逐盘最高 IOE 排序，相同 IOE 的摆法连续。
    Result::Ratio *row = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        const Candidate entry = initial[i];
        const int bestClicks = entry.clickLowerBound;
        const int bv = layouts[entry.id].threeBV;
        if (!row || bv * row->denominator != bestClicks * row->numerator) {
            const int divisor = std::gcd(bv, bestClicks);
            distribution.push_back({bv / divisor, bestClicks / divisor, winsByExtraClicks.push_back(), 0, 0});
            row = &distribution[distribution.size - 1];
        }
        const int actual = wonAt[entry.id];
        if (actual) {
            const int extra = actual - bestClicks;
            assert_(extra >= 0, "策略点击数小于同一雷图的最少点击数");
            while ((int)row->won.count <= extra) winsByExtraClicks.push_back(row->won, 0);
            ++winsByExtraClicks.data[row->won.offset + extra];
        } else if (i < result.completed) ++row->discarded;
        else ++row->unsearched;
    }
}

} // 命名空间 mss
