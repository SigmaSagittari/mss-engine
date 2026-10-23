#include "algo/winrate_engine/basic/selection.h"

#include <algorithm>

#include "core/algorithm/neighbors.h"

namespace mss {

void Selection::Result::swapStorage(Workspace &workspace) {
    cells.swap(workspace.cellStorage);
    observations.swap(workspace.observationStorage);
    fiftyFifty.swapStorage(workspace.fiftyFifty);
}

bool Selection::contains(span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell) {
    for (int i = 0; i < cells.size; ++i)
        if (cells.data[i] == cell)
            return true;
    return false;
}

bool Selection::isDead(const Observe::Result &observation) {
    int digits = 0;
    for (int digit = 0; digit <= 8; ++digit)
        if (observation.probability[digit] != 0.0L)
            ++digits;
    return digits == 1;
}

bool Selection::offEdgeCandidate(const ObservedBoard::Result &board, ObservedBoard::CellId cell) {
    const std::pair<int, int> position = board.pos(cell);
    int adjacent = 0;
    forEachAdjacent(position.first, position.second, board.cells.rows(), board.cells.cols(), [&](int row, int col) {
        if (board.cells[row][col] == ObservedBoard::CellState::Hidden)
            ++adjacent;
    });
    return adjacent > 1 && adjacent < 4;
}

bool Selection::meetsSpaceThreshold(int startRow, int startCol, const PositionState &state, int threshold, Workspace &workspace) {
    auto available = [&](int row, int col) {
        return state.board.cells[row][col] == ObservedBoard::CellState::Hidden && Probability::mineProbability(state.probability, state.board.id(row, col), state.board, state.basic, state.structure) != 1.0L;
    };
    if (workspace.visited.rows() != state.board.cells.rows() || workspace.visited.cols() != state.board.cells.cols())
        workspace.visited.resize(state.board.cells.rows(), state.board.cells.cols(), 0);
    if (++workspace.visit == 0) {
        workspace.visited.resize(state.board.cells.rows(), state.board.cells.cols(), 0);
        ++workspace.visit;
    }
    const unsigned visit = workspace.visit;
    workspace.stack.resize(0);
    workspace.stack.push_back(CellPosition(startRow, startCol));
    workspace.visited[startRow][startCol] = visit;
    int count = 0;
    while (workspace.stack.size != 0) {
        const CellPosition position = workspace.stack.data[workspace.stack.size - 1];
        workspace.stack.size -= 1;
        ++count;
        if (count >= threshold)
            return true;
        forEachAdjacent(position.row, position.col, state.board.cells.rows(), state.board.cells.cols(), [&](int row, int col) {
            if (workspace.visited[row][col] == visit)
                return;
            if (available(row, col)) {
                workspace.visited[row][col] = visit;
                workspace.stack.push_back(CellPosition(row, col));
                return;
            }
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) {
                workspace.visited[row][col] = visit;
                forEachAdjacent(row, col, state.board.cells.rows(), state.board.cells.cols(), [&](int nextRow, int nextCol) {
                    if (workspace.visited[nextRow][nextCol] != visit && available(nextRow, nextCol)) {
                        workspace.visited[nextRow][nextCol] = visit;
                        workspace.stack.push_back(CellPosition(nextRow, nextCol));
                    }
                });
            }
        });
    }
    return count >= threshold;
}

void Selection::window(vector<ObservedBoard::CellId> &result, const PositionState &state, const LongTermRisk::Result *risk, span<const ObservedBoard::CellId> offEdge, span<const ObservedBoard::CellId> witnesses, ObservedBoard::CellId firstOffEdge, const Config &config, Workspace &workspace) {
    workspace.boxes.resize(0);
    for (int component = 0; component < (int)state.structure.components.size; ++component) {
        const Structure::Instance &instance = state.shapes.instances.list.data[state.structure.components.data[component]];
        const Probability::Result::Component &layout = state.probability.components.data[component];
        const int boxCount = (int)instance.boxOf.count - 1;
        for (int box = 0; box < boxCount; ++box)
            workspace.boxes.push_back(FrontierBox(1.0L - state.probability.boxProbabilities.data[layout.boxOffset + box], state.structure.components.data[component], box));
    }
    std::sort(workspace.boxes.data, workspace.boxes.data + workspace.boxes.size, [](const FrontierBox &left, const FrontierBox &right) {
        return left.safety > right.safety;
    });
    auto cellsOf = [&](const FrontierBox &box) {
        const Structure::Instance &instance = state.shapes.instances.list.data[box.instance];
        const int *offsets = state.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        const ObservedBoard::CellId *cells = state.shapes.instances.cells.data.data + instance.cells.offset;
        return span<const ObservedBoard::CellId>(cells + offsets[box.box], offsets[box.box + 1] - offsets[box.box]);
    };
    long double bestSafety = state.basic.unknownSum != 0 ? 1.0L - state.probability.tCellProbability : 0.0L;
    if (workspace.boxes.size != 0)
        bestSafety = (std::max)(bestSafety, workspace.boxes.data[0].safety);
    long double test = bestSafety == 1.0L ? 1.0L : bestSafety - config.selectionThreshold1;
    long double test2 = bestSafety == 1.0L ? 1.0L : bestSafety - config.selectionThreshold2;

    workspace.onEdge.resize(0);
    for (int i = 0; i < (int)workspace.boxes.size; ++i) {
        const FrontierBox &box = workspace.boxes.data[i];
        if (box.safety < test && ((int)workspace.onEdge.size >= 2 || box.safety < test2))
            break;
        test = (std::min)(test, box.safety);
        const span<const ObservedBoard::CellId> cells = cellsOf(box);
        for (int cell = 0; cell < cells.size; ++cell)
            workspace.onEdge.push_back(cells.data[cell]);
    }

    if (risk != nullptr) {
        const long double cutoffTally = (bestSafety - config.selectionThreshold1) * state.probability.candidates;
        for (int row = 0; row < state.board.cells.rows(); ++row)
            for (int col = 0; col < state.board.cells.cols(); ++col) {
                const long double influence = risk->influences[row][col];
                if (influence == 0.0L)
                    continue;
                const ObservedBoard::CellId cell = state.board.id(row, col);
                const long double mineTally = Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) * state.probability.candidates;
                if (state.probability.candidates - mineTally + influence > cutoffTally && !contains(span<const ObservedBoard::CellId>(workspace.onEdge.data, (int)workspace.onEdge.size), cell))
                    workspace.onEdge.push_back(cell);
            }
    }

    workspace.offEdgeCandidates.resize(0);
    const long double offEdgeSafetyValue = 1.0L - state.probability.tCellProbability;
    if (bestSafety != 1.0L && offEdgeSafetyValue > bestSafety * 0.95L) {
        if (state.basic.unknownSum < 30) {
            for (int i = 0; i < offEdge.size; ++i) {
                const std::pair<int, int> position = state.board.pos(offEdge.data[i]);
                if (state.basic.marks[position.first][position.second] == Basic::Mark::T)
                    workspace.offEdgeCandidates.push_back(offEdge.data[i]);
            }
        } else {
            const int offsets[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
            auto addOffEdge = [&](int row, int col) {
                if (row < 0 || row >= state.board.cells.rows() || col < 0 || col >= state.board.cells.cols())
                    return;
                const ObservedBoard::CellId cell = state.board.id(row, col);
                if (state.basic.marks[row][col] == Basic::Mark::T && !contains(span<const ObservedBoard::CellId>(workspace.offEdgeCandidates.data, (int)workspace.offEdgeCandidates.size), cell))
                    workspace.offEdgeCandidates.push_back(cell);
            };
            for (int i = 0; i < witnesses.size; ++i) {
                const std::pair<int, int> position = state.board.pos(witnesses.data[i]);
                for (int offset = 0; offset < 4; ++offset)
                    addOffEdge(position.first + offsets[offset][0], position.second + offsets[offset][1]);
            }
            for (int i = 0; i < offEdge.size; ++i) {
                const std::pair<int, int> position = state.board.pos(offEdge.data[i]);
                if (state.basic.marks[position.first][position.second] == Basic::Mark::T && !contains(span<const ObservedBoard::CellId>(workspace.offEdgeCandidates.data, (int)workspace.offEdgeCandidates.size), offEdge.data[i]))
                    workspace.offEdgeCandidates.push_back(offEdge.data[i]);
            }
        }
    }

    result.resize(0);
    bool hasLarge = false;
    auto partition = [&](const vector<ObservedBoard::CellId> &tiles) {
        for (int i = 0; i < (int)tiles.size; ++i) {
            const ObservedBoard::CellId cell = tiles.data[i];
            const std::pair<int, int> position = state.board.pos(cell);
            const bool large = meetsSpaceThreshold(position.first, position.second, state, config.spaceThreshold, workspace);
            if (large && !hasLarge) {
                result.resize(0);
                hasLarge = true;
            }
            if ((large || !hasLarge) && !contains(span<const ObservedBoard::CellId>(result.data, (int)result.size), cell))
                result.push_back(cell);
        }
    };
    partition(workspace.onEdge);
    partition(workspace.offEdgeCandidates);

    if (result.size == 0) {
        ObservedBoard::CellId safest = firstOffEdge;
        long double safestSafety = firstOffEdge == -1 ? 0.0L : offEdgeSafetyValue;
        for (int i = 0; i < (int)workspace.boxes.size; ++i) {
            const span<const ObservedBoard::CellId> cells = cellsOf(workspace.boxes.data[i]);
            for (int cell = 0; cell < cells.size; ++cell)
                if (safest == -1 || workspace.boxes.data[i].safety > safestSafety || (workspace.boxes.data[i].safety == safestSafety && cells.data[cell] < safest)) {
                    safestSafety = workspace.boxes.data[i].safety;
                    safest = cells.data[cell];
                }
        }
        if (safest != -1)
            result.push_back(safest);
    }
}

void Selection::find(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, Workspace &workspace, bool useRisk, const vector<ObservedBoard::CellId> *cells) {
    result.cells.resize(0);
    result.observations.resize(0);
    result.reason = Reason::ScoreWins;
    if (cells != nullptr) {
        result.cells = *cells;
        result.reason = Reason::Specified;
        if (useRisk && cells->size != 0)
            FiftyFifty::prepareRisk(result.fiftyFifty, state, shapeConfig, fiftyConfig, workspace.fiftyFifty, false);
        return;
    }
    workspace.offEdge.resize(0);
    workspace.witnesses.resize(0);
    ObservedBoard::CellId firstOffEdge = -1;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col) {
            const ObservedBoard::CellId cell = state.board.id(row, col);
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8)
                workspace.witnesses.push_back(cell);
            else if (state.board.cells[row][col] != ObservedBoard::CellState::ForcedMine) {
                if (!useRisk && Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) == 0.0L) {
                    result.cells.push_back(cell);
                    return;
                }
                if (state.basic.marks[row][col] == Basic::Mark::T) {
                    if (firstOffEdge == -1)
                        firstOffEdge = cell;
                    if (state.basic.unknownSum < 30 || offEdgeCandidate(state.board, cell))
                        workspace.offEdge.push_back(cell);
                }
            }
        }

    Pseudo::Result &pseudo = result.fiftyFifty.pseudo;
    pseudo.cells.swap(result.cells);
    FiftyFifty::detect(pseudo, state, fiftyConfig, fiftyPool, workspace.fiftyFifty);
    if (pseudo.kind == Pseudo::Kind::None && useRisk)
        FiftyFifty::prepareRisk(result.fiftyFifty, state, shapeConfig, fiftyConfig, workspace.fiftyFifty, true);
    result.cells.swap(pseudo.cells);
    if (pseudo.kind != Pseudo::Kind::None) {
        result.reason = pseudo.kind == Pseudo::Kind::Forced ? Reason::Forced : Reason::Semi;
        return;
    }
    window(result.cells, state, useRisk ? &result.fiftyFifty.risk : nullptr, span<const ObservedBoard::CellId>(workspace.offEdge.data, (int)workspace.offEdge.size), span<const ObservedBoard::CellId>(workspace.witnesses.data, (int)workspace.witnesses.size), firstOffEdge, config, workspace);
    std::sort(result.cells.data, result.cells.data + result.cells.size, [&](ObservedBoard::CellId left, ObservedBoard::CellId right) {
        const long double leftProbability = Probability::mineProbability(state.probability, left, state.board, state.basic, state.structure);
        const long double rightProbability = Probability::mineProbability(state.probability, right, state.board, state.basic, state.structure);
        return leftProbability != rightProbability ? leftProbability < rightProbability : left < right;
    });
}

void Selection::removeDead(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, Probability::Workspace &probabilityWorkspace) {
    result.observations.resize(0);
    int count = 0;
    for (int i = 0; i < (int)result.cells.size; ++i) {
        const ObservedBoard::CellId cell = result.cells.data[i];
        Observe::Result observation;
        Observe::observe(state.board, state.basic, state.structure, state.shapes, state.probability, state.distributions, shapeConfig, cell, observation, probabilityWorkspace);
        const bool safe = Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) == 0.0L;
        if (result.reason != Reason::ScoreWins || safe || !isDead(observation)) {
            result.cells.data[count++] = cell;
            result.observations.push_back(observation);
        }
    }
    result.cells.size = count;
    if (result.cells.size != 0 || result.reason != Reason::ScoreWins)
        return;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col)
            if (state.board.cells[row][col] == ObservedBoard::CellState::Hidden && Probability::mineProbability(state.probability, state.board.id(row, col), state.board, state.basic, state.structure) != 1.0L) {
                const ObservedBoard::CellId cell = state.board.id(row, col);
                Observe::Result observation;
                Observe::observe(state.board, state.basic, state.structure, state.shapes, state.probability, state.distributions, shapeConfig, cell, observation, probabilityWorkspace);
                result.cells.push_back(cell);
                result.observations.push_back(observation);
                return;
            }
}

} // 命名空间 mss
