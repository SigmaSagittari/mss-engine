#pragma once

#include "algo/winrate_engine/selection/selection.h"
#include "algo/winrate_engine/result_type.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// JavaE：逐个尝试本格可能显示的数字，把各数字下的后续安全评分按发生概率相加。
// analyze 给出整次推荐；javaLite/evaluate 返回单个格子的 Result，供 Anytime 继续展开。
struct JavaEval {
    struct Config {
        Selection::Config selection;
        // 额外揭出必安格的奖励系数；只参与选招，不随 Anytime 的 value 向上累加。
        long double progressContribution;
        // 旧版长期风险补偿的权重；只在根层的 evaluate 中使用。
        long double influenceScale;

        Config(Selection::Config selection, long double progressContribution, long double influenceScale) : selection(selection), progressContribution(progressContribution), influenceScale(influenceScale) {}

        // 建议缓存身份：同一盘面下这些参数改变会改变招法。
        void hash(U128Hasher &hasher) const;
    };

    // 本格显示一个数字后产生的三层增量，Anytime 用它重放、撤回这个数字结局。
    // 增量只记录池内切片；对应 PositionState 的 basicPool/shapes 必须保持存活且不退池。
    struct Delta {
        // 揭出该数字后，尚未揭开的必安格数量。
        int knownSafeCount;
        // 该数字在点击前出现的概率，已包含本格安全的概率。
        long double probability;
        ObservedBoard::Delta boardDelta;
        Basic::Delta basicDelta;
        Structure::Delta structureDelta;

        Delta(int knownSafeCount, long double probability, const ObservedBoard::Delta &boardDelta, const Basic::Delta &basicDelta, const Structure::Delta &structureDelta) : knownSafeCount(knownSafeCount), probability(probability), boardDelta(boardDelta), basicDelta(basicDelta), structureDelta(structureDelta) {}
    };

    // 单格评分；与整次推荐使用的 SuggestResult 分开。
    struct Result {
        ObservedBoard::CellId cell;
        // 各数字后续评分的概率加权和，不含推进奖励和长期风险。
        long double value;
        long double progressMultiplier; // 推进奖励乘数，必安格固定为 1。
        long double riskMultiplier; // 根层旧 LTR 乘数，树内固定为 1。
        // 各数字结局在调用方 Delta 池中的切片；池退尾或清空后失效。
        vectorPool<Delta>::vector deltas;
        // 实际展开的数字结局数，每个结局调用一次全盘概率分析；安全假设和 Observe 由 Selection 另计。
        int nodes;
        // false：算到一半已低于 floor，value 不是完整评分，调用方应丢弃此候选。
        bool valid;

        Result(ObservedBoard::CellId cell, long double value, long double progressMultiplier, long double riskMultiplier) : cell(cell), value(value), progressMultiplier(progressMultiplier), riskMultiplier(riskMultiplier), deltas{}, nodes(0), valid(true) {}
        // JavaE 的最终选招分；Anytime 分别保存 value 和奖励乘数。
        long double score() const { return value * (riskMultiplier * progressMultiplier); }
    };

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : basic(basic), structure(structure), probability(probability), selection(basic, structure, probability) {}

        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        Selection::Workspace selection;
        PositionState::Reveal reveal;
    };

    // 返回完整评分与推荐；候选保持 Selection 顺序，state 必须已同步且概率有效。
    // cells=nullptr 要求入口已排除安全格；否则只评估指定格，保持输入顺序，格子互异且未揭开。
    // 空 vector 返回空结果；details 非空时重置并写入本次逐数字评分。
    static SuggestResult analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells = nullptr, SuggestResult::Details *details = nullptr);
    // 树内单格评分，不计算 LTR，保留每个数字的 Delta 供续搜；返回前恢复 state。
    // remainingSafe 是尚未揭开的安全格总数，linkedClears 是 Selection 准备好的推进基准。
    // isSafeMove 表示本格必安，此时不计推进奖励；floor<0 不剪，否则按未算完部分的最高可能分剪枝。
    static Result javaLite(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Delta> &deltas, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, long double floor, SuggestResult::Details *details = nullptr);
    // 根层单格评分，额外计算旧 LTR 的 riskMultiplier；risk 必须来自同一根盘面。
    // retainDeltas=true 供 Anytime 根层续搜，false 用于 JavaE 普通推荐；其余计分参数含义同上。
    static Result evaluate(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, vectorPool<Delta> &deltas, FiftyFifty::Result &risk, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool retainDeltas, SuggestResult::Details *details = nullptr);

  private:
    template <bool Prune, bool RetainDeltas>
    static Result javaLiteImpl(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Workspace &workspace, vectorPool<Delta> &deltas, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, long double floor, long double riskMultiplier, SuggestResult::Details *details = nullptr);

    struct Summary {
        // 后续最安全两格按 80% / 20% 混合的评分，以及当前未揭开的必安格数。
        long double blendedSafety;
        int knownSafeCount;

        Summary(long double blendedSafety, int knownSafeCount) : blendedSafety(blendedSafety), knownSafeCount(knownSafeCount) {}
    };

    static Summary shortTerm(const PositionState &state, const Probability::Result &probability, int remainingSafe);
};

} // 命名空间 mss
