#pragma once

#include "algo/winrate_engine/move_policy.h"
#include "core/algorithm/rng.h"

namespace mss {

struct MonteCarlo {
    struct Candidate {
        ObservedBoard::CellId cell;
        long long samples, wins;
        long double mean, variance;
        long long chanceSamples; // -1 表示尚未计算超过当前最佳均值的概率。
        long double chanceTarget, chanceLog;

        Candidate(ObservedBoard::CellId cell);
    };

    struct Result {
        bool winrate; // 有必安或已结束时，统计整局胜率。
        SuggestResult exact; // 暴力接管时直接返回的结果。
        long long samples, wins;
        vector<Candidate> candidates;
        vector<ObservedBoard::CellId> safeCells; // 整局采样先揭开这些必安格。
        Random random;

        Result();
        // 接收必安、终局或暴力接管的结果。
        explicit Result(SuggestResult &root);
        // 有候选时返回下标，优先比较已采样候选的胜率均值。
        int best() const;
        ObservedBoard::CellId recommended() const;
    };

    struct Pool {
        Suggest::Pool &suggestions;
        ShapeSolver::AssignmentPool assignments;

        // suggestions 须覆盖本池的使用期，可与普通选招串行共用。
        Pool(const Structure::Pool &shapes, Suggest::Pool &suggestions)
            : suggestions(suggestions), assignments(shapes) {}
    };

    // 需要猜测时，按 Selection 的顺序接收全部候选，每个候选先模拟一局。
    Result analyze(const Selection::Result &selected);
    // position 是生成首招结果时的根盘面，Structure 已同步；pool 绑定该盘面的 Structure 实例池。
    // policy 只用于模拟对局中的后续走法。每次模拟一局，暴力接管时已有精确结果。
    void update(Result &result, const Position::Result &position, const MovePolicy::Config &policy, Pool &pool);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    void estimate(Candidate &candidate);
    long double logChance(long double won, long double lost, long double target);
    int select(Result &result);
};

} // 命名空间 mss
