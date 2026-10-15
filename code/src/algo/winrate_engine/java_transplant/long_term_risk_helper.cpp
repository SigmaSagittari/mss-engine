#include "long_term_risk_helper.h"

#include <algorithm>
#include <cmath>

#include "core/utility/neighbors.h"

namespace mss {

void LongTermRisk::resetCache(Workspace &workspace, int size) {
    for (int i = 0; i < 3; ++i) {
        workspace.values[i].resize(size);
        workspace.ready[i].assign(size, 0);
    }
}

LongTermRisk::CellList::CellList(const CellList &other) : count(other.count) {
    for (int i = 0; i < count; ++i) cells[i] = other.cells[i];
}

LongTermRisk::CellList &LongTermRisk::CellList::operator=(const CellList &other) {
    count = other.count;
    for (int i = 0; i < count; ++i) cells[i] = other.cells[i];
    return *this;
}

void LongTermRisk::CellList::push_back(winrate::CellId cell) { cells[count++] = cell; }
const winrate::CellId *LongTermRisk::CellList::begin() const { return cells.data(); }
const winrate::CellId *LongTermRisk::CellList::end() const { return cells.data() + count; }
winrate::Cells LongTermRisk::CellList::span() const { return {cells.data(), end()}; }

bool LongTermRisk::contains(winrate::Cells cells, winrate::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

// Java isHidden：未确定雷且未揭示（Frontier 也算 hidden）。
bool LongTermRisk::hidden(const winrate::Board &board, const winrate::Basic &basic, int x, int y) {
    return x >= 0 && x < board.board.rows() && y >= 0 && y < board.board.cols() && board.board[x][y] == winrate::CellState::Hidden && basic.marks[x][y] != Basic::Mark::F;
}

// Java getMissingMines：任一源格已揭示则整个结构作废（hasRevealedSource 已先行
// 拦截）；余下非确定雷的源格为 missing。
LongTermRisk::CellList LongTermRisk::missingMines(const winrate::View &input, std::span<const std::pair<int, int>> source, long double eqEps) {
    const auto &[board, basic, structure, probability, shapes] = input;
    CellList missing;
    for (const std::pair<int, int> &position : source) {
        const auto [x, y] = position;
        if (x < 0 || x >= board.board.rows() || y < 0 || y >= board.board.cols())
            continue; // 越界 = Java null → 跳过
        const winrate::CellId cell = board.id(x, y);
        // 确定雷（basic Mine 或 PE 新雷）→ 归 present，不进 missing。
        if (probability.mineProbability(cell, board, basic, structure) >= 1.0L - eqEps)
            continue;
        missing.push_back(cell);
    }
    return missing;
}

bool LongTermRisk::hasRevealedSource(const winrate::Board &board, std::span<const std::pair<int, int>> source) {
    for (const std::pair<int, int> &position : source) {
        const auto [x, y] = position;
        if (x >= 0 && x < board.board.rows() && y >= 0 && y < board.board.cols() && board.board[x][y] <= winrate::CellState::Num8)
            return true;
    }
    return false;
}

// 单格雷迹 tally（Java Box.getTally() / offEdgeTally）。
long double LongTermRisk::boxTally(winrate::CellId cell, const winrate::View &input) {
    const auto &[board, basic, structure, probability, shapes] = input;
    return probability.mineProbability(cell, board, basic, structure) * probability.candidates();
}

// Java getHorizontal/getVertical/getBoxInfluence 的公共计数：强制结构雷/安全后
// 数全盘解数。mineCount = 1（2-tile）/ 2（2x2）；maxMissing 是"missing+n ≤ maxMissing"
// 的阈值（Java 直接传 maxMissingMines）。

std::vector<winrate::CellId> LongTermRisk::influencedTiles(const Result &risk, long double threshold, const winrate::View &input, winrate::Cells dead) {
    const auto &[board, basic, structure, probability, shapes] = input;
    std::vector<winrate::CellId> result;
    const long double cutoffTally = threshold * probability.candidates();
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y) {
            const winrate::CellId cell = board.id(x, y);
            const long double influence = risk.influences[x][y];
            if (influence == 0.0L || contains(dead, cell))
                continue;
            const long double mineTally = probability.mineProbability(cell, board, basic, structure) * probability.candidates();
            const long double safetyTally = probability.candidates() - mineTally + influence;
            if (safetyTally > cutoffTally)
                result.push_back(cell);
        }
    return result;
}

long double LongTermRisk::candidateTally(const winrate::Input &input, winrate::Cells tiles, std::span<const std::pair<int, int>> source, int mineCount, int maxMissing, int minesLeft, long double eqEps, TallyKind kind, int index, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    if (hasRevealedSource(board, source))
        return 0.0L;
    const CellList missing = missingMines(input, source, eqEps);
    const int extra = mineCount == 1 ? 1 : 2;
    if (missing.count + extra > maxMissing)
        return 0.0L;
    if (missing.count + extra > minesLeft)
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
    return cachedTally(workspace, kind, index, [&] { return countWithForces(input, mines.span(), safes.span(), workspace); });
}

long double LongTermRisk::countWithForces(const winrate::Input &input, winrate::Cells mines, winrate::Cells safes, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    ObservedBoard::Delta &updates = workspace.boardDelta;
    updates.clear();
    workspace.changedCells.clear();
    for (winrate::CellId cell : mines) {
        const auto [row, col] = board.pos(cell);
        updates.changes.emplace_back(cell, board.board[row][col], winrate::CellState::ForcedMine);
        workspace.changedCells.push_back(cell);
    }
    for (winrate::CellId cell : safes) {
        const auto [row, col] = board.pos(cell);
        updates.changes.emplace_back(cell, board.board[row][col], winrate::CellState::ForcedSafe);
        workspace.changedCells.push_back(cell);
    }
    ObservedBoard::applyDelta(board, updates.changes);
    Basic::update(basic, workspace.basicDelta, board, updates, workspace.basic);
    // 根局面有效，只重算本次标记变化所影响的数字约束；最后一个 H 被移走的矛盾数字会从 Structure 消失，不能漏掉。
    bool valid = true;
    for (const Basic::Delta::Change &change : workspace.basicDelta.changes) {
        const auto [row, col] = board.pos(change.cell);
        forEachAdjacent(row, col, board.board.rows(), board.board.cols(), [&](int numberRow, int numberCol) {
            const winrate::CellState state = board.board[numberRow][numberCol];
            if (!valid || state > winrate::CellState::Num8)
                return;
            int remaining = (int)state;
            int hidden = 0;
            forEachAdjacent(numberRow, numberCol, board.board.rows(), board.board.cols(), [&](int neighborRow, int neighborCol) {
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
    // 标记和邻域缓存由 Basic::update 维护；全局剩余雷数由 Probability 计数，不在这里重复审计。
    long double candidates = 0.0L;
    if (valid) {
        Structure::update(structure, workspace.structureDelta, board, basic, shapes, workspace.changedCells, workspace.structure);
        if (!workspace.forcedProbability) workspace.forcedProbability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability);
        else Probability::analyze(board, basic, structure, shapes, distributions, *workspace.forcedProbability, workspace.probability);
        candidates = workspace.forcedProbability->candidates();
        Structure::applyDelta(structure, shapes, workspace.structureDelta.removed, workspace.structureDelta.added, true);
    }
    Basic::applyDelta(basic, workspace.basicDelta.changes, true);
    ObservedBoard::applyDelta(board, updates.changes, true);
    return candidates;
}

void LongTermRisk::update(Result &out, const winrate::Input &input, winrate::Cells dead, const Config &cfg, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    const int size = board.board.rows() * board.board.cols();
    out.influences.resize(board.board.rows(), board.board.cols(), 0.0L);
    out.enablers.assign(size, 0.0L);
    out.pseudos.clear();
    out.pseudoKind = Pseudo::Kind::None;
    const int minesLeft = board.totalMines - basic.mineSum;

    // 找到 pseudo 即提前终止（Java influence 在两次扫描间提前 return）。
    bool stop = false;

    // 一个候选结构的完整处理：Java getHorizontal/getVertical/getBoxInfluence +
    // checkForPseudo + addInfluence 的合并。
    auto consider = [&](winrate::Cells tiles, std::span<const std::pair<int, int>> source, int mineCount, int maxMissing) {
        if (stop)
            return;
        if (hasRevealedSource(board, source))
            return;
        const CellList missing = missingMines(input, source, cfg.eqEps);
        const int extra = mineCount == 1 ? 1 : 2;
        if (missing.count + extra > maxMissing)
            return;
        if (missing.count + extra > minesLeft)
            return;

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
        const long double tally = countWithForces(input, mines.span(), safes.span(), workspace);
        if (tally == 0.0L)
            return;
        const long double ratio = tally / probability.candidates();

        // checkForPseudo：盒雷迹被结构影响度完全覆盖且非死格。
        for (winrate::CellId tile : tiles) {
            const long double mineTally = boxTally(tile, input);
            if (std::abs(tally - mineTally) <= cfg.eqEps * (std::max)(tally, mineTally) && !contains(dead, tile))
                out.pseudos.push_back(tile);
        }
        if (!out.pseudos.empty()) {
            // 两个互斥雷位覆盖全部布局时才称强制 50/50；其余保留 50/50 或安全的语义。
            out.pseudoKind = out.pseudos.size() == tiles.size() && std::abs(2.0L * tally - probability.candidates()) <= cfg.eqEps * probability.candidates() ? Pseudo::Kind::Forced : Pseudo::Kind::OrSafe;
            stop = true;
            return;
        }

        // addInfluence（FINAL_THRESHOLD 之上才记账）。
        if (ratio > cfg.influenceThreshold) {
            for (winrate::CellId tile : tiles) {
                const auto [row, col] = board.pos(tile);
                out.influences[row][col] += tally;
            }
            for (winrate::CellId enabler : missing) {
                const auto [row, col] = board.pos(enabler);
                out.influences[row][col] += tally;
                out.enablers[enabler] += tally;
            }
        }
    };

    // 横 2-tile（Java getHorizontal：同行 (x,y)-(x,y+1)；源格 = 对左右两列
    // (y-1, y+2)、三行 (x-1..x+1)）。
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y + 1 < board.board.cols(); ++y)
            if (hidden(board, basic, x, y) && hidden(board, basic, x, y + 1)) {
                const std::array cells{board.id(x, y), board.id(x, y + 1)};
                const std::array source{std::pair{x - 1, y - 1}, std::pair{x, y - 1}, std::pair{x + 1, y - 1}, std::pair{x - 1, y + 2}, std::pair{x, y + 2}, std::pair{x + 1, y + 2}};
                consider(cells, source, 1, 2);
                if (stop)
                    return;
            }
    // 竖 2-tile（Java getVertical：同列 (x,y)-(x+1,y)；源格 = 对上下两行
    // (x-1, x+2)、三列 (y-1..y+1)）。
    for (int x = 0; x + 1 < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y)
            if (hidden(board, basic, x, y) && hidden(board, basic, x + 1, y)) {
                const std::array cells{board.id(x, y), board.id(x + 1, y)};
                const std::array source{std::pair{x - 1, y - 1}, std::pair{x - 1, y}, std::pair{x - 1, y + 1}, std::pair{x + 2, y - 1}, std::pair{x + 2, y}, std::pair{x + 2, y + 1}};
                consider(cells, source, 1, 2);
                if (stop)
                    return;
            }
    // 2x2 块（Java getBoxInfluence：4 个对角源格）。
    for (int x = 0; x + 1 < board.board.rows(); ++x)
        for (int y = 0; y + 1 < board.board.cols(); ++y)
            if (hidden(board, basic, x, y) && hidden(board, basic, x + 1, y) && hidden(board, basic, x, y + 1) && hidden(board, basic, x + 1, y + 1)) {
                const std::array cells{board.id(x, y), board.id(x, y + 1), board.id(x + 1, y), board.id(x + 1, y + 1)};
                const std::array source{std::pair{x - 1, y - 1}, std::pair{x - 1, y + 2}, std::pair{x + 2, y - 1}, std::pair{x + 2, y + 2}};
                consider(cells, source, 2, 3);
                if (stop)
                    return;
            }
    return;
}

long double LongTermRisk::influence(winrate::CellId cell, const winrate::Input &input, const Result &full, const Config &cfg, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    const int minesLeft = board.totalMines - basic.mineSum;
    const auto [x, y] = board.pos(cell);

    // 横：含本格的两对（Java getHorizontal(tile) 与 getHorizontal(x-1, y)）；
    // 源格 = 对左右两列 (col-1, col+2)、三行 (row-1..row+1)。
    auto horizontal = [&](int row, int col) {
        if (row < 0 || row >= board.board.rows() || col < 0 || col + 1 >= board.board.cols())
            return 0.0L;
        if (!hidden(board, basic, row, col) || !hidden(board, basic, row, col + 1))
            return 0.0L;
        const std::array tiles{board.id(row, col), board.id(row, col + 1)};
        const std::array source{std::pair{row - 1, col - 1}, std::pair{row, col - 1}, std::pair{row + 1, col - 1}, std::pair{row - 1, col + 2}, std::pair{row, col + 2}, std::pair{row + 1, col + 2}};
        return candidateTally(input, tiles, source, 1, 4, minesLeft, cfg.eqEps, TallyKind::Horizontal, board.id(row, col), workspace);
    };
    // 竖：含本格的两对（Java getVertical(tile) 与 getVertical(x, y-1)）；
    // 源格 = 对上下两行 (row-1, row+2)、三列 (col-1..col+1)。
    auto vertical = [&](int row, int col) {
        if (row < 0 || row + 1 >= board.board.rows() || col < 0 || col >= board.board.cols())
            return 0.0L;
        if (!hidden(board, basic, row, col) || !hidden(board, basic, row + 1, col))
            return 0.0L;
        const std::array tiles{board.id(row, col), board.id(row + 1, col)};
        const std::array source{std::pair{row - 1, col - 1}, std::pair{row - 1, col}, std::pair{row - 1, col + 1}, std::pair{row + 2, col - 1}, std::pair{row + 2, col}, std::pair{row + 2, col + 1}};
        return candidateTally(input, tiles, source, 1, 4, minesLeft, cfg.eqEps, TallyKind::Vertical, board.id(row, col), workspace);
    };
    // 2x2：含本格的 4 个角。
    auto box = [&](int row, int col) {
        if (row < 0 || row + 1 >= board.board.rows() || col < 0 || col + 1 >= board.board.cols())
            return 0.0L;
        if (!hidden(board, basic, row, col) || !hidden(board, basic, row + 1, col) || !hidden(board, basic, row, col + 1) || !hidden(board, basic, row + 1, col + 1))
            return 0.0L;
        const std::array tiles{board.id(row, col), board.id(row, col + 1), board.id(row + 1, col), board.id(row + 1, col + 1)};
        const std::array source{std::pair{row - 1, col - 1}, std::pair{row - 1, col + 2}, std::pair{row + 2, col - 1}, std::pair{row + 2, col + 2}};
        return candidateTally(input, tiles, source, 2, 5, minesLeft, cfg.eqEps, TallyKind::Box, board.id(row, col), workspace);
    };

    long double influence = 0.0L;
    influence += (std::max)(horizontal(x, y), horizontal(x, y - 1));
    influence += (std::max)(vertical(x, y), vertical(x - 1, y));
    influence += (std::max)({(box(x, y)), box(x - 1, y), box(x, y - 1), box(x - 1, y - 1)});
    // enabler 贡献：点击 enabler 同样消除 50/50 风险（Java influence(tile) 加
    // influenceEnablers）。full 与当前根盘面一致。
    influence += full.enablers[cell];

    // 钳制：50/50 影响不可能超过 P(雷) 或 P(安全) 对应的 tally。
    long double maxInfluence = boxTally(cell, input);
    const long double other = probability.candidates() - maxInfluence;
    maxInfluence = (std::min)(maxInfluence, other);
    return (std::min)(influence, maxInfluence);
}

LongTermRisk::Result LongTermRisk::analyze(const winrate::Input &input, winrate::Cells dead, const Config &cfg, Workspace &workspace) {
    Result result;
    update(result, input, dead, cfg, workspace);
    return result;
}

} // 命名空间 mss
