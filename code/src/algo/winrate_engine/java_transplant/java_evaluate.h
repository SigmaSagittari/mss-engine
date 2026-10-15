#pragma once

#include <span>
#include <vector>

#include "algo/winrate_engine/internal/move_evaluate.h"
#include "pseudo_helper.h"

namespace mss {

struct JavaEval {
    using Config = MoveEval::Selection::Config;
    using Analysis = MoveEval::Analysis;
    using Result = std::vector<Analysis>;

    struct Workspace {
        MoveEval::Selection::Workspace selection;
        explicit Workspace(MoveEval::Workspace &moveEval) : moveEval(moveEval) {}
      private:
        friend struct JavaEval;
        MoveEval::Workspace &moveEval;
    };

    // 候选及观测已在同一有效根盘面上准备完成；只评分，不筛选、不剪枝，按输入顺序返回。
    // 结果拥有分支与 Delta；工作区串行复用，临时揭示在返回前已撤回。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace);
    static void update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace);
};

} // 命名空间 mss
