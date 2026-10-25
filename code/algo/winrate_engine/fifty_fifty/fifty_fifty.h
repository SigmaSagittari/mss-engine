#pragma once

#include <deque>
#include <map>

#include "algo/winrate_engine/fifty_fifty/old_java/long_term_risk.h"
#include "core/datastructure/hash.h"

namespace mss {

// 完整 50-50 算法族：先检测 Forced/Semi，未命中时才准备长期风险。
struct FiftyFifty {
    struct TemplateConfig {
        int prefix; // 使用排序后前多少个模板，范围为 1..templateCount()。
        explicit TemplateConfig(int prefix) : prefix(prefix) {}
    };
    // templates 为空时使用无参数的 OldJava；risk 只供 prepareRisk/describe 使用。
    // 两个参数只借用调用方配置，主选项属于 MovePolicy。
    struct Config {
        const TemplateConfig *templates;
        const LongTermRisk::Config *risk;
        Config(const TemplateConfig *templates, const LongTermRisk::Config *risk) : templates(templates), risk(risk) {}
    };

    // 一个模板前缀编译出的只读匹配表，包含旋转、镜像和边界/雷墙判断。
    // 相同前缀由 Pool 共享；Table::Build 只在首次编译时使用，不依赖具体盘面。
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
        struct Check { Test test; bool accept; int skip; };
        struct Prefix {
            Point offset; // 相对被检测格的位置。
            int first, count, begin, end, runBegin, runEnd; // 子前缀范围、checks 范围、连续必安检查范围。
            explicit Prefix(Point offset) : offset(offset), first(0), count(0), begin(0), end(0), runBegin(0), runEnd(0) {}
        };
        struct Build {
            // core 先放 a 个 A 组格，再放 B 组格；walls 是隔断两组数字差异所需的封口位。
            struct Pattern { int a; std::deque<Point> core, walls; };
            struct Rooted { std::deque<Test> tests; };
            std::deque<Pattern> patterns;
            std::deque<Rooted> rooted;
        };
        vector<Check> checks;
        vector<Prefix> prefixes;
        vector<Point> runs;
        Grid<int> starts; // 由相对偏移定位安全前缀；-1 表示没有该分支。
        int radiusR, radiusC;

        explicit Table(int prefix);
        void compilePrefix(std::deque<Build::Rooted> &entries, int begin, int end, int depth, int node);
        void compileBranch(std::deque<Build::Rooted> &entries, int begin, int end, int parent);
    };

    // 同一池按前缀延迟建立匹配表；OldJava 和 LowRisk 不初始化模板。
    struct Pool { std::map<int, Table> tables; };
    struct Workspace;
    // detect 只填 pseudo；prepareRisk 才填旧 LTR 的 risk，influence 可以继续补算单格。
    struct Result {
        Pseudo::Result pseudo;
        LongTermRisk::Result risk;
    };
    // 模板检测的临时推导：同时假设每个候选为雷，批量推进数字约束。
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
        // [被推导格][假设候选位图块]，每个 bit 对应一个“假设该候选为雷”的局面。
        Grid<std::uint64_t> mines, safes;
        vector<Basic::Mark> marks;
        vector<Adjacent> adjacent;
        vector<Constraint> constraints;
        vector<std::uint64_t> active, found; // 尚在推导的假设，以及已经命中模板的假设。
        int rows, cols, words;
        Facts(const PositionState &state, Workspace &workspace);
        void swapStorage(Workspace &workspace);
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
    // 只检测当前已成立的 Forced/Semi；根盘面只读，不计算长期风险的联合计数。
    static void detect(Pseudo::Result &result, const PositionState &state, const Config &config, Pool &pool, Workspace &workspace);
    // 扫描旧 LTR；collectPseudo=true 时同时补找死猜。临时标记和计数在返回前全部恢复。
    static void prepareRisk(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, bool collectPseudo);
    // 返回旧 LTR 对该格的补偿布局数，尚未除以全盘布局数；full 必须对应同一盘面。
    static long double influence(ObservedBoard::CellId cell, PositionState &state, Result &full, const ShapeSolverConfig &shapeConfig, Workspace &workspace);
    // 仅详情查询：cell 是雷率大于 0 的隐藏格；按所选检测器确认本体，部分风险沿用旧 LTR。
    static LongTermRisk::Details describe(ObservedBoard::CellId cell, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Pool &pool, Workspace &workspace);

  private:
    static constexpr long double halfTolerance = 1e-9L;
    static int matchPrefix(const Table &table, int node, const Facts &facts, int root);
};

} // 命名空间 mss
