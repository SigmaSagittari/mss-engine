#pragma once

#include <vector>

#include "algo/winrate_engine/internal/move_evaluate.h"

namespace mss {

struct JavaEval {
    struct Config {
        MoveEval::Selection::Config selection;
        MoveEval::Config moveEval;
        Config(MoveEval::Selection::Config selection, MoveEval::Config moveEval) : selection(selection), moveEval(moveEval) {}
    };
    using Analysis = MoveEval::Analysis;
    using Result = std::vector<Analysis>;

    struct Workspace {
        MoveEval::Selection::Workspace selection;
        MoveEval::Workspace moveEval;
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : moveEval(basic, structure, probability) {}
    };

    // 空 cells 自动找候选并过滤 dead；指定格按输入顺序评分。reason 仅用于返回自动选择原因。
    static Result analyze(const winrate::Input &input, const Config &cfg, Workspace &workspace, winrate::Cells cells = {}, MoveEval::Selection::Reason *reason = nullptr);
    // 已准备候选的评分入口保留原顺序，不执行搜索剪枝。
    // 结果拥有分支和 Delta 切片；记录借用 input 的各模块池，读取分支前不得清空或退回这些记录。工作区串行复用，临时揭示在返回前已撤回。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const Config &cfg, Workspace &workspace);
};

} // 命名空间 mss
