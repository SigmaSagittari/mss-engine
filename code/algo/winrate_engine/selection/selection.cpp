#include "algo/winrate_engine/selection/selection.h"

#include <algorithm>
#include <cmath>

#include "core/algorithm/neighbors.h"

namespace mss {

struct Selection::Workspace {
    vector<Cell> cells;
    Frontier frontier;
    vector<int> cellComponents, componentSizes;
    vector<ObservedBoard::Delta::Change> changes;
};

__declspec(noinline) Selection::Workspace &Selection::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

bool Selection::offEdgeCandidate(const ObservedBoard::Result &board, ObservedBoard::CellId cell) {
    const auto [row, col] = board.pos(cell);
    const int rows = board.cells.rows(), cols = board.cells.cols();
    int adjacent = 0;
    forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
        adjacent += board.cells[r][c] == ObservedBoard::CellState::Hidden;
    });
    if (adjacent == 2 || adjacent == 3) return true;

    const int offsets[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
    for (const int (&offset)[2] : offsets) {
        const int r = row + offset[0], c = col + offset[1];
        if (r >= 0 && r < rows && c >= 0 && c < cols && ObservedBoard().isNumber(board.cells[r][c])) return true;
    }
    return false;
}

bool Selection::isDead(const Observe::Result &observation) {
    int digits = 0;
    for (int digit = 0; digit <= 8; ++digit) digits += observation.probability[digit] != 0.0L;
    return digits == 1;
}

bool Selection::interchangeable(const Position::Result &position, const Position::ProbabilityResult &probability,
                               const Probability::Pool &probabilities, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs) {
    const ObservedBoard::Result &board = position.board;
    const auto [lr, lc] = board.pos(lhs);
    const auto [rr, rc] = board.pos(rhs);
    if (std::abs(lr - rr) > 1 || std::abs(lc - rc) > 1) return false;
    auto matches = [&](int row, int col, int otherRow, int otherCol, ObservedBoard::CellId other) {
        bool same = true;
        forEachAdjacent(row, col, board.cells.rows(), board.cells.cols(), [&](int r, int c) {
            const ObservedBoard::CellId cell = board.id(r, c);
            if (cell == other || (std::abs(r - otherRow) <= 1 && std::abs(c - otherCol) <= 1)) return;
            same &= Probability().mineProbability(probability.boxes, probabilities, cell, board, position.basic, position.structure) == 1.0L;
        });
        return same;
    };
    return matches(lr, lc, rr, rc, rhs) && matches(rr, rc, lr, lc, lhs);
}

int Selection::removeExchange(Candidate &candidate, span<Cell> following, Range lastRange, Frontier &frontier,
                              Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                              Probability::Pool &probabilities, const Config &config) {
    const long double mine = candidate.observation.probability[9];
    if (mine == 1.0L) return 0;
    auto compare = [&](const Selection::Cell &other) {
        return !other.removed && other.range <= lastRange && other.mine > 0.0L && other.mine < 1.0L &&
               (other.mine > mine ? config.dominance : other.mine == mine && config.equivalence);
    };
    if (mine == 0.0L) {
        for (Selection::Cell &other : following)
            if (compare(other)) other.removed = true;
        return 0;
    }
    if (std::none_of(following.begin(), following.end(), compare)) return 0;

    using Cell = ObservedBoard::CellState;
    if (frontier.knownSafe < 0) {
        frontier.knownSafe = 0;
        position.basic.safeCells([&](ObservedBoard::CellId id) {
            const auto [row, col] = position.board.pos(id);
            frontier.knownSafe += position.board.cells[row][col] != Cell::ForcedSafe;
        });
        frontier.cells.resize(0);
        probability.boxes.frontierCells(probabilities, position.structure, [&](ObservedBoard::CellId id, long double p) {
            frontier.cells.push_back({id, p});
        });
    }

    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::Delta::Change> &changes = workspace.changes;
    changes.resize(0);
    changes.push_back({candidate.cell, Cell::Hidden, Cell::ForcedMine});
    const Position::Delta delta = Position().updateWithDelta(position, changes, positions);
    const vectorPool<long double>::vector tail = probabilities.boxProbabilities.push_back();
    const Position::ProbabilityResult forced = Position().analyzeProbability(position, positions, probabilities);

    auto safeIfClicked = [&](ObservedBoard::CellId id, long double p) {
        const long double forcedMine = Probability().mineProbability(forced.boxes, probabilities, id, position.board, position.basic, position.structure);
        return (p - mine * forcedMine) / (1.0L - mine) <= 1e-10L;
    };
    for (Selection::Cell &other : following) {
        if (!compare(other)) continue;
        const long double forcedMine = Probability().mineProbability(forced.boxes, probabilities, other.cell, position.board, position.basic, position.structure);
        if (forcedMine == 1.0L) other.removed = true;
    }
    candidate.linkedClears = frontier.knownSafe;
    for (int i = 0; i < (int)frontier.cells.size; ++i) {
        const Frontier::Cell &other = frontier.cells[i];
        if (other.id != candidate.cell) candidate.linkedClears += safeIfClicked(other.id, other.mine);
    }
    probabilities.instanceBoxes.pop_back(forced.boxes.instanceBoxes);
    probabilities.boxProbabilities.pop_back(tail);
    Position().applyDelta(position, delta, positions, true);
    positions.popDelta(delta);
    return 1;
}

void Selection::collect(vector<Cell> &cells, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                        const Probability::Pool &probabilities, const Config &config, const vector<ObservedBoard::CellId> &requested,
                        const Grid<long double> *influences, bool oldJava) {
    cells.resize(0);

    const ObservedBoard::Result &board = position.board;
    const Basic::Result &basic = position.basic;
    const Structure::Result &structure = position.structure;
    const bool specified = requested.size != 0;
    const long double offEdgeMine = probability.boxes.tCellProbability;
    auto isOffEdge = [&](ObservedBoard::CellId cell) {
        const auto [row, col] = board.pos(cell);
        return basic.marks[row][col] == Basic::Mark::T;
    };
    const bool useLongTermRisk = config.longTermRisk && influences;
    const long double scale = oldJava ? probability.totalWays : 1.0L;
    long double lowestBoardMine = basic.unknownSum == 0 ? 1.0L : offEdgeMine;
    auto influence = [&](ObservedBoard::CellId cell) {
        const auto [row, col] = board.pos(cell);
        return (*influences)[row][col];
    };
    auto longTermRiskCandidate = [&](ObservedBoard::CellId cell, long double mine) {
        if (!useLongTermRisk) return false;
        const long double compensation = influence(cell);
        const long double cutoff = (1.0L - lowestBoardMine - config.selectionThreshold) * scale;
        return compensation > 0.0L && scale - mine * scale + compensation > cutoff;
    };
    if (specified) {
        for (int i = 0; i < (int)requested.size; ++i) {
            const ObservedBoard::CellId cell = requested[i];
            const long double mine = Probability().mineProbability(probability.boxes, probabilities, cell, board, basic, structure);
            cells.push_back({cell, mine, true, Range::First, false});
        }
    } else {
        basic.safeCells([&](ObservedBoard::CellId cell) { cells.push_back({cell, 0.0L, true, Range::First, false}); });
        if (cells.size != 0) lowestBoardMine = 0.0L;
        probability.boxes.frontierCells(probabilities, structure, [&](ObservedBoard::CellId cell, long double mine) {
            if (mine == 1.0L) return;
            lowestBoardMine = (std::min)(lowestBoardMine, mine);
            cells.push_back({cell, mine, true, Range::First, false});
        });
        if (basic.unknownSum != 0 && offEdgeMine != 1.0L) {
            const long double safetyGap = (1.0L - lowestBoardMine) * 0.95L - (1.0L - offEdgeMine);
            const bool offEdgeAllowed = lowestBoardMine != 0.0L && safetyGap < 0.0L;
            const bool compensateT = useLongTermRisk && (oldJava || (lowestBoardMine != 0.0L && safetyGap >= 0.0L));
            if (offEdgeAllowed || compensateT)
                basic.tCells([&](ObservedBoard::CellId cell) {
                    const bool preferred = basic.unknownSum < 30 || offEdgeCandidate(board, cell);
                    const bool compensated = compensateT && (oldJava ? longTermRiskCandidate(cell, offEdgeMine) : influence(cell) > safetyGap);
                    if ((offEdgeAllowed && preferred) || compensated)
                        cells.push_back({cell, offEdgeMine, preferred, Range::First, false});
                });
            // 全部 T 格都未入围时，沿用格号最小的可点 T 格。
            if (cells.size == 0) cells.push_back({basic.firstT(), offEdgeMine, true, Range::First, false});
        }
    }
    if (cells.size == 0) return;
    if (!specified && config.spaceThreshold > 1) {
        Workspace &workspace = getWorkspace();
        const Position::LogicComponentResult &components = Position().analyzeLogicComponents(position, positions);
        const Structure::Pool::Instances &instances = positions.structure.instances;
        vector<int> &cellComponents = workspace.cellComponents;
        vector<int> &componentSizes = workspace.componentSizes;
        cellComponents.resize((std::size_t)board.cells.rows() * board.cells.cols());
        std::fill_n(cellComponents.data, cellComponents.size, -1); // 不在 LogicComponent 中的格子。
        componentSizes.resize(components.result.components.size);
        for (int i = 0; i < (int)components.result.components.size; ++i) {
            const LogicComponent::Result::Component &component = components.result.components[i];
            // 可点的 T 格都计入块大小，包括前面筛掉的 T 格。
            componentSizes[i] = offEdgeMine == 1.0L ? 0 : component.tCells.count;
            for (Structure::InstanceId id : components.pool.instanceIds.span(component.instanceIds))
                for (const vectorPool<Structure::CellId>::vector &box : instances.boxes.span(instances.list[id].boxes))
                    for (Structure::CellId cell : instances.cells.span(box)) cellComponents[cell] = i;
            for (ObservedBoard::CellId cell : components.pool.tCells.span(component.tCells)) cellComponents[cell] = i;
        }
        for (int i = 0; i < (int)cells.size; ++i) {
            const ObservedBoard::CellId cell = cells[i].cell;
            const int component = cellComponents[cell];
            if (component >= 0 && !isOffEdge(cell)) ++componentSizes[component];
        }
        if (std::any_of(cells.data, cells.data + cells.size, [&](const Selection::Cell &cell) {
            const int component = cellComponents[cell.cell];
            return component >= 0 && componentSizes[component] >= config.spaceThreshold;
        })) {
            const Selection::Cell *end = std::remove_if(cells.data, cells.data + cells.size, [&](const Selection::Cell &cell) {
                const int component = cellComponents[cell.cell];
                return cell.mine != 0.0L && componentSizes[component] < config.spaceThreshold;
            });
            cells.resize(end - cells.data);
        }
    }
    if (!specified) {
        const long double lowestMine = std::min_element(cells.data, cells.data + cells.size, [](const Selection::Cell &a, const Selection::Cell &b) {
            return a.mine < b.mine;
        })->mine;
        const long double secondMineLimit = lowestBoardMine == 0.0L ? 0.0L : lowestBoardMine + config.selectionThreshold2;
        for (int i = 0; i < (int)cells.size; ++i) {
            Selection::Cell &cell = cells[i];
            if (cell.mine - lowestMine <= config.selectionThreshold || isOffEdge(cell.cell) || longTermRiskCandidate(cell.cell, cell.mine)) continue;
            cell.range = cell.mine <= secondMineLimit ? Range::Second : Range::Fallback;
        }
    }
    // 支配剪枝只看排在后面的格，所以启用它时，传入的候选也要按雷率从低到高排。
    if (!specified || config.dominance) {
        std::sort(cells.data, cells.data + cells.size, [](const Selection::Cell &a, const Selection::Cell &b) {
            if (a.range != b.range) return a.range < b.range;
            if (a.mine != b.mine) return a.mine < b.mine;
            if (a.preferred != b.preferred) return a.preferred;
            return a.cell < b.cell;
        });
    }
}

void Selection::select(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                       Probability::Pool &probabilities, const Config &config,
                       const vector<ObservedBoard::CellId> &requested, const Grid<long double> *influences, bool oldJava) {
    Workspace &workspace = getWorkspace();
    vector<Cell> &cells = workspace.cells;
    collect(cells, position, positions, probability, probabilities, config, requested, influences, oldJava);
    result.candidates.resize(0);
    result.nodes = 0;
    Frontier &frontier = workspace.frontier;
    frontier.knownSafe = -1;
    const bool specified = requested.size != 0;
    bool onlyDead = false;
    Range range = Range::First;
    for (int i = 0; i < (int)cells.size; ++i) {
        const Selection::Cell &cell = cells[i];
        if (cell.range != range) {
            if (result.candidates.size >= 2) break;
            range = cell.range;
        }
        if (cell.removed) continue;
        Candidate candidate{cell.cell, {}};
        Position().observe(position, probability, probabilities, cell.cell, candidate.observation);
        ++result.nodes;
        if (!specified && cell.mine != 0.0L && isDead(candidate.observation)) {
            if (result.candidates.size == 0) {
                result.candidates.push_back(candidate);
                onlyDead = true;
            }
            continue;
        }
        // 沿用旧版规则：普通候选全是死格时，留一个可点的格。
        if (onlyDead) {
            result.candidates.resize(0);
            onlyDead = false;
        }
        if (config.interchangeable && std::any_of(result.candidates.data, result.candidates.data + result.candidates.size, [&](const Candidate &other) {
            return interchangeable(position, probability, probabilities, candidate.cell, other.cell);
        })) continue;
        if (range != Range::Fallback && (config.equivalence || config.dominance)) {
            const Range lastRange = result.candidates.size == 0 ? Range::Fallback : range;
            result.nodes += removeExchange(candidate, {cells.data + i + 1, (int)cells.size - i - 1}, lastRange,
                                           frontier, position, positions, probability, probabilities, config);
        }
        result.candidates.push_back(candidate);
        if (range == Range::Fallback) break;
    }
}

} // 命名空间 mss
