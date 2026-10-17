#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "core/utility/vector_pool.h"
#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/types.h"
#include "algo/winrate_engine/java_transplant/long_term_risk_helper.h"

namespace mss {

// JavaEval 与 AnytimeSearch 共用的分支计算；树内只用短期评分，JavaEval 另计长期风险。
struct MoveEval {
    struct Config {
        LongTermRisk::Config longTermRisk;
        long double progressContribution;
        long double influenceScale;
        Config(LongTermRisk::Config longTermRisk, long double progressContribution, long double influenceScale) : longTermRisk(longTermRisk), progressContribution(progressContribution), influenceScale(influenceScale) {}
    };

    // 候选生成与 dead 过滤独立；结果拥有候选和逐项对应的观测。
    struct Selection {
        enum class Reason { ForcedFiftyFifty, FiftyFiftyOrSafe, Score, BlindGuess, Safe };
        struct Result {
            std::vector<winrate::CellId> cells;
            std::vector<Observe::Result> observations;
            Reason reason;
            Result(std::vector<winrate::CellId> cells, std::vector<Observe::Result> observations, Reason reason) : cells(std::move(cells)), observations(std::move(observations)), reason(reason) {}
        };
        struct Config {
            Pseudo::Config pseudo;
            long double selectionThreshold1;
            long double selectionThreshold2;
            int spaceThreshold;
            Config(Pseudo::Config pseudo, long double selectionThreshold1, long double selectionThreshold2, int spaceThreshold) : pseudo(pseudo), selectionThreshold1(selectionThreshold1), selectionThreshold2(selectionThreshold2), spaceThreshold(spaceThreshold) {}
        };
      private:
        struct FrontierBox {
            long double safety;
            Structure::InstanceId instance;
            Structure::BoxId box;
            FrontierBox(long double safety, Structure::InstanceId instance, Structure::BoxId box) : safety(safety), instance(instance), box(box) {}
        };

      public:
        struct Workspace {
            Workspace() : visit(0) {}
            Pseudo::Workspace pseudo;
          private:
            friend struct Selection;
            RawGrid<unsigned> visited;
            unsigned visit;
            std::vector<std::pair<int, int>> stack;
            std::vector<FrontierBox> boxes;
            std::vector<winrate::CellId> onEdge;
            std::vector<winrate::CellId> offEdgeCandidates;
            std::vector<winrate::CellId> offEdge;
            std::vector<winrate::CellId> witnesses;
        };
        // 普通候选窗写入 result 并复用容量；只读盘面，不计算观测或过滤 dead。
        // offEdge 在 T 少于 30 时覆盖全部 T，否则只给 2/3 隐藏邻格的 T；witnesses 按 CellId 排序。
        // risk 为空时只按概率准备候选；非空时须来自同一盘面。
        static void window(std::vector<winrate::CellId> &result, const winrate::View &input, const LongTermRisk::Result *risk, winrate::Cells offEdge, winrate::Cells witnesses, winrate::CellId firstOffEdge, const Config &cfg, Workspace &workspace);
        // find 只生成有序候选，观测为空；removeDead 保留特殊候选，普通候选全 dead 时按原顺序回退。
        // risk 非空时使用根层影响力资料；树内传空，仍识别 Pseudo，不计算长期风险。
        static Result find(const winrate::Input &input, const LongTermRisk::Result *risk, const Config &cfg, Workspace &workspace);
        // 结果由调用方持有，复用其数组；输出不得与输入格列表别名。
        static void find(Result &result, const winrate::Input &input, const LongTermRisk::Result *risk, winrate::Cells offEdge, winrate::Cells witnesses, winrate::CellId firstOffEdge, const Config &cfg, Workspace &workspace);
        static void removeDead(Result &result, const winrate::Input &input, Probability::Workspace &probability);
        // 显式指定格不经过自动筛选，按输入顺序准备观测。
        static Result specified(const winrate::Input &input, winrate::Cells cells, Probability::Workspace &probability);
        static bool offEdgeCandidate(const winrate::Board &board, winrate::CellId cell);
      private:
        static bool isDead(const Observe::Result &observation);
        static bool contains(winrate::Cells cells, winrate::CellId cell);
        static bool meetsSpaceThreshold(int startX, int startY, const winrate::View &input, int threshold, Workspace &workspace);
    };

    struct Branch {
        int digit;
        int clears;
        // 该数字结局的基础子评分，供 Search 第零层发布。
        long double value;
        // 真实结局权重和可重放切片；记录由输入的各模块 Pool 拥有，保留分支期间不得清空。
        long double probability;
        ObservedBoard::Delta boardDelta;
        Basic::Delta basicDelta;
        Structure::Delta structureDelta;
        Branch(int digit, int clears, long double value, long double probability, ObservedBoard::Delta boardDelta, Basic::Delta basicDelta, Structure::Delta structureDelta);
    };

    struct Analysis {
        winrate::CellId cellId;
        // 本招的评分。
        long double value;
        std::vector<Branch> branches;
        // 本招的 influence 与进度乘数；初始为无修正，完整评分后交给根层续搜。
        long double multiplier;
        // 固定候选集合的必安子集；集合相同意味着双方互相必安，不同根盘面不比较。
        // JavaEval 不用它裁剪或改写评分。
        std::optional<U128> equivalence;
        long long node;
        Analysis(winrate::CellId cellId, long double value) : cellId(cellId), value(value), multiplier(1.0L), node(0) {}
    };

    struct Workspace {
        Probability::Workspace &probability;
        LongTermRisk::Workspace longTermRisk;
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : probability(probability), longTermRisk(basic, structure, probability), basic(basic), structure(structure) {}

      private:
        friend struct MoveEval;
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        std::optional<winrate::Probability> nextProbability;
    };

    // 每次推进至多计算一个概率分支，临时揭示在返回前撤回。
    struct Cursor {
        Analysis analysis;
        // 逐个数字结局保留可证明始终安全且更安全的替代格。
        std::vector<winrate::CellId> alternatives;
        Cursor(winrate::CellId cell, Observe::Result observation, long double influence, bool safeSearch, long double candidates);
        // 开始下一候选并保留自身数组容量。
        void reset(winrate::CellId cell, Observe::Result observation, long double influence, bool safeSearch, long double candidates);
      private:
        friend struct MoveEval;
        Observe::Result observation;
        long double influence;
        bool safeSearch;
        // 空值表示强制安全分支尚未计算；0 表示已计算且没有额外安全格，安全搜索直接从 0 开始。
        std::optional<int> linkedClears;
        int digit;
        long double progress;
        long double remainingSafety;
        long double candidates;
        // 借用调用方的固定候选池；Cursor 结束前不改变池或其顺序。
        std::span<const std::pair<long double, winrate::CellId>> peers;
    };
    static Cursor begin(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::CellId cell, const Observe::Result &observation, const Config &cfg, Workspace &workspace);
    static void prepareAlternatives(Cursor &cursor, std::span<const std::pair<long double, winrate::CellId>> peers);
    static bool advance(Cursor &cursor, winrate::Board &board, winrate::Basic &basic, winrate::Structure &structure, winrate::Shapes &shapes, winrate::Distributions &distributions, ObservedBoard::Pool &boardPool, Basic::Pool &basicPool, int remainingSafe, int forcedSafes, const Config &cfg, Workspace &workspace);
    static long double optimistic(const Cursor &cursor, const Config &cfg);
  private:
    struct Summary {
        long double blendedSafety;
        int clears;
        Summary(long double blendedSafety, int clears) : blendedSafety(blendedSafety), clears(clears) {}
    };
    static Summary shortTerm(const winrate::View &input, int remainingSafe);
};

} // 命名空间 mss
