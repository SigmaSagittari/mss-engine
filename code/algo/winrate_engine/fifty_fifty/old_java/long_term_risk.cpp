#include "algo/winrate_engine/fifty_fifty/old_java/long_term_risk.h"

#include <algorithm>
#include <cmath>

#include "core/algorithm/neighbors.h"

namespace mss {

struct LongTermRisk::Pattern {
    struct Offset { int row, col; };

    int rows;
    int cols;
    int scanMines;
    int sourceCount;
    Offset sources[6];

    static const Pattern all[3];
};

constexpr LongTermRisk::Pattern LongTermRisk::Pattern::all[3] = {
    {1, 2, 2, 6, {{-1, -1}, {0, -1}, {1, -1}, {-1, 2}, {0, 2}, {1, 2}}},
    {2, 1, 2, 6, {{-1, -1}, {-1, 0}, {-1, 1}, {2, -1}, {2, 0}, {2, 1}}},
    {2, 2, 3, 4, {{-1, -1}, {-1, 2}, {2, -1}, {2, 2}}},
};

struct LongTermRisk::Match {
    // 危险：每次匹配只写入模板本体和有效的封口格，计数时仅读取对应范围。
    ObservedBoard::CellId tiles[4];
    ObservedBoard::CellId missing[6];
    int missingCount;
};

struct LongTermRisk::Workspace {
    vector<ShapeSolver::DistributionId> distributionIds;
    Result description;
};

__declspec(noinline) LongTermRisk::Workspace &LongTermRisk::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

long double LongTermRisk::mineTally(ObservedBoard::CellId cell, const Position::Result &position,
                                   const Position::ProbabilityResult &probability, const Probability::Pool &pool) {
    return Probability().mineProbability(probability.boxes, pool, cell, position.board, position.basic, position.structure) * probability.totalWays;
}

bool LongTermRisk::match(Match &found, const Pattern &pattern, int row, int col, int maxMines, const Position::Result &position,
                         const Position::ProbabilityResult &probability, const Probability::Pool &pool) {
    using Cell = ObservedBoard::CellState;
    const ObservedBoard::Result &board = position.board;
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    int tile = 0;
    for (int r = row; r < row + pattern.rows; ++r) {
        for (int c = col; c < col + pattern.cols; ++c) {
            if (board.cells[r][c] != Cell::Hidden || position.basic.marks[r][c] == Basic::Mark::F)
                return false;
            found.tiles[tile++] = board.id(r, c);
        }
    }

    found.missingCount = 0;
    for (int i = 0; i < pattern.sourceCount; ++i) {
        const int r = row + pattern.sources[i].row;
        const int c = col + pattern.sources[i].col;
        if (r < 0 || r >= rows || c < 0 || c >= cols)
            continue;
        const Cell state = board.cells[r][c];
        if (ObservedBoard().isNumber(state) || state == Cell::ForcedSafe)
            return false;
        found.missing[found.missingCount++] = board.id(r, c);
    }

    int missing = 0;
    for (int i = 0; i < found.missingCount; ++i) {
        const ObservedBoard::CellId cell = found.missing[i];
        if (Probability().mineProbability(probability.boxes, pool, cell, board, position.basic, position.structure) != 1.0L)
            found.missing[missing++] = cell;
    }
    found.missingCount = missing;
    return missing + tile / 2 <= maxMines;
}

bool LongTermRisk::checkChangedConstraints(const Position::Result &position, const Basic::Delta &delta, const Basic::Pool &pool) {
    const ObservedBoard::Result &board = position.board;
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    for (const Basic::Delta::Change &change : pool.changes.span(delta.changes)) {
        if (change.previous == change.next)
            continue;
        const auto [row, col] = board.pos(change.cell);
        bool valid = true;
        forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
            const ObservedBoard::CellState state = board.cells[r][c];
            if (!valid || !ObservedBoard().isNumber(state))
                return;
            int remaining = ObservedBoard().numberValue(state);
            int candidates = 0;
            forEachAdjacent(r, c, rows, cols, [&](int nr, int nc) {
                const Basic::Mark mark = position.basic.marks[nr][nc];
                remaining -= mark == Basic::Mark::F;
                candidates += mark == Basic::Mark::H || mark == Basic::Mark::T;
            });
            valid = remaining >= 0 && remaining <= candidates;
        });
        if (!valid)
            return false;
    }
    return true;
}

long double LongTermRisk::count(const Match &found, int tileCount, Position::Result &position, Position::Pool &positions,
                                Probability::Pool &probabilities) {
    using Cell = ObservedBoard::CellState;
    ObservedBoard::Result &board = position.board;
    ObservedBoard::Delta observed{positions.board.changes.push_back()};
    auto force = [&](ObservedBoard::CellId cell, Cell next) {
        const auto [row, col] = board.pos(cell);
        positions.board.changes.push_back(observed.changes, {cell, board.cells[row][col], next});
    };
    for (int i = 0; i < found.missingCount; ++i)
        force(found.missing[i], Cell::ForcedMine);
    force(found.tiles[0], Cell::ForcedMine);
    if (tileCount == 4)
        force(found.tiles[3], Cell::ForcedMine);
    for (int i = 1; i <= tileCount / 2; ++i)
        force(found.tiles[i], Cell::ForcedSafe);

    ObservedBoard().applyDelta(board, observed, positions.board);
    const Basic::Delta basic = Basic().update(position.basic, board, observed, positions.basic);
    long double ways = 0.0L;
    // 没有剩余候选格的数字也可能冲突，须在 Structure 丢掉该约束前检查。
    if (checkChangedConstraints(position, basic, positions.basic)) {
        const Structure::Delta structure = Structure().update(position.structure, board, position.basic, positions.structure, observed, basic, positions.basic);
        Workspace &workspace = getWorkspace();
        vector<ShapeSolver::DistributionId> &ids = workspace.distributionIds;
        ids.resize(0);
        for (std::size_t i = 0; i < position.structure.instanceIds.size; ++i) {
            const Structure::Instance &instance = positions.structure.instances.list[position.structure.instanceIds[i]];
            ids.push_back(ShapeSolver().analyze(instance, position.config().shapeSolver, probabilities.distributions));
        }
        if (position.basic.unknownSum != 0)
            ids.push_back(ShapeSolver().analyzeFreeCells(position.basic.unknownSum, probabilities.distributions));
        const Probability::Result forced = Probability().analyze(ids, board.totalMines - position.basic.mineSum, probabilities);
        ways = forced.totalWays;
        probabilities.blockProbabilities.pop_back(forced.probabilities);
        probabilities.blocks.pop_back(forced.blocks);
        // 直接撤回本次改动，原盘的 LogicComponent 结果可以继续使用。
        Structure().applyDelta(position.structure, structure, positions.structure, true);
        positions.structure.deltas.added.pop_back(structure.added);
        positions.structure.deltas.removed.pop_back(structure.removed);
    }
    Basic().applyDelta(position.basic, basic, positions.basic, true);
    ObservedBoard().applyDelta(board, observed, positions.board, true);
    positions.basic.changes.pop_back(basic.changes);
    positions.board.changes.pop_back(observed.changes);
    return ways;
}

template <typename Restriction>
void LongTermRisk::scan(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                        Probability::Pool &probabilities, const Config &config,
                        SemiResult *semi, Restriction restriction) {
    const int rows = position.board.cells.rows();
    const int cols = position.board.cells.cols();
    const int minesLeft = position.board.totalMines - position.basic.mineSum;
    result.influences.resize(rows, cols, 0.0L);
    result.enablers.resize(rows * cols);
    std::fill_n(result.enablers.data, result.enablers.size, 0.0L);
    result.tallies.resize(3, rows * cols, -1.0L);
    if (semi) {
        semi->kind = SemiResult::Kind::None;
        semi->cells.resize(0);
    }

    constexpr long double countTolerance = 1e-9L;
    for (int kind = 0; kind < 3; ++kind) {
        const Pattern &pattern = Pattern::all[kind];
        const int tileCount = pattern.rows * pattern.cols;
        const int maxMines = (std::min)(pattern.scanMines, minesLeft);
        for (int row = 0; row + pattern.rows <= rows; ++row) {
            for (int col = 0; col + pattern.cols <= cols; ++col) {
                Match found;
                if (!match(found, pattern, row, col, maxMines, position, probability, probabilities))
                    continue;
                const long double tally = count(found, tileCount, position, positions, probabilities);
                result.tallies[kind][position.board.id(row, col)] = tally;
                if (tally == 0.0L)
                    continue;
                if (semi) {
                    for (int i = 0; i < tileCount; ++i) {
                        const long double mines = mineTally(found.tiles[i], position, probability, probabilities);
                        if (std::abs(tally - mines) <= countTolerance * (std::max)(tally, mines))
                            semi->cells.push_back(found.tiles[i]);
                    }
                    if (semi->cells.size != 0) {
                        const bool forced = (int)semi->cells.size == tileCount &&
                            std::abs(2.0L * tally - probability.totalWays) <= countTolerance * probability.totalWays;
                        semi->kind = forced ? SemiResult::Kind::Forced : SemiResult::Kind::Semi;
                        return;
                    }
                }
                if (tally / probability.totalWays <= config.influenceThreshold)
                    continue;
                for (int i = 0; i < tileCount; ++i) {
                    const auto [r, c] = position.board.pos(found.tiles[i]);
                    result.influences[r][c] += tally;
                }
                for (int i = 0; i < found.missingCount; ++i) {
                    const ObservedBoard::CellId cell = found.missing[i];
                    const auto [r, c] = position.board.pos(cell);
                    result.influences[r][c] += tally;
                    result.enablers[cell] += tally;
                    restriction(cell, tally);
                }
            }
        }
    }
}

void LongTermRisk::update(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                          Probability::Pool &probabilities, const Config &config, SemiResult *semi) {
    scan(result, position, positions, probability, probabilities, config, semi, [](ObservedBoard::CellId, long double) {});
}

template <LongTermRisk::DetailRequest request>
LongTermRisk::InfluenceResult<request> LongTermRisk::evaluate(ObservedBoard::CellId cell, Position::Result &position, Position::Pool &positions,
                                                             const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                                             Result &result) {
    const auto [row, col] = position.board.pos(cell);
    const int rows = position.board.cells.rows();
    const int cols = position.board.cells.cols();
    const int minesLeft = position.board.totalMines - position.basic.mineSum;
    long double value = 0.0L;
    long double body = 0.0L;
    for (int kind = 0; kind < 3; ++kind) {
        const Pattern &pattern = Pattern::all[kind];
        const int maxMines = (std::min)(pattern.scanMines + 2, minesLeft);
        long double best = 0.0L;
        for (int r = (std::max)(0, row - pattern.rows + 1); r <= row && r + pattern.rows <= rows; ++r) {
            for (int c = (std::max)(0, col - pattern.cols + 1); c <= col && c + pattern.cols <= cols; ++c) {
                long double &tally = result.tallies[kind][position.board.id(r, c)];
                if (tally < 0.0L) {
                    Match found;
                    tally = match(found, pattern, r, c, maxMines, position, probability, probabilities)
                        ? count(found, pattern.rows * pattern.cols, position, positions, probabilities) : 0.0L;
                }
                best = (std::max)(best, tally);
            }
        }
        value += best;
        if constexpr (request == DetailRequest::Details) body = (std::max)(body, best);
    }
    const long double mines = mineTally(cell, position, probability, probabilities);
    const long double compensation = (std::min)(value + result.enablers[cell], (std::min)(mines, probability.totalWays - mines));
    if constexpr (request == DetailRequest::Details) return {compensation, body / mines, 0.0L};
    else return {compensation};
}

LongTermRisk::Score LongTermRisk::influence(ObservedBoard::CellId cell, Position::Result &position, Position::Pool &positions,
                                           const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                           Result &result) {
    return evaluate<DetailRequest::Score>(cell, position, positions, probability, probabilities, result);
}

LongTermRisk::Details LongTermRisk::describe(ObservedBoard::CellId cell, Position::Result &position, Position::Pool &positions,
                                             const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                             const Config &config) {
    Workspace &workspace = getWorkspace();
    Result &result = workspace.description;
    long double restriction = 0.0L;
    scan(result, position, positions, probability, probabilities, config, nullptr, [&](ObservedBoard::CellId source, long double tally) {
        if (source == cell) restriction = (std::max)(restriction, 2.0L * tally);
    });
    Details details = evaluate<DetailRequest::Details>(cell, position, positions, probability, probabilities, result);
    details.restriction = restriction / mineTally(cell, position, probability, probabilities);
    return details;
}

} // 命名空间 mss
