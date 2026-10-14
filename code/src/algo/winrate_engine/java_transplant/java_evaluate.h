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
        Workspace(MoveEval::Workspace &moveEval, Pseudo::Workspace &pseudo) : moveEval(moveEval), pseudo(pseudo) {}

      private:
        friend struct JavaEval;
        MoveEval::Workspace &moveEval;
        Pseudo::Workspace &pseudo;
        Pseudo::Result pseudoResult;
        MoveEval::Selection::Workspace selection;
        std::vector<Observe::Result> observations;
        std::vector<winrate::CellId> offEdge;
        std::vector<winrate::CellId> witnesses;
    };

    // 输入和 risk 对应同一有效盘面；工作区串行复用，临时揭示均已撤回。
    // 结果拥有 MoveEval 分支资料，可直接移动给 AnytimeSearch。
    // 自动入口先排除死格；全盘均为死格时只保留一个最安全格。对换剪枝后按分数降序返回，被剪项以 value=-1 留在末尾供 UI 显示。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const MoveEval::Config &evaluationConfig, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune = true);
    static void update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const MoveEval::Config &evaluationConfig, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune = true);
    // 指定格按输入顺序返回；prune=false 保留全部根招法并计算完全部数字分支。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const MoveEval::Config &evaluationConfig, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune);
    static void update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const MoveEval::Config &evaluationConfig, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune);

  private:
    static void evaluateCandidates(Result &result, winrate::Cells cells, const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune, bool automatic);
};

} // 命名空间 mss
