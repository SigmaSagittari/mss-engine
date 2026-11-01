#pragma once

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/result_type.h"
#include "game/position_state.h"

namespace mss {

// 单格数字结局评分；只依赖概率状态，不生成候选或计算长期风险。
struct JavaLite {
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

    // 一个局面的候选暂存；两份数组按同一下标对应，筛选结束后只转存保留招法。
    // probabilityPool 由调用方持有，须覆盖本池的使用期。
    struct Pool {
        vectorPool<Delta> deltas;
        vector<Probability::Result> probabilities;
        Probability::Pool &probabilityPool;

        Pool(Probability::Pool &probabilityPool) : probabilityPool(probabilityPool) {}
        // 只清空本层记录，概率池由调用方单独清空。
        void clear();
    };

    // 单格评分；与整次推荐使用的 SuggestResult 分开。
    struct Result {
        // 各数字后续评分的概率加权和，不含推进奖励和根层补偿。
        long double value;
        long double progressMultiplier; // 推进奖励乘数，必安格固定为 1。
        // 各数字结局在调用方 Pool::deltas 中的切片，同时索引 probabilities；池清空后失效。
        vectorPool<Delta>::vector deltas;
        // 本格的数字展开次数。
        int nodes;
        // false：候选已被剪枝，value 只保存已累积的部分评分，调用方应丢弃此候选。
        bool valid;

        Result(long double value, long double progressMultiplier, vectorPool<Delta>::vector deltas, int nodes, bool valid) : value(value), progressMultiplier(progressMultiplier), deltas(deltas), nodes(nodes), valid(valid) {}
    };

    // state 已同步且概率有效，observation 来自同一局面的 cell；cell 未揭开。
    // remainingSafe 是未揭开的安全格总数；linkedClears 是假设本格安全后的推进基准。
    // pool 为空时退回临时增量；非空时保留增量和完整概率，切片借用调用方的池。
    // pool 引用的概率池须绑定 state 的结构池和分布缓存。
    // prune 控制是否允许剪枝；floor 是比较下界，负数表示当前还没有可用下界；返回前恢复 state。
    Result evaluate(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Pool *pool, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, bool prune, long double floor, SuggestResult::Details *details = nullptr);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    template <bool Prune, bool RetainDeltas>
    Result evaluateImpl(PositionState &state, const ShapeSolverConfig &shapeConfig, long double progressContribution, Pool *pool, ObservedBoard::CellId cell, const Observe::Result &observation, int remainingSafe, int linkedClears, bool isSafeMove, long double floor, SuggestResult::Details *details);
};

} // 命名空间 mss
