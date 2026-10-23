#pragma once

#include <functional>

#include "algo/winrate_engine/basic/selection.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// JavaEval 的单步评分：Pseudo / LongTermRisk 决定候选，Selection 出候选，逐格展开数字结局。
struct JavaEval {
    struct Config {
        Selection::Config selection;
        long double progressContribution;
        long double influenceScale;

        Config(Selection::Config selection, long double progressContribution, long double influenceScale) : selection(selection), progressContribution(progressContribution), influenceScale(influenceScale) {}

        // 建议缓存身份：同一盘面下这些参数改变会改变招法。
        void hash(U128Hasher &hasher) const {
            auto mixReal = [&](long double value) { hasher.mix((std::uint64_t)std::hash<long double>{}(value)); };
            mixReal(selection.selectionThreshold1);
            mixReal(selection.selectionThreshold2);
            hasher.mix(selection.spaceThreshold);
            mixReal(progressContribution);
            mixReal(influenceScale);
        }
    };

    struct Branch {
        int clears;
        long double probability;
        ObservedBoard::Delta boardDelta;
        Basic::Delta basicDelta;
        Structure::Delta structureDelta;

        Branch(int clears, long double probability) : clears(clears), probability(probability), boardDelta{}, basicDelta{}, structureDelta{} {}
    };

    struct Peer {
        ObservedBoard::CellId cell;
        long double probability;

        Peer(ObservedBoard::CellId cell, long double probability) : cell(cell), probability(probability) {}
    };

    struct Analysis {
        ObservedBoard::CellId cell;
        long double value;
        long double multiplier;
        // 分支记录由调用方的 Branch 池持有；切片在该池退尾或清空前有效。
        vectorPool<Branch>::vector branches;
        // 必安子集哈希；只由 AnytimeSearch 填写。
        U128 equivalence;
        bool hasEquivalence;
        // 存在始终比本格更安全的替代格，树内不再展开本格。
        bool hasAlternative;
        // 本格评分消耗的节点数；与 1.5 一致，一个候选算一次。
        int node;
        // false 表示已算部分打不过 floor，剩余分支不再展开。
        bool valid;

        Analysis(ObservedBoard::CellId cell, long double value, long double multiplier) : cell(cell), value(value), multiplier(multiplier), branches{}, equivalence(), hasEquivalence(false), hasAlternative(false), node(0), valid(true) {}
    };

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : basic(basic), structure(structure), probability(probability), selection(basic, structure, probability) {}

        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        Selection::Workspace selection;
        PositionState::Reveal reveal;
        // javaLite 的更安全替代格临时区，同样只保留容量。
        vector<ObservedBoard::CellId> alternatives;
    };

    // 返回完整评分与推荐；候选保持 Selection 顺序，state 必须已同步且概率有效。
    // cells=nullptr 要求入口已排除安全格；否则只评估指定格，保持输入顺序，格子互异且未揭开。
    // 空 vector 返回空结果；details 非空时重置并写入本次逐数字评分。
    static SuggestResult analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells = nullptr, SuggestResult::Details *details = nullptr);
    // 只做单格后续分析，不读长期风险；multiplier 仅含进度奖励。树内有自己的候选生命周期，直接调它。
    // peers 用于强制安全后的必安子集和更安全替代格；safeSearch 时不计进度奖励。
    // floor 只在该候选已算一部分后生效；< 0 表示不剪。分支增量借用 state 的池，供树续搜。
    static Analysis javaLite(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Branch> &branches, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int forcedSafes, bool safeSearch, span<const Peer> peers, long double floor, SuggestResult::Details *details = nullptr);
    // JavaLite 之后再乘长期风险 influence；retainDeltas 仅供搜索根层保留 state 池中的增量。
    static Analysis evaluate(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, vectorPool<Branch> &branches, FiftyFifty::Result &risk, const FiftyFifty::Config &fiftyConfig, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int forcedSafes, span<const Peer> peers, bool retainDeltas, SuggestResult::Details *details = nullptr);

  private:
    template <bool Prune, bool RetainDeltas>
    static Analysis javaLiteImpl(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Branch> &branches, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int forcedSafes, bool safeSearch, span<const Peer> peers, long double floor, long double influence, SuggestResult::Details *details = nullptr);

    struct Summary {
        long double blendedSafety;
        int clears;

        Summary(long double blendedSafety, int clears) : blendedSafety(blendedSafety), clears(clears) {}
    };

    static Summary shortTerm(const PositionState &state, const Probability::Result &probability, int remainingSafe);
};

} // 命名空间 mss
