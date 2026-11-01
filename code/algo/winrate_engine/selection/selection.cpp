#include "algo/winrate_engine/selection/selection.h"

#include <algorithm>
#include <functional>

#include "core/algorithm/neighbors.h"

namespace mss {

struct Selection::Workspace {
    struct CellPosition {
        int row;
        int col;

        CellPosition(int row, int col) : row(row), col(col) {}
    };

    struct FrontierBox {
        long double mineProbability;
        Structure::InstanceId instance;
        Structure::BoxId box;

        FrontierBox(long double mineProbability, Structure::InstanceId instance, Structure::BoxId box) : mineProbability(mineProbability), instance(instance), box(box) {}
    };

    PositionState::Reveal reveal;
    Grid<unsigned> visited;
    vector<CellPosition> visitedCells;
    vector<CellPosition> stack;
    vector<FrontierBox> boxes;
    // prepare 保存假设前的前沿格及其雷概率，供本次条件查询使用。
    vector<ObservedBoard::CellId> frontierCells;
    vector<long double> frontierProbabilities;
    vector<ObservedBoard::CellId> onEdge;
    vector<ObservedBoard::CellId> offEdgeCandidates;
    vector<ObservedBoard::CellId> offEdge;
    vector<ObservedBoard::CellId> witnesses;
};

__declspec(noinline) Selection::Workspace &Selection::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void Selection::Config::hash(U128Hasher &hasher) const {
    auto mixReal = [&](long double value) { hasher.mix((std::uint64_t)std::hash<long double>{}(value)); };
    mixReal(selectionThreshold1);
    mixReal(selectionThreshold2);
    hasher.mix(spaceThreshold);
    hasher.mix(interchangeable);
    hasher.mix(equivalence);
    hasher.mix(dominance);
    mixReal(progressContribution);
    mixReal(influenceScale);
}

// 对换去重：两格独占的邻位只能是必雷或棋盘外，不依赖 Box。
bool Selection::interchangeable(const PositionState &state, const Probability::Pool &probabilities, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs) {
    const std::pair<int, int> left = state.board.pos(lhs);
    const std::pair<int, int> right = state.board.pos(rhs);
    if (std::abs(left.first - right.first) > 1 || std::abs(left.second - right.second) > 1)
        return false;
    auto matches = [&](int row, int col, int otherRow, int otherCol, ObservedBoard::CellId other) {
        bool same = true;
        forEachAdjacent(row, col, state.board.cells.rows(), state.board.cells.cols(), [&](int neighborRow, int neighborCol) {
            const ObservedBoard::CellId cell = state.board.id(neighborRow, neighborCol);
            if (cell == other || Probability().mineProbability(state.probability, probabilities, cell, state.board, state.basic, state.structure) == 1.0L)
                return;
            if (std::abs(neighborRow - otherRow) > 1 || std::abs(neighborCol - otherCol) > 1)
                same = false;
        });
        return same;
    };
    return matches(left.first, left.second, right.first, right.second, rhs) && matches(right.first, right.second, left.first, left.second, lhs);
}

bool Selection::prepare(Result &result, int index, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, int &linkedClears, int &nodes) {
    Workspace &workspace = getWorkspace();
    const ObservedBoard::CellId cell = result.cells.data[index];
    if (result.source != Result::Source::Specified && config.interchangeable)
        for (int i = 0; i < (int)result.prepared.size; ++i)
            if (interchangeable(state, probabilities, result.prepared.data[i], cell))
                return false;
    const long double mineProbability = result.observations.data[index].probability[9];
    if (mineProbability == 0.0L || mineProbability == 1.0L) {
        result.prepared.push_back(cell);
        linkedClears = 0;
        return true;
    }
    vector<ObservedBoard::CellId> &frontierCells = workspace.frontierCells;
    vector<long double> &frontierProbabilities = workspace.frontierProbabilities;
    frontierCells.resize(0);
    frontierProbabilities.resize(0);
    // 只统计原前沿和已知安全格；显式保存格子与概率，供假设后的条件查询使用。
    linkedClears = state.board.totalMines + result.remainingSafe - state.basic.mineSum - state.basic.unknownSum - result.forcedSafes;
    state.probability.frontierCells(probabilities, state.structure, [&](ObservedBoard::CellId other, long double probability) {
        frontierCells.push_back(other);
        frontierProbabilities.push_back(probability);
        --linkedClears;
    });
    Probability::Pool &conditionalPool = state.probabilityPool;
    state.applyReveal(cell, ObservedBoard::CellState::ForcedMine, shapeConfig, workspace.reveal, conditionalPool);
    ++nodes;
    auto conditionalMine = [&](ObservedBoard::CellId other) {
        return Probability().mineProbability(workspace.reveal.probability, conditionalPool, other, state.board, state.basic, state.structure);
    };
    // P(X雷|B安) = (P(X雷) - P(B雷)P(X雷|B雷)) / P(B安)。
    // 沿用概率查询的 1e-10 阈值，镜像用于归一化后的近零条件概率。
    auto conditionalSafe = [&](long double original, long double forced) { return (original - mineProbability * forced) / (1.0L - mineProbability) <= 1e-10L; };
    for (int i = 0; i < (int)frontierCells.size; ++i) {
        const ObservedBoard::CellId other = frontierCells.data[i];
        if (other != cell && conditionalSafe(frontierProbabilities.data[i], conditionalMine(other))) ++linkedClears;
    }
    bool dominated = false;
    int count = index + 1;
    // B雷推出A雷，即A安推出B安；优先代表直接删去后面的等价或受支配候选。
    for (int i = 0; i < (int)result.cells.size; ++i) {
        const long double other = result.observations.data[i].probability[9];
        if (result.source != Result::Source::Specified && i != index) {
            const long double forced = conditionalMine(result.cells.data[i]);
            if ((other < mineProbability ? config.dominance : config.equivalence && other == mineProbability && i < index) && conditionalSafe(other, forced)) dominated = true;
            if (i > index && other < 1.0L && (other > mineProbability ? config.dominance : config.equivalence && other == mineProbability) && forced >= 1.0L - 1e-10L) continue;
        }
        if (i > index) {
            result.cells.data[count] = result.cells.data[i];
            result.observations.data[count++] = result.observations.data[i];
        }
    }
    result.cells.size = result.observations.size = count;
    state.undoReveal(workspace.reveal, conditionalPool);
    if (dominated) return false;
    result.prepared.push_back(cell);
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

bool Selection::meetsSpaceThreshold(int startRow, int startCol, const PositionState &state, const Probability::Pool &probabilities, int threshold) {
    Workspace &workspace = getWorkspace();
    Grid<unsigned> &visited = workspace.visited;
    vector<Workspace::CellPosition> &visitedCells = workspace.visitedCells;
    vector<Workspace::CellPosition> &stack = workspace.stack;
    auto available = [&](int row, int col) {
        return state.board.cells[row][col] == ObservedBoard::CellState::Hidden && Probability().mineProbability(state.probability, probabilities, state.board.id(row, col), state.board, state.basic, state.structure) != 1.0L;
    };
    if (visited.rows() != state.board.cells.rows() || visited.cols() != state.board.cells.cols())
        visited.resize(state.board.cells.rows(), state.board.cells.cols(), 0);
    visitedCells.resize(0);
    stack.resize(0);
    stack.push_back(Workspace::CellPosition(startRow, startCol));
    visited[startRow][startCol] = 1;
    visitedCells.push_back(Workspace::CellPosition(startRow, startCol));
    int count = 0;
    while (stack.size != 0) {
        const Workspace::CellPosition position = stack.data[stack.size - 1];
        stack.size -= 1;
        ++count;
        if (count >= threshold)
            break;
        forEachAdjacent(position.row, position.col, state.board.cells.rows(), state.board.cells.cols(), [&](int row, int col) {
            if (visited[row][col])
                return;
            if (available(row, col)) {
                visited[row][col] = 1;
                visitedCells.push_back(Workspace::CellPosition(row, col));
                stack.push_back(Workspace::CellPosition(row, col));
                return;
            }
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) {
                visited[row][col] = 1;
                visitedCells.push_back(Workspace::CellPosition(row, col));
                forEachAdjacent(row, col, state.board.cells.rows(), state.board.cells.cols(), [&](int nextRow, int nextCol) {
                    if (!visited[nextRow][nextCol] && available(nextRow, nextCol)) {
                        visited[nextRow][nextCol] = 1;
                        visitedCells.push_back(Workspace::CellPosition(nextRow, nextCol));
                        stack.push_back(Workspace::CellPosition(nextRow, nextCol));
                    }
                });
            }
        });
    }
    // 达到阈值时仍有已标记的待访问格；返回前一起恢复为零。
    for (int i = 0; i < (int)visitedCells.size; ++i) {
        const Workspace::CellPosition position = visitedCells.data[i];
        visited[position.row][position.col] = 0;
    }
    visitedCells.resize(0);
    stack.resize(0);
    return count >= threshold;
}

void Selection::selectOrdinary(vector<ObservedBoard::CellId> &result, const PositionState &state, const Probability::Pool &probabilities, const LongTermRisk::Result *risk, span<const ObservedBoard::CellId> offEdge, span<const ObservedBoard::CellId> witnesses, ObservedBoard::CellId firstOffEdge, const Config &config) {
    Workspace &workspace = getWorkspace();
    workspace.boxes.resize(0);
    for (int component = 0; component < (int)state.structure.components.size; ++component) {
        const Structure::Instance &instance = state.shapes.instances.list.data[state.structure.components.data[component]];
        const Probability::Result::Component &layout = probabilities.components.data.data[state.probability.components.offset + component];
        const int boxCount = (int)instance.boxOf.count - 1;
        for (int box = 0; box < boxCount; ++box)
            workspace.boxes.push_back(Workspace::FrontierBox(probabilities.boxProbabilities.data.data[state.probability.boxProbabilities.offset + layout.boxOffset + box], state.structure.components.data[component], box));
    }
    std::sort(workspace.boxes.data, workspace.boxes.data + workspace.boxes.size, [](const Workspace::FrontierBox &left, const Workspace::FrontierBox &right) {
        return left.mineProbability < right.mineProbability;
    });
    auto cellsOf = [&](const Workspace::FrontierBox &box) {
        const Structure::Instance &instance = state.shapes.instances.list.data[box.instance];
        const int *offsets = state.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        const ObservedBoard::CellId *cells = state.shapes.instances.cells.data.data + instance.cells.offset;
        return span<const ObservedBoard::CellId>(cells + offsets[box.box], offsets[box.box + 1] - offsets[box.box]);
    };
    long double bestSafety = state.basic.unknownSum != 0 ? 1.0L - state.probability.tCellProbability : 0.0L;
    if (workspace.boxes.size != 0)
        bestSafety = (std::max)(bestSafety, (1.0L - workspace.boxes.data[0].mineProbability));
    long double test = bestSafety == 1.0L ? 1.0L : bestSafety - config.selectionThreshold1;
    long double test2 = bestSafety == 1.0L ? 1.0L : bestSafety - config.selectionThreshold2;

    workspace.onEdge.resize(0);
    for (int i = 0; i < (int)workspace.boxes.size; ++i) {
        const Workspace::FrontierBox &box = workspace.boxes.data[i];
        if ((1.0L - box.mineProbability) < test && ((int)workspace.onEdge.size >= 2 || (1.0L - box.mineProbability) < test2))
            break;
        test = (std::min)(test, (1.0L - box.mineProbability));
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
                const long double mineTally = Probability().mineProbability(state.probability, probabilities, cell, state.board, state.basic, state.structure) * state.probability.candidates;
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
            const bool large = meetsSpaceThreshold(position.first, position.second, state, probabilities, config.spaceThreshold);
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
                if (safest == -1 || (1.0L - workspace.boxes.data[i].mineProbability) > safestSafety || ((1.0L - workspace.boxes.data[i].mineProbability) == safestSafety && cells.data[cell] < safest)) {
                    safestSafety = (1.0L - workspace.boxes.data[i].mineProbability);
                    safest = cells.data[cell];
                }
        }
        if (safest != -1)
            result.push_back(safest);
    }
}

Selection::Result::Source Selection::find(PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, bool useRisk, const vector<ObservedBoard::CellId> *cells) {
    Workspace &workspace = getWorkspace();
    result.cells.resize(0);
    result.observations.resize(0);
    result.prepared.resize(0);
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
                if (!useRisk && Probability().mineProbability(state.probability, probabilities, cell, state.board, state.basic, state.structure) == 0.0L) {
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
    FiftyFifty().detect(semi, state, probabilities, fiftyConfig, fiftyPool);
    if (semi.kind == Semi::Kind::None && useRisk && fiftyConfig.algorithm == FiftyFifty::Algorithm::OldJava)
        FiftyFifty().prepareRisk(result.fiftyFifty, state, probabilities, shapeConfig, fiftyConfig, fiftyPool, true);
    result.cells.swap(semi.cells);
    if (semi.kind != Semi::Kind::None)
        return semi.kind == Semi::Kind::Forced ? Result::Source::Forced : Result::Source::Semi;
    selectOrdinary(result.cells, state, probabilities, useRisk && fiftyConfig.algorithm == FiftyFifty::Algorithm::OldJava ? &result.fiftyFifty.oldJava : nullptr, span<const ObservedBoard::CellId>(workspace.offEdge.data, (int)workspace.offEdge.size), span<const ObservedBoard::CellId>(workspace.witnesses.data, (int)workspace.witnesses.size), firstOffEdge, config);
    std::sort(result.cells.data, result.cells.data + result.cells.size, [&](ObservedBoard::CellId left, ObservedBoard::CellId right) {
        const long double leftProbability = Probability().mineProbability(state.probability, probabilities, left, state.board, state.basic, state.structure);
        const long double rightProbability = Probability().mineProbability(state.probability, probabilities, right, state.board, state.basic, state.structure);
        return leftProbability != rightProbability ? leftProbability < rightProbability : left < right;
    });
    return Result::Source::Ordinary;
}

int Selection::begin(PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Result &result, Mode mode, const vector<ObservedBoard::CellId> *cells) {
    const bool root = mode != Mode::Inner;
    result.mode = mode;
    result.next = 0;
    result.source = find(state, probabilities, shapeConfig, config, fiftyConfig, fiftyPool, result, root, cells);
    const bool semi = result.source == Result::Source::Forced || result.source == Result::Source::Semi;
    result.direct = mode == Mode::JavaEval && result.source == Result::Source::Forced && result.cells.size < 4;
    const int nodes = result.direct ? 0 : removeDead(result, result.source, state, probabilities, shapeConfig);
    if (root && !semi && result.cells.size != 0 && (fiftyConfig.algorithm == FiftyFifty::Algorithm::Templates || result.source == Result::Source::Specified))
        FiftyFifty().prepareRisk(result.fiftyFifty, state, probabilities, shapeConfig, fiftyConfig, fiftyPool, false);
    result.remainingSafe = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    result.forcedSafes = 0;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col) {
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) --result.remainingSafe;
            else if (root && state.board.cells[row][col] == ObservedBoard::CellState::ForcedSafe) ++result.forcedSafes;
        }
    return nodes;
}

Selection::Evaluation Selection::next(Result &result, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, JavaLite::Pool *pool, long double floor, SuggestResult::Details *details) {
    const int index = result.next++;
    const ObservedBoard::CellId cell = result.cells.data[index];
    if (result.mode == Mode::JavaEval && (result.direct || result.observations.data[index].probability[9] == 0.0L))
        return Evaluation(cell, JavaLite::Result(1.0L, 1.0L, {}, 0, true), 0.0L, 0);
    const Observe::Result &observation = result.observations.data[index];
    const bool safe = observation.probability[9] == 0.0L;
    if (result.mode == Mode::Inner && !safe && floor >= 0.0L) {
        long double safety = 0.0L;
        for (int digit = 0; digit <= 8; ++digit) safety += observation.probability[digit];
        if (safety * (1.0L + safety * config.progressContribution) < floor)
            return Evaluation(cell, JavaLite::Result(0.0L, 1.0L, {}, 0, false), 0.0L, 0);
    }
    int linkedClears;
    int preparationNodes = 0;
    if (!prepare(result, index, state, probabilities, shapeConfig, config, linkedClears, preparationNodes))
        return Evaluation(cell, JavaLite::Result(0.0L, 1.0L, {}, 0, false), 0.0L, preparationNodes);
    const bool risk = result.mode != Mode::Inner && !safe && result.source != Result::Source::Forced && result.source != Result::Source::Semi;
    const long double influence = risk && observation.probability[9] != 1.0L ? FiftyFifty().influence(cell, state, probabilities, result.fiftyFifty, shapeConfig, fiftyConfig) : 0.0L;
    const JavaLite::Result javaLite = JavaLite().evaluate(state, shapeConfig, config.progressContribution, pool, cell, observation, result.remainingSafe, linkedClears, safe, result.mode == Mode::Inner, floor, details);
    long double compensation = risk ? config.influenceScale * influence : 0.0L;
    if (risk && fiftyConfig.algorithm == FiftyFifty::Algorithm::OldJava && observation.probability[9] != 1.0L) compensation *= javaLite.value * javaLite.progressMultiplier / (1.0L - observation.probability[9]);
    return Evaluation(cell, javaLite, compensation, preparationNodes);
}

int Selection::removeDead(Result &result, Result::Source source, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig) {
    result.observations.resize(0);
    int nodes = 0;
    int count = 0;
    for (int i = 0; i < (int)result.cells.size; ++i) {
        const ObservedBoard::CellId cell = result.cells.data[i];
        Observe::Result observation;
        Observe().observe(state.board, state.basic, state.structure, state.probability, probabilities, shapeConfig, cell, observation);
        ++nodes;
        const bool safe = Probability().mineProbability(state.probability, probabilities, cell, state.board, state.basic, state.structure) == 0.0L;
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
            if (state.board.cells[row][col] == ObservedBoard::CellState::Hidden && Probability().mineProbability(state.probability, probabilities, state.board.id(row, col), state.board, state.basic, state.structure) != 1.0L) {
                const ObservedBoard::CellId cell = state.board.id(row, col);
                Observe::Result observation;
                Observe().observe(state.board, state.basic, state.structure, state.probability, probabilities, shapeConfig, cell, observation);
                ++nodes;
                result.cells.push_back(cell);
                result.observations.push_back(observation);
                return nodes;
            }
    return nodes;
}

} // 命名空间 mss
