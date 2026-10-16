#include "java_evaluate.h"

namespace mss {

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const Config &cfg, Workspace &workspace, winrate::Cells cells, MoveEval::Selection::Reason *reason) {
    const LongTermRisk::Result risk = LongTermRisk::analyze(input, {}, cfg.moveEval.longTermRisk, workspace.moveEval.longTermRisk);
    MoveEval::Selection::Result selected = cells.empty() ? MoveEval::Selection::find(input, &risk, cfg.selection, workspace.selection) : MoveEval::Selection::specified(input, cells, workspace.moveEval.probability);
    if (cells.empty()) MoveEval::Selection::removeDead(selected, input, workspace.moveEval.probability);
    if (reason) *reason = selected.reason;
    return analyze(input, risk, selected, cfg, workspace);
}

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const Config &cfg, Workspace &workspace) {
    Result result;
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    int remainingSafe = board.board.rows() * board.board.cols() - board.totalMines;
    int forcedSafes = 0;
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            if (board.board[row][col] <= winrate::CellState::Num8) --remainingSafe;
            else if (board.board[row][col] == winrate::CellState::ForcedSafe) ++forcedSafes;
        }
    for (int i = 0; i < (int)candidates.cells.size(); ++i) {
        if (candidates.reason == MoveEval::Selection::Reason::Safe) { result.emplace_back(candidates.cells[i], 1.0L); continue; }
        MoveEval::Cursor cursor = MoveEval::begin(input, risk, candidates.cells[i], candidates.observations[i], cfg.moveEval, workspace.moveEval);
        while (!MoveEval::advance(cursor, board, basic, structure, shapes, distributions, remainingSafe, forcedSafes, cfg.moveEval, workspace.moveEval)) {}
        result.push_back(std::move(cursor.analysis));
    }
    return result;
}

} // 命名空间 mss
