#include "algo/winrate_engine/fifty_fifty/templates/semi.h"

#include <algorithm>
#include <bit>

#include "algo/winrate_engine/fifty_fifty/templates/template_data.h"
#include "core/algorithm/neighbors.h"
#include "core/assert.h"

namespace mss {

int TemplateSemi::templateCount() { return sizeof(FiftyFiftyData::patterns) / sizeof(FiftyFiftyData::patterns[0]); }

TemplateSemi::Pool::Pool() {}

TemplateSemi::Pool::~Pool() {
    for (int i = 0; i < (int)tables.size; ++i) delete tables.data[i];
}

TemplateSemi::Table::Table(int prefix, vector<Point> &first, vector<Point> &second, vector<Point> &core, vector<Build::Boundary> &boundary, vector<Build::Pattern> &patterns, vector<Point> &points, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups) : prefix(prefix), radiusR(0), radiusC(0) {
    assert_(prefix > 0 && prefix <= templateCount(), "模板前缀超过目录");
    patterns.resize(0); points.resize(0); entries.resize(0); tests.resize(0); frequency.resize(0); groups.resize(0);
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
    prefixes.push_back(Prefix({0, 0}));
    compilePrefix(entries, tests, frequency, groups, 0, entries.size, 0, 0);
    for (int i = prefixes.data[0].first; i < prefixes.data[0].first + prefixes.data[0].count; ++i) {
        radiusR = (std::max)(radiusR, std::abs(prefixes.data[i].offset.r));
        radiusC = (std::max)(radiusC, std::abs(prefixes.data[i].offset.c));
    }
    starts.resize(radiusR * 2 + 1, radiusC * 2 + 1, -1);
    for (int i = prefixes.data[0].first; i < prefixes.data[0].first + prefixes.data[0].count; ++i) starts[prefixes.data[i].offset.r + radiusR][prefixes.data[i].offset.c + radiusC] = i;
}

void TemplateSemi::Table::compilePrefix(vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups, int begin, int end, int depth, int node) {
    prefixes.data[node].runBegin = runs.size;
    while (tests.data[entries.data[begin].begin + depth].kind == Kind::Safe && tests.data[entries.data[begin].begin + depth] == tests.data[entries.data[end - 1].begin + depth]) {
        const Test test = tests.data[entries.data[begin].begin + depth++];
        runs.push_back({test.r, test.c});
    }
    prefixes.data[node].runEnd = runs.size;
    const int first = prefixes.size, groupBegin = groups.size;
    int tail = begin;
    while (tail < end && tests.data[entries.data[tail].begin + depth].kind == Kind::Safe) {
        const Test test = tests.data[entries.data[tail].begin + depth];
        const int group = tail++;
        while (tail < end && tests.data[entries.data[tail].begin + depth] == test) ++tail;
        prefixes.push_back(Prefix({test.r, test.c})); groups.push_back({group, tail});
    }
    const int groupCount = groups.size - groupBegin;
    prefixes.data[node].first = first; prefixes.data[node].count = groupCount;
    for (int i = 0; i < groupCount; ++i) {
        const Build::Group group = groups.data[groupBegin + i];
        compilePrefix(entries, tests, frequency, groups, group.begin, group.end, depth + 1, first + i);
    }
    groups.resize(groupBegin);
    for (int i = tail; i < end; ++i) { entries.data[i].begin += depth; entries.data[i].count -= depth; }
    prefixes.data[node].begin = checks.size; compileBranch(entries, tests, frequency, tail, end, -1); prefixes.data[node].end = checks.size;
}

void TemplateSemi::Table::compileBranch(vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, int begin, int end, int parent) {
    if (begin == end) return;
    for (int i = begin; i < end; ++i) if (!entries.data[i].count) { checks.data[parent].accept = true; return; }
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
    const int index = checks.size;
    checks.resize(index + 1); checks.data[index].test = selected; checks.data[index].accept = false;
    compileBranch(entries, tests, frequency, begin, middle, index); checks.data[index].skip = checks.size;
    compileBranch(entries, tests, frequency, middle, end, parent);
}

TemplateSemi::Facts::Facts(const PositionState &state, Grid<std::uint64_t> &mines, Grid<std::uint64_t> &safes, vector<Basic::Mark> &marks, vector<Adjacent> &adjacent, vector<Constraint> &constraints, vector<std::uint64_t> &active, vector<std::uint64_t> &found) : mines(mines), safes(safes), marks(marks), adjacent(adjacent), constraints(constraints), active(active), found(found), rows(state.board.cells.rows()), cols(state.board.cells.cols()), words((rows * cols + 63) / 64) {
    const int cells = rows * cols;
    marks.resize(cells); adjacent.resize(cells); constraints.resize(0); active.resize(words); found.resize(words);
    mines.resize(cells, words, 0); safes.resize(cells, words, 0);
    for (int word = 0; word < words; ++word) { active.data[word] = 0; found.data[word] = 0; }
    for (int id = 0; id < cells; ++id) {
        const long double probability = Probability::mineProbability(state.probability, id, state.board, state.basic, state.structure);
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

bool TemplateSemi::Facts::matches(int id, int word, std::uint64_t bit, Table::Kind kind) const {
    if (kind == Table::Kind::Wall && marks.data[id] == Basic::Mark::F) return true;
    return marks.data[id] == Basic::Mark::H && ((kind == Table::Kind::Safe ? safes[id] : mines[id])[word] & bit);
}

int TemplateSemi::matchPrefix(const Table &table, int node, const Facts &facts, int root) {
    const Table::Prefix &prefix = table.prefixes.data[node];
    const int row = root / facts.cols, col = root % facts.cols, word = root / 64;
    const std::uint64_t bit = 1ULL << (root % 64);
    for (int i = prefix.runBegin; i < prefix.runEnd; ++i) {
        const int r = row + table.runs.data[i].r, c = col + table.runs.data[i].c;
        if (r < 0 || r >= facts.rows || c < 0 || c >= facts.cols || !facts.matches(r * facts.cols + c, word, bit, Table::Kind::Safe)) return -1;
    }
    for (int index = prefix.begin; index < prefix.end;) {
        const Table::Check &check = table.checks.data[index];
        const int r = row + check.test.r, c = col + check.test.c;
        const bool inside = r >= 0 && r < facts.rows && c >= 0 && c < facts.cols;
        const bool pass = inside ? facts.matches(r * facts.cols + c, word, bit, check.test.kind) : check.test.kind == Table::Kind::Wall;
        if (!pass) { index = check.skip; continue; }
        if (check.accept) return index;
        ++index;
    }
    for (int i = prefix.first; i < prefix.first + prefix.count; ++i) {
        const int r = row + table.prefixes.data[i].offset.r, c = col + table.prefixes.data[i].offset.c;
        if (r < 0 || r >= facts.rows || c < 0 || c >= facts.cols || !facts.matches(r * facts.cols + c, word, bit, Table::Kind::Safe)) continue;
        const int found = matchPrefix(table, i, facts, root);
        if (found >= 0) return found;
    }
    return -1;
}

void TemplateSemi::detect(Semi::Result &result, const PositionState &state, int prefix, Pool &pool, Workspace &workspace) {
    Table *selected = nullptr;
    for (int i = 0; i < (int)pool.tables.size; ++i) if (pool.tables.data[i]->prefix == prefix) { selected = pool.tables.data[i]; break; }
    if (selected == nullptr) {
        selected = new Table(prefix, workspace.first, workspace.second, workspace.core, workspace.boundary, workspace.patterns, workspace.points, workspace.entries, workspace.tests, workspace.frequency, workspace.groups);
        pool.tables.push_back(selected);
    }
    const Table &table = *selected;
    Facts facts(state, workspace.mineStorage, workspace.safeStorage, workspace.markStorage, workspace.adjacentStorage, workspace.constraintStorage, workspace.activeStorage, workspace.foundStorage);
    facts.propagate(workspace.pending, workspace.queued);
    for (int id = 0; id < facts.rows * facts.cols; ++id) if (facts.marks.data[id] == Basic::Mark::H)
        for (int word = 0; word < facts.words; ++word) for (std::uint64_t bits = facts.safes[id][word] & ~facts.found.data[word]; bits; bits &= bits - 1) {
            const int root = word * 64 + std::countr_zero(bits);
            const int dr = id / facts.cols - root / facts.cols, dc = id % facts.cols - root % facts.cols;
            if (std::abs(dr) > table.radiusR || std::abs(dc) > table.radiusC) continue;
            const int node = table.starts[dr + table.radiusR][dc + table.radiusC];
            if (node >= 0 && matchPrefix(table, node, facts, root) >= 0) facts.found.data[word] |= 1ULL << (root % 64);
        }
    result.kind = Semi::Kind::None; result.cells.resize(0);
    for (int root = 0; root < facts.rows * facts.cols; ++root) if (facts.found.data[root / 64] & (1ULL << (root % 64))) {
        const long double probability = Probability::mineProbability(state.probability, root, state.board, state.basic, state.structure);
        const bool forced = std::abs(probability - 0.5L) <= halfTolerance;
        if (forced && result.kind != Semi::Kind::Forced) { result.cells.resize(0); result.kind = Semi::Kind::Forced; }
        if (result.kind != Semi::Kind::Forced) result.kind = Semi::Kind::Semi;
        if (forced || result.kind == Semi::Kind::Semi) result.cells.push_back(root);
    }
}

} // 命名空间 mss
