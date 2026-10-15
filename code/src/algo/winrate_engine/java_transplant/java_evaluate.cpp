#include "java_evaluate.h"

namespace mss {

void JavaEval::update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace) {
    result.clear();
    LongTermRisk::resetCache(workspace.moveEval.risk, input.board.board.rows() * input.board.board.cols());
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
        MoveEval::Cursor cursor = MoveEval::begin(input, risk, candidates.cells[i], candidates.observations[i], cfg, riskConfig, workspace.moveEval);
        while (!MoveEval::advance(cursor, board, basic, structure, shapes, distributions, remainingSafe, forcedSafes, cfg, workspace.moveEval)) {}
        result.push_back(std::move(cursor.analysis));
    }
}

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const MoveEval::Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace) {
    Result result;
    update(result, input, risk, candidates, cfg, riskConfig, workspace);
    return result;
}

} // 命名空间 mss
