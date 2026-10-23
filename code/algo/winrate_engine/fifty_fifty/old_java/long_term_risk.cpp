#include "algo/winrate_engine/fifty_fifty/old_java/long_term_risk.h"

#include <algorithm>
#include <cmath>

namespace mss {

void LongTermRisk::Result::swapStorage(Workspace &workspace) {
    influences.swap(workspace.influenceStorage);
    enablers.swap(workspace.enablerStorage);
    values.swap(workspace.valueStorage);
    ready.swap(workspace.readyStorage);
}

bool LongTermRisk::hidden(const PositionState &state, int row, int col) {
    return row >= 0 && row < state.board.cells.rows() && col >= 0 && col < state.board.cells.cols() && state.board.cells[row][col] == ObservedBoard::CellState::Hidden && state.basic.marks[row][col] != Basic::Mark::F;
}

bool LongTermRisk::contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell) {
    for (int i = 0; i < cells.size; ++i)
        if (cells.data[i] == cell)
            return true;
    return false;
}

bool LongTermRisk::hasSafeSource(const ObservedBoard::Result &board, span<const CellPosition> source) {
    for (int i = 0; i < source.size; ++i) {
        const CellPosition &position = source.data[i];
        if (position.row >= 0 && position.row < board.cells.rows() && position.col >= 0 && position.col < board.cells.cols()) {
            const ObservedBoard::CellState state = board.cells[position.row][position.col];
            if (state <= ObservedBoard::CellState::Num8 || state == ObservedBoard::CellState::ForcedSafe)
                return true;
        }
    }
    return false;
}

LongTermRisk::CellList LongTermRisk::missingMines(const PositionState &state, span<const CellPosition> source, long double eqEps) {
    CellList missing;
    for (int i = 0; i < source.size; ++i) {
        const CellPosition &position = source.data[i];
        if (position.row < 0 || position.row >= state.board.cells.rows() || position.col < 0 || position.col >= state.board.cells.cols())
            continue;
        const ObservedBoard::CellId cell = state.board.id(position.row, position.col);
        if (Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) >= 1.0L - eqEps)
            continue;
        missing.push_back(cell);
    }
    return missing;
}

long double LongTermRisk::boxTally(ObservedBoard::CellId cell, const PositionState &state) {
    return Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) * state.probability.candidates;
}

long double LongTermRisk::countWithForces(PositionState &state, span<const ObservedBoard::CellId> mines, span<const ObservedBoard::CellId> safes, const ShapeSolverConfig &shapeConfig, Workspace &workspace) {
    return state.countCandidatesWithForces(mines, safes, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.changedCells, workspace.forcedProbability);
}

long double LongTermRisk::candidateTally(PositionState &state, span<const ObservedBoard::CellId> tiles, span<const CellPosition> source, int mineCount, int maxMissing, int minesLeft, const ShapeSolverConfig &shapeConfig, const Config &config, TallyKind kind, ObservedBoard::CellId cacheCell, Result &result, Workspace &workspace) {
    if (hasSafeSource(state.board, source))
        return 0.0L;
    const CellList missing = missingMines(state, source, config.eqEps);
    const int extra = mineCount == 1 ? 1 : 2;
    if (missing.count + extra > maxMissing || missing.count + extra > minesLeft)
        return 0.0L;
    CellList mines = missing;
    mines.push_back(tiles[0]);
    CellList safes;
    if (mineCount == 1) {
        safes.push_back(tiles[1]);
    } else {
        mines.push_back(tiles[3]);
        safes.push_back(tiles[1]);
        safes.push_back(tiles[2]);
    }
    const int slot = (int)kind;
    if (result.ready[slot][cacheCell])
        return result.values[slot][cacheCell];
    const long double value = countWithForces(state, mines.span(), safes.span(), shapeConfig, workspace);
    result.values[slot][cacheCell] = value;
    result.ready[slot][cacheCell] = 1;
    return value;
}

void LongTermRisk::update(Result &result, PositionState &state, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Pseudo::Result *pseudo, Details *details) {
    const int rows = state.board.cells.rows();
    const int cols = state.board.cells.cols();
    const int size = rows * cols;
    result.influences.resize(rows, cols, 0.0L);
    result.enablers.resize(size);
    std::fill(result.enablers.data, result.enablers.data + size, 0.0L);
    result.values.resize(3, size, 0.0L);
    result.ready.resize(3, size, 0);
    const int minesLeft = state.board.totalMines - state.basic.mineSum;
    if (pseudo != nullptr) {
        pseudo->kind = Pseudo::Kind::None;
        pseudo->cells.resize(0);
    }

    auto consider = [&](span<const ObservedBoard::CellId> tiles, span<const CellPosition> source, int mineCount, int maxMissing) -> bool {
        if (hasSafeSource(state.board, source))
            return false;
        const CellList missing = missingMines(state, source, config.eqEps);
        const int extra = mineCount == 1 ? 1 : 2;
        if (missing.count + extra > maxMissing || missing.count + extra > minesLeft)
            return false;
        CellList mines = missing;
        mines.push_back(tiles[0]);
        CellList safes;
        if (mineCount == 1) {
            safes.push_back(tiles[1]);
        } else {
            mines.push_back(tiles[3]);
            safes.push_back(tiles[1]);
            safes.push_back(tiles[2]);
        }
        const long double tally = countWithForces(state, mines.span(), safes.span(), shapeConfig, workspace);
        if (tally == 0.0L)
            return false;
        if (pseudo != nullptr) {
            for (int i = 0; i < tiles.size; ++i) {
                const long double mineTally = boxTally(tiles.data[i], state);
                if (std::abs(tally - mineTally) <= config.eqEps * (std::max)(tally, mineTally) && !contains(dead, tiles.data[i]))
                    pseudo->cells.push_back(tiles.data[i]);
            }
            if (pseudo->cells.size != 0) {
                pseudo->kind = (int)pseudo->cells.size == tiles.size && std::abs(2.0L * tally - state.probability.candidates) <= config.eqEps * state.probability.candidates ? Pseudo::Kind::Forced : Pseudo::Kind::Semi;
                return true;
            }
        }
        const long double ratio = tally / state.probability.candidates;
        if (ratio <= config.influenceThreshold)
            return false;
        // 限制格在两个交换方向中都是雷，命中量是单方向计数的两倍。
        if (details && contains(missing.span(), details->cell)) details->restriction = (std::max)(details->restriction, 2.0L * tally);
        for (int i = 0; i < tiles.size; ++i) {
            const std::pair<int, int> position = state.board.pos(tiles.data[i]);
            result.influences[position.first][position.second] += tally;
        }
        for (int i = 0; i < missing.count; ++i) {
            const ObservedBoard::CellId enabler = missing.cells[i];
            const std::pair<int, int> position = state.board.pos(enabler);
            result.influences[position.first][position.second] += tally;
            result.enablers.data[enabler] += tally;
        }
        return false;
    };

    for (int row = 0; row < rows; ++row)
        for (int col = 0; col + 1 < cols; ++col) {
            if (!hidden(state, row, col) || !hidden(state, row, col + 1))
                continue;
            const ObservedBoard::CellId tiles[2] = {state.board.id(row, col), state.board.id(row, col + 1)};
            const CellPosition source[6] = {CellPosition(row - 1, col - 1), CellPosition(row, col - 1), CellPosition(row + 1, col - 1), CellPosition(row - 1, col + 2), CellPosition(row, col + 2), CellPosition(row + 1, col + 2)};
            if (consider(span<const ObservedBoard::CellId>(tiles, 2), span<const CellPosition>(source, 6), 1, 2))
                return;
        }
    for (int row = 0; row + 1 < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            if (!hidden(state, row, col) || !hidden(state, row + 1, col))
                continue;
            const ObservedBoard::CellId tiles[2] = {state.board.id(row, col), state.board.id(row + 1, col)};
            const CellPosition source[6] = {CellPosition(row - 1, col - 1), CellPosition(row - 1, col), CellPosition(row - 1, col + 1), CellPosition(row + 2, col - 1), CellPosition(row + 2, col), CellPosition(row + 2, col + 1)};
            if (consider(span<const ObservedBoard::CellId>(tiles, 2), span<const CellPosition>(source, 6), 1, 2))
                return;
        }
    for (int row = 0; row + 1 < rows; ++row)
        for (int col = 0; col + 1 < cols; ++col) {
            if (!hidden(state, row, col) || !hidden(state, row, col + 1) || !hidden(state, row + 1, col) || !hidden(state, row + 1, col + 1))
                continue;
            const ObservedBoard::CellId tiles[4] = {state.board.id(row, col), state.board.id(row, col + 1), state.board.id(row + 1, col), state.board.id(row + 1, col + 1)};
            const CellPosition source[4] = {CellPosition(row - 1, col - 1), CellPosition(row - 1, col + 2), CellPosition(row + 2, col - 1), CellPosition(row + 2, col + 2)};
            if (consider(span<const ObservedBoard::CellId>(tiles, 4), span<const CellPosition>(source, 4), 2, 3))
                return;
        }
}

long double LongTermRisk::influence(ObservedBoard::CellId cell, PositionState &state, Result &full, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Details *details) {
    const int rows = state.board.cells.rows();
    const int cols = state.board.cells.cols();
    const int minesLeft = state.board.totalMines - state.basic.mineSum;
    const std::pair<int, int> position = state.board.pos(cell);
    const int row = position.first;
    const int col = position.second;

    auto horizontal = [&](int startRow, int startCol) {
        if (startRow < 0 || startRow >= rows || startCol < 0 || startCol + 1 >= cols)
            return 0.0L;
        if (!hidden(state, startRow, startCol) || !hidden(state, startRow, startCol + 1))
            return 0.0L;
        const ObservedBoard::CellId tiles[2] = {state.board.id(startRow, startCol), state.board.id(startRow, startCol + 1)};
        const CellPosition source[6] = {CellPosition(startRow - 1, startCol - 1), CellPosition(startRow, startCol - 1), CellPosition(startRow + 1, startCol - 1), CellPosition(startRow - 1, startCol + 2), CellPosition(startRow, startCol + 2), CellPosition(startRow + 1, startCol + 2)};
        return candidateTally(state, span<const ObservedBoard::CellId>(tiles, 2), span<const CellPosition>(source, 6), 1, 4, minesLeft, shapeConfig, config, TallyKind::Horizontal, state.board.id(startRow, startCol), full, workspace);
    };
    auto vertical = [&](int startRow, int startCol) {
        if (startRow < 0 || startRow + 1 >= rows || startCol < 0 || startCol >= cols)
            return 0.0L;
        if (!hidden(state, startRow, startCol) || !hidden(state, startRow + 1, startCol))
            return 0.0L;
        const ObservedBoard::CellId tiles[2] = {state.board.id(startRow, startCol), state.board.id(startRow + 1, startCol)};
        const CellPosition source[6] = {CellPosition(startRow - 1, startCol - 1), CellPosition(startRow - 1, startCol), CellPosition(startRow - 1, startCol + 1), CellPosition(startRow + 2, startCol - 1), CellPosition(startRow + 2, startCol), CellPosition(startRow + 2, startCol + 1)};
        return candidateTally(state, span<const ObservedBoard::CellId>(tiles, 2), span<const CellPosition>(source, 6), 1, 4, minesLeft, shapeConfig, config, TallyKind::Vertical, state.board.id(startRow, startCol), full, workspace);
    };
    auto box = [&](int startRow, int startCol) {
        if (startRow < 0 || startRow + 1 >= rows || startCol < 0 || startCol + 1 >= cols)
            return 0.0L;
        if (!hidden(state, startRow, startCol) || !hidden(state, startRow + 1, startCol) || !hidden(state, startRow, startCol + 1) || !hidden(state, startRow + 1, startCol + 1))
            return 0.0L;
        const ObservedBoard::CellId tiles[4] = {state.board.id(startRow, startCol), state.board.id(startRow, startCol + 1), state.board.id(startRow + 1, startCol), state.board.id(startRow + 1, startCol + 1)};
        const CellPosition source[4] = {CellPosition(startRow - 1, startCol - 1), CellPosition(startRow - 1, startCol + 2), CellPosition(startRow + 2, startCol - 1), CellPosition(startRow + 2, startCol + 2)};
        return candidateTally(state, span<const ObservedBoard::CellId>(tiles, 4), span<const CellPosition>(source, 4), 2, 5, minesLeft, shapeConfig, config, TallyKind::Box, state.board.id(startRow, startCol), full, workspace);
    };

    long double value = 0.0L;
    const long double horizontalValue = (std::max)(horizontal(row, col), horizontal(row, col - 1));
    const long double verticalValue = (std::max)(vertical(row, col), vertical(row - 1, col));
    value += horizontalValue;
    value += verticalValue;
    const long double boxes[4] = {box(row, col), box(row - 1, col), box(row, col - 1), box(row - 1, col - 1)};
    const long double boxValue = (std::max)((std::max)(boxes[0], boxes[1]), (std::max)(boxes[2], boxes[3]));
    value += boxValue;
    if (details) details->body = (std::max)((std::max)(horizontalValue, verticalValue), boxValue);
    value += full.enablers.data[cell];
    long double maxInfluence = boxTally(cell, state);
    maxInfluence = (std::min)(maxInfluence, state.probability.candidates - maxInfluence);
    return (std::min)(value, maxInfluence);
}

LongTermRisk::Details LongTermRisk::describe(ObservedBoard::CellId cell, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace) {
    Details details(cell);
    Result result;
    result.swapStorage(workspace);
    update(result, state, span<const ObservedBoard::CellId>(), shapeConfig, config, workspace, nullptr, &details);
    influence(cell, state, result, shapeConfig, config, workspace, &details);
    const long double mineTally = boxTally(cell, state);
    details.body /= mineTally;
    details.restriction /= mineTally;
    result.swapStorage(workspace);
    return details;
}

LongTermRisk::Result LongTermRisk::analyze(PositionState &state, span<const ObservedBoard::CellId> dead, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Pseudo::Result *pseudo) {
    Result result;
    update(result, state, dead, shapeConfig, config, workspace, pseudo);
    return result;
}

} // 命名空间 mss
