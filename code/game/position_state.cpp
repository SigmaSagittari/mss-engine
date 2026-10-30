#include "game/position_state.h"

#include "core/algorithm/neighbors.h"

namespace mss {

long double PositionState::countCandidatesWithForces(span<const ObservedBoard::CellId> mines, span<const ObservedBoard::CellId> safes, const ShapeSolverConfig &config, Basic::Workspace &basicWorkspace, Structure::Workspace &structureWorkspace, Probability::Workspace &probabilityWorkspace, vector<ObservedBoard::CellId> &changedCells) {
    ObservedBoard::Delta updates;
    updates.changes = basicPool.board.changes.push_back();
    changedCells.resize(0);
    for (int i = 0; i < mines.size; ++i) {
        const ObservedBoard::CellId cell = mines.data[i];
        const std::pair<int, int> position = board.pos(cell);
        basicPool.board.changes.push_back(updates.changes, {cell, board.cells[position.first][position.second], ObservedBoard::CellState::ForcedMine});
        changedCells.push_back(cell);
    }
    for (int i = 0; i < safes.size; ++i) {
        const ObservedBoard::CellId cell = safes.data[i];
        const std::pair<int, int> position = board.pos(cell);
        basicPool.board.changes.push_back(updates.changes, {cell, board.cells[position.first][position.second], ObservedBoard::CellState::ForcedSafe});
        changedCells.push_back(cell);
    }

    ObservedBoard::apply(board, updates, basicPool.board);
    const Basic::Delta basicDelta = Basic::update(basic, board, updates, basicPool, basicWorkspace);

    bool valid = true;
    for (int i = 0; i < (int)basicDelta.changes.count; ++i) {
        const Basic::Delta::Change &change = basicPool.changes.data.data[basicDelta.changes.offset + i];
        const std::pair<int, int> position = board.pos(change.cell);
        forEachAdjacent(position.first, position.second, board.cells.rows(), board.cells.cols(), [&](int row, int col) {
            const ObservedBoard::CellState state = board.cells[row][col];
            if (!valid || state > ObservedBoard::CellState::Num8)
                return;
            int remaining = (int)state;
            int hidden = 0;
            forEachAdjacent(row, col, board.cells.rows(), board.cells.cols(), [&](int neighborRow, int neighborCol) {
                const Basic::Mark mark = basic.marks[neighborRow][neighborCol];
                if (mark == Basic::Mark::F)
                    --remaining;
                else if (mark == Basic::Mark::H || mark == Basic::Mark::T)
                    ++hidden;
            });
            if (remaining < 0 || remaining > hidden)
                valid = false;
        });
        if (!valid)
            break;
    }

    long double candidates = 0.0L;
    if (valid) {
        const Structure::Delta structureDelta = Structure::update(structure, board, basic, shapes, changedCells.data, (int)changedCells.size, structureWorkspace);
        const Probability::Result forced = Probability::analyze(board, basic, structure, config, probabilityPool, probabilityWorkspace);
        candidates = forced.candidates;
        probabilityPool.boxProbabilities.pop_back(forced.boxProbabilities);
        probabilityPool.components.pop_back(forced.components);
        Structure::applyDelta(structure, structureDelta, shapes, true);
        shapes.deltas.removed.pop_back(structureDelta.removed);
        shapes.deltas.added.pop_back(structureDelta.added);
    }

    Basic::applyDelta(basic, basicDelta, basicPool, true);
    ObservedBoard::apply(board, updates, basicPool.board, true);
    basicPool.changes.pop_back(basicDelta.changes);
    basicPool.board.changes.pop_back(updates.changes);
    return candidates;
}

void PositionState::applyReveal(ObservedBoard::CellId cell, ObservedBoard::CellState next, const ShapeSolverConfig &config, Basic::Workspace &basicWorkspace, Structure::Workspace &structureWorkspace, Probability::Workspace &probabilityWorkspace, Reveal &reveal, Probability::Pool &results) {
    reveal.cell = cell;
    const std::pair<int, int> position = board.pos(cell);
    reveal.previous = board.cells[position.first][position.second];
    reveal.board.changes = basicPool.board.changes.push_back();
    basicPool.board.changes.push_back(reveal.board.changes, {cell, reveal.previous, next});
    ObservedBoard::apply(board, reveal.board, basicPool.board);
    reveal.basic = Basic::update(basic, board, reveal.board, basicPool, basicWorkspace);
    reveal.structure = Structure::update(structure, board, basic, shapes, &cell, 1, structureWorkspace);
    reveal.probability = Probability::analyze(board, basic, structure, config, results, probabilityWorkspace);
}

void PositionState::undoReveal(Reveal &reveal, Probability::Pool &results) {
    results.boxProbabilities.pop_back(reveal.probability.boxProbabilities);
    results.components.pop_back(reveal.probability.components);
    Structure::applyDelta(structure, reveal.structure, shapes, true);
    shapes.deltas.removed.pop_back(reveal.structure.removed);
    shapes.deltas.added.pop_back(reveal.structure.added);
    Basic::applyDelta(basic, reveal.basic, basicPool, true);
    ObservedBoard::apply(board, reveal.board, basicPool.board, true);
    basicPool.changes.pop_back(reveal.basic.changes);
    basicPool.board.changes.pop_back(reveal.board.changes);
}

} // 命名空间 mss
