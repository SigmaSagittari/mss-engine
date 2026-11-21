#include "algo/ioe_engine/bruteforce/layout_filter/joint_filter.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <numeric>

#include "core/datastructure/flat_hashtable.h"

namespace mss {

struct IoeJointFilter::Weights {
    int bv, click;
};

struct IoeJointFilter::MineGraph {
    // 3BV 的缓存键给合法雷图后缀编号留了 19 位。
    static constexpr std::uint32_t maxNode = (1U << 19) - 1;

    struct Node {
        std::uint32_t next[2]; // 0 表示无合法后缀，1 表示已决定全部雷位。
        std::uint64_t count;
    };

    struct Step {
        std::uint32_t neighbors;
        std::uint32_t fixedMines;
        std::uint32_t live;
        std::uint8_t cell;
        std::uint8_t end; // 扫描这个格之前需要决定 [0, end) 的待分配格。
        std::uint8_t digit; // 0～8 为已揭数字，9 为尚未揭开。
        std::uint8_t groupCount;
        std::uint32_t groups[IoeBoard::side + 1];

        // 同组雷位已经扫过，对后续每个格的数字有相同的贡献。
        std::uint32_t project(std::uint32_t previous) const {
            std::uint32_t pattern = (previous >> 1) & live;
            for (int i = 0; i < groupCount; ++i) {
                const std::uint32_t group = groups[i];
                const std::uint32_t old = pattern & group;
                if (!old || old == group) continue;
                std::uint32_t canonical = group;
                int remove = std::popcount(group ^ old);
                while (remove--) canonical ^= std::bit_floor(canonical);
                pattern ^= old ^ canonical;
            }
            return pattern;
        }
    };

    struct Result {
        IoeCells fixedMines;
        vector<Node> nodes;
        std::uint32_t root;
        int variables;
        // 危险：格序的全部 81 步由 build 写入；变量数组只读取前 variables 项。
        Step steps[IoeBoard::cells];
        std::uint8_t mineAt[IoeBoard::cells];
        std::uint8_t mineBit[IoeBoard::cells];
    };

    // 资源不足时返回 false。成功时 result 持有全部决策及其合法雷图数。
    bool build(const IoeConstraints::Result &constraints, Result &result, std::uint64_t &memoryLeft, std::uint64_t &visited, IoeObserver<std::uint64_t> progress);
};

struct IoeJointFilter::FrontierStep {
    enum class Cell : std::uint8_t { Passive, Number, Mine, Active, AlreadyOpen };

    struct State {
        // 低位表示紧接着要扫描的格；每项存接触格集合及其待付点击的类型。
        std::uint16_t started;
        std::uint16_t terms[12];
        std::uint8_t count;

        State() : started(0), terms{}, count(0) {}
        bool operator==(const State &other) const;
        std::size_t hash() const;
    };

    struct StateHash {
        std::size_t operator()(const State &state) const { return state.hash(); }
    };

    struct Pool {
        vector<State> states;
        FlatHashTable<State, std::uint32_t, StateHash> index;

        bool clear(std::uint64_t &memoryLeft);
    };

    struct Transition {
        std::uint32_t state;
        int cost;
    };

    struct Layer {
        std::uint16_t contacts, activations, openCenters;
        vector<std::uint32_t> transitions;
    };

    // scan 按几何顺序排列。保留的格在 relevant 中，只有 active 中的格能激活接触集合。
    bool project(const int *scan, const IoeCells &relevant, const IoeCells &active, const IoeCells &opened, Layer *layers);
    Transition next(std::uint32_t state, Cell cell, Layer &layer, Pool &pool, std::uint64_t &memoryLeft) {
        const std::size_t at = state * 5 + (int)cell;
        if (at < layer.transitions.size) {
            const std::uint32_t cached = layer.transitions[at];
            if (cached) return {(cached & ((1U << 24) - 1)) - 1, (int)(cached >> 24)};
        }
        return compute(state, cell, layer, pool, memoryLeft);
    }

  private:
    // 缓存命中只查表。合并编译后，保留未命中时的函数调用更快。
    __declspec(noinline) Transition compute(std::uint32_t state, Cell cell, Layer &layer, Pool &pool, std::uint64_t &memoryLeft);
    int advance(const State &state, std::uint16_t future, Cell cell, std::uint16_t openCenters, State &out);
};

struct IoeJointFilter::DecisionDiagram {
    struct Edge {
        std::uint32_t node; // 0 无可行雷图，1 后缀取常数。
        int value;
        bool operator==(const Edge &) const = default;
    };

    struct Node {
        Edge branches[2];
        int variable;
        int minimum;
        int maximum;
    };

    struct View {
        const vector<Node> &nodes; // 借用该图的节点，清空或重建后失效。
        Edge root;
    };

    struct Key {
        Edge low, high;
        int variable;
        bool operator==(const Key &) const = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key &key) const;
    };

    struct Pool {
        vector<Node> nodes;
        FlatHashTable<Key, std::uint32_t, KeyHash> index;
    };

    struct Build {
        Pool pool;
        FlatHashTable<Key, Edge, KeyHash> minima, limits;
        bool full;

        Build() : full(false) {}
        bool clear(std::uint64_t &memoryLeft);
    };

    Edge branch(const vector<Node> &nodes, Edge edge, int variable, bool mine) {
        const Node &node = nodes[edge.node];
        if (node.variable != variable) return edge;
        Edge next = node.branches[mine];
        if (next.node) next.value += edge.value;
        return next;
    }
    Edge choice(int variable, Edge safe, Edge mine, Build &build, std::uint64_t &memoryLeft) {
        return safe == mine ? safe : intern(variable, safe, mine, build.pool, build.full, memoryLeft);
    }
    Edge minimum(Edge a, Edge b, Build &build, std::uint64_t &memoryLeft) {
        if (!a.node) return b;
        if (!b.node) return a;
        if (a.node == b.node) return a.value <= b.value ? a : b;
        return merge(a, b, build, memoryLeft);
    }
    Edge within(Edge clicks, const vector<Node> &threeBV, Edge bv, int allowance, Weights weights, Build &build, std::uint64_t &memoryLeft) {
        if (!clicks.node || !bv.node) return {0, 0};
        const Node &d = build.pool.nodes[clicks.node], &b = threeBV[bv.node];
        const int balance = allowance + weights.bv * bv.value - weights.click * clicks.value;
        if (balance + weights.bv * b.maximum < weights.click * d.minimum) return {0, 0};
        if (balance + weights.bv * b.minimum >= weights.click * d.maximum) return clicks;
        return filter(clicks, threeBV, bv, balance, weights, build, memoryLeft);
    }

  private:
    // 分开编译时这三项从循环中调用。保留调用边界以维持百万雷图筛选性能。
    __declspec(noinline) Edge intern(int variable, Edge safe, Edge mine, Pool &pool, bool &full, std::uint64_t &memoryLeft);
    __declspec(noinline) Edge merge(Edge a, Edge b, Build &build, std::uint64_t &memoryLeft);
    __declspec(noinline) Edge filter(Edge clicks, const vector<Node> &threeBV, Edge bv, int balance, Weights weights, Build &build, std::uint64_t &memoryLeft);

};

struct IoeJointFilter::ThreeBV {
    // 成功时，root 引用 diagram.pool.nodes，清空或重建后失效。
    bool measure(const MineGraph::Result &mines, DecisionDiagram::Build &diagram, DecisionDiagram::Edge &root,
                 std::uint64_t &memoryLeft, std::uint64_t &visited, IoeObserver<std::uint64_t> progress);
};

struct IoeJointFilter::Clicks {
    struct Step { int scanIndex, variable; bool resolvesCell; };
    struct Layout {
        unsigned pattern;
        std::uint32_t legal, bv;
        bool operator==(const Layout &) const = default;
    };
    struct LayoutHash {
        std::size_t operator()(const Layout &layout) const {
            return splitmix64(((std::uint64_t)layout.legal << 22 | layout.pattern) ^ (layout.bv * 0x9e3779b97f4a7c15ULL));
        }
    };
    template <typename Word> struct Visit {
        // 已保留的预算差在 ±71 个 3BV 内。整倍门槛每个 3BV 占一个预算单位，其他门槛最多占 100 个。
        static constexpr int bias = sizeof(Word) == 4 ? 128 : 32768;
        Word word;

        Visit(std::uint32_t geometry, int balance) : word(geometry | ((Word)(balance + bias) << 24)) {}
        std::uint32_t geometry() const { return word & ((1U << 24) - 1); }
        int balance() const { return (int)(word >> 24) - bias; }
    };
    struct Move {
        std::size_t destination; // 0 无可行后缀，其余为下一层雷位编号加 1。
        std::uint16_t gain, remaining;
        FrontierStep::Cell role, chord; // chord 为 Passive 时没有双击分支。
        std::uint8_t fee;
    };
    struct Group {
        std::size_t begin, end;
        std::uint32_t bv;
        Move moves[2];
    };
    template <typename Word> struct Level {
        vector<Group> groups;
        vector<Visit<Word>> visits;
    };
    struct Source {
        std::size_t previous[2]; // 指向能到达同一雷位的上一条分支。
        bool counted;
    };
    template <typename Word> struct Answer {
        Visit<Word> visit;
        DecisionDiagram::Edge clicks;
    };
    static constexpr int capacity = 2 * IoeBoard::cells + 1;
    template <typename Word> struct History {
        Level<Word> levels[capacity];
        vector<Visit<Word>> pending;
        vector<Answer<Word>> answers[2];
    };

    template <typename Word>
    bool select(const MineGraph::Result &mines, const DecisionDiagram::View &threeBV, const IoeView &view, int spent, Weights weights,
                History<Word> &history, std::uint64_t &memoryLeft, vector<IoeLayout> &matches, std::uint64_t &visited, IoeObserver<std::uint64_t> progress);

  private:
    bool prepareMoves(const MineGraph::Result &mines, const vector<DecisionDiagram::Node> &bvNodes, const IoeView &view,
                      const Step &step, int end, int hiddenNeighbors, int bvWeight, const vector<Layout> &before, vector<Group> &groups,
                      vector<Layout> &after, vector<std::size_t> &sources, vector<Source> &origins, std::uint64_t &memoryLeft);
    template <bool resolvesCell>
    int choices(const Move &move, std::uint32_t state, int balance, int clickWeight, FrontierStep::Layer &layer, FrontierStep::Pool &frontiers,
                FrontierStep::Transition (&result)[2], std::uint64_t &memoryLeft);
    template <bool resolvesCell, typename Word>
    bool advance(const Level<Word> &from, Level<Word> &to, const vector<Layout> &layouts, const vector<std::size_t> &sources,
                 vector<Source> &origins, vector<std::size_t> &index, FrontierStep::Layer &layer, FrontierStep::Pool &frontiers,
                 vector<Visit<Word>> &pending, int clickWeight, std::uint64_t &memoryLeft, std::uint64_t &visited, IoeObserver<std::uint64_t> progress);
    template <bool resolvesCell, typename Word>
    bool buildAnswers(const Step &step, Level<Word> &from, const Level<Word> &to, const vector<Answer<Word>> &tails, vector<Answer<Word>> &heads,
                      const vector<DecisionDiagram::Node> &bvNodes, vector<std::size_t> (&index)[2], FrontierStep::Layer &layer,
                      FrontierStep::Pool &frontiers, DecisionDiagram::Build &output, Weights weights, std::uint64_t &memoryLeft,
                      std::uint64_t visited, IoeObserver<std::uint64_t> progress);
    void collect(const MineGraph::Result &mines, const vector<DecisionDiagram::Node> &bvNodes,
                 const vector<DecisionDiagram::Node> &clickNodes, int variable, std::uint32_t legal,
                 DecisionDiagram::Edge bv, DecisionDiagram::Edge clicks, IoeCells layout, vector<IoeLayout> &matches);
};

struct IoeJointFilter::Workspace {
    MineGraph::Result graph;
    DecisionDiagram::Build bv;

    struct MineBuild {
        FlatHashTable<std::uint64_t, std::uint32_t> memo[IoeBoard::cells];
        // 危险：每层只读取 levels[depth] 写入的约束和格号。
        struct Level {
            int activeCount;
            int usedCount;
            std::uint8_t active[14];
            std::uint8_t used[8];
        } levels[IoeBoard::cells];
    } mineBuild;

    struct BV {
        FlatHashTable<std::uint64_t, DecisionDiagram::Edge> memo[2 * IoeBoard::cells];
        FrontierStep::Layer layers[IoeBoard::cells];
        FrontierStep::Pool frontiers;
    } threeBV;

    struct ClickSearch {
        Clicks::History<std::uint32_t> compact;
        Clicks::History<std::uint64_t> wide;
        vector<Clicks::Layout> layouts[2];
        FlatHashTable<Clicks::Layout, std::size_t, Clicks::LayoutHash> layoutIndex;
        vector<std::size_t> sources, index[2];
        vector<Clicks::Source> origins;
        DecisionDiagram::Build output;
        FrontierStep::Layer layers[IoeBoard::cells];
        FrontierStep::Pool frontiers;
    } clicks;

    template <typename Table> std::uint64_t releaseTable(Table &table) {
        const std::uint64_t bytes = table.allocatedBytes();
        table = {};
        return bytes;
    }
    void release();
};

void IoeJointFilter::Workspace::release() {
    this->~Workspace();
    new (this) Workspace;
}

__declspec(noinline) IoeJointFilter::Workspace &IoeJointFilter::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

std::size_t IoeJointFilter::select(const IoeConstraints::Result &constraints, const IoeView &view, int spent, int minIoePercent,
                                    std::uint64_t memoryBytes, vector<IoeLayout> &matches, std::uint64_t &visited, IoeObserver<std::uint64_t> progress) {
    Workspace &workspace = getWorkspace();
    if (!memoryBytes) return 0;
    std::uint64_t memoryLeft = std::min<std::uint64_t>(memoryBytes, std::numeric_limits<std::size_t>::max());
    MineGraph::Result &graph = workspace.graph;
    DecisionDiagram::Build &bv = workspace.bv;
    DecisionDiagram::Edge root;
    const int percent = std::min(minIoePercent, 100 * (IoeBoard::cells - IoeBoard::mines) + 1);
    const int divisor = std::gcd(percent, 100);
    const Weights weights{100 / divisor, percent / divisor};
    auto measure = [&]() {
        if (!MineGraph().build(constraints, graph, memoryLeft, visited, progress)) return false;
        progress(visited);
        for (FlatHashTable<std::uint64_t, std::uint32_t> &table : workspace.mineBuild.memo) memoryLeft += workspace.releaseTable(table);
        if (!ThreeBV().measure(graph, bv, root, memoryLeft, visited, progress)) return false;
        progress(visited);
        if (weights.bv == 1)
            return Clicks().select(graph, {bv.pool.nodes, root}, view, spent, weights, workspace.clicks.compact, memoryLeft, matches, visited, progress);
        return Clicks().select(graph, {bv.pool.nodes, root}, view, spent, weights, workspace.clicks.wide, memoryLeft, matches, visited, progress);
    };
    std::size_t legal;
    {
        struct Cleanup {
            Workspace &workspace;
            ~Cleanup() { workspace.release(); }
        } cleanup{workspace};
        legal = measure() ? graph.nodes[graph.root].count : 0;
    }
    if (legal) IoeConstraints().sort(constraints, matches);
    return legal;
}

bool IoeJointFilter::MineGraph::build(const IoeConstraints::Result &constraints, Result &result, std::uint64_t &memoryLeft, std::uint64_t &visited, IoeObserver<std::uint64_t> progress) {
    constexpr int cells = IoeBoard::cells, side = IoeBoard::side;
    constexpr int radius = side + 1;
    const IoeCells *adjacent = IoeBoard().neighbors();
    Workspace &workspace = IoeJointFilter().getWorkspace();
    FlatHashTable<std::uint64_t, std::uint32_t> (&memo)[cells] = workspace.mineBuild.memo;
    Workspace::MineBuild::Level (&levels)[cells] = workspace.mineBuild.levels;
    result.fixedMines = constraints.fixedMines;
    result.variables = constraints.cellCount;
    struct Order {
        int width;
        int work;
        std::uint8_t cell[cells];
        std::uint8_t rank[cells];
        std::uint8_t first[cells];
        std::uint8_t last[cells];
    } orders[8];
    for (int direction = 0; direction < 8; ++direction) {
        Order &order = orders[direction];
        for (int i = 0; i < cells; ++i) {
            int row = i / side, col = i % side;
            if (direction & 4) std::swap(row, col);
            if (direction & 1) row = side - 1 - row;
            if (direction & 2) col = side - 1 - col;
            order.cell[i] = row * side + col;
            order.rank[order.cell[i]] = i;
        }
        int active[cells]{};
        for (int position = 0; position < cells; ++position) {
            const int cell = order.cell[position];
            int first = position, last = position;
            adjacent[cell].forEachSetBit([&](int other) {
                if (constraints.digits[other] < 9 || constraints.fixedMines.test(other)) return;
                first = std::min(first, (int)order.rank[other]);
                last = std::max(last, (int)order.rank[other]);
            });
            order.first[position] = first;
            order.last[position] = last;
            if (constraints.unknown.test(cell))
                for (int step = first; step <= last; ++step) ++active[step];
        }
        order.width = 0;
        order.work = 0;
        for (int n : active) {
            order.width = std::max(order.width, n);
            order.work += n * n;
        }
    }

    int directions[8]{0, 1, 2, 3, 4, 5, 6, 7};
    std::sort(directions, directions + 8, [&](int a, int b) {
        if (orders[a].width != orders[b].width) return orders[a].width < orders[b].width;
        if (orders[a].work != orders[b].work) return orders[a].work < orders[b].work;
        return a < b;
    });

    for (int direction : directions) {
        const Order &order = orders[direction];
        int unknown[cells], n = 0;
        for (int at = 0; at < cells; ++at)
            if (constraints.unknown.test(order.cell[at])) unknown[n++] = at;
        std::sort(unknown, unknown + n, [&](int a, int b) {
            return order.first[a] != order.first[b] ? order.first[a] < order.first[b] : a < b;
        });
        for (int i = 0, v = 0; i < cells; ++i) {
            Step &step = result.steps[i];
            step = {};
            step.cell = order.cell[i];
            step.digit = constraints.digits[step.cell];
            adjacent[step.cell].forEachSetBit([&](int other) {
                const int distance = order.rank[other] - i;
                step.neighbors |= 1U << (radius + distance);
            });
            for (int j = std::max(0, i - radius); j <= std::min(cells - 1, i + radius); ++j) {
                const int bit = radius + j - i;
                if (result.fixedMines.test(order.cell[j])) step.fixedMines |= 1U << bit;
                if (order.last[j] >= i && constraints.unknown.test(order.cell[j])) step.live |= 1U << bit;
            }
            std::uint16_t uses[radius]{};
            for (int j = std::max(0, i - radius); j < i; ++j) {
                const int bit = radius + j - i;
                if (!(step.live & (1U << bit))) continue;
                adjacent[order.cell[j]].forEachSetBit([&](int other) {
                    const int pos = order.rank[other];
                    if (pos >= i && constraints.digits[other] == 9 && !constraints.fixedMines.test(other)) uses[bit] |= 1U << (pos - i);
                });
            }
            for (int bit = 0; bit < radius; ++bit) {
                if (!uses[bit]) continue;
                unsigned group = 1U << bit;
                for (int other = bit + 1; other < radius; ++other) {
                    if (uses[other] != uses[bit]) continue;
                    group |= 1U << other;
                    uses[other] = 0;
                }
                if ((group & (group - 1)) != 0) step.groups[step.groupCount++] = group;
            }
            for (; v < n && order.first[unknown[v]] == i; ++v) {
                result.mineAt[v] = order.cell[unknown[v]];
                result.mineBit[v] = radius + unknown[v] - i;
            }
            step.end = v;
        }

        int degree[cells]{}, demand[cells], remaining[cells]{};
        std::uint8_t feasible[cells]{};
        bool wide = false;
        for (int i = 0; i < n; ++i) {
            memo[i].clear();
            levels[i].activeCount = levels[i].usedCount = 0;
        }
        for (int e = 0; e < constraints.equationCount; ++e) {
            const IoeConstraints::Equation &equation = constraints.equations[e];
            demand[e] = equation.demand;
            int first = n, last = 0;
            for (int i = 0; i < n; ++i) {
                if (!equation.unknown.test(result.mineAt[i])) continue;
                first = std::min(first, i);
                last = i;
                ++remaining[e];
                levels[i].used[levels[i].usedCount++] = e;
            }
            for (int i = first + 1; i <= last; ++i) {
                if (degree[i] == 14) { wide = true; break; }
                levels[i].active[degree[i]++] = e;
            }
            if (wide) break;
        }
        if (wide) continue;
        for (int i = 0; i < n; ++i) levels[i].activeCount = degree[i];

        vector<Node> &nodes = result.nodes;
        if (!nodes.resize(2, memoryLeft)) return false;
        nodes[0] = {{0, 0}, 0};
        nodes[1] = {{0, 0}, 1};
        std::uint64_t saved = 0;
        bool full = false;
        auto add = [&](auto &self, int depth, int minesLeft) -> std::uint32_t {
            if (full) return 0;
            if (depth == n) return 1;
            const Workspace::MineBuild::Level &level = levels[depth];
            std::uint64_t key = minesLeft;
            for (int i = 0; i < level.activeCount; ++i) key = (key << 4) | demand[level.active[i]];
            if (const std::uint32_t *found = memo[depth].find(key)) return *found;

            const int left = n - depth - 1;
            bool canBeSafe = minesLeft <= left, canBeMine = minesLeft > 0;
            for (int i = 0; i < level.usedCount; ++i) {
                const int e = level.used[i];
                canBeSafe &= --remaining[e] >= demand[e];
                canBeMine &= demand[e] > 0;
            }
            const std::uint32_t safe = canBeSafe ? self(self, depth + 1, minesLeft) : 0;
            std::uint32_t mine = 0;
            if (canBeMine) {
                for (int i = 0; i < level.usedCount; ++i) --demand[level.used[i]];
                mine = self(self, depth + 1, minesLeft - 1);
                for (int i = 0; i < level.usedCount; ++i) ++demand[level.used[i]];
            }
            for (int i = 0; i < level.usedCount; ++i) ++remaining[level.used[i]];
            if (full) return 0;
            std::uint32_t id = 0;
            if (safe || mine) {
                id = nodes.size;
                if (id > maxNode || !nodes.push_back({{safe, mine}, nodes[safe].count + nodes[mine].count}, memoryLeft)) {
                    full = true;
                    return 0;
                }
                feasible[depth] |= (safe ? 1 : 0) | (mine ? 2 : 0);
            }
            if (!(++saved & 32767)) progress(visited + saved);
            if (!memo[depth].emplace(key, id, memoryLeft)) full = true;
            return id;
        };
        result.root = add(add, 0, constraints.remainingMines);
        visited += saved;
        if (full || !result.root) continue;

        IoeCells fixed, newMines;
        for (int i = 0; i < n; ++i) {
            if (feasible[i] == 3) continue;
            const int cell = result.mineAt[i];
            fixed.set(cell);
            if (feasible[i] == 2) newMines.set(cell);
        }
        if (!fixed.any()) return true;

        // 全体合法雷图都同值的格，只参与后续几何计算。
        IoeConstraints::Result reduced;
        reduced.fixedMines = constraints.fixedMines | newMines;
        reduced.unknown = constraints.unknown & ~fixed;
        reduced.remainingMines = constraints.remainingMines - newMines.popcount();
        reduced.cellCount = reduced.equationCount = 0;
        std::copy_n(constraints.digits, cells, reduced.digits);
        for (int i = 0; i < constraints.cellCount; ++i)
            if (!fixed.test(constraints.cells[i])) reduced.cells[reduced.cellCount++] = constraints.cells[i];
        for (int i = 0; i < constraints.equationCount; ++i) {
            const IoeConstraints::Equation &equation = constraints.equations[i];
            const IoeCells remaining = equation.unknown & ~fixed;
            if (remaining.any())
                reduced.equations[reduced.equationCount++] = {remaining, (std::uint8_t)(equation.demand - (equation.unknown & newMines).popcount())};
        }
        return build(reduced, result, memoryLeft, visited, progress);
    }
    return false;
}

bool IoeJointFilter::ThreeBV::measure(const MineGraph::Result &mines, DecisionDiagram::Build &diagram, DecisionDiagram::Edge &root,
                                    std::uint64_t &memoryLeft, std::uint64_t &visited, IoeObserver<std::uint64_t> progress) {
    constexpr int cells = IoeBoard::cells;
    constexpr unsigned current = 1U << (IoeBoard::side + 1);
    using Edge = DecisionDiagram::Edge;
    using Cell = FrontierStep::Cell;
    Workspace &workspace = IoeJointFilter().getWorkspace();
    FlatHashTable<std::uint64_t, Edge> (&memo)[2 * cells] = workspace.threeBV.memo;
    for (FlatHashTable<std::uint64_t, Edge> &level : memo) level.clear();
    FrontierStep::Layer (&layers)[cells] = workspace.threeBV.layers;
    FrontierStep::Pool &frontiers = workspace.threeBV.frontiers;
    if (!frontiers.clear(memoryLeft)) return false;
    IoeCells shown, shownZeros, alreadyCovered, relevant, active;
    int scan[cells];
    const IoeCells *adjacent = IoeBoard().neighbors();
    for (int i = 0; i < cells; ++i) {
        const MineGraph::Step &step = mines.steps[i];
        scan[i] = step.cell;
        if (step.digit < 9) shown.set(step.cell);
        if (!step.digit) {
            shownZeros.set(step.cell);
            alreadyCovered |= adjacent[step.cell];
        }
    }
    const bool partial = (alreadyCovered & ~shown).any();
    int prepaid = 0;
    if (!partial) {
        IoeCells pending = shownZeros;
        while (pending.any()) {
            ++prepaid;
            IoeCells busy;
            busy.set(pending.firstSetBit());
            do {
                pending &= ~busy;
                IoeCells touching;
                busy.forEachSetBit([&](int cell) { touching |= adjacent[cell]; });
                busy = pending & touching;
            } while (busy.any());
        }
    }
    for (int i = 0; i < cells; ++i) {
        const MineGraph::Step &step = mines.steps[i];
        if ((step.fixedMines & current) || (!partial && !step.digit)) continue;
        if (step.digit > 0 && step.digit < 9) {
            if (alreadyCovered.test(step.cell)) continue;
            if ((adjacent[step.cell] & ~shown).popcount() == step.digit) {
                ++prepaid;
                continue;
            }
        }
        relevant.set(step.cell);
        if (!step.digit || (step.digit == 9 && !(step.fixedMines & step.neighbors))) active.set(step.cell);
    }
    FrontierStep geometry;
    if (!geometry.project(scan, relevant, active, {}, layers)) return false;
    if (!diagram.clear(memoryLeft)) return false;
    DecisionDiagram values;
    bool failed = false;
    std::uint64_t states = 0;
    auto measure = [&](auto &self, int at, int variable, unsigned pattern, std::uint32_t legal, std::uint32_t frontier) -> Edge {
        if (!legal || failed || diagram.full) return {0, 0};
        if (at == cells) return {1, 0};
        const MineGraph::Step &step = mines.steps[at];
        const unsigned carry = variable == step.end && at + 1 < cells ? mines.steps[at + 1].project(pattern) : 0;
        if (variable == step.end && !relevant.test(step.cell)) return self(self, at + 1, variable, carry, legal, frontier);
        const std::uint64_t key = ((std::uint64_t)frontier << 40) | ((std::uint64_t)legal << 21) | pattern;
        FlatHashTable<std::uint64_t, Edge> &layer = memo[at + variable];
        if (const Edge *cached = layer.find(key)) return *cached;
        if (!(++states & 32767)) progress(visited + states);

        Edge result;
        if (variable < step.end) {
            const MineGraph::Node &node = mines.nodes[legal];
            const Edge safe = self(self, at, variable + 1, pattern, node.next[0], frontier);
            const Edge mine = self(self, at, variable + 1, pattern | (1U << mines.mineBit[variable]), node.next[1], frontier);
            if (failed || diagram.full) return {0, 0};
            result = values.choice(variable, safe, mine, diagram, memoryLeft);
        } else {
            const unsigned bits = pattern | step.fixedMines;
            const Cell cell = bits & current ? Cell::Passive
                : step.digit < 9 ? (step.digit ? Cell::Number : Cell::Active)
                : bits & step.neighbors ? Cell::Number : Cell::Active;
            const FrontierStep::Transition stepResult = geometry.next(frontier, cell, layers[at], frontiers, memoryLeft);
            if (stepResult.cost < 0) {
                failed = true;
                return {0, 0};
            }
            result = self(self, at + 1, variable, carry, legal, stepResult.state);
            if (result.node) result.value += stepResult.cost;
        }
        if (!failed && !diagram.full && !layer.emplace(key, result, memoryLeft)) failed = true;
        return result;
    };

    root = measure(measure, 0, 0, 0, mines.root, 0);
    visited += states;
    if (failed || diagram.full) return false;
    root.value += prepaid;
    // 后续最少点击数计算只读 3BV 的节点和根边，建图所占的临时内存归还同一预算。
    for (FlatHashTable<std::uint64_t, Edge> &table : memo) memoryLeft += workspace.releaseTable(table);
    for (FrontierStep::Layer &layer : layers) memoryLeft += layer.transitions.release();
    memoryLeft += frontiers.states.release() + workspace.releaseTable(frontiers.index) + workspace.releaseTable(diagram.pool.index);
    return true;
}

template <typename Word>
bool IoeJointFilter::Clicks::select(const MineGraph::Result &mines, const DecisionDiagram::View &threeBV, const IoeView &view, int spent, Weights weights,
                                    History<Word> &history, std::uint64_t &memoryLeft, vector<IoeLayout> &matches, std::uint64_t &visited,
                                    IoeObserver<std::uint64_t> progress) {
    constexpr int cells = IoeBoard::cells;
    constexpr unsigned current = 1U << (IoeBoard::side + 1);
    Workspace &workspace = IoeJointFilter().getWorkspace();
    DecisionDiagram::Build &output = workspace.clicks.output;
    FrontierStep::Layer (&layers)[cells] = workspace.clicks.layers;
    FrontierStep::Pool &frontiers = workspace.clicks.frontiers;
    if (!frontiers.clear(memoryLeft) || !output.clear(memoryLeft)) return false;
    const vector<DecisionDiagram::Node> &bvNodes = threeBV.nodes;
    if (weights.bv * (threeBV.root.value + bvNodes[threeBV.root.node].maximum) < weights.click * spent) return true;

    int scan[cells], hiddenNeighbors[cells];
    IoeCells active, relevant;
    for (int i = 0; i < cells; ++i) {
        const MineGraph::Step &step = mines.steps[i];
        scan[i] = step.cell;
        hiddenNeighbors[i] = (IoeBoard().neighbors()[step.cell] & ~view.revealed).popcount();
        const bool revealed = step.digit < 9;
        const int fixedNear = std::popcount(step.fixedMines & step.neighbors);
        const bool mayActivate = revealed ? step.digit && hiddenNeighbors[i] - step.digit >= 2
            : !(step.fixedMines & current) && (!fixedNear || hiddenNeighbors[i] - fixedNear >= 2);
        if (mayActivate) active.set(step.cell);
        if (revealed ? mayActivate : !(view.flags.test(step.cell) && (step.fixedMines & current))) relevant.set(step.cell);
    }
    FrontierStep geometry;
    if (!geometry.project(scan, relevant, active, view.revealed, layers)) return false;

    // 若最后一颗雷位确定后即可判断当前格，同一层结算两类转移。
    Step steps[capacity];
    int count = 0;
    for (int i = 0, variable = 0; i < cells; ++i) {
        const bool resolves = relevant.test(scan[i]);
        const bool hasNewMine = variable < mines.steps[i].end;
        for (; variable < mines.steps[i].end; ++variable)
            steps[count++] = {i, variable, resolves && variable + 1 == mines.steps[i].end};
        if (resolves && !hasNewMine) steps[count++] = {i, -1, true};
    }
    steps[count] = {cells, -1, false};

    Level<Word> (&levels)[capacity] = history.levels;
    vector<Visit<Word>> &pending = history.pending;
    vector<Layout> (&layouts)[2] = workspace.clicks.layouts;
    vector<std::size_t> (&index)[2] = workspace.clicks.index;
    vector<std::size_t> &sources = workspace.clicks.sources;
    vector<Source> &origins = workspace.clicks.origins;
    if (!layouts[0].push_back({0, mines.root, threeBV.root.node}, memoryLeft)
        || !levels[0].groups.push_back({0, 1, threeBV.root.node, {}}, memoryLeft)
        || !levels[0].visits.push_back({0, weights.bv * threeBV.root.value - weights.click * spent}, memoryLeft)) return false;
    for (int i = 0; i < count; ++i) {
        const Step step = steps[i];
        Level<Word> &from = levels[i], &to = levels[i + 1];
        vector<Layout> &after = layouts[(i + 1) & 1];
        const int end = std::min(steps[i + 1].scanIndex, cells - 1);
        if (!prepareMoves(mines, bvNodes, view, step, end, hiddenNeighbors[step.scanIndex], weights.bv, layouts[i & 1], from.groups,
                          after, sources, origins, memoryLeft)) return false;
        FrontierStep::Layer &layer = layers[step.scanIndex];
        const bool advanced = step.resolvesCell
            ? advance<true>(from, to, after, sources, origins, index[0], layer, frontiers, pending, weights.click, memoryLeft, visited, progress)
            : advance<false>(from, to, after, sources, origins, index[0], layer, frontiers, pending, weights.click, memoryLeft, visited, progress);
        if (!advanced) return false;
        // 这个格的点法已全部向前推过。倒推只为仍能达标的雷位重新生成走法。
        if (step.resolvesCell) memoryLeft += layer.transitions.release();
        progress(visited);
        if (!to.visits.size) return true;
    }
    memoryLeft += layouts[0].release() + layouts[1].release() + sources.release() + origins.release() + pending.release()
        + workspace.releaseTable(workspace.clicks.layoutIndex);

    vector<Answer<Word>> (&answers)[2] = history.answers;
    Level<Word> &last = levels[count];
    vector<Answer<Word>> &terminal = answers[count & 1];
    if (!terminal.resize(last.visits.size, memoryLeft)) return false;
    for (std::size_t i = 0; i < last.visits.size; ++i) terminal[i] = {last.visits[i], {1, 0}};
    memoryLeft += last.visits.release();
    for (int i = count - 1; i >= 0; --i) {
        const Step step = steps[i];
        Level<Word> &from = levels[i], &to = levels[i + 1];
        const vector<Answer<Word>> &tails = answers[(i + 1) & 1];
        vector<Answer<Word>> &heads = answers[i & 1];
        FrontierStep::Layer &layer = layers[step.scanIndex];
        const bool built = step.resolvesCell
            ? buildAnswers<true>(step, from, to, tails, heads, bvNodes, index, layer, frontiers, output, weights, memoryLeft, visited, progress)
            : buildAnswers<false>(step, from, to, tails, heads, bvNodes, index, layer, frontiers, output, weights, memoryLeft, visited, progress);
        if (!built) return false;
        memoryLeft += from.visits.release() + to.groups.release();
        if (step.resolvesCell) memoryLeft += layer.transitions.release();
        progress(visited);
        if (!heads.size) return true;
    }
    collect(mines, bvNodes, output.pool.nodes, 0, mines.root, threeBV.root, answers[0][0].clicks, mines.fixedMines, matches);
    return true;
}

bool IoeJointFilter::Clicks::prepareMoves(const MineGraph::Result &mines, const vector<DecisionDiagram::Node> &bvNodes, const IoeView &view,
                                         const Step &step, int end, int hiddenNeighbors, int bvWeight, const vector<Layout> &before, vector<Group> &groups,
                                         vector<Layout> &after, vector<std::size_t> &sources, vector<Source> &origins, std::uint64_t &memoryLeft) {
    constexpr unsigned current = 1U << (IoeBoard::side + 1);
    const MineGraph::Step &placement = mines.steps[step.scanIndex];
    const bool shown = placement.digit < 9, flagged = view.flags.test(placement.cell);
    Workspace &workspace = IoeJointFilter().getWorkspace();
    FlatHashTable<Layout, std::size_t, LayoutHash> &layoutIndex = workspace.clicks.layoutIndex;
    after.size = sources.size = 0;
    layoutIndex.clear();
    if (!origins.resize(before.size, memoryLeft)) return false;
    DecisionDiagram values;
    for (std::size_t id = 0; id < before.size; ++id) {
        Group &group = groups[id];
        if (group.begin == group.end) continue;
        origins[id].counted = false;
        const Layout &layout = before[id];
        const int branches = step.variable >= 0 ? 2 : 1;
        for (int bit = 0; bit < branches; ++bit) {
            Move &move = group.moves[bit];
            move = {};
            Layout next = layout;
            if (step.variable >= 0) {
                next.legal = mines.nodes[layout.legal].next[bit];
                if (!next.legal) continue;
                const DecisionDiagram::Edge bv = values.branch(bvNodes, {layout.bv, 0}, step.variable, bit);
                next.bv = bv.node;
                if (!next.bv) continue;
                move.gain = bvWeight * bv.value;
                next.pattern |= (unsigned)bit << mines.mineBit[step.variable];
            }
            move.remaining = bvWeight * bvNodes[next.bv].maximum;
            if (step.resolvesCell) {
                const unsigned bits = next.pattern | placement.fixedMines;
                const int digit = shown ? placement.digit : std::popcount(bits & placement.neighbors);
                const bool mine = bits & current;
                move.fee = flagged && !mine;
                if (!mine && digit && hiddenNeighbors - digit >= 2)
                    move.chord = shown ? FrontierStep::Cell::AlreadyOpen : FrontierStep::Cell::Active;
                move.role = mine ? (flagged ? FrontierStep::Cell::Passive : FrontierStep::Cell::Mine)
                    : shown ? FrontierStep::Cell::Passive : digit ? FrontierStep::Cell::Number : FrontierStep::Cell::Active;
            }
            for (int at = step.scanIndex + 1; at <= end; ++at) next.pattern = mines.steps[at].project(next.pattern);
            std::size_t *destination = layoutIndex.getOrInsert(next, memoryLeft);
            if (!destination) return false;
            if (!*destination) {
                *destination = after.size + 1;
                if (!after.push_back(next, memoryLeft) || !sources.push_back(0, memoryLeft)) return false;
            }
            move.destination = *destination;
            std::size_t &first = sources[*destination - 1];
            origins[id].previous[bit] = first;
            first = 2 * id + bit + 1;
        }
    }
    return true;
}

template <bool resolvesCell>
int IoeJointFilter::Clicks::choices(const Move &move, std::uint32_t state, int balance, int clickWeight, FrontierStep::Layer &layer, FrontierStep::Pool &frontiers,
                                    FrontierStep::Transition (&result)[2], std::uint64_t &memoryLeft) {
    const int available = balance + move.remaining;
    if (available < 0) return 0;
    if constexpr (!resolvesCell) {
        result[0] = {state, 0};
        return 1;
    }
    FrontierStep geometry;
    result[0] = geometry.next(state, move.role, layer, frontiers, memoryLeft);
    if (result[0].cost < 0) return -1;
    int count = 1;
    if (move.chord != FrontierStep::Cell::Passive && available >= clickWeight * (1 + move.fee)) {
        FrontierStep::Transition take = geometry.next(state, move.chord, layer, frontiers, memoryLeft);
        if (take.cost < 0) return -1;
        ++take.cost;
        if (result[0].state == take.state) result[0].cost = std::min(result[0].cost, take.cost);
        else result[count++] = take;
    }
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        const int charge = result[i].cost + move.fee;
        if (clickWeight * charge <= available) result[kept++] = {result[i].state, charge};
    }
    return kept;
}

template <bool resolvesCell, typename Word>
bool IoeJointFilter::Clicks::advance(const Level<Word> &from, Level<Word> &to, const vector<Layout> &layouts, const vector<std::size_t> &sources,
                                    vector<Source> &origins, vector<std::size_t> &index, FrontierStep::Layer &layer, FrontierStep::Pool &frontiers,
                                    vector<Visit<Word>> &pending, int clickWeight, std::uint64_t &memoryLeft, std::uint64_t &visited,
                                    IoeObserver<std::uint64_t> progress) {
    pending.size = 0;
    if (!to.groups.resize(layouts.size, memoryLeft)) return false;
    std::fill_n(index.data, index.size, 0);
    for (std::size_t id = 0; id < layouts.size; ++id) {
        const std::size_t begin = pending.size;
        for (std::size_t link = sources[id]; link; ) {
            const std::size_t source = (link - 1) / 2;
            const int bit = (link - 1) & 1;
            const Group &group = from.groups[source];
            const Move &move = group.moves[bit];
            Source &origin = origins[source];
            link = origin.previous[bit];
            // 同一个前驱可以分向两种雷位；进度只在第一次处理它时计数。
            const bool count = !origin.counted;
            origin.counted = true;
            for (std::size_t at = group.begin; at < group.end; ++at) {
                if (!(at & 32767)) progress(visited);
                const Visit<Word> visit = from.visits[at];
                const int balance = visit.balance() + move.gain;
                FrontierStep::Transition next[2];
                const int n = choices<resolvesCell>(move, visit.geometry(), balance, clickWeight, layer, frontiers, next, memoryLeft);
                if (n < 0) return false;
                visited += count;
                for (int i = 0; i < n; ++i) {
                    const std::uint32_t state = next[i].state;
                    if (state >= index.size) {
                        const std::size_t old = index.size;
                        if (!index.resize(frontiers.states.size, memoryLeft)) return false;
                        std::fill(index.data + old, index.data + index.size, 0);
                    }
                    const int remaining = balance - clickWeight * next[i].cost;
                    std::size_t &slot = index[state];
                    // 当前雷位独占 [begin, end)，前一个雷位写下的编号均已过期。
                    if (slot <= begin) {
                        if (!pending.push_back({state, remaining}, memoryLeft)) return false;
                        slot = pending.size;
                    } else if (remaining > pending[slot - 1].balance()) pending[slot - 1] = {state, remaining};
                }
            }
        }
        to.groups[id] = {begin, pending.size, layouts[id].bv, {}};
    }
    // 各层共用扩容缓冲。留给倒推的数组按本层实际到达的状态数分配。
    if (!to.visits.resize(pending.size, memoryLeft)) return false;
    std::copy_n(pending.data, pending.size, to.visits.data);
    return true;
}

template <bool resolvesCell, typename Word>
bool IoeJointFilter::Clicks::buildAnswers(const Step &step, Level<Word> &from, const Level<Word> &to, const vector<Answer<Word>> &tails, vector<Answer<Word>> &heads,
                                         const vector<DecisionDiagram::Node> &bvNodes, vector<std::size_t> (&index)[2], FrontierStep::Layer &layer,
                                         FrontierStep::Pool &frontiers, DecisionDiagram::Build &output, Weights weights, std::uint64_t &memoryLeft,
                                         std::uint64_t visited, IoeObserver<std::uint64_t> progress) {
    const int clickWeight = weights.click;
    DecisionDiagram values;
    const int branches = step.variable < 0 ? 1 : 2;
    for (int bit = 0; bit < branches; ++bit) {
        if (!index[bit].resize(frontiers.states.size, memoryLeft)) return false;
        std::fill_n(index[bit].data, index[bit].size, 0);
    }
    heads.size = 0;
    for (std::size_t id = 0; id < from.groups.size; ++id) {
        Group &group = from.groups[id];
        const Group *destinations[2]{};
        for (int bit = 0; bit < branches; ++bit) {
            const std::size_t destination = group.moves[bit].destination;
            if (!destination) continue;
            const Group &target = to.groups[destination - 1];
            if (target.begin == target.end) continue;
            destinations[bit] = &target;
            for (std::size_t i = target.begin; i < target.end; ++i) index[bit][tails[i].visit.geometry()] = i + 1;
        }
        const std::size_t begin = heads.size;
        if (destinations[0] || destinations[1]) {
            for (std::size_t at = group.begin; at < group.end; ++at) {
                if (!(at & 32767)) progress(visited);
                const Visit<Word> visit = from.visits[at];
                DecisionDiagram::Edge edges[2]{};
                for (int bit = 0; bit < branches; ++bit) {
                    if (!destinations[bit]) continue;
                    const Group &target = *destinations[bit];
                    const Move &move = group.moves[bit];
                    const int balance = visit.balance() + move.gain;
                    FrontierStep::Transition next[2];
                    const int n = choices<resolvesCell>(move, visit.geometry(), balance, clickWeight, layer, frontiers, next, memoryLeft);
                    if (n < 0) return false;
                    for (int i = 0; i < n; ++i) {
                        const std::size_t slot = index[bit][next[i].state];
                        if (slot <= target.begin || slot > target.end) continue;
                        const Answer<Word> &answer = tails[slot - 1];
                        DecisionDiagram::Edge edge = answer.clicks;
                        const int remaining = balance - clickWeight * next[i].cost;
                        if (remaining < answer.visit.balance()) edge = values.within(edge, bvNodes, {target.bv, 0}, remaining, weights, output, memoryLeft);
                        if (edge.node) {
                            edge.value += next[i].cost;
                            edges[bit] = values.minimum(edges[bit], edge, output, memoryLeft);
                        }
                    }
                }
                const DecisionDiagram::Edge edge = step.variable < 0 ? edges[0] : values.choice(step.variable, edges[0], edges[1], output, memoryLeft);
                if (output.full || (edge.node && !heads.push_back({visit, edge}, memoryLeft))) return false;
            }
        }
        // 前一层只查询能达标的后续。倒推经过这里后，这段改为指向留下的答案。
        group.begin = begin;
        group.end = heads.size;
    }
    return true;
}

void IoeJointFilter::Clicks::collect(const MineGraph::Result &mines, const vector<DecisionDiagram::Node> &bvNodes,
                                    const vector<DecisionDiagram::Node> &clickNodes, int variable, std::uint32_t legal,
                                    DecisionDiagram::Edge bv, DecisionDiagram::Edge clicks, IoeCells layout, vector<IoeLayout> &matches) {
    if (!legal || !clicks.node) return;
    if (variable == mines.variables) {
        matches.push_back({layout, (std::uint8_t)bv.value, (std::uint8_t)clicks.value});
        return;
    }
    const int cell = mines.mineAt[variable];
    const MineGraph::Node &node = mines.nodes[legal];
    DecisionDiagram values;
    collect(mines, bvNodes, clickNodes, variable + 1, node.next[0], values.branch(bvNodes, bv, variable, false), values.branch(clickNodes, clicks, variable, false), layout, matches);
    layout.set(cell);
    collect(mines, bvNodes, clickNodes, variable + 1, node.next[1], values.branch(bvNodes, bv, variable, true), values.branch(clickNodes, clicks, variable, true), layout, matches);
}

bool IoeJointFilter::FrontierStep::State::operator==(const State &other) const {
    return started == other.started && count == other.count && std::equal(terms, terms + count, other.terms);
}

std::size_t IoeJointFilter::FrontierStep::State::hash() const {
    std::uint64_t value = started;
    for (int i = 0; i < count; ++i) value = (value ^ terms[i]) * 0x9e3779b185ebca87ULL;
    return value ^ (value >> 32);
}

bool IoeJointFilter::FrontierStep::Pool::clear(std::uint64_t &memoryLeft) {
    if (!states.resize(1, memoryLeft)) return false;
    states[0] = {};
    index.clear();
    return index.emplace(states[0], 0, memoryLeft);
}

bool IoeJointFilter::FrontierStep::project(const int *scan, const IoeCells &relevant, const IoeCells &active, const IoeCells &opened, Layer *layers) {
    constexpr int n = IoeBoard::cells;
    int rank[n];
    int count = 0;
    for (int i = 0; i < n; ++i)
        if (relevant.test(scan[i])) rank[scan[i]] = count++;
    const IoeCells *adjacent = IoeBoard().neighbors();
    for (int i = 0; i < n; ++i) {
        Layer &layer = layers[i];
        layer.transitions.size = 0;
        if (!relevant.test(scan[i])) continue;
        layer.contacts = layer.activations = layer.openCenters = 0;
        const int at = rank[scan[i]];
        bool fits = true;
        (adjacent[scan[i]] & relevant).forEachSetBit([&](int cell) {
            const int offset = rank[cell] - at - 1;
            if (offset < 0) return;
            if (offset >= 10) { fits = false; return; }
            layer.contacts |= 1U << offset;
            if (active.test(cell)) layer.activations |= 1U << offset;
        });
        if (!fits) return false;
        (opened & relevant).forEachSetBit([&](int cell) {
            const int offset = rank[cell] - at - 1;
            if (offset >= 0 && offset < 10) layer.openCenters |= 1U << offset;
        });
    }
    return true;
}

IoeJointFilter::FrontierStep::Transition IoeJointFilter::FrontierStep::compute(std::uint32_t id, Cell cell, Layer &layer, Pool &pool,
                                                                            std::uint64_t &memoryLeft) {
    const bool active = cell == Cell::Active || cell == Cell::AlreadyOpen;
    State out;
    const int cost = advance(pool.states[id], active ? layer.contacts : layer.activations, cell, layer.openCenters, out);
    if (cost < 0) return {0, -1};
    std::uint32_t next;
    if (const std::uint32_t *known = pool.index.find(out)) next = *known;
    else {
        next = pool.states.size;
        // 转移记录的低 24 位保存几何状态编号加 1。
        if (next >= (1U << 24) - 1 || !pool.states.push_back(out, memoryLeft) || !pool.index.emplace(out, next, memoryLeft)) return {0, -1};
    }
    vector<std::uint32_t> &transitions = layer.transitions;
    const std::size_t at = id * 5 + (int)cell;
    if (at >= transitions.size) {
        const std::size_t start = transitions.size;
        if (!transitions.resize(at + 1, memoryLeft)) return {0, -1};
        std::fill(transitions.data + start, transitions.data + transitions.size, 0);
    }
    transitions[at] = (next + 1) | ((unsigned)cost << 24);
    return {next, cost};
}

int IoeJointFilter::FrontierStep::advance(const State &state, std::uint16_t future, Cell cell, std::uint16_t openCenters, State &out) {
    constexpr int contactMask = 1023;
    constexpr int number = 1024;
    constexpr int mine = 2048;
    const bool active = cell == Cell::Active || cell == Cell::AlreadyOpen;
    const bool started = state.started & 1;
    bool touched = started;
    std::uint16_t reachable = state.started >> 1;
    int charge = 0;
    int connect = future, count = 0;
    int terms[13];
    for (int i = 0; i < state.count; ++i) {
        const int term = state.terms[i];
        const int kind = term & ~contactMask, rest = (term & contactMask) >> 1;
        const bool meets = term & 1;
        if (!kind && meets) touched = true;
        if (active && meets) {
            if (!kind) connect |= rest;
            if (kind == mine) ++charge;
        } else if (rest) terms[count++] = kind | rest;
        else if (kind != mine) ++charge;
    }

    if (active) {
        if (started || cell == Cell::AlreadyOpen) reachable |= connect;
        else if (connect) terms[count++] = connect;
        else ++charge;
    } else if (cell == Cell::Number) {
        if (!touched) {
            if (future) terms[count++] = number | future;
            else ++charge;
        }
    } else if (cell == Cell::Mine) {
        if (touched) ++charge;
        else if (future) terms[count++] = mine | future;
    }

    for (int i = 0; i < count; ++i) {
        const int set = terms[i];
        if (set >= number) continue;
        // 激活任一接触格后，当前分量可并入能够接触同一个格的分量。
        if ((set & ~(reachable | openCenters)) == 0) {
            terms[i] |= number;
            continue;
        }
        for (int j = 0; j < count; ++j) {
            const int other = terms[j];
            if (other >= number || (set == other && i <= j) || (set & ~other)) continue;
            terms[i] |= number;
            break;
        }
    }

    out.started = reachable & ~openCenters;
    out.count = 0;
    for (int i = 0; i < count; ++i) {
        if (terms[i] < number || terms[i] >= mine) continue;
        for (int j = 0; j < count; ++j) {
            if (terms[j] != (terms[i] ^ (number | mine))) continue;
            terms[j] = terms[i] = 0;
            ++charge;
            break;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (!terms[i]) continue;
        if (out.count == 12) return -1;
        out.terms[out.count++] = terms[i];
    }
    std::sort(out.terms, out.terms + out.count);
    return charge;
}

std::size_t IoeJointFilter::DecisionDiagram::KeyHash::operator()(const Key &key) const {
    const std::uint64_t pair = (std::uint64_t)key.low.node << 32 | key.high.node;
    return splitmix64(pair ^ (key.low.value * 0x9e3779b97f4a7c15ULL) ^ (key.high.value * 0xd1b54a32d192ed03ULL) ^ key.variable);
}

bool IoeJointFilter::DecisionDiagram::Build::clear(std::uint64_t &memoryLeft) {
    vector<Node> &nodes = pool.nodes;
    if (!nodes.resize(2, memoryLeft)) return false;
    nodes[0] = {{{0, 0}, {0, 0}}, 81, 0, 0};
    nodes[1] = {{{1, 0}, {1, 0}}, 81, 0, 0};
    pool.index.clear();
    minima.clear();
    limits.clear();
    full = false;
    return true;
}

IoeJointFilter::DecisionDiagram::Edge IoeJointFilter::DecisionDiagram::intern(int variable, Edge safe, Edge mine, Pool &pool, bool &full,
                                                                           std::uint64_t &memoryLeft) {
    if (full) return {0, 0};
    const int common = !safe.node ? mine.value : !mine.node ? safe.value : std::min(safe.value, mine.value);
    if (safe.node) safe.value -= common;
    if (mine.node) mine.value -= common;
    const Key key{safe, mine, variable};
    if (const std::uint32_t *known = pool.index.find(key)) return {*known, common};
    vector<Node> &nodes = pool.nodes;
    const int minimum = !safe.node ? mine.value + nodes[mine.node].minimum
        : !mine.node ? safe.value + nodes[safe.node].minimum
        : std::min(safe.value + nodes[safe.node].minimum, mine.value + nodes[mine.node].minimum);
    const int maximum = std::max(safe.value + nodes[safe.node].maximum, mine.value + nodes[mine.node].maximum);
    const std::uint32_t id = nodes.size;
    if (nodes.size > std::numeric_limits<std::uint32_t>::max() || !nodes.push_back({{safe, mine}, variable, minimum, maximum}, memoryLeft)
            || !pool.index.emplace(key, id, memoryLeft)) {
        full = true;
        return {0, 0};
    }
    return {id, common};
}

IoeJointFilter::DecisionDiagram::Edge IoeJointFilter::DecisionDiagram::merge(Edge a, Edge b, Build &build, std::uint64_t &memoryLeft) {
    if (build.full) return {0, 0};
    if (a.node > b.node) std::swap(a, b);
    const int common = std::min(a.value, b.value);
    a.value -= common;
    b.value -= common;
    const Key key{a, b, 0};
    if (const Edge *found = build.minima.find(key)) {
        Edge result = *found;
        result.value += common;
        return result;
    }
    const vector<Node> &nodes = build.pool.nodes;
    const int variable = std::min(nodes[a.node].variable, nodes[b.node].variable);
    const Edge safe = minimum(branch(nodes, a, variable, false), branch(nodes, b, variable, false), build, memoryLeft);
    const Edge mine = minimum(branch(nodes, a, variable, true), branch(nodes, b, variable, true), build, memoryLeft);
    Edge edge = choice(variable, safe, mine, build, memoryLeft);
    if (!build.full && build.minima.size() < (1U << 19)) build.minima.emplace(key, edge, memoryLeft);
    if (edge.node) edge.value += common;
    return edge;
}

IoeJointFilter::DecisionDiagram::Edge IoeJointFilter::DecisionDiagram::filter(Edge clicks, const vector<Node> &threeBV, Edge bv, int balance, Weights weights,
                                                                           Build &build, std::uint64_t &memoryLeft) {
    if (build.full) return {0, 0};
    const vector<Node> &nodes = build.pool.nodes;
    const Node &d = nodes[clicks.node], &b = threeBV[bv.node];
    const Key key{{clicks.node, 0}, {bv.node, 0}, balance};
    if (const Edge *found = build.limits.find(key)) {
        Edge result = *found;
        if (result.node) result.value += clicks.value;
        return result;
    }
    const int variable = std::min(d.variable, b.variable);
    const Edge base{clicks.node, 0}, remaining{bv.node, 0};
    const Edge safe = within(branch(nodes, base, variable, false), threeBV, branch(threeBV, remaining, variable, false), balance, weights, build, memoryLeft);
    const Edge mine = within(branch(nodes, base, variable, true), threeBV, branch(threeBV, remaining, variable, true), balance, weights, build, memoryLeft);
    Edge result = choice(variable, safe, mine, build, memoryLeft);
    if (!build.full && build.limits.size() < (1U << 19)) build.limits.emplace(key, result, memoryLeft);
    if (result.node) result.value += clicks.value;
    return result;
}

} // 命名空间 mss
