#pragma once

#include <cstdint>

#include "algo/winrate_engine/fifty_fifty/old_java/semi.h"

namespace mss {

struct TemplateSemi {
    // 一个模板前缀编译出的只读匹配表，包含旋转、镜像和边界/雷墙判断。
    // 相同前缀由 Pool 共享；构表缓冲由 Workspace 复用，不依赖具体盘面。
    struct Table {
        // Safe/Mine 是假设根格为雷后必须推导出的状态；Wall 可由必雷格或棋盘外挡住。
        enum class Kind : unsigned char { Safe, Mine, Wall };
        struct Point {
            int r, c;
            bool operator<(const Point &other) const { return r == other.r ? c < other.c : r < other.r; }
            bool operator==(const Point &) const = default;
        };
        struct Test {
            int r, c;
            Kind kind;
            Test(int r, int c, Kind kind) : r(r), c(c), kind(kind) {}
            bool operator==(const Test &) const = default;
            bool operator<(const Test &other) const { if (kind != other.kind) return kind < other.kind; return r == other.r ? c < other.c : r < other.r; }
        };
        // test 通过且 accept=true 即命中；失败时跳到 checks[skip]，跳过这一支。
        // 危险：构表时先写 test/accept，递归结束后写 skip；编译完成才发布整张表。
        struct Check { Test test; bool accept; int skip; };
        struct Prefix {
            Point offset; // 相对被检测格的位置。
            int first, count, begin, end, runBegin, runEnd; // 子前缀范围、checks 范围、连续必安检查范围。
            explicit Prefix(Point offset) : offset(offset), first(0), count(0), begin(0), end(0), runBegin(0), runEnd(0) {}
        };
        struct Build {
            // core 先放 a 个 A 组格，再放 B 组格；walls 是隔断两组数字差异所需的封口位。
            struct Pattern { int a, coreBegin, coreCount, wallBegin, wallCount; };
            struct Rooted { int begin, count; };
            struct Boundary { Point point; int difference; };
            struct Group { int begin, end; };
        };
        vector<Check> checks;
        vector<Prefix> prefixes;
        vector<Point> runs;
        Grid<int> starts; // 由相对偏移定位安全前缀；-1 表示没有该分支。
        int prefix, radiusR, radiusC;

        Table(int prefix, vector<Point> &first, vector<Point> &second, vector<Point> &core, vector<Build::Boundary> &boundary, vector<Build::Pattern> &patterns, vector<Point> &points, vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups);
        void compilePrefix(vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, vector<Build::Group> &groups, int begin, int end, int depth, int node);
        void compileBranch(vector<Build::Rooted> &entries, vector<Test> &tests, vector<Test> &frequency, int begin, int end, int parent);
    };

    struct Pool {
        vector<Table *> tables;
        Pool();
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;
        ~Pool();
    };
    struct Workspace;
    // 模板检测的临时推导：同时假设每个候选为雷，批量推进数字约束。
    struct Facts {
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
        Facts(const PositionState &state, Grid<std::uint64_t> &mines, Grid<std::uint64_t> &safes, vector<Basic::Mark> &marks, vector<Adjacent> &adjacent, vector<Constraint> &constraints, vector<std::uint64_t> &active, vector<std::uint64_t> &found);
        void propagate(vector<int> &pending, vector<unsigned char> &queued);
        bool matches(int id, int word, std::uint64_t bit, Table::Kind kind) const;
    };
    struct Workspace {
        vector<Table::Point> first, second, core, points;
        vector<Table::Build::Boundary> boundary;
        vector<Table::Build::Pattern> patterns;
        vector<Table::Build::Rooted> entries;
        vector<Table::Build::Group> groups;
        vector<Table::Test> tests, frequency;
        Grid<std::uint64_t> mineStorage, safeStorage;
        vector<Basic::Mark> markStorage;
        vector<Facts::Adjacent> adjacentStorage;
        vector<Facts::Constraint> constraintStorage;
        vector<std::uint64_t> activeStorage, foundStorage;
        vector<int> pending;
        vector<unsigned char> queued;
    };

    static int templateCount();
    static void detect(Semi::Result &result, const PositionState &state, int prefix, Pool &pool, Workspace &workspace);

  private:
    static constexpr long double halfTolerance = 1e-9L;
    static int matchPrefix(const Table &table, int node, const Facts &facts, int root);
};

} // 命名空间 mss
