#pragma once

#include "algo/winrate_engine/result_type.h"
#include "algo/winrate_engine/selection/selection.h"
#include "core/datastructure/grid.h"

namespace mss {

struct JavaEval {
    enum class Compensation { Additive, ScaleWithProgress, ScaleWithoutProgress };

    struct Config {
        long double progressContribution; // 非负。

        void hash(U128Hasher &hasher) const { hasher.mix(progressContribution); }
    };

    // 候选、概率及 remainingSafe 对应同一盘面。longTermRisk 按盘面行列存放各候选的非负补偿。
    // 缩放模式按本格安全条件下的平均评分折算补偿，可选择是否把推进奖励计入折算。
    // 选招只返回完整算过分数的候选。
    SuggestResult analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                          Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                          int remainingSafe, const Config &config, Compensation mode = Compensation::Additive);

    // 按输入顺序完整评估候选，并覆盖逐数字明细。
    SuggestResult analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                          Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                          int remainingSafe, const Config &config, SuggestResult::Details &details, Compensation mode = Compensation::Additive);

  private:
    struct Workspace;
    struct Outcome { long double safety; int knownSafe; };
    struct Evaluation { long double value; bool valid; };

    Workspace &getWorkspace();
    Outcome scoreOutcome(const Position::Result &position, const Position::ProbabilityResult &probability,
                         const Probability::Pool &probabilities, int remaining);

    template <bool Details, typename Record>
    SuggestResult analyzeImpl(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                               Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                               int remainingSafe, const Config &config, Compensation mode, Record record);
};

} // 命名空间 mss
