#pragma once

#include "algo/winrate_engine/java_lite/java_lite.h"
#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

// 生成候选、过滤死格并完成单格评分；根层补偿和候选消耗由同一入口结算。
struct Selection {
    // JavaEval 允许必安和短 Forced 直接评分；Root 完整评估根层；Inner 不计算根层 LTR。
    enum class Mode { JavaEval, Root, Inner };

    // 候选短期评分与根层补偿各保存一份；最终选招分在这里组合。
    struct Evaluation {
        JavaLite::Result javaLite;
        long double compensation; // 根层长期风险加法补偿，树内固定为 0。
        ObservedBoard::CellId cell;
        int preparationNodes; // 本候选评分前实际执行的概率查询次数。

        Evaluation(ObservedBoard::CellId cell, const JavaLite::Result &javaLite, long double compensation, int preparationNodes) : javaLite(javaLite), compensation(compensation), cell(cell), preparationNodes(preparationNodes) {}
        long double score() const { return javaLite.value * javaLite.progressMultiplier + compensation; }
        int nodes() const { return preparationNodes + javaLite.nodes; }
    };

    struct Result {
        // Ordinary：按安全率和空间挑选；Safe：内层已有必安；Specified：调用方指定。
        // Forced：封闭 50-50；Semi：假设候选为雷后必然形成死猜。
        enum class Source { Ordinary, Safe, Specified, Forced, Semi };
        vector<ObservedBoard::CellId> cells;
        vector<Observe::Result> observations; // direct 时不使用，否则与 cells 一一对应。
        vector<ObservedBoard::CellId> prepared; // 对换判断使用已经通过评分前剪枝的格子。
        // 危险：复用容器时不重建 Result；begin 完整写入这些局面字段后才能调用 next。
        Source source;
        int next;
        int remainingSafe, forcedSafes;
        Mode mode;
        bool direct;
        FiftyFifty::Result fiftyFifty;

        SuggestResult::Reason reason() const { return source == Source::Forced ? SuggestResult::Reason::Forced : source == Source::Semi ? SuggestResult::Reason::Semi : SuggestResult::Reason::ScoreWins; }
    };

    struct Config {
        // 优先保留与最高安全率相差不超过此值的候选。
        long double selectionThreshold1;
        // 前沿候选不足两格时允许放宽的安全率差距。
        long double selectionThreshold2;
        // 优先保留能连到至少这么多未知格的位置。
        int spaceThreshold;
        bool interchangeable; // 对换去重：两格独占邻位都是必雷或棋盘外。
        bool equivalence; // 等价类去重：假设安全后得到相同的必安候选集合。
        bool dominance; // 支配去重：假设本格安全后，有一个原本更安全的候选必安。

        long double progressContribution;

        Config(long double selectionThreshold1, long double selectionThreshold2, int spaceThreshold, bool interchangeable, bool equivalence, bool dominance, long double progressContribution) : selectionThreshold1(selectionThreshold1), selectionThreshold2(selectionThreshold2), spaceThreshold(spaceThreshold), interchangeable(interchangeable), equivalence(equivalence), dominance(dominance), progressContribution(progressContribution) {}
        void hash(U128Hasher &hasher) const;
    };
    // state 已同步且概率有效，probabilities 为 state.probability 所属池；初始化候选与根层风险，返回实际 Observe 次数。
    // cells 非空时格子互异且未揭开，保持输入顺序并关闭评分前剪枝；空数组得到空结果。
    // mode 决定评分规则；Result 须持续存活，每次 next 的 state 须回到 begin 的局面。
    int begin(PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, Mode mode, const vector<ObservedBoard::CellId> *cells = nullptr);
    // 消费一个候选并返回评分；被剪掉时 javaLite.valid=false，nodes() 仍包含本次实际工作量。
    // pool 非空时保存数字结局增量及完整概率，切片借用该池；快捷评分没有增量。
    // 调用前 next<cells.size；每个候选完整计算后才返回，Anytime 完成本局面的全部候选后才因预算暂停。
    // 只有 Inner 允许剪枝，floor 提供比较下界；负数表示尚无下界，根层忽略 floor。
    Evaluation next(Result &result, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, JavaLite::Pool *pool, long double floor, SuggestResult::Details *details = nullptr);
    bool offEdgeCandidate(const ObservedBoard::Result &board, ObservedBoard::CellId cell);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    Result::Source find(PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, const vector<ObservedBoard::CellId> *cells);
    int removeDead(Result &result, Result::Source source, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig);
    bool prepare(Result &result, int index, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, int &linkedClears, int &nodes);
    bool interchangeable(const PositionState &state, const Probability::Pool &probabilities, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    bool contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell);
    bool meetsSpaceThreshold(int startRow, int startCol, const PositionState &state, const Probability::Pool &probabilities, int threshold);
    void selectOrdinary(vector<ObservedBoard::CellId> &result, const PositionState &state, const Probability::Pool &probabilities, const LongTermRisk::Result *risk, span<const ObservedBoard::CellId> offEdge, span<const ObservedBoard::CellId> witnesses, ObservedBoard::CellId firstOffEdge, const Config &config);
    bool isDead(const Observe::Result &observation);
};

} // 命名空间 mss
