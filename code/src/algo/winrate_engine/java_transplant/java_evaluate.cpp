#include "java_evaluate.h"

#include <algorithm>

namespace mss {

void JavaEval::evaluateCandidates(Result &result, winrate::Cells cells, const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune, bool automatic) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    int remainingSafe = board.board.rows() * board.board.cols() - board.totalMines;
    int forcedSafes = 0;
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            if (board.board[row][col] <= winrate::CellState::Num8) --remainingSafe;
            else if (board.board[row][col] == winrate::CellState::ForcedSafe) ++forcedSafes;
        }
    std::vector<Observe::Result> &observations = workspace.observations;
    observations.clear();
    bool live = false;
    for (winrate::CellId cell : cells) {
        observations.push_back(Observe::observe(board, basic, structure, shapes, probability, distributions, cell, workspace.moveEval.probability));
        if (!MoveEval::isDead(observations.back())) live = true;
    }
    std::optional<std::pair<winrate::CellId, Observe::Result>> fallback;
    if (automatic && !live) {
        // 候选窗全 dead 时先在全盘找活格；只有全盘都 dead 才保留一个最安全格。
        std::vector<std::pair<long double, winrate::CellId>> order;
        for (int row = 0; row < board.board.rows(); ++row)
            for (int col = 0; col < board.board.cols(); ++col)
                if (board.board[row][col] == winrate::CellState::Hidden || board.board[row][col] == winrate::CellState::ForcedSafe) {
                    const winrate::CellId cell = board.id(row, col);
                    const long double mine = probability.mineProbability(cell, board, basic, structure);
                    if (mine < 1.0L) order.emplace_back(mine, cell);
                }
        std::stable_sort(order.begin(), order.end(), [](const std::pair<long double, winrate::CellId> &lhs, const std::pair<long double, winrate::CellId> &rhs) { return lhs.first < rhs.first; });
        for (const std::pair<long double, winrate::CellId> &candidate : order) {
            const Observe::Result observation = Observe::observe(board, basic, structure, shapes, probability, distributions, candidate.second, workspace.moveEval.probability);
            if (!fallback) fallback.emplace(candidate.second, observation);
            if (!MoveEval::isDead(observation)) { fallback.emplace(candidate.second, observation); live = true; break; }
        }
        if (fallback) {
            for (winrate::CellId cell : cells)
                if (cell != fallback->first) result.emplace_back(cell, -1.0L);
            cells = std::span{&fallback->first, 1};
            observations.assign(1, fallback->second);
        }
    }
    std::vector<winrate::CellId> active;
    for (int i = 0; i < (int)cells.size(); ++i)
        if (!automatic || !live || !MoveEval::isDead(observations[i])) active.push_back(cells[i]);
    std::vector<std::pair<long double, winrate::CellId>> peers;
    for (winrate::CellId cell : active) peers.emplace_back(probability.mineProbability(cell, board, basic, structure), cell);
    long double best = 0.0L;
    for (int i = 0; i < (int)cells.size(); ++i) {
        if (automatic && live && MoveEval::isDead(observations[i])) { result.emplace_back(cells[i], -1.0L); continue; }
        MoveEval::Cursor cursor = MoveEval::begin(input, risk, cells[i], observations[i], cfg, riskConfig, workspace.moveEval);
        MoveEval::prepareAlternatives(cursor, active, peers, automatic || prune);
        while (!MoveEval::advance(cursor, input, remainingSafe, forcedSafes, &risk, cfg, workspace.moveEval)) {
            if (prune && MoveEval::optimistic(cursor, cfg) < best) { cursor.analysis.value = -1.0L; cursor.analysis.branches.clear(); break; }
        }
        // 只有全部数字结局都能证明替代格安全才剪掉本招；完整指定格分析不做这一步。
        if (!cursor.alternatives.empty()) { cursor.analysis.value = -1.0L; cursor.analysis.branches.clear(); }
        best = (std::max)(best, cursor.analysis.value);
        result.push_back(std::move(cursor.analysis));
    }
}

void JavaEval::update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const MoveEval::Config &evaluationConfig, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune) {
    result.clear();
    LongTermRisk::resetCache(workspace.moveEval.risk, input.board.board.rows() * input.board.board.cols());
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    workspace.offEdge.clear();
    workspace.witnesses.clear();
    winrate::CellId firstOffEdge = -1;
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            const winrate::CellId cell = board.id(row, col);
            if (board.board[row][col] <= winrate::CellState::Num8) workspace.witnesses.push_back(cell);
            else if (board.board[row][col] != winrate::CellState::ForcedMine) {
                if (probability.mineProbability(cell, board, basic, structure) == 0.0L) { result.emplace_back(cell, 1.0L); return; }
                if (basic.marks[row][col] == Basic::Mark::T) {
                    if (firstOffEdge == -1) firstOffEdge = cell;
                    if (basic.unknownSum < 30 || MoveEval::offEdgeCandidate(board, cell)) workspace.offEdge.push_back(cell);
                }
            }
        }
    if (risk.pseudos.empty()) Pseudo::update(workspace.pseudoResult, input, dead, pseudoConfig, workspace.pseudo);
    const std::vector<winrate::CellId> &pseudos = risk.pseudos.empty() ? workspace.pseudoResult : risk.pseudos;
    const winrate::Cells cells = pseudos.empty() ? MoveEval::Selection::select(input, risk, dead, workspace.offEdge, workspace.witnesses, firstOffEdge, cfg, workspace.selection) : winrate::Cells(pseudos);
    evaluateCandidates(result, cells, input, risk, evaluationConfig, riskConfig, workspace, prune, true);
    std::stable_sort(result.begin(), result.end(), [](const Analysis &lhs, const Analysis &rhs) { return lhs.value > rhs.value; });
}

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const MoveEval::Config &evaluationConfig, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune) {
    Result result;
    update(result, input, risk, dead, cfg, evaluationConfig, riskConfig, pseudoConfig, workspace, prune);
    return result;
}

void JavaEval::update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune) {
    result.clear();
    LongTermRisk::resetCache(workspace.moveEval.risk, input.board.board.rows() * input.board.board.cols());
    evaluateCandidates(result, cells, input, risk, cfg, riskConfig, workspace, prune, false);
}

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune) {
    Result result;
    update(result, input, risk, cells, cfg, riskConfig, workspace, prune);
    return result;
}

} // 命名空间 mss
