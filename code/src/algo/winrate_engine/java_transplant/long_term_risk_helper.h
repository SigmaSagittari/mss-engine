#pragma once

#include <array>
#include <span>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"

#include "core/config.h"

namespace mss {

// ─────────────────────────────────────────────────────────────
// algo/winrate_engine/java_transplant/long_term_risk_helper.h — Java LongTermRiskHelper 的忠实移植。
//
// 全盘扫描"候选 50/50 结构"（2-tile 横/竖对、2x2 块），对每个结构用
// countWithForces（把一组格强制为雷、另一组强制为安全后数全盘解数）求
// 影响度 tally；输出：
//   tiles/enablers  每格被 50/50 覆盖的雷迹（tally 单位 = 方案数）
//   pseudos         雷迹完全被 50/50 结构覆盖的格（不可避免的 50/50）
//   hotspots        单缺雷且 50/50 的"热点"格（ltrSafety + 豁免集）
//   possible        概率 > FINAL_THRESHOLD 的可能 50/50（exemptions）
//
// 约束：
//   - 只读算法层接口，不改 basic/structure/probability；countWithForces
//     临时复制盘面 + 强制事实后全量重建，天然无副作用。
//   - 数值口径与 Java 一致：tally 用"方案数"单位（long double），阈值
//     （0.025 / 0.9 / 0.6）与魔法数原样保留；等值比较用相对容差。
// ─────────────────────────────────────────────────────────────

struct LongTermRiskReference {

    // 热点：点击后会把局面逼进 50/50 的格。
    struct RiskHotspot {
        ObservedBoard::CellId cell = -1;               // 热点格
        long double safety = 0;         // ltrSafety = 0.5 + 0.5×P(enabler 安全)
        std::vector<ObservedBoard::CellId> exemptions; // 参与该 50/50 的格（豁免：点了它们不惩罚）

        bool isExempt(ObservedBoard::CellId candidate) const;
    };

    // 可能 50/50：概率 > FINAL_THRESHOLD 的候选结构。
    struct Possible5050 {
        long double probability = 0;    // 归一化概率（tally / candidates）
        std::vector<ObservedBoard::CellId> exemptions; // enablers ∪ 结构格

        bool isExempt(ObservedBoard::CellId candidate) const;
    };

    // 一次全盘扫描的产物。tiles/enablers 按 ObservedBoard::CellId 索引，单位 = 方案数。
    struct Influence {
        std::vector<long double> tiles;    // 50/50 结构格的影响度
        std::vector<long double> enablers; // enabler（缺雷源格）的影响度
        std::vector<ObservedBoard::CellId> pseudos;       // 不可避免的 50/50 格
        std::vector<RiskHotspot> hotspots;
        std::vector<Possible5050> possible;

        // Java getInfluencedTiles(threshold)：返回"安全额 = 方案数 − 雷迹 +
        // 影响度超过 方案数×threshold、且非死格、且影响度 ≠ 0"的格。
        std::vector<ObservedBoard::CellId> influencedTiles(long double threshold, const ObservedBoard::Result &board, const Basic::Result &basic,
                                            const Structure::Result &structure, const Probability::Result &probability,
                                            std::span<const ObservedBoard::CellId> dead) const;
    };

    struct Workspace {
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;

        struct Force {
            Probability::Result probability;
            ObservedBoard::Delta boardDelta;
        } force{};

        struct TallyCache {
            std::array<std::vector<long double>, 6> values;
            std::array<std::vector<unsigned char>, 6> ready;
            const void *owner = nullptr;
            const ObservedBoard::Result *board = nullptr;
            const Basic::Result *basic = nullptr;
            const Structure::Result *structure = nullptr;
            const Probability::Result *probability = nullptr;
            const Structure::Pool *shapes = nullptr;
            const ShapeSolver::DistributionPool *distributions = nullptr;

            void reset(int rows, int cols) {
                const int size = rows * cols;
                for (int i = 0; i < 6; ++i) {
                    values[i].resize(size);
                    ready[i].assign(size, 0);
                }
                owner = nullptr;
            }

            bool reusable(const ObservedBoard::Result &board_, const Basic::Result &basic_, const Structure::Result &structure_,
                          const Probability::Result &probability_, const Structure::Pool &shapes_,
                          const ShapeSolver::DistributionPool &distributions_, const Influence &full) const {
                return owner == full.tiles.data() && board == &board_ && basic == &basic_ && structure == &structure_ &&
                       probability == &probability_ && shapes == &shapes_ && distributions == &distributions_;
            }
        } tallyCache{};
    };

    // 全盘扫描：横/竖 2-tile 对 + 2x2 块 → tiles/enablers/pseudos/hotspots/
    // possible。与 Java findInfluence() 一致：找到 pseudo 即提前终止；
    // hotspot 只在扫描阶段记录。
    static Influence findInfluence(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                                   const Probability::Result &probability, Structure::Pool &shapes,
                                   ShapeSolver::DistributionPool &distributions, std::span<const ObservedBoard::CellId> dead,
                                   const Config &cfg, Workspace &workspace);

    // Java findInfluence(Location tile)：单格聚合——参与的不超过 4 个 2-tile
    // 对取 max + 不超过 4 个 2x2 块取 max + 全盘扫描的 enabler 贡献，钳制到
    // min(mineTally, 方案数 − mineTally)。返回 tally 单位。
    // full：全盘扫描产物（取 enabler 贡献）；传空则忽略 enabler 部分。
    static long double findInfluence(ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic,
                                    const Structure::Result &structure, const Probability::Result &probability,
                                    Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions,
                                    const Influence &full, Workspace &workspace);

    // 计数原语（公开，便于测试）：强制一组雷 / 一组安全后的全盘解数。
    // 与 Java validatePosition(mines, noMines, EMPTY_AREA) 语义一致。
    static long double countWithForces(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                                       Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions,
                                       std::span<const ObservedBoard::CellId> mines, std::span<const ObservedBoard::CellId> safes,
                                       Workspace &workspace);
};

} // namespace mss

//==============================================================================

#include <algorithm>
#include <cmath>

namespace mss {
namespace LongTermRiskInternal {

inline bool contains(std::span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

struct CellList {
    std::array<ObservedBoard::CellId, 8> cells{};
    int count = 0;

    void push_back(ObservedBoard::CellId cell) {
        cells[count++] = cell;
    }
    ObservedBoard::CellId operator[](int index) const {
        return cells[index];
    }
    const ObservedBoard::CellId *begin() const {
        return cells.data();
    }
    const ObservedBoard::CellId *end() const {
        return cells.data() + count;
    }
    int size() const {
        return count;
    }
    std::span<const ObservedBoard::CellId> span() const {
        return {cells.data(), end()};
    }
};

enum class TallyKind : unsigned char {
    FullHorizontal,
    FullVertical,
    FullBox,
    CellHorizontal,
    CellVertical,
    CellBox,
};

template <typename Compute>
inline long double cachedTally(LongTermRiskReference::Workspace &workspace, TallyKind kind, int index, Compute &&compute) {
    LongTermRiskReference::Workspace::TallyCache &cache = workspace.tallyCache;
    const int slot = (int)(kind);
    if (cache.ready[slot][index])
        return cache.values[slot][index];
    const long double value = compute();
    cache.values[slot][index] = value;
    cache.ready[slot][index] = 1;
    return value;
}

// Java isHidden：未确定雷且未揭示（Frontier 也算 hidden）。
inline bool hidden(const ObservedBoard::Result &board, const Basic::Result &basic, int x, int y) {
    return x >= 0 && x < board.board.rows() && y >= 0 && y < board.board.cols() && board.board[x][y] == ObservedBoard::CellState::Hidden &&
           basic.marks[x][y] != Basic::Mark::F;
}

// Java getMissingMines：任一源格已揭示则整个结构作废（hasRevealedSource 已先行
// 拦截）；余下非确定雷的源格为 missing。
inline CellList missingMines(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                      const Probability::Result &probability, std::span<const std::pair<int, int>> source, long double eqEps) {
    CellList missing;
    for (const auto [x, y] : source) {
        if (x < 0 || x >= board.board.rows() || y < 0 || y >= board.board.cols())
            continue; // 越界 = Java null → 跳过
        const ObservedBoard::CellId cell = board.id(x, y);
        // 确定雷（basic Mine 或 PE 新雷）→ 归 present，不进 missing。
        if (probability.mineProbability(cell, board, basic, structure) >= 1.0L - eqEps)
            continue;
        missing.push_back(cell);
    }
    return missing;
}

inline bool hasRevealedSource(const ObservedBoard::Result &board, std::span<const std::pair<int, int>> source) {
    for (const auto [x, y] : source)
        if (x >= 0 && x < board.board.rows() && y >= 0 && y < board.board.cols() && board.board[x][y] != ObservedBoard::CellState::Hidden)
            return true;
    return false;
}

// 单格雷迹 tally（Java Box.getTally() / offEdgeTally）。
inline long double boxTally(ObservedBoard::CellId cell, const Probability::Result &probability, const ObservedBoard::Result &board, const Basic::Result &basic,
                     const Structure::Result &structure) {
    return probability.mineProbability(cell, board, basic, structure) * probability.candidates();
}

// Java getHorizontal/getVertical/getBoxInfluence 的公共计数：强制结构雷/安全后
// 数全盘解数。mineCount = 1（2-tile）/ 2（2x2）；maxMissing 是"missing+n ≤ maxMissing"
// 的阈值（Java 直接传 maxMissingMines）。

} // namespace LongTermRiskInternal

inline bool LongTermRiskReference::RiskHotspot::isExempt(ObservedBoard::CellId candidate) const {
    return candidate == cell || LongTermRiskInternal::contains(exemptions, candidate);
}

inline bool LongTermRiskReference::Possible5050::isExempt(ObservedBoard::CellId candidate) const {
    return LongTermRiskInternal::contains(exemptions, candidate);
}





inline std::vector<ObservedBoard::CellId> LongTermRiskReference::Influence::influencedTiles(long double threshold, const ObservedBoard::Result &board,
                                                                      const Basic::Result &basic, const Structure::Result &structure,
                                                                      const Probability::Result &probability,
                                                                      std::span<const ObservedBoard::CellId> dead) const {
    std::vector<ObservedBoard::CellId> result;
    const long double cutoffTally = threshold * probability.candidates();
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y) {
            const ObservedBoard::CellId cell = board.id(x, y);
            const long double influence = tiles[cell] + enablers[cell];
            if (influence == 0.0L || LongTermRiskInternal::contains(dead, cell))
                continue;
            const long double mineTally = probability.mineProbability(cell, board, basic, structure) * probability.candidates();
            const long double safetyTally = probability.candidates() - mineTally + influence;
            if (safetyTally > cutoffTally)
                result.push_back(cell);
        }
    return result;
}

} // namespace mss

namespace mss {
namespace LongTermRiskInternal {
inline long double candidateTally(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                           const Probability::Result &probability, Structure::Pool &shapes,
                           ShapeSolver::DistributionPool &distributions, std::span<const ObservedBoard::CellId> tiles,
                           std::span<const std::pair<int, int>> source, int mineCount, int maxMissing, int minesLeft, long double eqEps,
                           LongTermRiskReference::Workspace &workspace) {
    if (hasRevealedSource(board, source))
        return 0.0L;
    const CellList missing = missingMines(board, basic, structure, probability, source, eqEps);
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
    return LongTermRiskReference::countWithForces(board, basic, structure, shapes, distributions, mines.span(), safes.span(), workspace);
}
} // namespace LongTermRiskInternal

inline long double LongTermRiskReference::countWithForces(const ObservedBoard::Result &board, const Basic::Result &basic,
                                                          const Structure::Result &structure, Structure::Pool &shapes,
                                                          ShapeSolver::DistributionPool &distributions,
                                                          std::span<const ObservedBoard::CellId> mines,
                                                          std::span<const ObservedBoard::CellId> safes, Workspace &workspace) {
    // 调用方传入的是可回滚的可变状态，只在接口层以 const 视图传递；
    // 这里原地施加强制事实，返回前用 Delta 逆序恢复。
    ObservedBoard::Result &forced = const_cast<ObservedBoard::Result &>(board);
    Basic::Result &forcedBasic = const_cast<Basic::Result &>(basic);
    Structure::Result &forcedStructure = const_cast<Structure::Result &>(structure);
    Workspace::Force &force = workspace.force;
    Probability::Result &forcedProbability = force.probability;
    ObservedBoard::Delta &boardDelta = force.boardDelta;
    boardDelta.changes.clear();
    boardDelta.changes.reserve(mines.size() + safes.size());
    for (ObservedBoard::CellId cell : mines)
        boardDelta.changes.push_back({cell, ObservedBoard::CellState::ForcedMine});
    for (ObservedBoard::CellId cell : safes)
        boardDelta.changes.push_back({cell, ObservedBoard::CellState::ForcedSafe});
    ObservedBoard::update(forced, boardDelta);
    Basic::Delta basicDelta;
    Basic::update(forcedBasic, basicDelta, forced, boardDelta, workspace.basic);
    Structure::Delta structureDelta;
    Structure::update(forcedStructure, structureDelta, forced, forcedBasic, shapes, boardDelta, workspace.structure);
    Probability::analyze(forced, forcedBasic, forcedStructure, shapes, distributions, forcedProbability, workspace.probability);
    const long double candidates = forcedProbability.candidates();
    Structure::reverseDelta(forcedStructure, shapes, structureDelta);
    Basic::reverseDelta(forcedBasic, basicDelta);
    ObservedBoard::reverseDelta(forced, boardDelta);
    return candidates;
}

inline LongTermRiskReference::Influence LongTermRiskReference::findInfluence(const ObservedBoard::Result &board, const Basic::Result &basic,
                                                                             const Structure::Result &structure,
                                                                             const Probability::Result &probability,
                                                                             Structure::Pool &shapes,
                                                                             ShapeSolver::DistributionPool &distributions,
                                                                             std::span<const ObservedBoard::CellId> dead,
                                                                             const Config &cfg, Workspace &workspace) {
    using namespace LongTermRiskInternal;
    Influence out;
    const int size = board.board.rows() * board.board.cols();
    out.tiles.assign(size, 0.0L);
    out.enablers.assign(size, 0.0L);
    Workspace::TallyCache &tallyCache = workspace.tallyCache;
    tallyCache.reset(board.board.rows(), board.board.cols());
    tallyCache.owner = out.tiles.data();
    tallyCache.board = &board;
    tallyCache.basic = &basic;
    tallyCache.structure = &structure;
    tallyCache.probability = &probability;
    tallyCache.shapes = &shapes;
    tallyCache.distributions = &distributions;
    if (probability.candidates() == 0.0L)
        return out;
    const int minesLeft = board.totalMines - basic.mineSum;

    // 找到 pseudo 即提前终止（Java findInfluence 在两次扫描间提前 return）。
    bool stop = false;
    bool storeHotspots = true;

    // 一个候选结构的完整处理：Java getHorizontal/getVertical/getBoxInfluence +
    // checkForPseudo + addInfluence 的合并。
    auto consider = [&](std::span<const ObservedBoard::CellId> tiles, std::span<const std::pair<int, int>> source, int mineCount, int maxMissing,
                        TallyKind tallyKind, int tallyIndex) {
        if (stop)
            return;
        if (hasRevealedSource(board, source))
            return;
        const CellList missing = missingMines(board, basic, structure, probability, source, cfg.longTermRisk.eqEps);
        const int extra = mineCount == 1 ? 1 : 2;
        if (missing.count + extra > maxMissing)
            return;
        if (missing.count + extra > minesLeft)
            return;

        // 热点：2-tile 单缺雷 enabler，且该盒雷迹恰为半数方案（50/50）。
        // Java 要求 subject 位于某盒（getBox != null）——离网格没有盒概念。
        if (storeHotspots && missing.size() == 1 && mineCount == 1 && structure.cellLoc[tiles[0]].box != -1) {
            const long double tally0 = boxTally(tiles[0], probability, board, basic, structure);
            if (std::abs(2.0L * tally0 - probability.candidates()) <= cfg.longTermRisk.eqEps * probability.candidates()) {
                const long double safety = 1.0L - probability.mineProbability(missing[0], board, basic, structure);
                if (safety < cfg.longTermRisk.hotspotEnablerSafety) {
                    RiskHotspot hotspot;
                    hotspot.cell = missing[0];
                    hotspot.safety = 0.5L + 0.5L * safety;
                    hotspot.exemptions.assign(tiles.begin(), tiles.end());
                    out.hotspots.push_back(std::move(hotspot));
                }
            }
        }

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
        const long double tally = cachedTally(workspace, tallyKind, tallyIndex, [&] {
            return countWithForces(board, basic, structure, shapes, distributions, mines.span(), safes.span(), workspace);
        });
        if (tally == 0.0L)
            return;
        const long double ratio = tally / probability.candidates();

        // checkForPseudo：盒雷迹被结构影响度完全覆盖且非死格。
        for (ObservedBoard::CellId tile : tiles) {
            const long double mineTally = boxTally(tile, probability, board, basic, structure);
            if (std::abs(tally - mineTally) <= cfg.longTermRisk.eqEps * (std::max)(tally, mineTally) && !contains(dead, tile))
                out.pseudos.push_back(tile);
        }
        if (!out.pseudos.empty()) {
            stop = true;
            return;
        }

        // addInfluence（FINAL_THRESHOLD 之上才记账）。
        if (ratio > cfg.longTermRisk.influenceThreshold) {
            Possible5050 possible;
            possible.probability = ratio;
            possible.exemptions.insert(possible.exemptions.end(), missing.begin(), missing.end());
            possible.exemptions.insert(possible.exemptions.end(), tiles.begin(), tiles.end());
            out.possible.push_back(std::move(possible));
            for (ObservedBoard::CellId tile : tiles)
                out.tiles[tile] += tally;
            for (ObservedBoard::CellId enabler : missing)
                out.enablers[enabler] += tally;
        }
    };

    // 横 2-tile（Java getHorizontal：同行 (x,y)-(x,y+1)；源格 = 对左右两列
    // (y-1, y+2)、三行 (x-1..x+1)）。
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y + 1 < board.board.cols(); ++y)
            if (hidden(board, basic, x, y) && hidden(board, basic, x, y + 1)) {
                const std::array cells{board.id(x, y), board.id(x, y + 1)};
                const std::array source{std::pair{x - 1, y - 1}, std::pair{x, y - 1}, std::pair{x + 1, y - 1},
                                        std::pair{x - 1, y + 2}, std::pair{x, y + 2}, std::pair{x + 1, y + 2}};
                consider(cells, source, 1, 2, TallyKind::FullHorizontal, board.id(x, y));
                if (stop)
                    return out;
            }
    // 竖 2-tile（Java getVertical：同列 (x,y)-(x+1,y)；源格 = 对上下两行
    // (x-1, x+2)、三列 (y-1..y+1)）。
    for (int x = 0; x + 1 < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y)
            if (hidden(board, basic, x, y) && hidden(board, basic, x + 1, y)) {
                const std::array cells{board.id(x, y), board.id(x + 1, y)};
                const std::array source{std::pair{x - 1, y - 1}, std::pair{x - 1, y}, std::pair{x - 1, y + 1},
                                        std::pair{x + 2, y - 1}, std::pair{x + 2, y}, std::pair{x + 2, y + 1}};
                consider(cells, source, 1, 2, TallyKind::FullVertical, board.id(x, y));
                if (stop)
                    return out;
            }
    // 2x2 块（Java getBoxInfluence：4 个对角源格）。
    for (int x = 0; x + 1 < board.board.rows(); ++x)
        for (int y = 0; y + 1 < board.board.cols(); ++y)
            if (hidden(board, basic, x, y) && hidden(board, basic, x + 1, y) && hidden(board, basic, x, y + 1) &&
                hidden(board, basic, x + 1, y + 1)) {
                const std::array cells{board.id(x, y), board.id(x, y + 1), board.id(x + 1, y), board.id(x + 1, y + 1)};
                const std::array source{std::pair{x - 1, y - 1}, std::pair{x - 1, y + 2}, std::pair{x + 2, y - 1}, std::pair{x + 2, y + 2}};
                consider(cells, source, 2, 3, TallyKind::FullBox, board.id(x, y));
                if (stop)
                    return out;
            }
    return out;
}

inline long double LongTermRiskReference::findInfluence(ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic,
                                                        const Structure::Result &structure, const Probability::Result &probability,
                                                        Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions,
                                                        const Influence &full, Workspace &workspace) {
    using namespace LongTermRiskInternal;
    if (probability.candidates() == 0.0L)
        return 0.0L;
    const int minesLeft = board.totalMines - basic.mineSum;
    const auto [x, y] = board.pos(cell);
    Workspace::TallyCache &tallyCache = workspace.tallyCache;
    const bool useTallyCache = tallyCache.reusable(board, basic, structure, probability, shapes, distributions, full);

    // 横：含本格的两对（Java getHorizontal(tile) 与 getHorizontal(x-1, y)）；
    // 源格 = 对左右两列 (col-1, col+2)、三行 (row-1..row+1)。
    auto horizontal = [&](int row, int col) {
        if (row < 0 || row >= board.board.rows() || col < 0 || col + 1 >= board.board.cols())
            return 0.0L;
        if (!hidden(board, basic, row, col) || !hidden(board, basic, row, col + 1))
            return 0.0L;
        const std::array tiles{board.id(row, col), board.id(row, col + 1)};
        const std::array source{std::pair{row - 1, col - 1}, std::pair{row, col - 1}, std::pair{row + 1, col - 1},
                                std::pair{row - 1, col + 2}, std::pair{row, col + 2}, std::pair{row + 1, col + 2}};
        if (useTallyCache)
            return cachedTally(workspace, TallyKind::CellHorizontal, board.id(row, col), [&] {
                return candidateTally(board, basic, structure, probability, shapes, distributions, tiles, source, 1, 4, minesLeft,
                                      1e-9L, workspace);
            });
        return candidateTally(board, basic, structure, probability, shapes, distributions, tiles, source, 1, 4, minesLeft, 1e-9L,
                              workspace);
    };
    // 竖：含本格的两对（Java getVertical(tile) 与 getVertical(x, y-1)）；
    // 源格 = 对上下两行 (row-1, row+2)、三列 (col-1..col+1)。
    auto vertical = [&](int row, int col) {
        if (row < 0 || row + 1 >= board.board.rows() || col < 0 || col >= board.board.cols())
            return 0.0L;
        if (!hidden(board, basic, row, col) || !hidden(board, basic, row + 1, col))
            return 0.0L;
        const std::array tiles{board.id(row, col), board.id(row + 1, col)};
        const std::array source{std::pair{row - 1, col - 1}, std::pair{row - 1, col}, std::pair{row - 1, col + 1},
                                std::pair{row + 2, col - 1}, std::pair{row + 2, col}, std::pair{row + 2, col + 1}};
        if (useTallyCache)
            return cachedTally(workspace, TallyKind::CellVertical, board.id(row, col), [&] {
                return candidateTally(board, basic, structure, probability, shapes, distributions, tiles, source, 1, 4, minesLeft,
                                      1e-9L, workspace);
            });
        return candidateTally(board, basic, structure, probability, shapes, distributions, tiles, source, 1, 4, minesLeft, 1e-9L,
                              workspace);
    };
    // 2x2：含本格的 4 个角。
    auto box = [&](int row, int col) {
        if (row < 0 || row + 1 >= board.board.rows() || col < 0 || col + 1 >= board.board.cols())
            return 0.0L;
        if (!hidden(board, basic, row, col) || !hidden(board, basic, row + 1, col) || !hidden(board, basic, row, col + 1) ||
            !hidden(board, basic, row + 1, col + 1))
            return 0.0L;
        const std::array tiles{board.id(row, col), board.id(row, col + 1), board.id(row + 1, col), board.id(row + 1, col + 1)};
        const std::array source{std::pair{row - 1, col - 1}, std::pair{row - 1, col + 2}, std::pair{row + 2, col - 1},
                                std::pair{row + 2, col + 2}};
        if (useTallyCache)
            return cachedTally(workspace, TallyKind::CellBox, board.id(row, col), [&] {
                return candidateTally(board, basic, structure, probability, shapes, distributions, tiles, source, 2, 5, minesLeft,
                                      1e-9L, workspace);
            });
        return candidateTally(board, basic, structure, probability, shapes, distributions, tiles, source, 2, 5, minesLeft, 1e-9L,
                              workspace);
    };

    long double influence = 0.0L;
    influence += (std::max)(horizontal(x, y), horizontal(x, y - 1));
    influence += (std::max)(vertical(x, y), vertical(x - 1, y));
    influence += (std::max)({(box(x, y)), box(x - 1, y), box(x, y - 1), box(x - 1, y - 1)});
    // enabler 贡献：点击 enabler 同样消除 50/50 风险（Java findInfluence(tile) 加
    // influenceEnablers）。全盘扫描未执行（full 为空）时忽略。
    if (cell >= 0 && cell < (int)(full.enablers.size()))
        influence += full.enablers[cell];

    // 钳制：50/50 影响不可能超过 P(雷) 或 P(安全) 对应的 tally。
    long double maxInfluence = boxTally(cell, probability, board, basic, structure);
    const long double other = probability.candidates() - maxInfluence;
    maxInfluence = (std::min)(maxInfluence, other);
    return (std::min)(influence, maxInfluence);
}
} // namespace mss
