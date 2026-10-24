#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"

#include <algorithm>
#include <bit>
#include <set>

#include "algo/winrate_engine/fifty_fifty/fifty_fifty_data.h"
#include "core/algorithm/neighbors.h"
#include "core/assert.h"

namespace mss {

int FiftyFifty::templateCount() { return sizeof(FiftyFiftyData::patterns) / sizeof(FiftyFiftyData::patterns[0]); }

FiftyFifty::Table::Table(int prefix) : radiusR(0), radiusC(0) {
    assert_(prefix > 0 && prefix <= templateCount(), "模板前缀超过目录");
    Build build;
    for (int source = 0; source < prefix; ++source) {
        std::deque<Point> first, second;
        int row = 0, col = 0;
        for (const char *text = FiftyFiftyData::patterns[source]; *text; ++text) {
            if (*text == '/') { ++row; col = 0; }
            else { if (*text == 'A') first.push_back({row, col}); if (*text == 'B') second.push_back({row, col}); ++col; }
        }
        std::set<std::pair<std::deque<Point>, std::deque<Point>>> seen;
        for (int turn = 0; turn < 4; ++turn) for (int mirror = 0; mirror < 2; ++mirror) {
            std::deque<Point> a = first, b = second;
            auto transform = [&](std::deque<Point> &points) { for (Point &point : points) { for (int i = 0; i < turn; ++i) point = {point.c, -point.r}; if (mirror) point.c = -point.c; } };
            transform(a); transform(b);
            int top = a[0].r, left = a[0].c;
            for (const Point &point : a) { top = (std::min)(top, point.r); left = (std::min)(left, point.c); }
            for (const Point &point : b) { top = (std::min)(top, point.r); left = (std::min)(left, point.c); }
            for (Point &point : a) { point.r -= top; point.c -= left; }
            for (Point &point : b) { point.r -= top; point.c -= left; }
            std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
            if (b < a) a.swap(b);
            if (!seen.emplace(a, b).second) continue;
            Build::Pattern pattern{(int)a.size(), a, {}};
            pattern.core.insert(pattern.core.end(), b.begin(), b.end());
            std::map<Point, int> difference;
            for (int i = 0; i < (int)pattern.core.size(); ++i) for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc)
                if (dr || dc) difference[{pattern.core[i].r + dr, pattern.core[i].c + dc}] += i < pattern.a ? 1 : -1;
            for (const std::pair<const Point, int> &point : difference)
                if (point.second && std::find(pattern.core.begin(), pattern.core.end(), point.first) == pattern.core.end()) pattern.walls.push_back(point.first);
            build.patterns.push_back(pattern);
        }
    }
    for (const Build::Pattern &pattern : build.patterns) for (int slot = 0; slot < (int)pattern.core.size(); ++slot) {
        const Point origin = pattern.core[slot];
        Build::Rooted entry;
        for (int i = 0; i < (int)pattern.core.size(); ++i)
            if (i != slot) entry.tests.push_back({pattern.core[i].r - origin.r, pattern.core[i].c - origin.c, (i < pattern.a) == (slot < pattern.a) ? Kind::Mine : Kind::Safe});
        for (const Point &wall : pattern.walls) entry.tests.push_back({wall.r - origin.r, wall.c - origin.c, Kind::Wall});
        std::sort(entry.tests.begin(), entry.tests.end());
        build.rooted.push_back(entry);
    }
    std::sort(build.rooted.begin(), build.rooted.end(), [](const Build::Rooted &a, const Build::Rooted &b) { return a.tests < b.tests; });
    prefixes.push_back(Prefix({0, 0}));
    compilePrefix(build.rooted, 0, (int)build.rooted.size(), 0, 0);
    for (int i = prefixes.data[0].first; i < prefixes.data[0].first + prefixes.data[0].count; ++i) {
        radiusR = (std::max)(radiusR, std::abs(prefixes.data[i].offset.r));
        radiusC = (std::max)(radiusC, std::abs(prefixes.data[i].offset.c));
    }
    starts.resize(radiusR * 2 + 1, radiusC * 2 + 1, -1);
    for (int i = prefixes.data[0].first; i < prefixes.data[0].first + prefixes.data[0].count; ++i) starts[prefixes.data[i].offset.r + radiusR][prefixes.data[i].offset.c + radiusC] = i;
}

void FiftyFifty::Table::compilePrefix(std::deque<Build::Rooted> &entries, int begin, int end, int depth, int node) {
    // 合并无分叉的安全前缀，孩子连续存放；同一安全前缀只检查一次。
    prefixes.data[node].runBegin = (int)runs.size;
    while (entries[begin].tests[depth].kind == Kind::Safe && entries[begin].tests[depth] == entries[end - 1].tests[depth]) {
        const Test test = entries[begin].tests[depth++];
        runs.push_back({test.r, test.c});
    }
    prefixes.data[node].runEnd = (int)runs.size;
    std::deque<std::pair<int, int>> groups;
    const int first = (int)prefixes.size;
    int tail = begin;
    while (tail < end && entries[tail].tests[depth].kind == Kind::Safe) {
        const Test test = entries[tail].tests[depth];
        const int group = tail++;
        while (tail < end && entries[tail].tests[depth] == test) ++tail;
        prefixes.push_back(Prefix({test.r, test.c})); groups.push_back({group, tail});
    }
    prefixes.data[node].first = first; prefixes.data[node].count = (int)groups.size();
    for (int i = 0; i < (int)groups.size(); ++i) compilePrefix(entries, groups[i].first, groups[i].second, depth + 1, first + i);
    for (int i = tail; i < end; ++i) entries[i].tests.erase(entries[i].tests.begin(), entries[i].tests.begin() + depth);
    prefixes.data[node].begin = (int)checks.size; compileBranch(entries, tail, end, -1); prefixes.data[node].end = (int)checks.size;
}

void FiftyFifty::Table::compileBranch(std::deque<Build::Rooted> &entries, int begin, int end, int parent) {
    if (begin == end) return;
    for (int i = begin; i < end; ++i) if (entries[i].tests.empty()) { checks.data[parent].accept = true; return; }
    std::map<Test, int> frequency;
    for (int i = begin; i < end; ++i) for (const Test &test : entries[i].tests) ++frequency[test];
    Test selected = frequency.begin()->first;
    int count = 0;
    for (const std::pair<const Test, int> &entry : frequency) if (entry.first.kind == selected.kind && entry.second > count) { selected = entry.first; count = entry.second; }
    const int middle = (int)(std::partition(entries.begin() + begin, entries.begin() + end, [&](const Build::Rooted &entry) { return std::find(entry.tests.begin(), entry.tests.end(), selected) != entry.tests.end(); }) - entries.begin());
    for (int i = begin; i < middle; ++i) entries[i].tests.erase(std::find(entries[i].tests.begin(), entries[i].tests.end(), selected));
    const int index = (int)checks.size; checks.push_back({selected, false, 0});
    compileBranch(entries, begin, middle, index); checks.data[index].skip = (int)checks.size;
    compileBranch(entries, middle, end, parent);
}

void FiftyFifty::Result::swapStorage(Workspace &workspace) { risk.swapStorage(workspace.risk); }

void FiftyFifty::Facts::swapStorage(Workspace &workspace) {
    mines.swap(workspace.mineStorage); safes.swap(workspace.safeStorage);
    marks.swap(workspace.markStorage); adjacent.swap(workspace.adjacentStorage); constraints.swap(workspace.constraintStorage);
    active.swap(workspace.activeStorage); found.swap(workspace.foundStorage);
}

FiftyFifty::Facts::Facts(const PositionState &state, Workspace &workspace) : rows(state.board.cells.rows()), cols(state.board.cells.cols()), words((rows * cols + 63) / 64) {
    swapStorage(workspace);
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
        Constraint constraint((int)value);
        forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
            const int id = r * cols + c;
            if (marks.data[id] == Basic::Mark::F) --constraint.sum;
            else if (marks.data[id] == Basic::Mark::H) constraint.cells[constraint.count++] = id;
        });
        if (!constraint.count) continue;
        for (int i = 0; i < constraint.count; ++i) {
            Adjacent &neighbors = adjacent.data[constraint.cells[i]];
            neighbors.indices[neighbors.count++] = (int)constraints.size;
        }
        constraints.push_back(constraint);
    }
}

void FiftyFifty::Facts::propagate(Workspace &workspace) {
    vector<int> &pending = workspace.pending;
    vector<unsigned char> &queued = workspace.queued;
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

bool FiftyFifty::Facts::matches(int id, int word, std::uint64_t bit, Table::Kind kind) const {
    if (kind == Table::Kind::Wall && marks.data[id] == Basic::Mark::F) return true;
    return marks.data[id] == Basic::Mark::H && ((kind == Table::Kind::Safe ? safes[id] : mines[id])[word] & bit);
}

int FiftyFifty::matchPrefix(const Table &table, int node, const Facts &facts, int root) {
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

void FiftyFifty::detect(Pseudo::Result &result, const PositionState &state, const Config &config, Pool &pool, Workspace &workspace) {
    if (config.oldJava) { Pseudo::update(result, state, span<const ObservedBoard::CellId>(), *config.oldJava, workspace.oldJava); return; }
    const Table &table = pool.tables.try_emplace(config.templates->prefix, config.templates->prefix).first->second;
    Facts facts(state, workspace);
    facts.propagate(workspace);
    for (int id = 0; id < facts.rows * facts.cols; ++id) if (facts.marks.data[id] == Basic::Mark::H)
        for (int word = 0; word < facts.words; ++word) for (std::uint64_t bits = facts.safes[id][word] & ~facts.found.data[word]; bits; bits &= bits - 1) {
            const int root = word * 64 + std::countr_zero(bits);
            const int dr = id / facts.cols - root / facts.cols, dc = id % facts.cols - root % facts.cols;
            if (std::abs(dr) > table.radiusR || std::abs(dc) > table.radiusC) continue;
            const int node = table.starts[dr + table.radiusR][dc + table.radiusC];
            if (node >= 0 && matchPrefix(table, node, facts, root) >= 0) facts.found.data[word] |= 1ULL << (root % 64);
        }
    result.kind = Pseudo::Kind::None; result.cells.resize(0);
    for (int root = 0; root < facts.rows * facts.cols; ++root) if (facts.found.data[root / 64] & (1ULL << (root % 64))) {
        const long double probability = Probability::mineProbability(state.probability, root, state.board, state.basic, state.structure);
        const bool forced = std::abs(probability - 0.5L) <= config.templates->eqEps;
        if (forced && result.kind != Pseudo::Kind::Forced) { result.cells.resize(0); result.kind = Pseudo::Kind::Forced; }
        if (result.kind != Pseudo::Kind::Forced) result.kind = Pseudo::Kind::Semi;
        if (forced || result.kind == Pseudo::Kind::Semi) result.cells.push_back(root);
    }
    facts.swapStorage(workspace);
}

void FiftyFifty::prepareRisk(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, bool collectPseudo) {
    LongTermRisk::update(result.risk, state, span<const ObservedBoard::CellId>(), shapeConfig, *config.risk, workspace.risk, collectPseudo ? &result.pseudo : nullptr);
}

long double FiftyFifty::influence(ObservedBoard::CellId cell, PositionState &state, Result &full, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace) {
    return LongTermRisk::influence(cell, state, full.risk, shapeConfig, *config.risk, workspace.risk);
}

LongTermRisk::Details FiftyFifty::describe(ObservedBoard::CellId cell, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Pool &pool, Workspace &workspace) {
    LongTermRisk::Details details = LongTermRisk::describe(cell, state, shapeConfig, *config.risk, workspace.risk);
    if (config.oldJava) {
        Pseudo::Result pseudo;
        if (Pseudo::update(pseudo, state, span<const ObservedBoard::CellId>(), *config.oldJava, workspace.oldJava, cell) != Pseudo::Kind::None) details.body = 1.0L;
    } else {
        const Table &table = pool.tables.try_emplace(config.templates->prefix, config.templates->prefix).first->second;
        Facts facts(state, workspace);
        facts.propagate(workspace);
        if (matchPrefix(table, 0, facts, cell) >= 0) details.body = 1.0L;
        facts.swapStorage(workspace);
    }
    return details;
}

} // 命名空间 mss
