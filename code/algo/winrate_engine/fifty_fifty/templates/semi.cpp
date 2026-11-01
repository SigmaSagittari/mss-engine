#include "algo/winrate_engine/fifty_fifty/templates/semi.h"

#include <algorithm>
#include <bit>

#include "algo/winrate_engine/fifty_fifty/templates/template_data.h"
#include "core/algorithm/neighbors.h"
#include "core/assert.h"

namespace mss {

// Safe/Mine 是假设根格为雷后必须推导出的状态；Wall 可由必雷格或棋盘外挡住。
enum class TemplateSemi::Pool::Kind : unsigned char { Safe, Mine, Wall };
struct TemplateSemi::Pool::Point {
    int r, c;
    bool operator<(const Point &other) const { return r == other.r ? c < other.c : r < other.r; }
    bool operator==(const Point &) const = default;
};
struct TemplateSemi::Pool::Test {
    int r, c;
    Kind kind;
    Test(int r, int c, Kind kind) : r(r), c(c), kind(kind) {}
    bool operator==(const Test &) const = default;
    bool operator<(const Test &other) const { if (kind != other.kind) return kind < other.kind; return r == other.r ? c < other.c : r < other.r; }
};
// test 通过且 accept=true 即命中；失败时跳到 checks[skip]，跳过这一支。
// 危险：构表时先写 test/accept，递归结束后写 skip；编译完成才发布整张表。
struct TemplateSemi::Pool::Check { Test test; bool accept; int skip; };
struct TemplateSemi::Pool::Prefix {
    Point offset; // 相对被检测格的位置。
    int first, count, begin, end, runBegin, runEnd; // 子前缀范围、checks 范围、连续必安检查范围。
};

struct TemplateSemi::Pool::Table {
    struct Build {
        // core 先放 a 个 A 组格，再放 B 组格；walls 是隔断两组数字差异所需的封口位。
        struct Pattern { int a, coreBegin, coreCount, wallBegin, wallCount; };
        struct Rooted { int begin, count; };
        struct Boundary { Point point; int difference; };
        struct Group { Point offset; int begin, end; };
    };
    vectorPool<Check>::vector checks;
    vectorPool<Prefix>::vector prefixes;
    vectorPool<Point>::vector runs;
    vectorPool<int>::vector starts; // 由相对偏移定位安全前缀；-1 表示没有该分支。
    int prefix, radiusR, radiusC;

    Table(int prefix, Pool &pool, vector<Point> &first, vector<Point> &second, vector<Point> &core, vector<Build::Boundary> &boundary, vector<Build::Pattern> &patterns, vector<Point> &points, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups, vector<Prefix> &compiled);
    Prefix compilePrefix(Pool &pool, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups, vector<Prefix> &compiled, int begin, int end, int depth, Point offset);
    void compileBranch(Pool &pool, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, int begin, int end, int parent);
};

// 模板检测的临时推导：同时假设每个候选为雷，批量推进数字约束。
struct TemplateSemi::Facts {
    struct Adjacent {
        // 危险：Facts 构造时先写 count，再填 [0,count) 的数字约束索引。
        int indices[8];
        int count;
    };
    struct Constraint {
        // 危险：复用槽只填写有效的 cells；Facts 构造时先写 count/sum，再填 [0,count)，不复制未用数组项。
        int cells[8];
        int count, sum;
    };
    // [被推导格][假设候选位图块]，每个 bit 对应一个“假设该候选为雷”的局面。
    Grid<std::uint64_t> &mines, &safes;
    vector<Basic::Mark> &marks;
    vector<Adjacent> &adjacent;
    vector<Constraint> &constraints;
    vector<std::uint64_t> &active, &found; // 尚在推导的假设，以及已经命中模板的假设。
    int rows, cols, words;
    Facts(const PositionState &state, const Probability::Pool &probabilities, Grid<std::uint64_t> &mines, Grid<std::uint64_t> &safes, vector<Basic::Mark> &marks, vector<Adjacent> &adjacent, vector<Constraint> &constraints, vector<std::uint64_t> &active, vector<std::uint64_t> &found);
    void propagate(vector<int> &pending, vector<unsigned char> &queued);
    bool matches(int id, int word, std::uint64_t bit, Pool::Kind kind) const;
};

struct TemplateSemi::Workspace {
    vector<Pool::Point> first, second, core, points;
    vector<Pool::Table::Build::Boundary> boundary;
    vector<Pool::Table::Build::Pattern> patterns;
    vector<Pool::Table::Build::Rooted> entries;
    vector<Pool::Table::Build::Group> groups;
    vector<Pool::Test> tests, frequency;
    vector<Pool::Prefix> compiled;
    Grid<std::uint64_t> mineStorage, safeStorage;
    vector<Basic::Mark> markStorage;
    vector<Facts::Adjacent> adjacentStorage;
    vector<Facts::Constraint> constraintStorage;
    vector<std::uint64_t> activeStorage, foundStorage;
    vector<int> pending;
    vector<unsigned char> queued;
};

__declspec(noinline) TemplateSemi::Workspace &TemplateSemi::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

int TemplateSemi::templateCount() { return sizeof(FiftyFiftyData::patterns) / sizeof(FiftyFiftyData::patterns[0]); }

TemplateSemi::Pool::Pool() {}

TemplateSemi::Pool::Table::Table(int prefix, Pool &pool, vector<Point> &first, vector<Point> &second, vector<Point> &core, vector<Build::Boundary> &boundary, vector<Build::Pattern> &patterns, vector<Point> &points, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups, vector<Prefix> &compiled) : checks(pool.checks.push_back()), prefixes(pool.prefixes.push_back()), runs(pool.runs.push_back()), starts(pool.starts.push_back()), prefix(prefix), radiusR(0), radiusC(0) {
    assert_(prefix > 0 && prefix <= TemplateSemi().templateCount(), "模板前缀超过目录");
    patterns.resize(0); points.resize(0); entries.resize(0); tests.resize(0); frequency.resize(0); groups.resize(0); compiled.resize(0);
    for (int source = 0; source < prefix; ++source) {
        first.resize(0); second.resize(0);
        int row = 0, col = 0;
        for (const char *text = FiftyFiftyData::patterns[source]; *text; ++text) {
            if (*text == '/') { ++row; col = 0; }
            else { if (*text == 'A') first.push_back({row, col}); if (*text == 'B') second.push_back({row, col}); ++col; }
        }
        const int a = first.size, sourceBegin = patterns.size;
        for (int turn = 0; turn < 4; ++turn) for (int mirror = 0; mirror < 2; ++mirror) {
            core.resize(first.size + second.size);
            for (int i = 0; i < a; ++i) { core.data[i] = first.data[i]; core.data[a + i] = second.data[i]; }
            for (int i = 0; i < (int)core.size; ++i) {
                Point &point = core.data[i];
                for (int step = 0; step < turn; ++step) point = {point.c, -point.r};
                if (mirror) point.c = -point.c;
            }
            int top = core.data[0].r, left = core.data[0].c;
            for (int i = 0; i < (int)core.size; ++i) { top = (std::min)(top, core.data[i].r); left = (std::min)(left, core.data[i].c); }
            for (int i = 0; i < (int)core.size; ++i) { core.data[i].r -= top; core.data[i].c -= left; }
            std::sort(core.data, core.data + a); std::sort(core.data + a, core.data + core.size);
            if (std::lexicographical_compare(core.data + a, core.data + core.size, core.data, core.data + a)) for (int i = 0; i < a; ++i) std::swap(core.data[i], core.data[a + i]);
            bool duplicate = false;
            for (int i = sourceBegin; i < (int)patterns.size; ++i) {
                const Build::Pattern &pattern = patterns.data[i];
                if (std::equal(core.data, core.data + core.size, points.data + pattern.coreBegin)) { duplicate = true; break; }
            }
            if (duplicate) continue;
            boundary.resize(0);
            for (int i = 0; i < (int)core.size; ++i) for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc)
                if (dr || dc) boundary.push_back({{core.data[i].r + dr, core.data[i].c + dc}, i < a ? 1 : -1});
            std::sort(boundary.data, boundary.data + boundary.size, [](const Build::Boundary &x, const Build::Boundary &y) { return x.point < y.point; });
            const int coreBegin = points.size;
            for (int i = 0; i < (int)core.size; ++i) points.push_back(core.data[i]);
            const int wallBegin = points.size;
            for (int i = 0; i < (int)boundary.size;) {
                const Point point = boundary.data[i].point;
                int difference = 0;
                do { difference += boundary.data[i++].difference; } while (i < (int)boundary.size && boundary.data[i].point == point);
                if (difference && std::find(core.data, core.data + core.size, point) == core.data + core.size) points.push_back(point);
            }
            patterns.push_back({a, coreBegin, (int)core.size, wallBegin, (int)points.size - wallBegin});
        }
    }
    for (int p = 0; p < (int)patterns.size; ++p) {
        const Build::Pattern &pattern = patterns.data[p];
        for (int slot = 0; slot < pattern.coreCount; ++slot) {
            const Point origin = points.data[pattern.coreBegin + slot];
            const int begin = tests.size;
            for (int i = 0; i < pattern.coreCount; ++i) if (i != slot) {
                const Point point = points.data[pattern.coreBegin + i];
                tests.push_back({point.r - origin.r, point.c - origin.c, (i < pattern.a) == (slot < pattern.a) ? Kind::Mine : Kind::Safe});
            }
            for (int i = 0; i < pattern.wallCount; ++i) {
                const Point point = points.data[pattern.wallBegin + i];
                tests.push_back({point.r - origin.r, point.c - origin.c, Kind::Wall});
            }
            std::sort(tests.data + begin, tests.data + tests.size);
            entries.push_back({begin, (int)tests.size - begin});
        }
    }
    std::sort(entries.data, entries.data + entries.size, [&](const Build::Rooted &x, const Build::Rooted &y) { return std::lexicographical_compare(tests.data + x.begin, tests.data + x.begin + x.count, tests.data + y.begin, tests.data + y.begin + y.count); });
    const Prefix root = compilePrefix(pool, entries, tests, frequency, groups, compiled, 0, entries.size, 0, {0, 0});
    for (int i = root.first; i < root.first + root.count; ++i) {
        radiusR = (std::max)(radiusR, std::abs(pool.prefixes.data.data[this->prefixes.offset + i].offset.r));
        radiusC = (std::max)(radiusC, std::abs(pool.prefixes.data.data[this->prefixes.offset + i].offset.c));
    }
    starts.count = (radiusR * 2 + 1) * (radiusC * 2 + 1); pool.starts.data.resize(starts.offset + starts.count);
    for (int i = 0; i < (int)starts.count; ++i) pool.starts.data.data[starts.offset + i] = -1;
    for (int i = root.first; i < root.first + root.count; ++i) {
        const Prefix &item = pool.prefixes.data.data[this->prefixes.offset + i];
        pool.starts.data.data[starts.offset + (item.offset.r + radiusR) * (radiusC * 2 + 1) + item.offset.c + radiusC] = i;
    }
}

TemplateSemi::Pool::Prefix TemplateSemi::Pool::Table::compilePrefix(Pool &pool, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups, vector<Prefix> &compiled, int begin, int end, int depth, Point offset) {
    const int runBegin = runs.count;
    while (tests.data[entries.data[begin].begin + depth].kind == Kind::Safe && tests.data[entries.data[begin].begin + depth] == tests.data[entries.data[end - 1].begin + depth]) {
        const Test test = tests.data[entries.data[begin].begin + depth++];
        pool.runs.push_back(runs, Point{test.r, test.c});
    }
    const int runEnd = runs.count, groupBegin = groups.size, compiledBegin = compiled.size;
    int tail = begin;
    while (tail < end && tests.data[entries.data[tail].begin + depth].kind == Kind::Safe) {
        const Test test = tests.data[entries.data[tail].begin + depth];
        const int group = tail++;
        while (tail < end && tests.data[entries.data[tail].begin + depth] == test) ++tail;
        groups.push_back({{test.r, test.c}, group, tail});
    }
    const int groupCount = groups.size - groupBegin;
    for (int i = 0; i < groupCount; ++i) {
        const Build::Group group = groups.data[groupBegin + i];
        compiled.push_back(compilePrefix(pool, entries, tests, frequency, groups, compiled, group.begin, group.end, depth + 1, group.offset));
    }
    const int first = prefixes.count;
    // 子记录完整后按组连续入池，递归及扩容期间只复制已经填完的记录。
    for (int i = 0; i < groupCount; ++i) pool.prefixes.push_back(prefixes, compiled.data[compiledBegin + i]);
    compiled.resize(compiledBegin);
    groups.resize(groupBegin);
    for (int i = tail; i < end; ++i) { entries.data[i].begin += depth; entries.data[i].count -= depth; }
    const int checkBegin = checks.count;
    compileBranch(pool, entries, tests, frequency, tail, end, -1);
    return {offset, first, groupCount, checkBegin, (int)checks.count, runBegin, runEnd};
}

void TemplateSemi::Pool::Table::compileBranch(Pool &pool, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, int begin, int end, int parent) {
    if (begin == end) return;
    for (int i = begin; i < end; ++i) if (!entries.data[i].count) { pool.checks.data.data[checks.offset + parent].accept = true; return; }
    frequency.resize(0);
    for (int i = begin; i < end; ++i) {
        const Build::Rooted &entry = entries.data[i];
        for (int j = 0; j < entry.count; ++j) frequency.push_back(tests.data[entry.begin + j]);
    }
    std::sort(frequency.data, frequency.data + frequency.size);
    Test selected = frequency.data[0];
    int count = 0;
    for (int i = 0; i < (int)frequency.size;) {
        const Test test = frequency.data[i];
        const int first = i++;
        while (i < (int)frequency.size && frequency.data[i] == test) ++i;
        if (test.kind == selected.kind && i - first > count) { selected = test; count = i - first; }
    }
    const int middle = (int)(std::partition(entries.data + begin, entries.data + end, [&](const Build::Rooted &entry) { return std::find(tests.data + entry.begin, tests.data + entry.begin + entry.count, selected) != tests.data + entry.begin + entry.count; }) - entries.data);
    for (int i = begin; i < middle; ++i) {
        Build::Rooted &entry = entries.data[i];
        const int removed = (int)(std::find(tests.data + entry.begin, tests.data + entry.begin + entry.count, selected) - tests.data);
        for (int j = removed; j + 1 < entry.begin + entry.count; ++j) tests.data[j] = tests.data[j + 1];
        --entry.count;
    }
    const int index = checks.count;
    pool.checks.data.resize(checks.offset + index + 1); checks.count = index + 1; pool.checks.data.data[checks.offset + index].test = selected; pool.checks.data.data[checks.offset + index].accept = false;
    compileBranch(pool, entries, tests, frequency, begin, middle, index); pool.checks.data.data[checks.offset + index].skip = checks.count;
    compileBranch(pool, entries, tests, frequency, middle, end, parent);
}

TemplateSemi::Facts::Facts(const PositionState &state, const Probability::Pool &probabilities, Grid<std::uint64_t> &mines, Grid<std::uint64_t> &safes, vector<Basic::Mark> &marks, vector<Adjacent> &adjacent, vector<Constraint> &constraints, vector<std::uint64_t> &active, vector<std::uint64_t> &found) : mines(mines), safes(safes), marks(marks), adjacent(adjacent), constraints(constraints), active(active), found(found), rows(state.board.cells.rows()), cols(state.board.cells.cols()), words((rows * cols + 63) / 64) {
    const int cells = rows * cols;
    marks.resize(cells); adjacent.resize(cells); constraints.resize(0); active.resize(words); found.resize(words);
    mines.resize(cells, words, 0); safes.resize(cells, words, 0);
    for (int word = 0; word < words; ++word) { active.data[word] = 0; found.data[word] = 0; }
    for (int id = 0; id < cells; ++id) {
        const long double probability = Probability().mineProbability(state.probability, probabilities, id, state.board, state.basic, state.structure);
        marks.data[id] = probability == 1 ? Basic::Mark::F : probability == 0 ? Basic::Mark::S : state.basic.marks[id / cols][id % cols];
        adjacent.data[id].count = 0;
        if (marks.data[id] != Basic::Mark::H) continue;
        const std::uint64_t bit = 1ULL << (id % 64);
        active.data[id / 64] |= bit; mines[id][id / 64] = bit;
    }
    for (int row = 0; row < rows; ++row) for (int col = 0; col < cols; ++col) {
        const ObservedBoard::CellState value = state.board.cells[row][col];
        if (value > ObservedBoard::CellState::Num8) continue;
        const int index = constraints.size;
        constraints.resize(index + 1);
        Constraint &constraint = constraints.data[index];
        constraint.count = 0; constraint.sum = (int)value;
        forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
            const int id = r * cols + c;
            if (marks.data[id] == Basic::Mark::F) --constraint.sum;
            else if (marks.data[id] == Basic::Mark::H) constraint.cells[constraint.count++] = id;
        });
        if (!constraint.count) { constraints.resize(index); continue; }
        for (int i = 0; i < constraint.count; ++i) {
            Adjacent &neighbors = adjacent.data[constraint.cells[i]];
            neighbors.indices[neighbors.count++] = index;
        }
    }
}

void TemplateSemi::Facts::propagate(vector<int> &pending, vector<unsigned char> &queued) {
    pending.resize(0); queued.resize(constraints.size);
    for (int i = 0; i < (int)constraints.size; ++i) { pending.push_back(i); queued.data[i] = 1; }
    for (int head = 0; head < (int)pending.size; ++head) {
        const int index = pending.data[head]; queued.data[index] = 0;
        const Constraint &constraint = constraints.data[index];
        for (int word = 0; word < words; ++word) {
            if (!active.data[word]) continue;
            std::uint64_t mineCount[4] = {}, safeCount[4] = {};
            for (int i = 0; i < constraint.count; ++i) {
                const int id = constraint.cells[i];
                std::uint64_t m = mines[id][word], s = safes[id][word];
                for (int bit = 0; bit < 4; ++bit) { const std::uint64_t nextM = mineCount[bit] & m, nextS = safeCount[bit] & s; mineCount[bit] ^= m; safeCount[bit] ^= s; m = nextM; s = nextS; }
            }
            std::uint64_t forceSafe = active.data[word], forceMine = active.data[word];
            for (int bit = 0; bit < 4; ++bit) { forceSafe &= constraint.sum & (1 << bit) ? mineCount[bit] : ~mineCount[bit]; forceMine &= (constraint.count - constraint.sum) & (1 << bit) ? safeCount[bit] : ~safeCount[bit]; }
            for (int i = 0; i < constraint.count; ++i) {
                const int id = constraint.cells[i];
                const std::uint64_t unknown = ~(mines[id][word] | safes[id][word]);
                const std::uint64_t m = forceMine & unknown, s = forceSafe & unknown;
                if (!(m | s)) continue;
                mines[id][word] |= m; safes[id][word] |= s;
                for (int i = 0; i < adjacent.data[id].count; ++i) {
                    const int neighbor = adjacent.data[id].indices[i];
                    if (!queued.data[neighbor]) { queued.data[neighbor] = 1; pending.push_back(neighbor); }
                }
            }
        }
    }
}

bool TemplateSemi::Facts::matches(int id, int word, std::uint64_t bit, Pool::Kind kind) const {
    if (kind == Pool::Kind::Wall && marks.data[id] == Basic::Mark::F) return true;
    return marks.data[id] == Basic::Mark::H && ((kind == Pool::Kind::Safe ? safes[id] : mines[id])[word] & bit);
}

int TemplateSemi::matchPrefix(const Pool::Table &table, const Pool &pool, int node, const Facts &facts, int root) {
    const Pool::Prefix &prefix = pool.prefixes.data.data[table.prefixes.offset + node];
    const int row = root / facts.cols, col = root % facts.cols, word = root / 64;
    const std::uint64_t bit = 1ULL << (root % 64);
    for (int i = prefix.runBegin; i < prefix.runEnd; ++i) {
        const Pool::Point &point = pool.runs.data.data[table.runs.offset + i];
        const int r = row + point.r, c = col + point.c;
        if (r < 0 || r >= facts.rows || c < 0 || c >= facts.cols || !facts.matches(r * facts.cols + c, word, bit, Pool::Kind::Safe)) return -1;
    }
    for (int index = prefix.begin; index < prefix.end;) {
        const Pool::Check &check = pool.checks.data.data[table.checks.offset + index];
        const int r = row + check.test.r, c = col + check.test.c;
        const bool inside = r >= 0 && r < facts.rows && c >= 0 && c < facts.cols;
        const bool pass = inside ? facts.matches(r * facts.cols + c, word, bit, check.test.kind) : check.test.kind == Pool::Kind::Wall;
        if (!pass) { index = check.skip; continue; }
        if (check.accept) return index;
        ++index;
    }
    for (int i = prefix.first; i < prefix.first + prefix.count; ++i) {
        const Pool::Prefix &child = pool.prefixes.data.data[table.prefixes.offset + i];
        const int r = row + child.offset.r, c = col + child.offset.c;
        if (r < 0 || r >= facts.rows || c < 0 || c >= facts.cols || !facts.matches(r * facts.cols + c, word, bit, Pool::Kind::Safe)) continue;
        const int found = matchPrefix(table, pool, i, facts, root);
        if (found >= 0) return found;
    }
    return -1;
}

void TemplateSemi::detect(Semi::Result &result, const PositionState &state, const Probability::Pool &probabilities, int prefix, Pool &pool) {
    Workspace &workspace = getWorkspace();
    int selected = -1;
    for (int i = 0; i < (int)pool.tables.size; ++i) if (pool.tables.data[i].prefix == prefix) { selected = i; break; }
    if (selected == -1) {
        selected = pool.tables.size;
        pool.tables.push_back(Pool::Table(prefix, pool, workspace.first, workspace.second, workspace.core, workspace.boundary, workspace.patterns, workspace.points, workspace.entries, workspace.tests, workspace.frequency, workspace.groups, workspace.compiled));
    }
    const Pool::Table &table = pool.tables.data[selected];
    Facts facts(state, probabilities, workspace.mineStorage, workspace.safeStorage, workspace.markStorage, workspace.adjacentStorage, workspace.constraintStorage, workspace.activeStorage, workspace.foundStorage);
    facts.propagate(workspace.pending, workspace.queued);
    for (int id = 0; id < facts.rows * facts.cols; ++id) if (facts.marks.data[id] == Basic::Mark::H)
        for (int word = 0; word < facts.words; ++word) for (std::uint64_t bits = facts.safes[id][word] & ~facts.found.data[word]; bits; bits &= bits - 1) {
            const int root = word * 64 + std::countr_zero(bits);
            const int dr = id / facts.cols - root / facts.cols, dc = id % facts.cols - root % facts.cols;
            if (std::abs(dr) > table.radiusR || std::abs(dc) > table.radiusC) continue;
            const int node = pool.starts.data.data[table.starts.offset + (dr + table.radiusR) * (table.radiusC * 2 + 1) + dc + table.radiusC];
            if (node >= 0 && matchPrefix(table, pool, node, facts, root) >= 0) facts.found.data[word] |= 1ULL << (root % 64);
        }
    result.kind = Semi::Kind::None; result.cells.resize(0);
    for (int root = 0; root < facts.rows * facts.cols; ++root) if (facts.found.data[root / 64] & (1ULL << (root % 64))) {
        const long double probability = Probability().mineProbability(state.probability, probabilities, root, state.board, state.basic, state.structure);
        const bool forced = std::abs(probability - 0.5L) <= halfTolerance;
        if (forced && result.kind != Semi::Kind::Forced) { result.cells.resize(0); result.kind = Semi::Kind::Forced; }
        if (result.kind != Semi::Kind::Forced) result.kind = Semi::Kind::Semi;
        if (forced || result.kind == Semi::Kind::Semi) result.cells.push_back(root);
    }
}

} // 命名空间 mss
