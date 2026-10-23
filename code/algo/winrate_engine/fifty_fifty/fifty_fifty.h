#pragma once

#include <deque>
#include <map>

#include "algo/winrate_engine/fifty_fifty/old_java/long_term_risk.h"
#include "core/datastructure/hash.h"

namespace mss {

// 完整 50-50 算法族：先检测 Forced/Semi，未命中时才准备长期风险。
struct FiftyFifty {
    struct TemplateConfig {
        int prefix;
        long double eqEps;
        TemplateConfig(int prefix, long double eqEps) : prefix(prefix), eqEps(eqEps) {}
    };
    // 只借用已选算法的参数；oldJava 与 templates 恰有一个非空，主选项属于 MovePolicy。
    struct Config {
        const Pseudo::Config *oldJava;
        const TemplateConfig *templates;
        const LongTermRisk::Config *risk;
        Config(const Pseudo::Config *oldJava, const TemplateConfig *templates, const LongTermRisk::Config *risk) : oldJava(oldJava), templates(templates), risk(risk) {}
    };

    struct Table {
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
        struct Check { Test test; bool accept; int skip; };
        struct Prefix {
            Point offset;
            int first, count, begin, end, runBegin, runEnd;
            explicit Prefix(Point offset) : offset(offset), first(0), count(0), begin(0), end(0), runBegin(0), runEnd(0) {}
        };
        struct Build {
            struct Pattern { int a; std::deque<Point> core, walls; };
            struct Rooted { std::deque<Test> tests; };
            std::deque<Pattern> patterns;
            std::deque<Rooted> rooted;
        };
        vector<Check> checks;
        vector<Prefix> prefixes;
        vector<Point> runs;
        Grid<int> starts;
        int radiusR, radiusC;

        explicit Table(int prefix);
        void compilePrefix(std::deque<Build::Rooted> &entries, int begin, int end, int depth, int node);
        void compileBranch(std::deque<Build::Rooted> &entries, int begin, int end, int parent);
    };

    // 同一池按前缀延迟建立匹配表；OldJava 和 LowRisk 不初始化模板。
    struct Pool { std::map<int, Table> tables; };
    struct Workspace;
    struct Result {
        Pseudo::Result pseudo;
        LongTermRisk::Result risk;
        void swapStorage(Workspace &workspace);
    };
    struct Facts {
        struct Adjacent {
            // 危险：仅 [0,count) 有效，构造数字约束时写入。
            int indices[8];
            int count;
            Adjacent() : count(0) {}
        };
        struct Constraint {
            // 危险：仅 [0,count) 有效，构造数字约束时写入。
            int cells[8];
            int count, sum;
            explicit Constraint(int sum) : count(0), sum(sum) {}
        };
        Grid<std::uint64_t> mines, safes;
        vector<Basic::Mark> marks;
        vector<Adjacent> adjacent;
        vector<Constraint> constraints;
        vector<std::uint64_t> active, found;
        int rows, cols, words;
        Facts() : rows(0), cols(0), words(0) {}
        void swapStorage(Workspace &workspace);
        void build(const PositionState &state);
        void propagate(Workspace &workspace);
        bool matches(int id, int word, std::uint64_t bit, Table::Kind kind) const;
    };
    struct Workspace {
        Pseudo::Workspace oldJava;
        LongTermRisk::Workspace risk;
        Grid<std::uint64_t> mineStorage, safeStorage;
        vector<Basic::Mark> markStorage;
        vector<Facts::Adjacent> adjacentStorage;
        vector<Facts::Constraint> constraintStorage;
        vector<std::uint64_t> activeStorage, foundStorage;
        vector<int> pending;
        vector<unsigned char> queued;
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : risk(basic, structure, probability) {}
    };

    static int templateCount();
    static void detect(Pseudo::Result &result, const PositionState &state, const Config &config, Pool &pool, Workspace &workspace);
    static void prepareRisk(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, bool collectPseudo);
    static long double influence(ObservedBoard::CellId cell, PositionState &state, Result &full, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);

  private:
    static int matchPrefix(const Table &table, int node, const Facts &facts, int root);
};

} // 命名空间 mss
