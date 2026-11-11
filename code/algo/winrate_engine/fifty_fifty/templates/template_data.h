#pragma once

#include <algorithm>

#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/winrate_engine/fifty_fifty/templates/template_catalog.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct FiftyFiftyData {
    struct Point {
        int row, col;

        bool operator==(const Point &other) const = default;
        bool operator<(const Point &other) const { return row == other.row ? col < other.col : row < other.row; }
        Point operator+(Point other) const { return {row + other.row, col + other.col}; }
        Point operator-(Point other) const { return {row - other.row, col - other.col}; }
        // 盘外返回 -1，封口匹配把盘外视为已封住。
        ObservedBoard::CellId cell(Point anchor, int rows, int cols) const {
            const Point point = anchor + *this;
            return point.row >= 0 && point.row < rows && point.col >= 0 && point.col < cols ? point.row * cols + point.col : -1;
        }
    };

    struct Blocker { int pattern; Point offset; };
    struct Pattern {
        vectorPool<Point>::vector points; // 依次为 A 组、B 组、封口格。
        int half;
        vectorPool<Blocker>::vector blockers;
    };
    struct Rooted { Point safe; int pattern, slot; };
    struct Check { Point point; int skip, pattern; };

    struct Pool {
        vectorPool<Point> points;
        vectorPool<Blocker> blockers;
        vector<Pattern> patterns;
        vectorPool<Rooted> roots;
        Grid<vectorPool<Rooted>::vector> starts; // 按相对必安格的偏移查 roots。
        vector<Check> checks; // 本体检查失败跳到 skip；成功且 pattern 非负时输出匹配。
    };

    int count() const { return sizeof(FiftyFiftyCatalog::patterns) / sizeof(FiftyFiftyCatalog::patterns[0]); }
    // prefix 为 [1, count()]。同一前缀编译一次，供多个盘面复用。
    void compile(Pool &pool, int prefix) {
        pool.patterns.resize(0);
        pool.points.data.resize(0);
        pool.blockers.data.resize(0);
        pool.roots.data.resize(0);
        pool.checks.resize(0);
        for (int i = 0; i < prefix; ++i) compilePattern(pool, FiftyFiftyCatalog::patterns[i]);
        compileBlockers(pool);
        compileChecks(pool);
        compileRoots(pool);
    }

  private:
    static constexpr int maxHalf = [] {
        int maximum = 0;
        for (const char *pattern : FiftyFiftyCatalog::patterns) {
            int count = 0;
            for (const char *cell = pattern; *cell; ++cell) count += *cell == 'A';
            maximum = (std::max)(maximum, count);
        }
        return maximum;
    }();

    struct Node { Point point; int child, sibling, output; };

    struct Workspace {
        vector<Node> nodes;
    };

    __declspec(noinline) Workspace &getWorkspace() {
        static thread_local Workspace workspace;
        return workspace;
    }

    void sortGroup(Point *group, int count) {
        for (int i = 1; i < count; ++i) {
            const Point point = group[i];
            int j = i;
            while (j > 0 && point < group[j - 1]) { group[j] = group[j - 1]; --j; }
            group[j] = point;
        }
    }

    void compilePattern(Pool &pool, const char *text) {
        Point groups[2][maxHalf];
        int sizes[2] = {};
        Point point{0, 0};
        for (; *text; ++text) {
            if (*text == '/') point = {point.row + 1, 0};
            else {
                if (*text == 'A' || *text == 'B') {
                    const int group = *text == 'B';
                    groups[group][sizes[group]++] = point;
                }
                ++point.col;
            }
        }
        const int half = sizes[0], coreCount = 2 * half;
        for (int turn = 0; turn < 4; ++turn) for (int mirror = 0; mirror < 2; ++mirror) {
            Point core[2 * maxHalf];
            for (int i = 0; i < coreCount; ++i) {
                Point rotated = groups[i / half][i % half];
                for (int k = 0; k < turn; ++k) rotated = {rotated.col, -rotated.row};
                if (mirror) rotated.col = -rotated.col;
                core[i] = rotated;
            }
            sortGroup(core, half);
            sortGroup(core + half, half);
            if (core[half] < core[0]) std::swap_ranges(core, core + half, core + half);
            const Point anchor = core[0];
            for (int i = 0; i < coreCount; ++i) core[i] = core[i] - anchor;
            bool duplicate = false;
            for (int i = 0; i < (int)pool.patterns.size && !duplicate; ++i) {
                const Pattern &pattern = pool.patterns[i];
                if (pattern.half == half)
                    duplicate = std::equal(core, core + coreCount, pool.points.span(pattern.points).begin());
            }
            if (duplicate) continue;

            vectorPool<Point>::vector points = pool.points.push_back();
            for (int i = 0; i < coreCount; ++i) pool.points.push_back(points, core[i]);
            // 外圈格若相邻 A、B 数量不同，必须封住才能保持数字不变。
            for (int i = 0; i < coreCount; ++i) for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
                const Point wall = core[i] + Point{dr, dc};
                if (std::find(core, core + coreCount, wall) != core + coreCount) continue;
                int difference = 0;
                for (int j = 0; j < coreCount; ++j) {
                    const Point delta = wall - core[j];
                    if (delta.row >= -1 && delta.row <= 1 && delta.col >= -1 && delta.col <= 1)
                        difference += j < half ? 1 : -1;
                }
                if (difference && std::find(pool.points.span(points).begin() + coreCount, pool.points.span(points).end(), wall) == pool.points.span(points).end())
                    pool.points.push_back(points, wall);
            }
            std::sort(pool.points.span(points).begin() + coreCount, pool.points.span(points).end());
            pool.patterns.push_back({points, half, {}});
        }
    }

    void compileBlockers(Pool &pool) {
        for (int index = 0; index < (int)pool.patterns.size; ++index) {
            Pattern &parent = pool.patterns[index];
            const Point *body = pool.points.span(parent.points).begin();
            parent.blockers = pool.blockers.push_back();
            for (int childId = 0; childId < (int)pool.patterns.size; ++childId) {
                const Pattern &child = pool.patterns[childId];
                if (child.half >= parent.half) continue;
                const Point *points = pool.points.span(child.points).begin();
                for (int slot = 0; slot < parent.half * 2; ++slot) {
                    const Point offset = body[slot];
                    const bool flipped = slot >= parent.half;
                    bool contained = true;
                    for (int i = 1; i < child.half * 2 && contained; ++i) {
                        const Point *group = body + ((i >= child.half) != flipped) * parent.half;
                        contained = std::binary_search(group, group + parent.half, points[i] + offset);
                    }
                    if (!contained) continue;
                    bool first = false, second = false;
                    for (int i = child.half * 2; i < (int)child.points.count; ++i) {
                        first |= std::binary_search(body, body + parent.half, points[i] + offset);
                        second |= std::binary_search(body + parent.half, body + parent.half * 2, points[i] + offset);
                    }
                    if (!(first && second)) pool.blockers.push_back(parent.blockers, {childId, offset});
                }
            }
        }
    }

    void emit(Pool &pool, const vector<Node> &nodes, int node) {
        const int check = pool.checks.size;
        pool.checks.push_back({nodes[node].point, 0, nodes[node].output});
        for (int child = nodes[node].child; child >= 0; child = nodes[child].sibling)
            emit(pool, nodes, child);
        pool.checks[check].skip = pool.checks.size;
    }

    void compileChecks(Pool &pool) {
        Workspace &workspace = getWorkspace();
        vector<Node> &nodes = workspace.nodes;
        nodes.resize(0);
        nodes.push_back({{}, -1, -1, -1});
        for (int id = 0; id < (int)pool.patterns.size; ++id) {
            const Pattern &pattern = pool.patterns[id];
            int node = 0;
            for (int i = 1; i < pattern.half * 2; ++i) {
                const Point point = pool.points.span(pattern.points)[i];
                int child = nodes[node].child;
                while (child >= 0 && !(nodes[child].point == point)) child = nodes[child].sibling;
                if (child < 0) {
                    child = nodes.size;
                    nodes.push_back({point, -1, nodes[node].child, -1});
                    nodes[node].child = child;
                }
                node = child;
            }
            // 本体坐标序列及其长度已唯一确定 A、B 分组。
            nodes[node].output = id;
        }
        for (int child = nodes[0].child; child >= 0; child = nodes[child].sibling) emit(pool, nodes, child);
    }

    void compileRoots(Pool &pool) {
        vector<Rooted> &roots = pool.roots.data;
        int radius = 0;
        for (int id = 0; id < (int)pool.patterns.size; ++id) {
            const Pattern &pattern = pool.patterns[id];
            for (int slot = 0; slot < pattern.half * 2; ++slot) {
                const Point safe = pool.points.span(pattern.points)[slot < pattern.half ? pattern.half : 0] - pool.points.span(pattern.points)[slot];
                radius = (std::max)({radius, safe.row, -safe.row, safe.col, -safe.col});
                roots.push_back({safe, id, slot});
            }
        }
        pool.starts.resize(2 * radius + 1, 2 * radius + 1, {});
        std::sort(roots.data, roots.data + roots.size, [](const Rooted &a, const Rooted &b) { return a.safe < b.safe; });
        for (int i = 0; i < (int)roots.size;) {
            const Point safe = roots[i].safe;
            const int begin = i++;
            while (i < (int)roots.size && roots[i].safe == safe) ++i;
            pool.starts[safe.row + radius][safe.col + radius] = {std::size_t(begin), std::size_t(i - begin)};
        }
    }
};

} // 命名空间 mss
