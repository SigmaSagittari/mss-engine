#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/types.h"
#include "algo/winrate_engine/java_transplant/long_term_risk_helper.h"

namespace mss {

struct JavaEval;
struct AnytimeSearch;

// JavaEval 与 AnytimeSearch 共用的分支计算，组合内部 javaLite 与 50-50 influence。
struct MoveEval {
    struct Config {
        long double progressContribution;
        long double influenceScale;
        Config(long double progressContribution, long double influenceScale) : progressContribution(progressContribution), influenceScale(influenceScale) {}
    };

    // 共用候选窗只准备招法，不计算评分；Search 与 JavaEval 在此保持相同选择规则。
    struct Selection {
        struct Config {
            long double selectionThreshold1;
            long double selectionThreshold2;
            int spaceThreshold;
            Config(long double selectionThreshold1, long double selectionThreshold2, int spaceThreshold) : selectionThreshold1(selectionThreshold1), selectionThreshold2(selectionThreshold2), spaceThreshold(spaceThreshold) {}
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
          private:
            friend struct Selection;
            RawGrid<unsigned> visited;
            unsigned visit;
            std::vector<std::pair<int, int>> stack;
            std::vector<FrontierBox> boxes;
            std::vector<winrate::CellId> onEdge;
            std::vector<winrate::CellId> offEdgeCandidates;
            std::vector<winrate::CellId> notDeferred;
            std::vector<winrate::CellId> deferred;
        };
        // 候选借用 workspace，下一次 select 即失效；只读根盘面，不计算评分或概率分支。
        // offEdge 在 T 少于 30 时覆盖全部 T，否则只给 2/3 隐藏邻格的 T；witnesses 按 CellId 排序。
        static winrate::Cells select(const winrate::View &input, const LongTermRisk::Result &risk, winrate::Cells dead, winrate::Cells offEdge, winrate::Cells witnesses, winrate::CellId firstOffEdge, const Config &cfg, Workspace &workspace);
      private:
        static bool contains(winrate::Cells cells, winrate::CellId cell);
        static bool meetsSpaceThreshold(int startX, int startY, const winrate::View &input, int threshold, Workspace &workspace);
    };

    struct Branch {
        int digit;
        int clears;
        // 该数字结局的基础子评分，供 Search 第零层发布。
        long double value;
        winrate::Probability probability;
        Branch(int digit, int clears, long double value, winrate::Probability probability) : digit(digit), clears(clears), value(value), probability(std::move(probability)) {}
    };

    struct Analysis {
        winrate::CellId cellId;
        // -1 表示候选准备或评分上界剪掉的显示项。
        long double value;
        std::vector<Branch> branches;
        // 固定候选集合的必安子集；集合相同意味着双方互相必安，不同根盘面不比较。
        // JavaEval 不用它裁剪或改写评分。
        std::optional<U128> equivalence;
        long long node;
        Analysis(winrate::CellId cellId, long double value) : cellId(cellId), value(value), node(0) {}
    };

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, LongTermRisk::Workspace &risk) : basic(basic), structure(structure), probability(probability), risk(risk) {}

      private:
        friend struct MoveEval;
        friend struct JavaEval;
        friend struct AnytimeSearch;
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        LongTermRisk::Workspace &risk;
        ObservedBoard::Delta boardDelta;
        Basic::Delta basicDelta;
        Structure::Delta structureDelta;
        std::optional<winrate::Probability> nextProbability;
    };

  private:
    friend struct JavaEval;
    friend struct AnytimeSearch;
    // 每次推进至多计算一个概率分支，临时揭示在返回前撤回。
    struct Cursor {
        Analysis analysis;
        Observe::Result observation;
        long double influence;
        long double baseHotspot;
        bool safeSearch;
        // 空值表示强制安全分支尚未计算；0 表示已计算且没有额外安全格，安全搜索直接从 0 开始。
        std::optional<int> linkedClears;
        int digit;
        long double progress;
        long double remainingSafety;
        // 逐个数字结局保留可证明始终安全且更安全的替代格。
        std::vector<winrate::CellId> alternatives;
        // 借用调用方的固定候选池；Cursor 结束前不改变池或其顺序。
        std::span<const std::pair<long double, winrate::CellId>> peers;
        Cursor(winrate::CellId cell, Observe::Result observation, long double influence, long double baseHotspot, bool safeSearch);
    };
    static Cursor begin(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::CellId cell, const Observe::Result &observation, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace);
    static void prepareAlternatives(Cursor &cursor, winrate::Cells cells, std::span<const std::pair<long double, winrate::CellId>> peers, bool prune);
    static bool advance(Cursor &cursor, const winrate::Input &input, int remainingSafe, int forcedSafes, const LongTermRisk::Result *risk, const Config &cfg, Workspace &workspace);
    static long double optimistic(const Cursor &cursor, const Config &cfg);
    static bool isDead(const Observe::Result &observation);
    static bool offEdgeCandidate(const winrate::Board &board, winrate::CellId cell);
    static long double hotspotSafety(const LongTermRisk::Result &risk, winrate::CellId candidate, const winrate::View &input);
    static bool hotspotExempt(const LongTermRisk::Result &risk, winrate::CellId cell);
};

} // 命名空间 mss
