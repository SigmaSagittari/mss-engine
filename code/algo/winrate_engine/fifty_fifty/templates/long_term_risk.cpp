#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "algo/winrate_engine/fifty_fifty/templates/template_data.h"
#include "core/assert.h"

namespace mss {

TemplateLongTermRisk::Pool::Table::Table(int prefix, Pool &pool) : prefix(prefix), points(pool.points.push_back()), patterns(pool.patterns.push_back()), groups(pool.groups.push_back()), blockers(pool.blockers.push_back()), checks(pool.checks.push_back()), outputs(pool.outputs.push_back()) {}

TemplateLongTermRisk::Pool::Pool() {}

TemplateLongTermRisk::Workspace::Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : basic(basic), structure(structure), probability(probability) {}

struct TemplateLongTermRiskEngine {
    static bool samePoint(const TemplateLongTermRisk::Pool::Point &a, const TemplateLongTermRisk::Pool::Point &b) { return a.row == b.row && a.col == b.col; }

    static bool pointLess(const TemplateLongTermRisk::Pool::Point &a, const TemplateLongTermRisk::Pool::Point &b) { return a.row == b.row ? a.col < b.col : a.row < b.row; }

    static void sortPoints(TemplateLongTermRisk::Pool::Point *points, int count) {
        for (int i = 1; i < count; ++i) {
            const TemplateLongTermRisk::Pool::Point value = points[i];
            int j = i;
            while (j > 0 && pointLess(value, points[j - 1])) { points[j] = points[j - 1]; --j; }
            points[j] = value;
        }
    }

    static bool pointsLess(const TemplateLongTermRisk::Pool::Point *first, const TemplateLongTermRisk::Pool::Point *second, int count) {
        for (int i = 0; i < count; ++i) {
            if (samePoint(first[i], second[i])) continue;
            return pointLess(first[i], second[i]);
        }
        return false;
    }

    static void normalizeGroups(TemplateLongTermRisk::Pool::Point *first, TemplateLongTermRisk::Pool::Point *second, int aCount) {
        sortPoints(first, aCount); sortPoints(second, aCount);
        if (pointsLess(second, first, aCount)) for (int i = 0; i < aCount; ++i) { const TemplateLongTermRisk::Pool::Point temp = first[i]; first[i] = second[i]; second[i] = temp; }
        const int row = first[0].row, col = first[0].col;
        for (int i = 0; i < aCount; ++i) { first[i].row -= row; first[i].col -= col; second[i].row -= row; second[i].col -= col; }
    }

    static void transformPoint(TemplateLongTermRisk::Pool::Point &point, int turn, int mirror) {
        for (int i = 0; i < turn; ++i) { const int row = point.row; point.row = point.col; point.col = -row; }
        if (mirror) point.col = -point.col;
    }

    static bool sameCore(const TemplateLongTermRisk::Pool &pool, const TemplateLongTermRisk::Pool::Table &table, const TemplateLongTermRisk::Pool::Pattern &pattern, const TemplateLongTermRisk::Pool::Point *first, const TemplateLongTermRisk::Pool::Point *second, int aCount) {
        if (pattern.aCount != aCount || pattern.coreCount != 2 * aCount) return false;
        const TemplateLongTermRisk::Pool::Point *points = pool.points.data.data + table.points.offset + pattern.pointBegin;
        for (int i = 0; i < aCount; ++i) if (!samePoint(points[i], first[i]) || !samePoint(points[aCount + i], second[i])) return false;
        return true;
    }

    static void boundary(const TemplateLongTermRisk::Pool::Point *core, int coreCount, int aCount, TemplateLongTermRisk::Pool::Point *walls, int &wallCount) {
        TemplateLongTermRisk::Pool::Point candidates[256];
        int differences[256];
        int candidateCount = 0;
        for (int i = 0; i < coreCount; ++i) for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            const TemplateLongTermRisk::Pool::Point candidate{core[i].row + dr, core[i].col + dc};
            int index = 0;
            while (index < candidateCount && !samePoint(candidates[index], candidate)) ++index;
            if (index == candidateCount) { assert_(candidateCount < 256, "模板封口候选超过固定范围"); candidates[candidateCount] = candidate; differences[candidateCount++] = 0; }
            differences[index] += i < aCount ? 1 : -1;
        }
        wallCount = 0;
        for (int i = 0; i < candidateCount; ++i) {
            if (!differences[i]) continue;
            bool isCore = false;
            for (int j = 0; j < coreCount; ++j) if (samePoint(candidates[i], core[j])) { isCore = true; break; }
            if (!isCore) walls[wallCount++] = candidates[i];
        }
        sortPoints(walls, wallCount);
    }

    static void appendPattern(TemplateLongTermRisk::Pool &pool, TemplateLongTermRisk::Pool::Table &table, const TemplateLongTermRisk::Pool::Point *rawFirst, const TemplateLongTermRisk::Pool::Point *rawSecond, int aCount) {
        for (int turn = 0; turn < 4; ++turn) for (int mirror = 0; mirror < 2; ++mirror) {
            TemplateLongTermRisk::Pool::Point first[16], second[16], core[32], walls[256];
            for (int i = 0; i < aCount; ++i) { first[i] = rawFirst[i]; second[i] = rawSecond[i]; transformPoint(first[i], turn, mirror); transformPoint(second[i], turn, mirror); }
            normalizeGroups(first, second, aCount);
            for (int i = 0; i < aCount; ++i) { core[i] = first[i]; core[aCount + i] = second[i]; }
            bool duplicate = false;
            for (int i = 0; i < (int)table.patterns.count; ++i) if (sameCore(pool, table, pool.patterns.data.data[table.patterns.offset + i], first, second, aCount)) { duplicate = true; break; }
            if (duplicate) continue;
            int wallCount = 0;
            boundary(core, 2 * aCount, aCount, walls, wallCount);
            assert_(2 * aCount < 32 && wallCount <= 64, "模板本体或封口超出位掩码范围");
            const int begin = table.points.count;
            for (int i = 0; i < 2 * aCount; ++i) pool.points.push_back(table.points, core[i]);
            for (int i = 0; i < wallCount; ++i) pool.points.push_back(table.points, walls[i]);
            const int pattern = table.patterns.count;
            pool.patterns.data.resize(table.patterns.offset + pattern + 1);
            table.patterns.count = pattern + 1;
            TemplateLongTermRisk::Pool::Pattern &stored = pool.patterns.data.data[table.patterns.offset + pattern];
            stored.pointBegin = begin; stored.aCount = aCount; stored.coreCount = 2 * aCount; stored.wallCount = wallCount;
        }
    }

    static bool childExists(const TemplateLongTermRisk::Pool &pool, const TemplateLongTermRisk::Pool::Table &table, const TemplateLongTermRisk::Pool::Point *subFirst, const TemplateLongTermRisk::Pool::Point *subSecond, int aCount) {
        for (int turn = 0; turn < 4; ++turn) for (int mirror = 0; mirror < 2; ++mirror) {
            TemplateLongTermRisk::Pool::Point first[16], second[16];
            for (int i = 0; i < aCount; ++i) { first[i] = subFirst[i]; second[i] = subSecond[i]; transformPoint(first[i], turn, mirror); transformPoint(second[i], turn, mirror); }
            normalizeGroups(first, second, aCount);
            for (int i = 0; i < (int)table.patterns.count; ++i) if (sameCore(pool, table, pool.patterns.data.data[table.patterns.offset + i], first, second, aCount)) return true;
        }
        return false;
    }

    static void appendUniquePoint(TemplateLongTermRisk::Pool::Point *points, int &count, int capacity, const TemplateLongTermRisk::Pool::Point &point) {
        for (int i = 0; i < count; ++i) if (samePoint(points[i], point)) return;
        assert_(count < capacity, "模板封口集合超过固定范围");
        points[count++] = point;
    }

    static void appendBlocker(TemplateLongTermRisk::Pool &pool, TemplateLongTermRisk::Pool::Table &table, const TemplateLongTermRisk::Pool::Blocker &blocker, int begin) {
        for (int i = begin; i < (int)table.blockers.count; ++i) {
            const TemplateLongTermRisk::Pool::Blocker &existing = pool.blockers.data.data[table.blockers.offset + i];
            if (existing.parent == blocker.parent && existing.extra == blocker.extra && existing.core == blocker.core) return;
        }
        pool.blockers.push_back(table.blockers, blocker);
    }

    static void buildPatternBlockers(TemplateLongTermRisk::Pool &pool, TemplateLongTermRisk::Pool::Table &table, vector<TemplateLongTermRisk::Pool::BuildChild> &buildChildren, vector<TemplateLongTermRisk::Pool::Point> &buildChildWalls, int patternIndex) {
        const TemplateLongTermRisk::Pool::Pattern &pattern = pool.patterns.data.data[table.patterns.offset + patternIndex];
        TemplateLongTermRisk::Pool::Point points[96];
        for (int i = 0; i < pattern.coreCount + pattern.wallCount; ++i) points[i] = pool.points.data.data[table.points.offset + pattern.pointBegin + i];
        TemplateLongTermRisk::Pool::Point extra[64];
        int extraCount = 0;
        buildChildren.resize(0); buildChildWalls.resize(0);
        const unsigned int limit = 1U << pattern.aCount;
        for (unsigned int first = 1; first + 1 < limit; ++first) for (unsigned int second = 1; second + 1 < limit; ++second) {
            if (std::popcount(first) != std::popcount(second)) continue;
            TemplateLongTermRisk::Pool::Point subFirst[16], subSecond[16], subCore[32], childWalls[256];
            int subCount = 0;
            for (int i = 0; i < pattern.aCount; ++i) if (first & (1U << i)) subFirst[subCount++] = points[i];
            int secondCount = 0;
            for (int i = 0; i < pattern.aCount; ++i) if (second & (1U << i)) subSecond[secondCount++] = points[pattern.aCount + i];
            if (subCount != secondCount || !childExists(pool, table, subFirst, subSecond, subCount)) continue;
            for (int i = 0; i < subCount; ++i) { subCore[i] = subFirst[i]; subCore[subCount + i] = subSecond[i]; }
            int childWallCount = 0;
            boundary(subCore, 2 * subCount, subCount, childWalls, childWallCount);
            bool hitsFirst = false, hitsSecond = false;
            for (int i = 0; i < childWallCount; ++i) for (int j = 0; j < pattern.coreCount; ++j) if (samePoint(childWalls[i], points[j])) { if (j < pattern.aCount) hitsFirst = true; else hitsSecond = true; }
            if (hitsFirst && hitsSecond) continue;
            const int wallBegin = (int)buildChildWalls.size;
            for (int i = 0; i < childWallCount; ++i) {
                buildChildWalls.push_back(childWalls[i]);
                bool existingCore = false, existingWall = false;
                for (int j = 0; j < pattern.coreCount; ++j) if (samePoint(childWalls[i], points[j])) existingCore = true;
                for (int j = 0; j < pattern.wallCount; ++j) if (samePoint(childWalls[i], points[pattern.coreCount + j])) existingWall = true;
                if (!existingCore && !existingWall) appendUniquePoint(extra, extraCount, 64, childWalls[i]);
            }
            buildChildren.push_back({wallBegin, childWallCount});
        }
        sortPoints(extra, extraCount);
        TemplateLongTermRisk::Pool::Pattern &stored = pool.patterns.data.data[table.patterns.offset + patternIndex];
        stored.extraBegin = table.points.count;
        stored.extraCount = extraCount;
        for (int i = 0; i < extraCount; ++i) pool.points.push_back(table.points, extra[i]);
        stored.groupBegin = table.groups.count;
        const int blockerBegin = table.blockers.count;
        for (int i = 0; i < (int)buildChildren.size; ++i) {
            const TemplateLongTermRisk::Pool::BuildChild &child = buildChildren.data[i];
            TemplateLongTermRisk::Pool::Blocker relation{0, 0, 0};
            for (int j = 0; j < child.wallCount; ++j) {
                const TemplateLongTermRisk::Pool::Point &point = buildChildWalls.data[child.wallBegin + j];
                for (int k = 0; k < pattern.coreCount; ++k) if (samePoint(point, points[k])) relation.core |= 1ULL << k;
                for (int k = 0; k < pattern.wallCount; ++k) if (samePoint(point, points[pattern.coreCount + k])) relation.parent |= 1ULL << k;
                for (int k = 0; k < extraCount; ++k) if (samePoint(point, extra[k])) relation.extra |= 1ULL << k;
            }
            appendBlocker(pool, table, relation, blockerBegin);
        }
        pool.groups.push_back(table.groups, TemplateLongTermRisk::Pool::Group{blockerBegin, (int)table.blockers.count});
        stored.groupEnd = table.groups.count;
    }

    static void emitCheck(TemplateLongTermRisk::Pool &pool, TemplateLongTermRisk::Pool::Table &table, const vector<TemplateLongTermRisk::Pool::BuildNode> &buildNodes, const vector<TemplateLongTermRisk::Pool::BuildOutput> &buildOutputs, int node) {
        const int index = table.checks.count;
        const int outputBegin = table.outputs.count;
        for (int output = buildNodes.data[node].outputHead; output != -1; output = buildOutputs.data[output].next) pool.outputs.push_back(table.outputs, buildOutputs.data[output].pattern);
        const TemplateLongTermRisk::Pool::Point point = buildNodes.data[node].point;
        pool.checks.push_back(table.checks, TemplateLongTermRisk::Pool::Check{point.row, point.col, index + 1, outputBegin, (int)table.outputs.count});
        for (int child = buildNodes.data[node].firstChild; child != -1; child = buildNodes.data[child].nextSibling) emitCheck(pool, table, buildNodes, buildOutputs, child);
        pool.checks.data.data[table.checks.offset + index].skip = table.checks.count;
    }

    static void compileTable(TemplateLongTermRisk::Pool &pool, TemplateLongTermRisk::Pool::Table &table, vector<TemplateLongTermRisk::Pool::BuildNode> &buildNodes, vector<TemplateLongTermRisk::Pool::BuildOutput> &buildOutputs, vector<TemplateLongTermRisk::Pool::BuildChild> &buildChildren, vector<TemplateLongTermRisk::Pool::Point> &buildChildWalls) {
        buildNodes.resize(0); buildOutputs.resize(0);
        for (int source = 0; source < table.prefix; ++source) {
            TemplateLongTermRisk::Pool::Point first[16], second[16];
            int row = 0, col = 0, aCount = 0, bCount = 0;
            for (const char *text = FiftyFiftyData::patterns[source]; *text; ++text) {
                if (*text == '/') { ++row; col = 0; }
                else { if (*text == 'A') first[aCount++] = {row, col}; if (*text == 'B') second[bCount++] = {row, col}; ++col; }
            }
            assert_(aCount > 0 && aCount == bCount && aCount < 16, "模板的本体尺寸不符合目录约束");
            appendPattern(pool, table, first, second, aCount);
        }
        for (int pattern = 0; pattern < (int)table.patterns.count; ++pattern) buildPatternBlockers(pool, table, buildChildren, buildChildWalls, pattern);
        buildNodes.push_back({{0, 0}, -1, -1, -1});
        for (int pattern = 0; pattern < (int)table.patterns.count; ++pattern) {
            const TemplateLongTermRisk::Pool::Pattern &entry = pool.patterns.data.data[table.patterns.offset + pattern];
            const TemplateLongTermRisk::Pool::Point *points = pool.points.data.data + table.points.offset + entry.pointBegin;
            int node = 0;
            for (int slot = 1; slot < entry.coreCount; ++slot) {
                int child = buildNodes.data[node].firstChild;
                while (child != -1 && !samePoint(buildNodes.data[child].point, points[slot])) child = buildNodes.data[child].nextSibling;
                if (child == -1) {
                    child = (int)buildNodes.size;
                    buildNodes.push_back({points[slot], -1, buildNodes.data[node].firstChild, -1});
                    buildNodes.data[node].firstChild = child;
                }
                node = child;
            }
            buildOutputs.push_back({pattern, buildNodes.data[node].outputHead});
            buildNodes.data[node].outputHead = (int)buildOutputs.size - 1;
        }
        for (int child = buildNodes.data[0].firstChild; child != -1; child = buildNodes.data[child].nextSibling) emitCheck(pool, table, buildNodes, buildOutputs, child);
    }

    static int getTable(TemplateLongTermRisk::Pool &pool, vector<TemplateLongTermRisk::Pool::BuildNode> &buildNodes, vector<TemplateLongTermRisk::Pool::BuildOutput> &buildOutputs, vector<TemplateLongTermRisk::Pool::BuildChild> &buildChildren, vector<TemplateLongTermRisk::Pool::Point> &buildChildWalls, int prefix) {
        for (int i = 0; i < (int)pool.tables.size; ++i) if (pool.tables.data[i].prefix == prefix) return i;
        TemplateLongTermRisk::Pool::Table table(prefix, pool);
        compileTable(pool, table, buildNodes, buildOutputs, buildChildren, buildChildWalls);
        pool.tables.push_back(table);
        return pool.tables.size - 1;
    }

    static int cell(const TemplateLongTermRisk::Result &result, int row, int col, const TemplateLongTermRisk::Pool::Point &point) {
        row += point.row; col += point.col;
        return row < 0 || row >= result.rows || col < 0 || col >= result.cols ? -1 : row * result.cols + col;
    }

    static void consider(TemplateLongTermRisk::Result &result, int patternId, int row, int col, const PositionState &state) {
        const TemplateLongTermRisk::Pool::Table &table = result.pool->tables.data[result.tableIndex];
        const TemplateLongTermRisk::Pool::Pattern &pattern = result.pool->patterns.data.data[table.patterns.offset + patternId];
        const TemplateLongTermRisk::Pool::Point *points = result.pool->points.data.data + table.points.offset + pattern.pointBegin;
        int missing = 0;
        long double sealProbability = 1.0L;
        for (int i = 0; i < pattern.wallCount; ++i) {
            const int id = cell(result, row, col, points[pattern.coreCount + i]);
            if (id == -1 || result.probabilities.data[id] >= 1.0L - 1e-12L) continue;
            sealProbability *= result.probabilities.data[id];
            if (sealProbability < 0.01L) return;
            ++missing;
        }
        if (pattern.aCount >= 4 && state.basic.unknownSum >= 9) {
            int first = 0, second = 0;
            for (int i = 0; i < pattern.coreCount; ++i) {
                const int id = cell(result, row, col, points[i]);
                if (state.basic.marks[id / result.cols][id % result.cols] == Basic::Mark::T) (i < pattern.aCount ? first : second)++;
            }
            if (first >= 4 && second >= 4) return;
        }
        if (pattern.aCount + missing > state.board.totalMines - state.basic.mineSum) return;
        appendMatch(result, patternId, row, col);
    }

    static bool containsCell(const vector<ObservedBoard::CellId> &cells, int cell) {
        for (int i = 0; i < (int)cells.size; ++i) if (cells.data[i] == cell) return true;
        return false;
    }

    static void appendCell(vector<ObservedBoard::CellId> &cells, int cell) {
        if (!containsCell(cells, cell)) cells.push_back(cell);
    }

    static void sortCells(vector<ObservedBoard::CellId> &cells) {
        for (int i = 1; i < (int)cells.size; ++i) {
            const int value = cells.data[i];
            int j = i;
            while (j > 0 && value < cells.data[j - 1]) { cells.data[j] = cells.data[j - 1]; --j; }
            cells.data[j] = value;
        }
    }

    static void sortCellRange(vector<ObservedBoard::CellId> &cells, int begin, int count) {
        for (int i = 1; i < count; ++i) {
            const int value = cells.data[begin + i];
            int j = i;
            while (j > 0 && value < cells.data[begin + j - 1]) { cells.data[begin + j] = cells.data[begin + j - 1]; --j; }
            cells.data[begin + j] = value;
        }
    }

    static bool subsetCells(const vector<ObservedBoard::CellId> &cells, int cellBegin, int cellCount, const vector<ObservedBoard::CellId> &other, int otherBegin, int otherCount) {
        int i = 0, j = 0;
        while (i < cellCount && j < otherCount) {
            const int a = cells.data[cellBegin + i], b = other.data[otherBegin + j];
            if (a == b) { ++i; ++j; }
            else if (a > b) ++j;
            else return false;
        }
        return i == cellCount;
    }

    static bool sameCellRange(const vector<ObservedBoard::CellId> &cells, int cellBegin, int cellCount, const vector<ObservedBoard::CellId> &other, int otherBegin, int otherCount) {
        return cellCount == otherCount && subsetCells(cells, cellBegin, cellCount, other, otherBegin, otherCount);
    }

    static bool baseSubset(const TemplateLongTermRisk::Result &result, const TemplateLongTermRisk::Result::Event &narrow, const TemplateLongTermRisk::Result::Event &broad) {
        return subsetCells(result.eventCells, narrow.baseMineBegin, narrow.baseMineCount, result.eventCells, broad.baseMineBegin, broad.baseMineCount) && subsetCells(result.eventCells, narrow.baseSafeBegin, narrow.baseSafeCount, result.eventCells, broad.baseSafeBegin, broad.baseSafeCount);
    }

    static bool baseSafeEqual(const TemplateLongTermRisk::Result &result, const TemplateLongTermRisk::Result::Event &a, const TemplateLongTermRisk::Result::Event &b) {
        return sameCellRange(result.eventCells, a.baseSafeBegin, a.baseSafeCount, result.eventCells, b.baseSafeBegin, b.baseSafeCount);
    }

    static void addBlockerPoint(const TemplateLongTermRisk::Result &result, const vector<ObservedBoard::CellId> &mines, const vector<ObservedBoard::CellId> &safes, vector<ObservedBoard::CellId> &eventBlockerCells, const TemplateLongTermRisk::Result::Match &match, const TemplateLongTermRisk::Pool::Point &point, bool &possible) {
        const int id = cell(result, match.row, match.col, point);
        if (id == -1) return;
        if (result.probabilities.data[id] == 0.0L || containsCell(safes, id)) { possible = false; return; }
        if (result.probabilities.data[id] < 1.0L && !containsCell(mines, id)) eventBlockerCells.push_back(id);
    }

    static void appendBlocker(const TemplateLongTermRisk::Pool::Blocker &blocker, const TemplateLongTermRisk::Pool::Pattern &pattern, const TemplateLongTermRisk::Result::Match &match, const TemplateLongTermRisk::Result &result, const vector<ObservedBoard::CellId> &mines, const vector<ObservedBoard::CellId> &safes, vector<ObservedBoard::CellId> &eventBlockerCells, bool &possible) {
        const TemplateLongTermRisk::Pool::Table &table = result.pool->tables.data[result.tableIndex];
        const TemplateLongTermRisk::Pool::Point *points = result.pool->points.data.data + table.points.offset + pattern.pointBegin;
        const TemplateLongTermRisk::Pool::Point *extra = result.pool->points.data.data + table.points.offset + pattern.extraBegin;
        for (unsigned long long bits = blocker.core; bits && possible; bits &= bits - 1) addBlockerPoint(result, mines, safes, eventBlockerCells, match, points[std::countr_zero(bits)], possible);
        for (unsigned long long bits = blocker.parent; bits && possible; bits &= bits - 1) addBlockerPoint(result, mines, safes, eventBlockerCells, match, points[pattern.coreCount + std::countr_zero(bits)], possible);
        for (unsigned long long bits = blocker.extra; bits && possible; bits &= bits - 1) addBlockerPoint(result, mines, safes, eventBlockerCells, match, extra[std::countr_zero(bits)], possible);
    }

    static void removeBlocker(vector<TemplateLongTermRisk::Workspace::BlockerWork> &eventBlockers, int index) {
        for (int i = index + 1; i < (int)eventBlockers.size; ++i) eventBlockers.data[i - 1] = eventBlockers.data[i];
        --eventBlockers.size;
    }

    static bool workSubset(const vector<ObservedBoard::CellId> &eventBlockerCells, const TemplateLongTermRisk::Workspace::BlockerWork &a, const TemplateLongTermRisk::Workspace::BlockerWork &b) {
        int i = 0, j = 0;
        while (i < a.cellCount && j < b.cellCount) {
            const int x = eventBlockerCells.data[a.cellBegin + i], y = eventBlockerCells.data[b.cellBegin + j];
            if (x == y) { ++i; ++j; }
            else if (x > y) ++j;
            else return false;
        }
        return i == a.cellCount;
    }

    static void normalizeBlockers(vector<TemplateLongTermRisk::Workspace::BlockerWork> &eventBlockers, vector<ObservedBoard::CellId> &eventBlockerCells, vector<ObservedBoard::CellId> &safes, bool &possible) {
        bool changed = true;
        while (changed && possible) {
            changed = false;
            for (int i = 0; i < (int)eventBlockers.size;) {
                TemplateLongTermRisk::Workspace::BlockerWork &blocker = eventBlockers.data[i];
                bool safe = false;
                int used = 0;
                for (int j = 0; j < blocker.cellCount; ++j) {
                    const int id = eventBlockerCells.data[blocker.cellBegin + j];
                    if (containsCell(safes, id)) safe = true;
                    else eventBlockerCells.data[blocker.cellBegin + used++] = id;
                }
                blocker.cellCount = used;
                if (safe) { removeBlocker(eventBlockers, i); changed = true; continue; }
                if (!used) { possible = false; break; }
                if (used == 1) { appendCell(safes, eventBlockerCells.data[blocker.cellBegin]); removeBlocker(eventBlockers, i); changed = true; continue; }
                ++i;
            }
        }
        if (!possible) return;
        for (int i = 0; i < (int)eventBlockers.size; ++i) for (int j = i + 1; j < (int)eventBlockers.size;) {
            const TemplateLongTermRisk::Workspace::BlockerWork a = eventBlockers.data[i], b = eventBlockers.data[j];
            const bool aInB = workSubset(eventBlockerCells, a, b), bInA = workSubset(eventBlockerCells, b, a);
            if (aInB) removeBlocker(eventBlockers, j);
            else if (bInA) { removeBlocker(eventBlockers, i); --i; break; }
            else ++j;
        }
    }

    static void prepareEvent(TemplateLongTermRisk::Result &result, int eventIndex, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<TemplateLongTermRisk::Workspace::BlockerWork> &eventBlockers, vector<ObservedBoard::CellId> &eventBlockerCells) {
        TemplateLongTermRisk::Result::Event &event = result.events.data[eventIndex];
        if (event.prepared) return;
        const TemplateLongTermRisk::Result::Match &match = result.matches.data[event.match];
        const TemplateLongTermRisk::Pool::Table &table = result.pool->tables.data[result.tableIndex];
        const TemplateLongTermRisk::Pool::Pattern &pattern = result.pool->patterns.data.data[table.patterns.offset + match.pattern];
        const TemplateLongTermRisk::Pool::Point *points = result.pool->points.data.data + table.points.offset + pattern.pointBegin;
        mines.resize(0); safes.resize(0);
        for (int i = 0; i < pattern.coreCount; ++i) {
            const int id = cell(result, match.row, match.col, points[i]);
            const bool mine = (i < pattern.aCount) == (event.direction == 0);
            (mine ? mines : safes).push_back(id);
        }
        for (int i = 0; i < pattern.wallCount; ++i) {
            const int id = cell(result, match.row, match.col, points[pattern.coreCount + i]);
            if (id != -1 && result.probabilities.data[id] < 1.0L - 1e-12L) mines.push_back(id);
        }
        eventBlockers.resize(0); eventBlockerCells.resize(0);
        bool possible = true;
        if (event.kind == 1) for (int groupIndex = pattern.groupBegin; groupIndex < pattern.groupEnd && possible; ++groupIndex) {
            const TemplateLongTermRisk::Pool::Group &group = result.pool->groups.data.data[table.groups.offset + groupIndex];
            for (int blockerIndex = group.blockerBegin; blockerIndex < group.blockerEnd && possible; ++blockerIndex) {
                const int begin = (int)eventBlockerCells.size;
                bool blockerPossible = true;
                appendBlocker(result.pool->blockers.data.data[table.blockers.offset + blockerIndex], pattern, match, result, mines, safes, eventBlockerCells, blockerPossible);
                if (!blockerPossible) { eventBlockerCells.resize(begin); continue; }
                sortCellRange(eventBlockerCells, begin, (int)eventBlockerCells.size - begin);
                const int count = (int)eventBlockerCells.size - begin;
                if (!count) { possible = false; break; }
                eventBlockers.push_back({begin, count});
            }
        }
        if (event.kind == 1) normalizeBlockers(eventBlockers, eventBlockerCells, safes, possible);
        sortCells(mines); sortCells(safes);
        event.baseMineBegin = (int)result.eventCells.size; event.baseMineCount = (int)mines.size;
        for (int i = 0; i < (int)mines.size; ++i) result.eventCells.push_back(mines.data[i]);
        event.baseSafeBegin = (int)result.eventCells.size; event.baseSafeCount = (int)safes.size;
        for (int i = 0; i < (int)safes.size; ++i) result.eventCells.push_back(safes.data[i]);
        event.blockerBegin = (int)result.blockerSets.size; event.blockerCount = possible ? (int)eventBlockers.size : 0;
        for (int i = 0; i < event.blockerCount; ++i) {
            const TemplateLongTermRisk::Workspace::BlockerWork &blocker = eventBlockers.data[i];
            const int begin = (int)result.blockerCells.size;
            for (int j = 0; j < blocker.cellCount; ++j) result.blockerCells.push_back(eventBlockerCells.data[blocker.cellBegin + j]);
            result.blockerSets.push_back({begin, blocker.cellCount});
        }
        event.possible = possible;
        event.prepared = true;
    }

    static unsigned long long signatureHash(const vector<unsigned short> &signature) {
        unsigned long long hash = 1469598103934665603ULL;
        for (int i = 0; i < (int)signature.size; ++i) { hash ^= signature.data[i]; hash *= 1099511628211ULL; }
        return hash;
    }

    static long double countForces(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) {
        int used = 0;
        for (int i = 0; i < (int)mines.size; ++i) {
            const int id = mines.data[i];
            const Basic::Mark mark = state.basic.marks[id / result.cols][id % result.cols];
            if (mark == Basic::Mark::S) return 0.0L;
            if (mark != Basic::Mark::F) mines.data[used++] = id;
        }
        mines.resize(used);
        used = 0;
        for (int i = 0; i < (int)safes.size; ++i) {
            const int id = safes.data[i];
            const Basic::Mark mark = state.basic.marks[id / result.cols][id % result.cols];
            if (mark == Basic::Mark::F) return 0.0L;
            if (mark != Basic::Mark::S) safes.data[used++] = id;
        }
        safes.resize(used);
        const int signatureSize = 2 * ((int)state.probability.boxProbabilities.size + 1);
        countSignature.resize(signatureSize);
        for (int i = 0; i < signatureSize; ++i) countSignature.data[i] = 0;
        for (int side = 0; side < 2; ++side) {
            const vector<ObservedBoard::CellId> &cells = side ? safes : mines;
            for (int i = 0; i < (int)cells.size; ++i) {
                const int id = cells.data[i];
                const Structure::Result::CellLocation &where = state.structure.cellLoc.data[id];
                const int box = where.box < 0 ? 0 : state.probability.components.data[where.component].boxOffset + where.box + 1;
                ++countSignature.data[2 * box + side];
            }
        }
        const unsigned long long hash = signatureHash(countSignature);
        int *head = result.countIndex.find(hash);
        if (head != nullptr) for (int index = *head; index != -1; index = result.counts.data[index].next) {
            const TemplateLongTermRisk::Result::CachedCount &entry = result.counts.data[index];
            if (entry.keyLength != signatureSize) continue;
            bool same = true;
            for (int i = 0; i < signatureSize; ++i) if (result.countKeys.data[entry.keyBegin + i] != countSignature.data[i]) { same = false; break; }
            if (same) return entry.value;
        }
        const long double value = state.countCandidatesWithForces({mines.data, (int)mines.size}, {safes.data, (int)safes.size}, shapeConfig, basic, structure, probability, changedCells, forcedProbability);
        const int keyBegin = (int)result.countKeys.size, next = head == nullptr ? -1 : *head;
        for (int i = 0; i < signatureSize; ++i) result.countKeys.push_back(countSignature.data[i]);
        const int index = (int)result.counts.size;
        result.counts.push_back({keyBegin, signatureSize, next, value});
        result.countIndex[hash] = index;
        return value;
    }

    static long double eventProbability(TemplateLongTermRisk::Result &result, int eventIndex, PositionState &state, const ShapeSolverConfig &shapeConfig, int boardWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<TemplateLongTermRisk::Workspace::BlockerWork> &eventBlockers, vector<ObservedBoard::CellId> &eventBlockerCells, vector<int> &expressions, vector<int> &nextExpressions, vector<unsigned long long> &expressionWords, vector<unsigned long long> &nextExpressionWords, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) {
        TemplateLongTermRisk::Result::Event &event = result.events.data[eventIndex];
        if (event.probabilityValid) return event.probability;
        prepareEvent(result, eventIndex, mines, safes, eventBlockers, eventBlockerCells);
        if (!event.possible) { event.probability = 0.0L; event.probabilityValid = true; return 0.0L; }
        if (!event.blockerCount) {
            mines.resize(0); safes.resize(0);
            for (int i = 0; i < event.baseMineCount; ++i) mines.push_back(result.eventCells.data[event.baseMineBegin + i]);
            for (int i = 0; i < event.baseSafeCount; ++i) safes.push_back(result.eventCells.data[event.baseSafeBegin + i]);
            event.probability = countForces(result, state, shapeConfig, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
            event.probabilityValid = true;
            return event.probability;
        }
        expressions.resize(1); expressionWords.resize(boardWords);
        for (int i = 0; i < boardWords; ++i) expressionWords.data[i] = 0;
        expressions.data[0] = 1;
        for (int blockerIndex = event.blockerBegin; blockerIndex < event.blockerBegin + event.blockerCount; ++blockerIndex) {
            const int oldCount = (int)expressions.size;
            nextExpressions.resize(0); nextExpressionWords.resize(0);
            for (int i = 0; i < oldCount; ++i) {
                const int begin = (int)nextExpressionWords.size;
                nextExpressionWords.resize(begin + boardWords);
                for (int word = 0; word < boardWords; ++word) nextExpressionWords.data[begin + word] = expressionWords.data[i * boardWords + word];
                nextExpressions.push_back(expressions.data[i]);
            }
            const TemplateLongTermRisk::Result::BlockerSet &blocker = result.blockerSets.data[blockerIndex];
            for (int i = 0; i < oldCount; ++i) {
                const int begin = (int)nextExpressionWords.size;
                nextExpressionWords.resize(begin + boardWords);
                for (int word = 0; word < boardWords; ++word) nextExpressionWords.data[begin + word] = expressionWords.data[i * boardWords + word];
                for (int j = 0; j < blocker.cellCount; ++j) { const int id = result.blockerCells.data[blocker.cellBegin + j]; nextExpressionWords.data[begin + id / 64] |= 1ULL << (id % 64); }
                int existing = 0;
                while (existing < (int)nextExpressions.size) {
                    bool same = true;
                    for (int word = 0; word < boardWords; ++word) if (nextExpressionWords.data[existing * boardWords + word] != nextExpressionWords.data[begin + word]) { same = false; break; }
                    if (same) break;
                    ++existing;
                }
                if (existing < (int)nextExpressions.size) { nextExpressions.data[existing] -= expressions.data[i]; nextExpressionWords.resize(begin); }
                else nextExpressions.push_back(-expressions.data[i]);
            }
            int used = 0;
            for (int i = 0; i < (int)nextExpressions.size; ++i) if (nextExpressions.data[i]) {
                if (used != i) {
                    for (int word = 0; word < boardWords; ++word) nextExpressionWords.data[used * boardWords + word] = nextExpressionWords.data[i * boardWords + word];
                    nextExpressions.data[used] = nextExpressions.data[i];
                }
                ++used;
            }
            nextExpressions.resize(used); nextExpressionWords.resize(used * boardWords);
            expressions.swap(nextExpressions); expressionWords.swap(nextExpressionWords);
        }
        long double total = 0.0L;
        for (int i = 0; i < (int)expressions.size; ++i) {
            const int coefficient = expressions.data[i];
            if (!coefficient) continue;
            mines.resize(0); safes.resize(0);
            for (int j = 0; j < event.baseMineCount; ++j) mines.push_back(result.eventCells.data[event.baseMineBegin + j]);
            for (int j = 0; j < event.baseSafeCount; ++j) safes.push_back(result.eventCells.data[event.baseSafeBegin + j]);
            for (int word = 0; word < boardWords; ++word) for (unsigned long long bits = expressionWords.data[i * boardWords + word]; bits; bits &= bits - 1) mines.push_back(word * 64 + std::countr_zero(bits));
            total += coefficient * countForces(result, state, shapeConfig, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
        }
        event.probability = total;
        event.probabilityValid = true;
        return total;
    }

    static bool isGroupCell(const vector<ObservedBoard::CellId> &groupCells, int cell) {
        for (int i = 0; i < (int)groupCells.size; ++i) if (groupCells.data[i] == cell) return true;
        return false;
    }

    static void gatherTerms(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, int boardWords, const vector<ObservedBoard::CellId> &groupCells, vector<int> &pendingMatches, vector<TemplateLongTermRisk::Workspace::Term> &terms, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<TemplateLongTermRisk::Workspace::BlockerWork> &eventBlockers, vector<ObservedBoard::CellId> &eventBlockerCells, vector<int> &expressions, vector<int> &nextExpressions, vector<unsigned long long> &expressionWords, vector<unsigned long long> &nextExpressionWords, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) {
        pendingMatches.resize(0); terms.resize(0);
        for (int i = 0; i < (int)groupCells.size; ++i) {
            const int id = groupCells.data[i];
            for (int link = result.offsets.data[id]; link < result.offsets.data[id + 1]; ++link) pendingMatches.push_back(result.links.data[link].match);
        }
        for (int i = 1; i < (int)pendingMatches.size; ++i) {
            const int value = pendingMatches.data[i];
            int j = i;
            while (j > 0 && value < pendingMatches.data[j - 1]) { pendingMatches.data[j] = pendingMatches.data[j - 1]; --j; }
            pendingMatches.data[j] = value;
        }
        int matchCount = 0;
        for (int i = 0; i < (int)pendingMatches.size; ++i) if (!matchCount || pendingMatches.data[i] != pendingMatches.data[matchCount - 1]) pendingMatches.data[matchCount++] = pendingMatches.data[i];
        pendingMatches.resize(matchCount);
        const TemplateLongTermRisk::Pool::Table &table = result.pool->tables.data[result.tableIndex];
        for (int i = 0; i < matchCount; ++i) {
            const int matchIndex = pendingMatches.data[i];
            const TemplateLongTermRisk::Result::Match &match = result.matches.data[matchIndex];
            const TemplateLongTermRisk::Pool::Pattern &pattern = result.pool->patterns.data.data[table.patterns.offset + match.pattern];
            const TemplateLongTermRisk::Pool::Point *points = result.pool->points.data.data + table.points.offset + pattern.pointBegin;
            for (int slot = 0; slot < pattern.coreCount + pattern.wallCount; ++slot) {
                const int id = cell(result, match.row, match.col, points[slot]);
                if (id < 0 || !isGroupCell(groupCells, id)) continue;
                if (slot < pattern.coreCount) {
                    const int direction = slot < pattern.aCount ? 0 : 1;
                    const int event = match.eventBegin + direction * 2;
                    terms.push_back({event, matchIndex, 1.0L, eventProbability(result, event, state, shapeConfig, boardWords, mines, safes, eventBlockers, eventBlockerCells, expressions, nextExpressions, expressionWords, nextExpressionWords, countSignature, changedCells, forcedProbability, basic, structure, probability)});
                } else for (int direction = 0; direction < 2; ++direction) {
                    const int event = match.eventBegin + direction * 2 + 1;
                    terms.push_back({event, matchIndex, 0.5L, eventProbability(result, event, state, shapeConfig, boardWords, mines, safes, eventBlockers, eventBlockerCells, expressions, nextExpressions, expressionWords, nextExpressionWords, countSignature, changedCells, forcedProbability, basic, structure, probability)});
                }
            }
        }
    }

    static void pruneTerms(const TemplateLongTermRisk::Result &result, vector<TemplateLongTermRisk::Workspace::Term> &terms, vector<int> &pendingEvents) {
        const int count = (int)terms.size;
        pendingEvents.resize(count);
        for (int i = 0; i < count; ++i) pendingEvents.data[i] = 1;
        for (int i = 0; i < count; ++i) {
            const TemplateLongTermRisk::Workspace::Term &term = terms.data[i];
            const TemplateLongTermRisk::Result::Event &event = result.events.data[term.event];
            if (term.weight == 1.0L) {
                for (int j = 0; j < count; ++j) {
                    if (i == j || terms.data[j].weight != 1.0L) continue;
                    const TemplateLongTermRisk::Result::Event &other = result.events.data[terms.data[j].event];
                    if (!baseSubset(result, other, event)) continue;
                    const bool equal = baseSubset(result, event, other);
                    if (!equal || j < i) { pendingEvents.data[i] = 0; break; }
                }
            } else {
                for (int j = 0; j < i; ++j) {
                    const TemplateLongTermRisk::Workspace::Term &earlier = terms.data[j];
                    if (earlier.weight != 0.5L || earlier.family != term.family) continue;
                    if (baseSafeEqual(result, result.events.data[earlier.event], event)) { pendingEvents.data[i] = 0; break; }
                }
            }
        }
        int used = 0;
        for (int i = 0; i < count; ++i) if (pendingEvents.data[i]) terms.data[used++] = terms.data[i];
        terms.resize(used);
    }

    static int buildRankOrder(const vector<TemplateLongTermRisk::Workspace::Term> &terms, vector<int> &order) {
        order.resize(0);
        for (int i = 0; i < (int)terms.size; ++i) {
            const long double mass = terms.data[i].mass;
            if (mass <= 0.0L) continue;
            int position = (int)order.size;
            while (position > 0 && mass > terms.data[order.data[position - 1]].mass) --position;
            order.resize(order.size + 1);
            for (int j = (int)order.size - 1; j > position; --j) order.data[j] = order.data[j - 1];
            order.data[position] = i;
        }
        return (int)order.size;
    }

    static void cacheGroup(TemplateLongTermRisk::Result &result, const vector<ObservedBoard::CellId> &groupCells, long double value) {
        for (int i = 0; i < (int)groupCells.size; ++i) {
            TemplateLongTermRisk::Result::CellCache &cache = result.cells.data[groupCells.data[i]];
            cache.influence = value;
            cache.valid = true;
        }
    }

    static void addSupport(vector<int> &denseCells, vector<ObservedBoard::CellId> &originalCells, int cell) {
        if (denseCells.data[cell] != -1) return;
        denseCells.data[cell] = (int)originalCells.size;
        originalCells.push_back(cell);
    }

    static int buildSupport(const TemplateLongTermRisk::Result &result, const vector<TemplateLongTermRisk::Workspace::Term> &terms, const vector<int> &order, vector<int> &denseCells, vector<ObservedBoard::CellId> &originalCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<TemplateLongTermRisk::Workspace::Node> &nodes, FlatHashTable<unsigned long long, int> &nodeIndex, vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, vector<TemplateLongTermRisk::Workspace::Factor> &factors, vector<int> &factorBlockers, int rootCell, int selectedCount) {
        denseCells.resize((int)result.probabilities.size);
        for (int i = 0; i < (int)denseCells.size; ++i) denseCells.data[i] = -1;
        originalCells.resize(0);
        addSupport(denseCells, originalCells, rootCell);
        for (int rank = 0; rank < selectedCount; ++rank) {
            const TemplateLongTermRisk::Workspace::Term &term = terms.data[order.data[rank]];
            const TemplateLongTermRisk::Result::Event &event = result.events.data[term.event];
            for (int i = 0; i < event.baseMineCount; ++i) addSupport(denseCells, originalCells, result.eventCells.data[event.baseMineBegin + i]);
            for (int i = 0; i < event.baseSafeCount; ++i) addSupport(denseCells, originalCells, result.eventCells.data[event.baseSafeBegin + i]);
            for (int i = 0; i < event.blockerCount; ++i) {
                const TemplateLongTermRisk::Result::BlockerSet &blocker = result.blockerSets.data[event.blockerBegin + i];
                for (int j = 0; j < blocker.cellCount; ++j) addSupport(denseCells, originalCells, result.blockerCells.data[blocker.cellBegin + j]);
            }
        }
        const int words = ((int)originalCells.size + 63) / 64;
        domainIndex.clear(); nodeIndex.clear(); tailIndex.clear();
        domains.resize(0); domainMineWords.resize(words); domainSafeWords.resize(words);
        for (int i = 0; i < words; ++i) { domainMineWords.data[i] = 0; domainSafeWords.data[i] = 0; }
        domains.push_back({-1, 0.0L});
        nodes.resize(0); tails.resize(0); factors.resize(0); factorBlockers.resize(0);
        tails.push_back({-1, 0});
        return words;
    }

    static unsigned long long domainHash(int words, const vector<unsigned long long> &mines, const vector<unsigned long long> &safes) {
        unsigned long long hash = 1469598103934665603ULL;
        for (int i = 0; i < words; ++i) { hash ^= mines.data[i]; hash *= 1099511628211ULL; hash ^= safes.data[i]; hash *= 1099511628211ULL; }
        return hash;
    }

    static bool sameDomain(int words, const vector<unsigned long long> &domainMineWords, const vector<unsigned long long> &domainSafeWords, int domain, const vector<unsigned long long> &mines, const vector<unsigned long long> &safes) {
        const int begin = domain * words;
        for (int i = 0; i < words; ++i) if (domainMineWords.data[begin + i] != mines.data[i] || domainSafeWords.data[begin + i] != safes.data[i]) return false;
        return true;
    }

    static long double countDomain(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, const vector<unsigned long long> &mineBits, const vector<unsigned long long> &safeBits) {
        mines.resize(0); safes.resize(0);
        for (int word = 0; word < words; ++word) {
            for (unsigned long long bits = mineBits.data[word]; bits; bits &= bits - 1) mines.push_back(originalCells.data[word * 64 + std::countr_zero(bits)]);
            for (unsigned long long bits = safeBits.data[word]; bits; bits &= bits - 1) safes.push_back(originalCells.data[word * 64 + std::countr_zero(bits)]);
        }
        return countForces(result, state, shapeConfig, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
    }

    static int internDomain(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, const vector<unsigned long long> &mineBits, const vector<unsigned long long> &safeBits) {
        for (int i = 0; i < words; ++i) if (mineBits.data[i] & safeBits.data[i]) return 0;
        const unsigned long long hash = domainHash(words, mineBits, safeBits);
        int *head = domainIndex.find(hash);
        if (head != nullptr) for (int index = *head; index != -1; index = domains.data[index].next) if (sameDomain(words, domainMineWords, domainSafeWords, index, mineBits, safeBits)) return index;
        const long double mass = countDomain(result, state, shapeConfig, words, originalCells, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, mineBits, safeBits);
        if (mass == 0.0L) return 0;
        const int index = (int)domains.size, begin = index * words;
        domainMineWords.resize(begin + words); domainSafeWords.resize(begin + words);
        for (int i = 0; i < words; ++i) { domainMineWords.data[begin + i] = mineBits.data[i]; domainSafeWords.data[begin + i] = safeBits.data[i]; }
        domains.push_back({head == nullptr ? -1 : *head, mass});
        domainIndex[hash] = index;
        return index;
    }

    static int mergeDomain(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mergeMineWords, vector<unsigned long long> &mergeSafeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, int first, int second) {
        const int a = first * words, b = second * words;
        mergeMineWords.resize(words); mergeSafeWords.resize(words);
        for (int i = 0; i < words; ++i) {
            const unsigned long long joinedMines = domainMineWords.data[a + i] | domainMineWords.data[b + i];
            const unsigned long long joinedSafes = domainSafeWords.data[a + i] | domainSafeWords.data[b + i];
            if (joinedMines & joinedSafes) return 0;
            mergeMineWords.data[i] = joinedMines; mergeSafeWords.data[i] = joinedSafes;
        }
        return internDomain(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, mergeMineWords, mergeSafeWords);
    }

    static int packEvent(TemplateLongTermRisk::Result &result, int eventIndex, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, const vector<int> &denseCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mineWords, vector<unsigned long long> &safeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) {
        const TemplateLongTermRisk::Result::Event &event = result.events.data[eventIndex];
        mineWords.resize(words); safeWords.resize(words);
        for (int i = 0; i < words; ++i) { mineWords.data[i] = 0; safeWords.data[i] = 0; }
        for (int i = 0; i < event.baseMineCount; ++i) { const int dense = denseCells.data[result.eventCells.data[event.baseMineBegin + i]]; mineWords.data[dense / 64] |= 1ULL << (dense % 64); }
        for (int i = 0; i < event.baseSafeCount; ++i) { const int dense = denseCells.data[result.eventCells.data[event.baseSafeBegin + i]]; safeWords.data[dense / 64] |= 1ULL << (dense % 64); }
        return internDomain(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, mineWords, safeWords);
    }

    static int packBlocker(TemplateLongTermRisk::Result &result, const TemplateLongTermRisk::Result::BlockerSet &blocker, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, const vector<int> &denseCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mineWords, vector<unsigned long long> &safeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) {
        mineWords.resize(words); safeWords.resize(words);
        for (int i = 0; i < words; ++i) { mineWords.data[i] = 0; safeWords.data[i] = 0; }
        for (int i = 0; i < blocker.cellCount; ++i) { const int dense = denseCells.data[result.blockerCells.data[blocker.cellBegin + i]]; mineWords.data[dense / 64] |= 1ULL << (dense % 64); }
        return internDomain(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, mineWords, safeWords);
    }

    static bool sameFactor(const vector<int> &factorBlockers, const TemplateLongTermRisk::Workspace::Factor &a, int base, const vector<int> &blockers, int blockerCount) {
        if (a.baseKey != base || a.blockerCount != blockerCount) return false;
        for (int i = 0; i < blockerCount; ++i) if (factorBlockers.data[a.blockerBegin + i] != blockers.data[i]) return false;
        return true;
    }

    static void appendFactor(TemplateLongTermRisk::Result &result, int eventIndex, long double weight, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, const vector<int> &denseCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mineWords, vector<unsigned long long> &safeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, vector<TemplateLongTermRisk::Workspace::Factor> &factors, vector<int> &factorBlockers, vector<int> &pendingEvents) {
        const TemplateLongTermRisk::Result::Event &event = result.events.data[eventIndex];
        const int base = packEvent(result, eventIndex, state, shapeConfig, words, originalCells, denseCells, domains, domainMineWords, domainSafeWords, domainIndex, mineWords, safeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
        pendingEvents.resize(0);
        for (int i = 0; i < event.blockerCount; ++i) {
            const TemplateLongTermRisk::Result::BlockerSet &blocker = result.blockerSets.data[event.blockerBegin + i];
            const int key = packBlocker(result, blocker, state, shapeConfig, words, originalCells, denseCells, domains, domainMineWords, domainSafeWords, domainIndex, mineWords, safeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
            if (key) pendingEvents.push_back(key);
        }
        for (int i = 1; i < (int)pendingEvents.size; ++i) {
            const int value = pendingEvents.data[i];
            int j = i;
            while (j > 0 && value < pendingEvents.data[j - 1]) { pendingEvents.data[j] = pendingEvents.data[j - 1]; --j; }
            pendingEvents.data[j] = value;
        }
        int blockerCount = 0;
        for (int i = 0; i < (int)pendingEvents.size; ++i) if (!blockerCount || pendingEvents.data[i] != pendingEvents.data[blockerCount - 1]) pendingEvents.data[blockerCount++] = pendingEvents.data[i];
        pendingEvents.resize(blockerCount);
        for (int factor = 0; factor < (int)factors.size; ++factor) {
            if (!sameFactor(factorBlockers, factors.data[factor], base, pendingEvents, blockerCount)) continue;
            const long double previous = factors.data[factor].weight;
            factors.data[factor].weight = 1.0L - (1.0L - previous) * (1.0L - weight);
            factors.data[factor].bound = (std::min)(factors.data[factor].bound, event.probability);
            return;
        }
        const int begin = (int)factorBlockers.size;
        for (int i = 0; i < blockerCount; ++i) factorBlockers.push_back(pendingEvents.data[i]);
        factors.push_back({base, begin, blockerCount, weight, event.probability});
    }

    static int factorStatus(int words, const vector<unsigned long long> &domainMineWords, const vector<unsigned long long> &domainSafeWords, const vector<int> &factorBlockers, int domain, int baseKey, int blockerBegin, int blockerCount) {
        const int known = domain * words, base = baseKey * words;
        bool certain = true;
        for (int i = 0; i < words; ++i) {
            const unsigned long long knownMines = domainMineWords.data[known + i], knownSafes = domainSafeWords.data[known + i];
            const unsigned long long baseMines = domainMineWords.data[base + i], baseSafes = domainSafeWords.data[base + i];
            if ((knownMines & baseSafes) || (knownSafes & baseMines)) return 0;
            certain &= (knownMines & baseMines) == baseMines && (knownSafes & baseSafes) == baseSafes;
        }
        for (int blocker = 0; blocker < blockerCount; ++blocker) {
            const int key = factorBlockers.data[blockerBegin + blocker] * words;
            bool allMine = true, someSafe = false;
            for (int i = 0; i < words; ++i) {
                const unsigned long long mines = domainMineWords.data[known + i], safes = domainSafeWords.data[known + i], clause = domainMineWords.data[key + i];
                allMine &= (mines & clause) == clause;
                someSafe |= (safes & clause) != 0;
            }
            if (allMine) return 0;
            certain &= someSafe;
        }
        return certain ? 2 : 1;
    }

    static int suffix(vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, int factor, int next) {
        const unsigned long long key = ((unsigned long long)(unsigned int)factor << 32) | (unsigned int)next;
        const int *found = tailIndex.find(key);
        if (found != nullptr) return *found;
        const int index = (int)tails.size;
        tails.push_back({factor, next});
        tailIndex.emplace(key, index);
        return index;
    }

    static int clauseFactor(const vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<TemplateLongTermRisk::Workspace::Factor> &factors, const vector<int> &factorBlockers, int key) {
        for (int i = 0; i < (int)factors.size; ++i) if (factors.data[i].baseKey == key && factors.data[i].blockerCount == 0 && factors.data[i].weight == 1.0L) return i;
        const int begin = (int)factorBlockers.size;
        const int index = (int)factors.size;
        factors.push_back({key, begin, 0, 1.0L, domains.data[key].mass});
        return index;
    }

    static int includeTail(const vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<TemplateLongTermRisk::Workspace::Factor> &factors, const vector<int> &factorBlockers, vector<int> &pendingEvents, vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, int tail, const TemplateLongTermRisk::Workspace::Factor &factor) {
        pendingEvents.resize(0);
        for (int i = 0; i < factor.blockerCount; ++i) pendingEvents.push_back(clauseFactor(domains, factors, factorBlockers, factorBlockers.data[factor.blockerBegin + i]));
        for (int i = 1; i < (int)pendingEvents.size; ++i) {
            const int value = pendingEvents.data[i];
            int j = i;
            while (j > 0 && value < pendingEvents.data[j - 1]) { pendingEvents.data[j] = pendingEvents.data[j - 1]; --j; }
            pendingEvents.data[j] = value;
        }
        for (int i = (int)pendingEvents.size - 1; i >= 0; --i) tail = suffix(tails, tailIndex, pendingEvents.data[i], tail);
        return tail;
    }

    static long double evaluateTree(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mergeMineWords, vector<unsigned long long> &mergeSafeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, vector<TemplateLongTermRisk::Workspace::Factor> &factors, vector<int> &factorBlockers, vector<int> &pendingEvents, vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, vector<TemplateLongTermRisk::Workspace::Node> &nodes, FlatHashTable<unsigned long long, int> &nodeIndex, int domain, int tailId) {
        if (!domain) return 0.0L;
        if (!tailId) return domains.data[domain].mass;
        const unsigned long long key = ((unsigned long long)(unsigned int)domain << 32) | (unsigned int)tailId;
        const int *cached = nodeIndex.find(key);
        if (cached != nullptr) return nodes.data[*cached].value;
        const TemplateLongTermRisk::Workspace::Tail tail = tails.data[tailId];
        const TemplateLongTermRisk::Workspace::Factor factor = factors.data[tail.factor];
        const int status = factorStatus(words, domainMineWords, domainSafeWords, factorBlockers, domain, factor.baseKey, factor.blockerBegin, factor.blockerCount);
        long double value = 0.0L;
        if (status == 0) value = evaluateTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, factors, factorBlockers, pendingEvents, tails, tailIndex, nodes, nodeIndex, domain, tail.next);
        else if (status == 2) value = (1.0L - factor.weight) * evaluateTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, factors, factorBlockers, pendingEvents, tails, tailIndex, nodes, nodeIndex, domain, tail.next);
        else {
            const int joined = mergeDomain(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, domain, factor.baseKey);
            const long double without = evaluateTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, factors, factorBlockers, pendingEvents, tails, tailIndex, nodes, nodeIndex, domain, tail.next);
            if (!joined) value = without;
            else {
                const int included = includeTail(domains, factors, factorBlockers, pendingEvents, tails, tailIndex, tail.next, factor);
                value = without - factor.weight * evaluateTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, factors, factorBlockers, pendingEvents, tails, tailIndex, nodes, nodeIndex, joined, included);
            }
        }
        const int index = (int)nodes.size;
        nodes.push_back({value});
        nodeIndex.emplace(key, index);
        return value;
    }

    static bool sameDescriptionFactor(const vector<int> &factorBlockers, const TemplateLongTermRisk::Workspace::DescriptionFactor &factor, int base, const vector<int> &blockers, int blockerCount) {
        if (factor.baseKey != base || factor.blockerCount != blockerCount) return false;
        for (int i = 0; i < blockerCount; ++i) if (factorBlockers.data[factor.blockerBegin + i] != blockers.data[i]) return false;
        return true;
    }

    static void appendDescriptionFactor(TemplateLongTermRisk::Result &result, const TemplateLongTermRisk::Workspace::Term &term, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, const vector<int> &denseCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mineWords, vector<unsigned long long> &safeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, vector<TemplateLongTermRisk::Workspace::DescriptionFactor> &descriptionFactors, vector<int> &factorBlockers, vector<int> &pendingEvents) {
        const TemplateLongTermRisk::Result::Event &event = result.events.data[term.event];
        const int base = packEvent(result, term.event, state, shapeConfig, words, originalCells, denseCells, domains, domainMineWords, domainSafeWords, domainIndex, mineWords, safeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
        pendingEvents.resize(0);
        for (int i = 0; i < event.blockerCount; ++i) {
            const TemplateLongTermRisk::Result::BlockerSet &blocker = result.blockerSets.data[event.blockerBegin + i];
            const int key = packBlocker(result, blocker, state, shapeConfig, words, originalCells, denseCells, domains, domainMineWords, domainSafeWords, domainIndex, mineWords, safeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability);
            if (key) pendingEvents.push_back(key);
        }
        for (int i = 1; i < (int)pendingEvents.size; ++i) {
            const int value = pendingEvents.data[i];
            int j = i;
            while (j > 0 && value < pendingEvents.data[j - 1]) { pendingEvents.data[j] = pendingEvents.data[j - 1]; --j; }
            pendingEvents.data[j] = value;
        }
        int blockerCount = 0;
        for (int i = 0; i < (int)pendingEvents.size; ++i) if (!blockerCount || pendingEvents.data[i] != pendingEvents.data[blockerCount - 1]) pendingEvents.data[blockerCount++] = pendingEvents.data[i];
        pendingEvents.resize(blockerCount);
        const int bodyCount = term.weight == 1.0L ? 1 : 0;
        const int restrictionCount = term.weight == 0.5L ? 1 : 0;
        for (int factor = 0; factor < (int)descriptionFactors.size; ++factor) {
            TemplateLongTermRisk::Workspace::DescriptionFactor &stored = descriptionFactors.data[factor];
            if (!sameDescriptionFactor(factorBlockers, stored, base, pendingEvents, blockerCount)) continue;
            stored.bodyCount += bodyCount;
            stored.restrictionCount += restrictionCount;
            stored.bound = (std::min)(stored.bound, event.probability);
            return;
        }
        const int begin = (int)factorBlockers.size;
        for (int i = 0; i < blockerCount; ++i) factorBlockers.push_back(pendingEvents.data[i]);
        descriptionFactors.push_back({base, begin, blockerCount, bodyCount, restrictionCount, event.probability});
    }

    static int descriptionClauseFactor(const vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<TemplateLongTermRisk::Workspace::DescriptionFactor> &descriptionFactors, const vector<int> &factorBlockers, int key) {
        for (int i = 0; i < (int)descriptionFactors.size; ++i) {
            const TemplateLongTermRisk::Workspace::DescriptionFactor &factor = descriptionFactors.data[i];
            if (factor.baseKey == key && factor.blockerCount == 0 && !factor.bodyCount && !factor.restrictionCount) return i;
        }
        const int begin = (int)factorBlockers.size;
        const int index = (int)descriptionFactors.size;
        descriptionFactors.push_back({key, begin, 0, 0, 0, domains.data[key].mass});
        return index;
    }

    static int descriptionIncludeTail(const vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<TemplateLongTermRisk::Workspace::DescriptionFactor> &descriptionFactors, const vector<int> &factorBlockers, vector<int> &pendingEvents, vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, int tail, const TemplateLongTermRisk::Workspace::DescriptionFactor &factor) {
        pendingEvents.resize(0);
        for (int i = 0; i < factor.blockerCount; ++i) pendingEvents.push_back(descriptionClauseFactor(domains, descriptionFactors, factorBlockers, factorBlockers.data[factor.blockerBegin + i]));
        for (int i = 1; i < (int)pendingEvents.size; ++i) {
            const int value = pendingEvents.data[i];
            int j = i;
            while (j > 0 && value < pendingEvents.data[j - 1]) { pendingEvents.data[j] = pendingEvents.data[j - 1]; --j; }
            pendingEvents.data[j] = value;
        }
        for (int i = (int)pendingEvents.size - 1; i >= 0; --i) tail = suffix(tails, tailIndex, pendingEvents.data[i], tail);
        return tail;
    }

    static int evaluateDescriptionTree(TemplateLongTermRisk::Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<ObservedBoard::CellId> &originalCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mergeMineWords, vector<unsigned long long> &mergeSafeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, vector<TemplateLongTermRisk::Workspace::DescriptionFactor> &descriptionFactors, vector<int> &factorBlockers, vector<int> &pendingEvents, vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, vector<TemplateLongTermRisk::Workspace::DescriptionNode> &descriptionNodes, vector<long double> &descriptionValues, FlatHashTable<unsigned long long, int> &descriptionNodeIndex, int domain, int tailId, int bins) {
        if (!domain) return -1;
        const unsigned long long key = ((unsigned long long)(unsigned int)domain << 32) | (unsigned int)tailId;
        const int *cached = descriptionNodeIndex.find(key);
        if (cached != nullptr) return *cached;
        int restNode = -1, includedNode = -1;
        int status = 0;
        int restrictionCount = 0;
        if (tailId) {
            const TemplateLongTermRisk::Workspace::Tail tail = tails.data[tailId];
            const TemplateLongTermRisk::Workspace::DescriptionFactor factor = descriptionFactors.data[tail.factor];
            status = factorStatus(words, domainMineWords, domainSafeWords, factorBlockers, domain, factor.baseKey, factor.blockerBegin, factor.blockerCount);
            restrictionCount = factor.restrictionCount;
            restNode = evaluateDescriptionTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, descriptionFactors, factorBlockers, pendingEvents, tails, tailIndex, descriptionNodes, descriptionValues, descriptionNodeIndex, domain, tail.next, bins);
            if (status == 1) {
                const int joined = mergeDomain(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, domain, factor.baseKey);
                if (joined) includedNode = evaluateDescriptionTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, descriptionFactors, factorBlockers, pendingEvents, tails, tailIndex, descriptionNodes, descriptionValues, descriptionNodeIndex, joined, descriptionIncludeTail(domains, descriptionFactors, factorBlockers, pendingEvents, tails, tailIndex, tail.next, factor), bins);
            }
        }
        const int index = (int)descriptionNodes.size;
        const int valueBegin = (int)descriptionValues.size;
        descriptionValues.resize(valueBegin + bins);
        descriptionNodes.push_back({valueBegin});
        if (!tailId) {
            descriptionValues.data[valueBegin] = domains.data[domain].mass;
            for (int k = 1; k < bins; ++k) descriptionValues.data[valueBegin + k] = 0.0L;
        } else if (status == 2) {
            if (restrictionCount && !descriptionFactors.data[tails.data[tailId].factor].bodyCount) {
                const int restBegin = descriptionNodes.data[restNode].valueBegin;
                for (int k = 0; k < bins; ++k) descriptionValues.data[valueBegin + k] = k >= restrictionCount ? descriptionValues.data[restBegin + k - restrictionCount] : 0.0L;
            } else for (int k = 0; k < bins; ++k) descriptionValues.data[valueBegin + k] = 0.0L;
        } else {
            const int restBegin = descriptionNodes.data[restNode].valueBegin;
            int includedBegin = -1;
            if (includedNode >= 0) includedBegin = descriptionNodes.data[includedNode].valueBegin;
            const TemplateLongTermRisk::Workspace::DescriptionFactor &factor = descriptionFactors.data[tails.data[tailId].factor];
            for (int k = 0; k < bins; ++k) {
                const long double without = descriptionValues.data[restBegin + k];
                const long double included = includedBegin < 0 ? 0.0L : descriptionValues.data[includedBegin + k];
                if (factor.restrictionCount && !factor.bodyCount) {
                    const long double shifted = k >= factor.restrictionCount && includedBegin >= 0 ? descriptionValues.data[includedBegin + k - factor.restrictionCount] : 0.0L;
                    descriptionValues.data[valueBegin + k] = without - included + shifted;
                } else descriptionValues.data[valueBegin + k] = without - included;
            }
        }
        descriptionNodeIndex.emplace(key, index);
        return index;
    }

    static void cacheDescription(TemplateLongTermRisk::Result &result, const vector<ObservedBoard::CellId> &groupCells, int begin, int bins, long double bodyUnion) {
        for (int i = 0; i < (int)groupCells.size; ++i) {
            TemplateLongTermRisk::Result::CellCache &cache = result.cells.data[groupCells.data[i]];
            cache.noBodyRestrictionBegin = begin;
            cache.noBodyRestrictionBins = bins;
            cache.bodyUnion = bodyUnion;
            cache.descriptionValid = true;
        }
    }

    static long double buildFactors(TemplateLongTermRisk::Result &result, int rootCell, int selectedCount, PositionState &state, const ShapeSolverConfig &shapeConfig, int words, const vector<TemplateLongTermRisk::Workspace::Term> &terms, vector<int> &order, const vector<ObservedBoard::CellId> &originalCells, const vector<int> &denseCells, vector<TemplateLongTermRisk::Workspace::Domain> &domains, vector<unsigned long long> &domainMineWords, vector<unsigned long long> &domainSafeWords, FlatHashTable<unsigned long long, int> &domainIndex, vector<unsigned long long> &mergeMineWords, vector<unsigned long long> &mergeSafeWords, vector<unsigned long long> &mineWords, vector<unsigned long long> &safeWords, vector<ObservedBoard::CellId> &mines, vector<ObservedBoard::CellId> &safes, vector<unsigned short> &countSignature, vector<ObservedBoard::CellId> &changedCells, Probability::Result &forcedProbability, Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, vector<TemplateLongTermRisk::Workspace::Factor> &factors, vector<int> &factorBlockers, vector<int> &pendingEvents, vector<TemplateLongTermRisk::Workspace::Tail> &tails, FlatHashTable<unsigned long long, int> &tailIndex, vector<TemplateLongTermRisk::Workspace::Node> &nodes, FlatHashTable<unsigned long long, int> &nodeIndex) {
        for (int rank = 0; rank < selectedCount; ++rank) {
            const TemplateLongTermRisk::Workspace::Term &term = terms.data[order.data[rank]];
            appendFactor(result, term.event, term.weight, state, shapeConfig, words, originalCells, denseCells, domains, domainMineWords, domainSafeWords, domainIndex, mineWords, safeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, factors, factorBlockers, pendingEvents);
        }
        order.resize(factors.size);
        for (int i = 0; i < (int)factors.size; ++i) {
            const long double priority = factors.data[i].weight * factors.data[i].bound;
            int position = i;
            while (position > 0) {
                const int previous = order.data[position - 1];
                const long double previousPriority = factors.data[previous].weight * factors.data[previous].bound;
                if (priority <= previousPriority) break;
                order.data[position] = previous;
                --position;
            }
            order.data[position] = i;
        }
        tails.resize(1); tails.data[0] = {-1, 0}; tailIndex.clear();
        int tail = 0;
        for (int i = (int)factors.size - 1; i >= 0; --i) tail = suffix(tails, tailIndex, order.data[i], tail);
        nodes.resize(0); nodeIndex.clear();
        mineWords.resize(words); safeWords.resize(words);
        for (int i = 0; i < words; ++i) { mineWords.data[i] = 0; safeWords.data[i] = 0; }
        const int dense = denseCells.data[rootCell];
        mineWords.data[dense / 64] |= 1ULL << (dense % 64);
        const int domain = internDomain(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, mineWords, safeWords);
        if (!domain) return 0.0L;
        const long double rootMass = domains.data[domain].mass;
        const long double remainder = evaluateTree(result, state, shapeConfig, words, originalCells, domains, domainMineWords, domainSafeWords, domainIndex, mergeMineWords, mergeSafeWords, mines, safes, countSignature, changedCells, forcedProbability, basic, structure, probability, factors, factorBlockers, pendingEvents, tails, tailIndex, nodes, nodeIndex, domain, tail);
        return (rootMass - remainder) / state.probability.candidates;
    }

    static void appendMatch(TemplateLongTermRisk::Result &result, int pattern, int row, int col) {
        const int match = (int)result.matches.size;
        const int eventBegin = (int)result.events.size;
        result.matches.push_back({pattern, row, col, eventBegin});
        for (int direction = 0; direction < 2; ++direction) for (int kind = 0; kind < 2; ++kind) {
            result.events.resize(result.events.size + 1);
            TemplateLongTermRisk::Result::Event &event = result.events.data[result.events.size - 1];
            event.match = match; event.direction = direction; event.kind = kind; event.prepared = false; event.probabilityValid = false;
        }
    }






};

int TemplateLongTermRisk::maxPrefix() {
    return (int)(sizeof(FiftyFiftyData::patterns) / sizeof(FiftyFiftyData::patterns[0]));
}

void TemplateLongTermRisk::update(Result &result, PositionState &state, int prefix, const Config &config, Pool &pool, Workspace &workspace) {
    assert_(prefix >= 1 && prefix <= maxPrefix(), "LTR模板前缀超出目录范围");
    assert_(config.eventLimit > 0, "LTR方向事件上限必须为正数");
    result.pool = &pool; result.tableIndex = TemplateLongTermRiskEngine::getTable(pool, workspace.buildNodes, workspace.buildOutputs, workspace.buildChildren, workspace.buildChildWalls, prefix);
    const Pool::Table &table = pool.tables.data[result.tableIndex];
    result.eventLimit = config.eventLimit;
    result.rows = state.board.cells.rows(); result.cols = state.board.cells.cols();
    const int size = result.rows * result.cols;
    result.probabilities.resize(size); result.hidden.resize(size);
    for (int id = 0; id < size; ++id) {
        const long double probability = Probability::mineProbability(state.probability, id, state.board, state.basic, state.structure);
        result.probabilities.data[id] = probability;
        result.hidden.data[id] = probability > 0.01L && probability < 0.99L;
    }
    result.matches.resize(0); result.events.resize(0); result.eventCells.resize(0); result.blockerCells.resize(0); result.blockerSets.resize(0);
    result.offsets.resize(size + 1); result.links.resize(0); result.countKeys.resize(0); result.counts.resize(0); result.noBodyRestrictions.resize(0); result.countIndex.clear();
    result.cells.resize(size);
    for (int id = 0; id < size; ++id) { result.cells.data[id].valid = false; result.cells.data[id].descriptionValid = false; }
    for (int row = 0; row < result.rows; ++row) for (int col = 0; col < result.cols; ++col) {
        const int anchor = row * result.cols + col;
        if (!result.hidden.data[anchor]) continue;
        for (int checkIndex = 0; checkIndex < (int)table.checks.count;) {
            const Pool::Check &check = pool.checks.data.data[table.checks.offset + checkIndex];
            const int nextRow = row + check.row, nextCol = col + check.col;
            if (nextRow < 0 || nextRow >= result.rows || nextCol < 0 || nextCol >= result.cols || !result.hidden.data[nextRow * result.cols + nextCol]) { checkIndex = check.skip; continue; }
            for (int output = check.outputBegin; output < check.outputEnd; ++output) TemplateLongTermRiskEngine::consider(result, pool.outputs.data.data[table.outputs.offset + output], row, col, state);
            ++checkIndex;
        }
    }
    for (int id = 0; id <= size; ++id) result.offsets.data[id] = 0;
    for (int matchIndex = 0; matchIndex < (int)result.matches.size; ++matchIndex) {
        const Result::Match &match = result.matches.data[matchIndex];
        const Pool::Pattern &pattern = pool.patterns.data.data[table.patterns.offset + match.pattern];
        const Pool::Point *points = pool.points.data.data + table.points.offset + pattern.pointBegin;
        for (int slot = 0; slot < pattern.coreCount + pattern.wallCount; ++slot) {
            const int id = TemplateLongTermRiskEngine::cell(result, match.row, match.col, points[slot]);
            if (id < 0 || (slot >= pattern.coreCount && result.probabilities.data[id] >= 1.0L)) continue;
            ++result.offsets.data[id + 1];
        }
    }
    for (int id = 1; id <= size; ++id) result.offsets.data[id] += result.offsets.data[id - 1];
    workspace.order.resize(size + 1);
    for (int id = 0; id <= size; ++id) workspace.order.data[id] = result.offsets.data[id];
    result.links.resize(result.offsets.data[size]);
    for (int matchIndex = 0; matchIndex < (int)result.matches.size; ++matchIndex) {
        const Result::Match &match = result.matches.data[matchIndex];
        const Pool::Pattern &pattern = pool.patterns.data.data[table.patterns.offset + match.pattern];
        const Pool::Point *points = pool.points.data.data + table.points.offset + pattern.pointBegin;
        for (int slot = 0; slot < pattern.coreCount + pattern.wallCount; ++slot) {
            const int id = TemplateLongTermRiskEngine::cell(result, match.row, match.col, points[slot]);
            if (id >= 0 && (slot < pattern.coreCount || result.probabilities.data[id] < 1.0L)) result.links.data[workspace.order.data[id]++] = {matchIndex, slot};
        }
    }
}

long double TemplateLongTermRisk::influence(ObservedBoard::CellId cell, PositionState &state, Result &result, const ShapeSolverConfig &shapeConfig, Workspace &workspace) {
    Result::CellCache &cache = result.cells.data[cell];
    if (cache.valid) return cache.influence;
    const int boardWords = (result.rows * result.cols + 63) / 64;
    const long double probability = result.probabilities.data[cell];
    if (probability <= 0.0L || probability >= 1.0L) {
        cache.influence = 0.0L; cache.valid = true;
        return 0.0L;
    }
    workspace.groupCells.resize(0);
    workspace.groupCells.push_back(cell);
    state.applyReveal(cell, ObservedBoard::CellState::ForcedSafe, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal);
    for (int id = 0; id < (int)result.probabilities.size; ++id) {
        if (id == cell) continue;
        const long double candidateProbability = result.probabilities.data[id];
        const long double tolerance = 1e-9L * (std::min)(probability, 1.0L - probability);
        if (std::abs(candidateProbability - probability) <= tolerance && Probability::mineProbability(workspace.reveal.probability, id, state.board, state.basic, state.structure) == 0.0L) workspace.groupCells.push_back(id);
    }
    state.undoReveal(workspace.reveal);
    TemplateLongTermRiskEngine::gatherTerms(result, state, shapeConfig, boardWords, workspace.groupCells, workspace.pendingMatches, workspace.terms, workspace.mines, workspace.safes, workspace.eventBlockers, workspace.eventBlockerCells, workspace.expressions, workspace.nextExpressions, workspace.expressionWords, workspace.nextExpressionWords, workspace.countSignature, workspace.changedCells, workspace.forcedProbability, workspace.basic, workspace.structure, workspace.probability);
    TemplateLongTermRiskEngine::pruneTerms(result, workspace.terms, workspace.pendingEvents);
    const int positiveCount = TemplateLongTermRiskEngine::buildRankOrder(workspace.terms, workspace.order);
    if (!positiveCount) {
        TemplateLongTermRiskEngine::cacheGroup(result, workspace.groupCells, 0.0L);
        return 0.0L;
    }
    const int selectedCount = (std::min)(result.eventLimit, positiveCount);
    const int words = TemplateLongTermRiskEngine::buildSupport(result, workspace.terms, workspace.order, workspace.denseCells, workspace.originalCells, workspace.domains, workspace.domainMineWords, workspace.domainSafeWords, workspace.domainIndex, workspace.nodes, workspace.nodeIndex, workspace.tails, workspace.tailIndex, workspace.factors, workspace.factorBlockers, cell, selectedCount);
    const long double value = TemplateLongTermRiskEngine::buildFactors(result, cell, selectedCount, state, shapeConfig, words, workspace.terms, workspace.order, workspace.originalCells, workspace.denseCells, workspace.domains, workspace.domainMineWords, workspace.domainSafeWords, workspace.domainIndex, workspace.mergeMineWords, workspace.mergeSafeWords, workspace.mineWords, workspace.safeWords, workspace.mines, workspace.safes, workspace.countSignature, workspace.changedCells, workspace.forcedProbability, workspace.basic, workspace.structure, workspace.probability, workspace.factors, workspace.factorBlockers, workspace.pendingEvents, workspace.tails, workspace.tailIndex, workspace.nodes, workspace.nodeIndex);
    TemplateLongTermRiskEngine::cacheGroup(result, workspace.groupCells, value);
    return value;
}

TemplateLongTermRisk::Description TemplateLongTermRisk::describe(ObservedBoard::CellId cell, PositionState &state, Result &result, const ShapeSolverConfig &shapeConfig, Workspace &workspace) {
    Result::CellCache &cache = result.cells.data[cell];
    if (cache.descriptionValid) return Description(cache.bodyUnion, cache.noBodyRestrictionBins);
    const long double probability = result.probabilities.data[cell];
    assert_(probability > 0.0L, "LTR说明要求该格有正雷概率");
    const int boardWords = (result.rows * result.cols + 63) / 64;
    workspace.groupCells.resize(0);
    workspace.groupCells.push_back(cell);
    if (probability < 1.0L) {
        state.applyReveal(cell, ObservedBoard::CellState::ForcedSafe, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal);
        for (int id = 0; id < (int)result.probabilities.size; ++id) {
            if (id == cell) continue;
            const long double candidateProbability = result.probabilities.data[id];
            const long double tolerance = 1e-9L * (std::min)(probability, 1.0L - probability);
            if (std::abs(candidateProbability - probability) <= tolerance && Probability::mineProbability(workspace.reveal.probability, id, state.board, state.basic, state.structure) == 0.0L) workspace.groupCells.push_back(id);
        }
        state.undoReveal(workspace.reveal);
    }
    TemplateLongTermRiskEngine::gatherTerms(result, state, shapeConfig, boardWords, workspace.groupCells, workspace.pendingMatches, workspace.terms, workspace.mines, workspace.safes, workspace.eventBlockers, workspace.eventBlockerCells, workspace.expressions, workspace.nextExpressions, workspace.expressionWords, workspace.nextExpressionWords, workspace.countSignature, workspace.changedCells, workspace.forcedProbability, workspace.basic, workspace.structure, workspace.probability);
    TemplateLongTermRiskEngine::pruneTerms(result, workspace.terms, workspace.pendingEvents);
    const int positiveCount = TemplateLongTermRiskEngine::buildRankOrder(workspace.terms, workspace.order);
    const int selectedCount = (std::min)(result.eventLimit, positiveCount);
    int restrictionCount = 0;
    for (int rank = 0; rank < selectedCount; ++rank) if (workspace.terms.data[workspace.order.data[rank]].weight == 0.5L) ++restrictionCount;
    const int bins = restrictionCount + 1;
    const int words = TemplateLongTermRiskEngine::buildSupport(result, workspace.terms, workspace.order, workspace.denseCells, workspace.originalCells, workspace.domains, workspace.domainMineWords, workspace.domainSafeWords, workspace.domainIndex, workspace.nodes, workspace.nodeIndex, workspace.tails, workspace.tailIndex, workspace.factors, workspace.factorBlockers, cell, selectedCount);
    workspace.descriptionFactors.resize(0); workspace.factorBlockers.resize(0);
    for (int rank = 0; rank < selectedCount; ++rank) TemplateLongTermRiskEngine::appendDescriptionFactor(result, workspace.terms.data[workspace.order.data[rank]], state, shapeConfig, words, workspace.originalCells, workspace.denseCells, workspace.domains, workspace.domainMineWords, workspace.domainSafeWords, workspace.domainIndex, workspace.mineWords, workspace.safeWords, workspace.mines, workspace.safes, workspace.countSignature, workspace.changedCells, workspace.forcedProbability, workspace.basic, workspace.structure, workspace.probability, workspace.descriptionFactors, workspace.factorBlockers, workspace.pendingEvents);
    workspace.order.resize(workspace.descriptionFactors.size);
    for (int i = 0; i < (int)workspace.descriptionFactors.size; ++i) {
        int position = i;
        const long double bound = workspace.descriptionFactors.data[i].bound;
        while (position > 0 && bound > workspace.descriptionFactors.data[workspace.order.data[position - 1]].bound) {
            workspace.order.data[position] = workspace.order.data[position - 1];
            --position;
        }
        workspace.order.data[position] = i;
    }
    workspace.tails.resize(1); workspace.tails.data[0] = {-1, 0}; workspace.tailIndex.clear();
    int tail = 0;
    for (int i = (int)workspace.descriptionFactors.size - 1; i >= 0; --i) tail = TemplateLongTermRiskEngine::suffix(workspace.tails, workspace.tailIndex, workspace.order.data[i], tail);
    workspace.descriptionNodes.resize(0); workspace.descriptionValues.resize(0); workspace.descriptionNodeIndex.clear();
    workspace.mineWords.resize(words); workspace.safeWords.resize(words);
    for (int i = 0; i < words; ++i) { workspace.mineWords.data[i] = 0; workspace.safeWords.data[i] = 0; }
    const int dense = workspace.denseCells.data[cell];
    workspace.mineWords.data[dense / 64] |= 1ULL << (dense % 64);
    const int domain = TemplateLongTermRiskEngine::internDomain(result, state, shapeConfig, words, workspace.originalCells, workspace.domains, workspace.domainMineWords, workspace.domainSafeWords, workspace.domainIndex, workspace.mines, workspace.safes, workspace.countSignature, workspace.changedCells, workspace.forcedProbability, workspace.basic, workspace.structure, workspace.probability, workspace.mineWords, workspace.safeWords);
    assert_(domain != 0, "LTR说明的已知雷条件没有可行布局");
    const long double rootMass = workspace.domains.data[domain].mass;
    const int node = TemplateLongTermRiskEngine::evaluateDescriptionTree(result, state, shapeConfig, words, workspace.originalCells, workspace.domains, workspace.domainMineWords, workspace.domainSafeWords, workspace.domainIndex, workspace.mergeMineWords, workspace.mergeSafeWords, workspace.mines, workspace.safes, workspace.countSignature, workspace.changedCells, workspace.forcedProbability, workspace.basic, workspace.structure, workspace.probability, workspace.descriptionFactors, workspace.factorBlockers, workspace.pendingEvents, workspace.tails, workspace.tailIndex, workspace.descriptionNodes, workspace.descriptionValues, workspace.descriptionNodeIndex, domain, tail, bins);
    const int valueBegin = workspace.descriptionNodes.data[node].valueBegin;
    const int histogramBegin = (int)result.noBodyRestrictions.size;
    result.noBodyRestrictions.resize(histogramBegin + bins);
    long double noBody = 0.0L;
    for (int k = 0; k < bins; ++k) {
        const long double value = workspace.descriptionValues.data[valueBegin + k] / rootMass;
        result.noBodyRestrictions.data[histogramBegin + k] = value;
        noBody += value;
    }
    const long double bodyUnion = 1.0L - noBody;
    TemplateLongTermRiskEngine::cacheDescription(result, workspace.groupCells, histogramBegin, bins, bodyUnion);
    return Description(bodyUnion, bins);
}

long double TemplateLongTermRisk::noBodyByRestrictionCount(const Result &result, ObservedBoard::CellId cell, int k) {
    const Result::CellCache &cache = result.cells.data[cell];
    assert_(cache.descriptionValid, "LTR说明必须先调用describe");
    assert_(k >= 0 && k < cache.noBodyRestrictionBins, "LTR限制事件计数超出分布范围");
    return result.noBodyRestrictions.data[cache.noBodyRestrictionBegin + k];
}

} // 命名空间 mss
