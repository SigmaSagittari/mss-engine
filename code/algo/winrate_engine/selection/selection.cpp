#include "algo/winrate_engine/selection/selection.h"

#include <algorithm>

#include "core/algorithm/neighbors.h"

namespace mss {

// 对换去重：两格独占的邻位只能是必雷或棋盘外，不依赖 Box。
bool Selection::interchangeable(const PositionState &state, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs) {
    const std::pair<int, int> left = state.board.pos(lhs);
    const std::pair<int, int> right = state.board.pos(rhs);
    if (std::abs(left.first - right.first) > 1 || std::abs(left.second - right.second) > 1)
        return false;
    auto matches = [&](int row, int col, int otherRow, int otherCol, ObservedBoard::CellId other) {
        bool same = true;
        forEachAdjacent(row, col, state.board.cells.rows(), state.board.cells.cols(), [&](int neighborRow, int neighborCol) {
            const ObservedBoard::CellId cell = state.board.id(neighborRow, neighborCol);
            if (cell == other || Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) == 1.0L)
                return;
            if (std::abs(neighborRow - otherRow) > 1 || std::abs(neighborCol - otherCol) > 1)
                same = false;
        });
        return same;
    };
    return matches(left.first, left.second, right.first, right.second, rhs) && matches(right.first, right.second, left.first, left.second, lhs);
}

bool Selection::prepare(Result &result, int index, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, int remainingSafe, int forcedSafes, bool isSafeMove, long long &nodes, Workspace &workspace) {
    const ObservedBoard::CellId cell = result.cells.data[index];
    if (config.equivalence)
        for (int i = 0; i < (int)result.equivalent.size; ++i)
            if (result.equivalent.data[i] == index)
                return false;
    if (config.interchangeable)
        for (int i = 0; i < (int)result.prepared.size; ++i)
            if (interchangeable(state, result.cells.data[result.prepared.data[i].index], cell))
                return false;
    const long double mineProbability = result.observations.data[index].probability[9];
    if (isSafeMove || mineProbability == 1.0L) {
        result.prepared.push_back(Result::Prepared(index, 0));
        return true;
    }
    state.applyReveal(cell, ObservedBoard::CellState::ForcedSafe, shapeConfig, workspace.basic, workspace.structure, workspace.probability, workspace.reveal);
    ++nodes;
    // 只统计前沿和已知安全格；无前沿格的额外清除不计入推进基准。
    int linkedClears = state.board.totalMines + remainingSafe - state.basic.mineSum - state.basic.unknownSum;
    for (int component = 0; component < (int)state.structure.components.size; ++component) {
        const Structure::Instance &instance = state.shapes.instances.list.data[state.structure.components.data[component]];
        const Probability::Result::Component &layout = workspace.reveal.probability.components.data[component];
        const int *offsets = state.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        for (int box = 0; box + 1 < (int)instance.boxOf.count; ++box)
            if (1.0L - workspace.reveal.probability.boxProbabilities.data[layout.boxOffset + box] != 1.0L)
                linkedClears -= offsets[box + 1] - offsets[box];
    }
    linkedClears -= forcedSafes + (workspace.reveal.previous == ObservedBoard::CellState::ForcedSafe ? 0 : 1);
    U128Hasher hasher;
    bool dominated = false;
    const int equivalentCount = result.equivalent.size;
    if (config.equivalence || config.dominance)
        for (int i = 0; i < (int)result.cells.size; ++i)
            if (Probability::mineProbability(workspace.reveal.probability, result.cells.data[i], state.board, state.basic, state.structure) == 0.0L) {
                // 等价类去重：假设安全后得到同一个必安候选集合。
                if (config.equivalence) {
                    hasher.mix(result.cells.data[i]);
                    // A 安全推出 B 安全且两者等概率，则安全事件相同，后续 B 直接复用此类。
                    if (i > index && result.observations.data[i].probability[9] == mineProbability) result.equivalent.push_back(i);
                }
                // 支配去重的充分条件：原本更安全的候选在本格安全时必安。
                if (config.dominance && result.observations.data[i].probability[9] < mineProbability) dominated = true;
            }
    state.undoReveal(workspace.reveal);
    if (dominated) {
        result.equivalent.resize(equivalentCount);
        return false;
    }
    if (config.equivalence) {
        const U128 identity = hasher.finalize();
        for (int i = 0; i < (int)result.identities.size; ++i)
            if (result.identities.data[i] == identity) {
                result.equivalent.resize(equivalentCount);
                return false;
            }
        result.identities.push_back(identity);
    }
    result.prepared.push_back(Result::Prepared(index, linkedClears));
    return true;
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

void Selection::selectOrdinary(vector<ObservedBoard::CellId> &result, const PositionState &state, const LongTermRisk::Result *risk, span<const ObservedBoard::CellId> offEdge, span<const ObservedBoard::CellId> witnesses, ObservedBoard::CellId firstOffEdge, const Config &config, Workspace &workspace) {
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

Selection::Result::Source Selection::find(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, Workspace &workspace, bool useRisk, const vector<ObservedBoard::CellId> *cells) {
    result.cells.resize(0);
    result.observations.resize(0);
    result.prepared.resize(0);
    result.identities.resize(0);
    result.equivalent.resize(0);
    if (cells != nullptr) {
        result.cells = *cells;
        return Result::Source::Specified;
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
                    return Result::Source::Safe;
                }
                if (state.basic.marks[row][col] == Basic::Mark::T) {
                    if (firstOffEdge == -1)
                        firstOffEdge = cell;
                    if (state.basic.unknownSum < 30 || offEdgeCandidate(state.board, cell))
                        workspace.offEdge.push_back(cell);
                }
            }
        }

    Semi::Result &semi = result.fiftyFifty.semi;
    semi.cells.swap(result.cells);
    FiftyFifty::detect(semi, state, fiftyConfig, fiftyPool, workspace.fiftyFifty);
    if (semi.kind == Semi::Kind::None && useRisk && fiftyConfig.algorithm == FiftyFifty::Algorithm::OldJava)
        FiftyFifty::prepareRisk(result.fiftyFifty, state, shapeConfig, fiftyConfig, fiftyPool, workspace.fiftyFifty, true);
    result.cells.swap(semi.cells);
    if (semi.kind != Semi::Kind::None)
        return semi.kind == Semi::Kind::Forced ? Result::Source::Forced : Result::Source::Semi;
    selectOrdinary(result.cells, state, useRisk && fiftyConfig.algorithm == FiftyFifty::Algorithm::OldJava ? &result.fiftyFifty.oldJava : nullptr, span<const ObservedBoard::CellId>(workspace.offEdge.data, (int)workspace.offEdge.size), span<const ObservedBoard::CellId>(workspace.witnesses.data, (int)workspace.witnesses.size), firstOffEdge, config, workspace);
    std::sort(result.cells.data, result.cells.data + result.cells.size, [&](ObservedBoard::CellId left, ObservedBoard::CellId right) {
        const long double leftProbability = Probability::mineProbability(state.probability, left, state.board, state.basic, state.structure);
        const long double rightProbability = Probability::mineProbability(state.probability, right, state.board, state.basic, state.structure);
        return leftProbability != rightProbability ? leftProbability < rightProbability : left < right;
    });
    return Result::Source::Ordinary;
}

int Selection::removeDead(Result &result, Result::Source source, PositionState &state, const ShapeSolverConfig &shapeConfig, Probability::Workspace &probabilityWorkspace) {
    result.observations.resize(0);
    int nodes = 0;
    int count = 0;
    for (int i = 0; i < (int)result.cells.size; ++i) {
        const ObservedBoard::CellId cell = result.cells.data[i];
        Observe::Result observation;
        Observe::observe(state.board, state.basic, state.structure, state.shapes, state.probability, state.distributions, shapeConfig, cell, observation, probabilityWorkspace);
        ++nodes;
        const bool safe = Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) == 0.0L;
        if (source != Result::Source::Ordinary || safe || !isDead(observation)) {
            result.cells.data[count++] = cell;
            result.observations.push_back(observation);
        }
    }
    result.cells.size = count;
    if (result.cells.size != 0 || source != Result::Source::Ordinary)
        return nodes;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col)
            if (state.board.cells[row][col] == ObservedBoard::CellState::Hidden && Probability::mineProbability(state.probability, state.board.id(row, col), state.board, state.basic, state.structure) != 1.0L) {
                const ObservedBoard::CellId cell = state.board.id(row, col);
                Observe::Result observation;
                Observe::observe(state.board, state.basic, state.structure, state.shapes, state.probability, state.distributions, shapeConfig, cell, observation, probabilityWorkspace);
                ++nodes;
                result.cells.push_back(cell);
                result.observations.push_back(observation);
                return nodes;
            }
    return nodes;
}

} // 命名空间 mss
