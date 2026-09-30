#pragma once

#include <span>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/java_transplant/long_term_risk_helper.h"
#include "algo/winrate_engine/java_transplant/pseudo_helper.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/utility/neighbors.h"

namespace mss {
// ─────────────────────────────────────────────────────────────
// algo/winrate_engine/java_transplant/java_evaluate.h — Java 猜牌参考入口的忠实移植。
//
// 覆盖 Java Solver 在"无确定安全格"时的完整决策链：
//   1. pseudo 提前出口：LongTermRiskHelper 的 pseudos，或 PseudoHelper 的
//      pseudo 50/50，非空则只评估它们直接落子（Solver.java:899-931）；
//   2. 候选窗（pe.getBestCandidates 等价：盒安全度阈值 + 死格排除）；
//   3. 长期风险影响格（LongTermRiskReference::influencedTiles）；
//   4. 离网候选（evaluateOffEdgeCandidates，含大块 off-edge 兴趣点缩减）；
//   5. SpaceCounter 空间阈值：小区域候选推迟，大区域优先；
//   6. 逐格评估（doFullEvaluateTile：dominated 快路径 / 各结局临时揭示 +
//      新局 blendedSafety × 50/50 influence × 热点比率 / progress gating /
//      validValues==1 → deferGuessing / singleSafestTile 支配）；
//   7. bestMove（defer 垫底排序 + findAlternativeMove 简单支配 +
//      dominatingLocation 复杂支配交换）。
//
// 死格一律用 Observe::observe 判定：观测数字分布只有一个正概率结局即死格。
//
// 与算法层的接口只读：临时揭示通过 ObservedBoard/Basic/Structure 的
// update + reverseDelta 就地回滚，不修改算法层。
// ─────────────────────────────────────────────────────────────

struct JavaEvaluate {
    struct Workspace {
        LongTermRiskReference::Workspace &risk;
    };

    // 与 Java SolverSettings / SecondarySafetyEvaluator 默认值一致。
    struct Config {
        long double progressContribution = 0.001L;
        long double selectionThreshold1 = 0.10L;
        long double selectionThreshold2 = 0.20L;
        long double influenceScale = 0.9L;       // FIFTYFIFTY_INFLUENCE_SCALE
        long double equalityThreshold = 0.0001L; // EQUALITY_THRESHOLD
        int weight1 = 4;
        int weight2 = 1;
        int spaceThreshold = 8; // SpaceCounter 阈值
        long double eqEps = 1e-9L;
    };

    // 单候选报告（与 Java EvaluatedLocation 的公开字段对齐）。
    struct Candidate {
        int x = 0;                 // 1-based 行
        int y = 0;                 // 1-based 列
        long double safety = 0;    // P(安全)
        long double influence = 0; // 50/50 影响度（findInfluence tally）
        long double expectedClears = 0;
        long double maxValueProgress = 0; // 最坏结局概率（minimax 排序用，保留口径）
        long double weight = 0;
        bool deferGuessing = false; // 死格：唯一结局，最后才考虑
    };

    struct Result {
        int x = 0;
        int y = 0;
        long double weight = 0;
        bool pseudo5050 = false;           // 是否进入 Java 的 pseudo 50/50 分支
        std::vector<Candidate> candidates; // 全候选，按权重降序
        // 计算过程中由 observe 判定为死格（单结局）的格，供 UI 标记。只含
        // "被观察到"的格；没参与评估的死格不在此列。
        std::vector<ObservedBoard::CellId> deadCells;
    };

    // 求解一次猜牌。risk 必须来自同局面的 LongTermRiskReference::findInfluence。
    // dead：调用方可选的已知死格（默认空；求解内用 observe 自行判定并合并）。
    // 求解期间会临时修改 board/basic/structure，返回前恢复。
    static Result solve(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure,
                        const Probability::Result &probability, Structure::Pool &shapes,
                        ShapeSolver::DistributionPool &distributions, const LongTermRiskReference::Influence &risk,
                        std::span<const ObservedBoard::CellId> dead, const Config &config, Workspace &workspace);
};

} // namespace mss

//==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace mss {
namespace JavaEvaluateInternal {

constexpr bool kCheckDeadLocations = false;

// ── 小工具 ──

inline bool isNumberState(ObservedBoard::CellState state) {
    return (int)(state) <= (int)(ObservedBoard::CellState::Num8);
}

inline bool evaluateContains(std::span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

// 死格判定：Java 的 deadLocations（单结局）。一律用 observe 的数字分布：
// 只有一个正概率结局即死格。
inline bool deadByObserve(const Observe::Result &observation) {
    int outcomes = 0;
    for (long double chance : observation.probability)
        if (chance != 0.0L)
            ++outcomes;
    return outcomes == 1;
}

// 单格雷迹 tally（Java Box.getTally() / offEdgeTally）。
inline long double evaluateBoxTally(ObservedBoard::CellId cell, const Probability::Result &probability, const ObservedBoard::Result &board,
                                    const Basic::Result &basic, const Structure::Result &structure) {
    return probability.mineProbability(cell, board, basic, structure) * probability.candidates();
}

// Java calculateHotspotSafety：未豁免热点（candidate = -1 = 全部）的
// (1 + P(安全)) × 0.5 连乘。
inline long double hotspotSafety(const LongTermRiskReference::Influence &risk, ObservedBoard::CellId candidate, const ObservedBoard::Result &board,
                                 const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability) {
    long double result = 1.0L;
    for (const LongTermRiskReference::RiskHotspot &hotspot : risk.hotspots) {
        if (candidate != -1 && hotspot.isExempt(candidate))
            continue;
        const long double safety = 1.0L - probability.mineProbability(hotspot.cell, board, basic, structure);
        result *= (1.0L + safety) * 0.5L;
    }
    return result;
}

// Java isTileExempt：候选格是否被某个热点豁免（点了它等于破 50/50，不惩罚）。
inline bool hotspotExempt(const LongTermRiskReference::Influence &risk, ObservedBoard::CellId cell) {
    for (const LongTermRiskReference::RiskHotspot &hotspot : risk.hotspots)
        if (hotspot.isExempt(cell))
            return true;
    return false;
}

// 强制揭示候选格后的评估对象（Java EvaluatedLocation 等价物）。
struct Eval {
    ObservedBoard::CellId cell = -1;
    long double safety = 0;    // safeProbability
    long double influence = 0; // findInfluence tally（Java 的 50/50 影响度）
    long double weight = 0;
    long double expectedClears = 0;
    long double maxValueProgress = 0;
    bool deferGuessing = false;
    bool pruned = false;
    ObservedBoard::CellId dominatingLocation = -1;
    std::vector<ObservedBoard::CellId> commonClears; // 全结局共有的安全盒格（findAlternativeMove 用）
};

// Java SORT_BY_WEIGHT：defer 垫底，其次权重降序，其次 expectedClears 降序。
inline bool evalLess(const Eval &lhs, const Eval &rhs) {
    if (lhs.deferGuessing != rhs.deferGuessing)
        return !lhs.deferGuessing;
    if (lhs.weight != rhs.weight)
        return lhs.weight > rhs.weight;
    if (lhs.expectedClears != rhs.expectedClears)
        return lhs.expectedClears > rhs.expectedClears;
    return lhs.cell < rhs.cell;
}

// Java getLivingClearCount / getEmptyBoxes / bestSafety 的一部分：
// 新局面的"活清空数"、tally-0 安全盒格集、与 dominated 判定。
struct ForcedInfo {
    long double livingClears = 0;
    bool dominated = false;
    std::vector<std::vector<ObservedBoard::CellId>> emptyBoxes; // 每个 tally-0 盒的格集
};

inline ForcedInfo inspectForced(const Structure::Result &structure, const Probability::Result &probability, ObservedBoard::CellId exclude,
                                const Structure::Pool &shapes) {
    ForcedInfo info;
    const std::vector<Probability::Result::Component> &componentProbabilities = probability.components();
    const std::vector<long double> &boxProbabilities = probability.boxProbabilities();
    for (int cid = 0; cid < (int)(componentProbabilities.size()); ++cid) {
        const Structure::Instance &inst = shapes.getInstance(structure.components[cid]);
        for (int bid = 0; bid < (int)(inst.boxes.count()); ++bid) {
            if (boxProbabilities[componentProbabilities[cid].boxOffset + bid] != 0.0L)
                continue; // tally-0 = 全安全
            std::vector<ObservedBoard::CellId> cells;
            for (int k = inst.boxes.boxOf.span(shapes.boxOf)[bid]; k < inst.boxes.boxOf.span(shapes.boxOf)[bid + 1]; ++k)
                cells.push_back(inst.boxes.cells.span(shapes.cells)[k]);
            info.livingClears += cells.size();
            info.emptyBoxes.push_back(std::move(cells));
        }
    }
    // dominated：存在不含本格、size > 1 的全安全盒（Java doFullEvaluateTile）。
    for (const std::vector<ObservedBoard::CellId> &cells : info.emptyBoxes) {
        if (cells.size() <= 1)
            continue;
        if (evaluateContains(cells, exclude))
            continue;
        info.dominated = true;
        break;
    }
    return info;
}

// 临时施加强制事实并回滚，复用父状态的 Board、Basic、Structure 容量。


// ── 新局面的摘要（Java ProbabilityEngineFast 的 bestSafety 族）──
//   blendedSafety   = (最安全活格×weight1 + 次安全活格×weight2) / 和
//   clears          = 活清空格数（tally-0 盒内非死格）
//   singleSafest    = 唯一最安全活格（无则 -1）
//   emptyBoxes      = tally-0 盒格集（供 commonClears 交叉）
struct BoardSummary {
    long double blendedSafety = 0;
    long double clears = 0;
    ObservedBoard::CellId singleSafestTile = -1;
    std::vector<std::vector<ObservedBoard::CellId>> emptyBoxes;
};

inline BoardSummary summarizeBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
                                   const Probability::Result &probability, const Structure::Pool &shapes,
                                   const JavaEvaluate::Config &cfg) {
    BoardSummary out;
    long double best = 1.0L - probability.tCellProbability();
    long double second = best;
    ObservedBoard::CellId bestCell = -1;

    // 逐格：死格用 observe 判定并排除（Java 的 deadLocations 语义）。
    // 注意：全安全格（tally-0，safety 1.0）也是活格，必须参与 best/second——
    // Java 的 safest1 会被它推到 1.0（曾漏掉导致权重系统性偏低）。
    //
    // 懒 observe（纯逻辑优化）：死格判定（observe）只对"需要"的格执行。
    //   - 所有 mine==0 格：为了统计 clears（死格不计）必须判死；
    //   - 高安全度格：按安全度降序处理，直到凑齐前两名活格（best/second）。
    //     一旦 best/second 已定，剩余 mine>0 的低安全度格既进不了 best/second
    //     （安全度 ≤ second），也不进 clears（mine>0），无需再 observe。
    //   dead 判定结果与原逐格 observe 逐位一致（只是少算了用不到的格）。
    struct Item {
        long double safety;
        ObservedBoard::CellId cell;
        int order;
    };
    Item bestItem;
    Item secondItem;
    bool haveBestItem = false;
    bool haveSecondItem = false;
    int order = 0;
    auto addItem = [&](Item item, bool zeroMine) {
        if (zeroMine)
            out.clears += 1.0L;
        const auto better = [](const Item &lhs, const Item &rhs) {
            return lhs.safety > rhs.safety || (lhs.safety == rhs.safety && lhs.order < rhs.order);
        };
        if (!haveBestItem) {
            bestItem = item;
            haveBestItem = true;
        } else if (better(item, bestItem)) {
            secondItem = bestItem;
            haveSecondItem = true;
            bestItem = item;
        } else if (!haveSecondItem || better(item, secondItem)) {
            secondItem = item;
            haveSecondItem = true;
        }
    };
    // 已知安全格先入列：Java 的 safest1/safest2 会把 safety 1.0 的活格算进去
    // （0 结局翻出的邻居全是 Safe，不泛洪也能确定全安全）。
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y)
            if (board.board[x][y] == ObservedBoard::CellState::Hidden && basic.marks[x][y] == Basic::Mark::S)
                addItem({1.0L, board.id(x, y), order++}, true);
    probability.frontierCells(structure, shapes, [&](ObservedBoard::CellId cell, long double mine) {
        addItem({1.0L - mine, cell, order++}, mine == 0.0L);
    });
    if (haveBestItem && bestItem.safety > best) {
        second = best;
        best = bestItem.safety;
        bestCell = bestItem.cell;
        if (haveSecondItem && secondItem.safety > second)
            second = secondItem.safety;
    }
    out.blendedSafety = (best * cfg.weight1 + second * cfg.weight2) / (cfg.weight1 + cfg.weight2);
    if (best > second)
        out.singleSafestTile = bestCell;

    // tally-0 盒格集（盒级，直接读 boxProbs）。
    const std::vector<Probability::Result::Component> &componentProbabilities = probability.components();
    const std::vector<long double> &boxProbabilities = probability.boxProbabilities();
    for (int cid = 0; cid < (int)(componentProbabilities.size()); ++cid) {
        const Structure::Instance &inst = shapes.getInstance(structure.components[cid]);
        for (int bid = 0; bid < (int)(inst.boxes.count()); ++bid) {
            if (boxProbabilities[componentProbabilities[cid].boxOffset + bid] != 0.0L)
                continue;
            std::vector<ObservedBoard::CellId> cells;
            for (int k = inst.boxes.boxOf.span(shapes.boxOf)[bid]; k < inst.boxes.boxOf.span(shapes.boxOf)[bid + 1]; ++k)
                cells.push_back(inst.boxes.cells.span(shapes.cells)[k]);
            std::sort(cells.begin(), cells.end());
            out.emptyBoxes.push_back(std::move(cells));
        }
    }
    return out;
}

// Java mergeEmptyBoxes：按"格集完全相同"取各结局安全盒的交集。
inline std::vector<std::vector<ObservedBoard::CellId>> intersectBoxes(const std::vector<std::vector<ObservedBoard::CellId>> &a,
                                                       const std::vector<std::vector<ObservedBoard::CellId>> &b) {
    std::vector<std::vector<ObservedBoard::CellId>> out;
    for (const std::vector<ObservedBoard::CellId> &boxA : a)
        if (std::find(b.begin(), b.end(), boxA) != b.end())
            out.push_back(boxA);
    return out;
}

inline std::vector<ObservedBoard::CellId> flattenBoxes(const std::vector<std::vector<ObservedBoard::CellId>> &boxes) {
    std::vector<ObservedBoard::CellId> out;
    for (const std::vector<ObservedBoard::CellId> &box : boxes)
        out.insert(out.end(), box.begin(), box.end());
    return out;
}

// ── 单格评估（Java doFullEvaluateTile）──
// board/basic/structure 会被临时修改并回滚。observation 是该格的观测分布。
// best：当前最高分（Java 的 best 字段，供乐观剪枝）；cell = -1 表示尚无第一名。


// ── 候选窗 / 离网候选 ──

// Java SpaceCounter.meetsThreshold：未开格连通区（可跳过已揭示格）≥ 阈值。
inline bool meetsSpaceThreshold(int startX, int startY, const ObservedBoard::Result &board, const Basic::Result &basic, int threshold) {
    RawGrid<char> visited(board.board.rows(), board.board.cols(), 0);
    std::vector<std::pair<int, int>> stack{{startX, startY}};
    visited[startX][startY] = 1;
    int count = 0;
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        ++count;
        if (count >= threshold)
            return true;
        forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
            if (visited[nx][ny])
                return;
            if (board.board[nx][ny] == ObservedBoard::CellState::Hidden && basic.marks[nx][ny] != Basic::Mark::F) {
                visited[nx][ny] = 1;
                stack.emplace_back(nx, ny);
                return;
            }
            if (isNumberState(board.board[nx][ny])) { // 已揭示 → 可跳过继续找连通区
                visited[nx][ny] = 1;
                forEachAdjacent(nx, ny, board.board.rows(), board.board.cols(), [&](int nx2, int ny2) {
                    if (!visited[nx2][ny2] && board.board[nx2][ny2] == ObservedBoard::CellState::Hidden &&
                        basic.marks[nx2][ny2] != Basic::Mark::F) {
                        visited[nx2][ny2] = 1;
                        stack.emplace_back(nx2, ny2);
                    }
                });
            }
        });
    }
    return count >= threshold;
}

// ── bestMove 支配替换（Java findAlternativeMove）──
inline const Eval *findAlternativeMove(const Eval &move, const std::vector<Eval> &evaluated, const JavaEvaluate::Config &cfg) {
    if (move.commonClears.empty())
        return nullptr;
    for (const Eval &candidate : evaluated) {
        if (candidate.cell == move.cell)
            continue;
        if (candidate.safety - move.safety > cfg.equalityThreshold && evaluateContains(move.commonClears, candidate.cell))
            return &candidate;
    }
    return nullptr;
}

} // namespace JavaEvaluateInternal

} // namespace mss

namespace mss {
namespace JavaEvaluateInternal {
inline ForcedInfo analyzeForced(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure,
                                Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions,
                                std::span<const ObservedBoard::CellId> mines, std::span<const ObservedBoard::CellId> safes,
                                ObservedBoard::CellId exclude, JavaEvaluate::Workspace &workspace) {
    ObservedBoard::Delta updates;
    updates.changes.reserve(mines.size() + safes.size());
    for (ObservedBoard::CellId cell : mines)
        updates.changes.push_back({cell, ObservedBoard::CellState::ForcedMine});
    for (ObservedBoard::CellId cell : safes)
        updates.changes.push_back({cell, ObservedBoard::CellState::ForcedSafe});
    ObservedBoard::update(board, updates);
    Basic::Delta basicDelta;
    Basic::update(basic, basicDelta, board, updates, workspace.risk.basic);
    Structure::Delta structureDelta;
    Structure::update(structure, structureDelta, board, basic, shapes, updates, workspace.risk.structure);
    Probability::Result probability;
    Probability::analyze(board, basic, structure, shapes, distributions, probability, workspace.risk.probability);
    ForcedInfo info = inspectForced(structure, probability, exclude, shapes);
    Structure::reverseDelta(structure, shapes, structureDelta);
    Basic::reverseDelta(basic, basicDelta);
    ObservedBoard::reverseDelta(board, updates);
    return info;
}
} // namespace JavaEvaluateInternal

namespace JavaEvaluateInternal {
inline Eval evaluate(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure, const Probability::Result &probability,
                     Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions,
                     const LongTermRiskReference::Influence &risk, long double baseHotspot,
                     const Observe::Result &observation, ObservedBoard::CellId cell, const Eval &best, const JavaEvaluate::Config &cfg,
                     JavaEvaluate::Workspace &workspace) {
    Eval out;
    out.cell = cell;
    out.safety = 1.0L - observation.probability[9];

    // fiftyFiftyInfluence = (安全迹 + 0.9×影响度) / 安全迹（Java findInfluence(tile)）。
    const long double tally = LongTermRiskReference::findInfluence(cell, board, basic, structure, probability, shapes, distributions,
                                                                   risk, workspace.risk);
    out.influence = tally;
    const long double safetyTally = probability.candidates() - evaluateBoxTally(cell, probability, board, basic, structure);
    long double fiftyFiftyInfluence = 1.0L;
    if (safetyTally > 0.0L)
        fiftyFiftyInfluence = (safetyTally + cfg.influenceScale * tally) / safetyTally;

    const bool hotSpotTile = hotspotExempt(risk, cell);
    const long double pruneFiftyFiftyInfluence = (hotSpotTile ? 1.0L : fiftyFiftyInfluence) / baseHotspot;

    // dominated 快路径（Java doFullEvaluateTile 前半段）：强制本格安全后，
    // 存在不含本格的全安全盒（size>1）→ 信息冗余，直接按一次翻开的收益记分。
    const ObservedBoard::CellId selfCell[] = {cell};
    const ForcedInfo dominatedInfo = analyzeForced(board, basic, structure, shapes, distributions, std::span<const ObservedBoard::CellId>{}, selfCell,
                                                    cell, workspace);
    const long double linkedTilesCount = dominatedInfo.livingClears;
    if (dominatedInfo.dominated) {
        out.weight = out.safety * (1.0L + out.safety * cfg.progressContribution);
        out.expectedClears = linkedTilesCount;
        out.maxValueProgress = out.safety;
        for (const std::vector<ObservedBoard::CellId> &cells : dominatedInfo.emptyBoxes)
            out.commonClears.insert(out.commonClears.end(), cells.begin(), cells.end());
        return out;
    }

    long double secondarySafety = 0.0L;
    long double progressProb = 0.0L;
    long double expectedClears = 0.0L;
    long double maxValueProgress = 0.0L;
    long double safetyThisTileLeft = out.safety;
    ObservedBoard::CellId singleSafestTile = -1;
    bool sameSingleSafestTile = true;
    int validValues = 0;
    // 全结局共有的安全盒（Java commonClears 的逐结局交集）。
    std::vector<std::vector<ObservedBoard::CellId>> commonBoxes;
    bool haveCommon = false;

    ObservedBoard::Delta updates;
    updates.changes.resize(1);
    updates.changes[0].cell = cell;
    for (int value = 0; value <= 8; ++value) {
        const long double probV = observation.probability[value];
        if (probV == 0.0L)
            continue;
        // 乐观上界剪枝（Java 同款）：剩余安全度全按 100% 计也追不上第一名。
        const long double progressBonus = 1.0L + (progressProb + safetyThisTileLeft) * cfg.progressContribution;
        const long double optimistic = (secondarySafety + safetyThisTileLeft * pruneFiftyFiftyInfluence) * progressBonus;
        if (best.cell != -1 && optimistic < best.weight && !best.deferGuessing) {
            out.pruned = true;
            out.weight = optimistic;
            out.expectedClears = expectedClears;
            out.maxValueProgress = maxValueProgress;
            out.commonClears = flattenBoxes(commonBoxes);
            return out;
        }

        // 临时揭示本格为 value，随后回滚。
        updates.changes[0].next = (ObservedBoard::CellState)(value);
        ObservedBoard::update(board, updates);
        Basic::Delta basicDelta;
        Basic::update(basic, basicDelta, board, updates, workspace.risk.basic);
        Structure::Delta structureDelta;
        Structure::update(structure, structureDelta, board, basic, shapes, updates, workspace.risk.structure);
        const Probability::Result next = Probability::analyze(board, basic, structure, shapes, distributions, workspace.risk.probability);

        if (next.candidates() != 0.0L) {
            ++validValues;
            const long double prob = probV;
            const BoardSummary summary = summarizeBoard(board, basic, structure, next, shapes, cfg);
            const long double clears = summary.clears;
            const long double nextMoveSafety = summary.blendedSafety;
            const long double nextHotspot = hotspotSafety(risk, cell, board, basic, structure, next);
            const long double newFiftyFiftyInfluence = (hotSpotTile ? 1.0L : fiftyFiftyInfluence) * nextHotspot / baseHotspot;

            maxValueProgress = (std::max)(maxValueProgress, prob);
            expectedClears += clears * prob;
            secondarySafety += prob * nextMoveSafety * newFiftyFiftyInfluence;
            if (clears > linkedTilesCount)
                progressProb += prob;

            if (summary.singleSafestTile == -1) {
                sameSingleSafestTile = false;
            } else if (singleSafestTile == -1) {
                singleSafestTile = summary.singleSafestTile;
            } else if (singleSafestTile != summary.singleSafestTile) {
                sameSingleSafestTile = false;
            }

            // 逐结局交集：commonClears 只保留所有结局里都全安全的盒。
            if (!haveCommon) {
                commonBoxes = summary.emptyBoxes;
                haveCommon = true;
            } else if (!commonBoxes.empty()) {
                commonBoxes = intersectBoxes(commonBoxes, summary.emptyBoxes);
            }
            safetyThisTileLeft -= prob;
        }

        Structure::reverseDelta(structure, shapes, structureDelta);
        Basic::reverseDelta(basic, basicDelta);
        ObservedBoard::reverseDelta(board, updates);
    }

    out.weight = secondarySafety * (1.0L + progressProb * cfg.progressContribution);
    out.expectedClears = expectedClears;
    out.maxValueProgress = maxValueProgress;
    out.commonClears = flattenBoxes(commonBoxes);
    if (validValues == 1)
        out.deferGuessing = true; // 唯一结局 → 死格，垫底
    if (sameSingleSafestTile && singleSafestTile != -1) {
        // 本格之后某格恒为最安全活格且更安全 → 它支配本格（Java 复杂支配）。
        const long double singleSafestSafety = 1.0L - probability.mineProbability(singleSafestTile, board, basic, structure);
        if (singleSafestSafety > out.safety)
            out.dominatingLocation = singleSafestTile;
    }
    return out;
}
} // namespace JavaEvaluateInternal

inline JavaEvaluate::Result JavaEvaluate::solve(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure,
                                                const Probability::Result &probability, Structure::Pool &shapes,
                                                ShapeSolver::DistributionPool &distributions,
                                                const LongTermRiskReference::Influence &risk,
                                                std::span<const ObservedBoard::CellId> dead, const Config &cfg,
                                                Workspace &workspace) {
    using JavaEvaluateInternal::deadByObserve;
    using JavaEvaluateInternal::evaluate;
    using JavaEvaluateInternal::evaluateContains;
    using JavaEvaluateInternal::Eval;
    using JavaEvaluateInternal::evalLess;
    using JavaEvaluateInternal::findAlternativeMove;
    using JavaEvaluateInternal::hotspotSafety;
    using JavaEvaluateInternal::isNumberState;
    using JavaEvaluateInternal::kCheckDeadLocations;
    using JavaEvaluateInternal::meetsSpaceThreshold;

    // 无解盘面（方案数 0）：引擎会产出 NaN/Inf，直接返回空结果，避免污染前端。
    if (probability.candidates() == 0.0L)
        return Result{};
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y)
            if (board.board[x][y] == ObservedBoard::CellState::Hidden && basic.marks[x][y] == Basic::Mark::S) {
                Result result;
                result.x = x + 1;
                result.y = y + 1;
                result.weight = 1.0L;
                result.candidates.push_back({x + 1, y + 1, 1.0L, 0.0L, 0.0L, 1.0L, 1.0L, false});
                return result;
            }
    const int cellCount = board.board.rows() * board.board.cols();
    bool hasPseudo5050 = false;

    // 观测缓存（observe 一次，供死格判定与评估复用）。
    std::vector<Observe::Result> observations(cellCount);
    std::vector<char> observed(cellCount, 0);
    // 计算过程中被 observe 判定的死格（单结局），供 UI 标记。
    std::vector<ObservedBoard::CellId> javaDead;
    auto observeOf = [&](ObservedBoard::CellId cell) -> const Observe::Result & {
        if (!observed[cell]) {
            observations[cell] = Observe::observe(board, basic, structure, shapes, probability, distributions, cell,
                                                      workspace.risk.probability);
            observed[cell] = 1;
        }
        return observations[cell];
    };

    // 评估集 → 报告：defer 垫底排序 + 简单支配替换 + 复杂支配交换 + 输出候选。
    auto finish = [&](std::vector<Eval> &evaluated) -> Result {
        Result r;
        r.pseudo5050 = hasPseudo5050;
        r.deadCells = javaDead;
        if (!evaluated.empty()) {
            std::sort(evaluated.begin(), evaluated.end(), evalLess);
            const Eval *evalLoc = &evaluated[0];
            const Eval *alternative = findAlternativeMove(*evalLoc, evaluated, cfg);
            if (alternative != nullptr)
                evalLoc = alternative;
            if (evalLoc->dominatingLocation != -1) {
                for (const Eval &e : evaluated)
                    if (e.cell == evalLoc->dominatingLocation) {
                        evalLoc = &e;
                        break;
                    }
            }
            r.x = board.pos(evalLoc->cell).first + 1;
            r.y = board.pos(evalLoc->cell).second + 1;
            r.weight = evalLoc->weight;
        }
        for (const Eval &e : evaluated) {
            Candidate c;
            const auto [cx, cy] = board.pos(e.cell);
            c.x = cx + 1;
            c.y = cy + 1;
            c.safety = e.safety;
            c.influence = e.influence;
            c.expectedClears = e.expectedClears;
            c.maxValueProgress = e.maxValueProgress;
            c.weight = e.weight;
            c.deferGuessing = e.deferGuessing;
            r.candidates.push_back(std::move(c));
        }
        std::sort(r.candidates.begin(), r.candidates.end(), [](const Candidate &lhs, const Candidate &rhs) {
            if (lhs.deferGuessing != rhs.deferGuessing)
                return !lhs.deferGuessing;
            if (lhs.weight != rhs.weight)
                return lhs.weight > rhs.weight;
            if (lhs.expectedClears != rhs.expectedClears)
                return lhs.expectedClears > rhs.expectedClears;
            return lhs.x != rhs.x ? lhs.x < rhs.x : lhs.y < rhs.y;
        });
        return r;
    };

    // ── 1. pseudo 提前出口（Java Solver.java:899-931）──
    std::vector<ObservedBoard::CellId> pseudos = risk.pseudos;
    if (pseudos.empty())
        pseudos = PseudoReference::findPseudo5050(board, basic, structure, shapes, probability, dead);
    hasPseudo5050 = !pseudos.empty();
    if (!pseudos.empty()) {
        const long double baseHotspot = hotspotSafety(risk, -1, board, basic, structure, probability);
        std::vector<Eval> evaluated;
        Eval best;
        for (ObservedBoard::CellId cell : pseudos) {
            Eval r = evaluate(board, basic, structure, probability, shapes, distributions, risk, baseHotspot, observeOf(cell), cell,
                              best, cfg, workspace);
            if (best.cell == -1)
                best = r;
            else if (best.deferGuessing && !r.deferGuessing)
                best = r;
            else if (!best.deferGuessing && r.deferGuessing)
                ;
            else if (r.weight > best.weight)
                best = r;
            evaluated.push_back(std::move(r));
        }
        return finish(evaluated);
    }

    // ── 2. 基线热点安全度 ──
    const long double baseHotspot = hotspotSafety(risk, -1, board, basic, structure, probability);

    // ── 3. 前沿盒（Java pe.getBestCandidates 的数据源）──
    struct FrontierBox {
        long double safety = 0;
        std::vector<ObservedBoard::CellId> cells;
    };
    std::vector<FrontierBox> boxes;
    const std::vector<Probability::Result::Component> &componentProbabilities = probability.components();
    const std::vector<long double> &boxProbabilities = probability.boxProbabilities();
    for (int cid = 0; cid < (int)(structure.components.size()); ++cid) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[cid]);
        for (int bid = 0; bid < (int)(instance.boxes.count()); ++bid) {
            FrontierBox box;
            box.safety = 1.0L - boxProbabilities[componentProbabilities[cid].boxOffset + bid];
            for (int k = instance.boxes.boxOf.span(shapes.boxOf)[bid]; k < instance.boxes.boxOf.span(shapes.boxOf)[bid + 1]; ++k)
                box.cells.push_back(instance.boxes.cells.span(shapes.cells)[k]);
            boxes.push_back(std::move(box));
        }
    }
    std::sort(boxes.begin(), boxes.end(), [](const FrontierBox &lhs, const FrontierBox &rhs) {
        return lhs.safety > rhs.safety;
    });

    // ── 3+3.5+4. 懒 observe 死格判定 + bestSafety/bestLivingSafety + 候选窗 ──
    // 只有"可能进候选窗 / 影响 best/second / 影响 influencedTiles"的格才需要
    // observe 判死；低安全度、无影响的格无需 observe。boxes 已按安全度降序：
    //   第一遍 观察最高安全度前缀，直到确定 bestSafety/bestLivingSafety（含活格的盒）；
    //   第二遍 候选窗（懒 observe：已观察的用缓存，未观察的观察直到断点）；
    //   第三遍 为 influencedTiles 补齐 influence>0 的死格（deadSet）。
    std::vector<ObservedBoard::CellId> deadSet(dead.begin(), dead.end());
    std::vector<char> isDead(cellCount, 0);
    auto noteDead = [&](ObservedBoard::CellId cell) {
        if (isDead[cell])
            return;
        isDead[cell] = 1;
        deadSet.push_back(cell);
    };
    auto observeCell = [&](ObservedBoard::CellId cell) -> bool { // true = 活格
        if constexpr (kCheckDeadLocations) {
            if (deadByObserve(observeOf(cell))) {
                noteDead(cell);
                return false;
            }
        }
        return true;
    };

    long double bestLivingSafety = 1.0L - probability.tCellProbability();
    long double bestSafety = bestLivingSafety;

    // 第一遍：观察前缀直到含活格的盒（bestLivingSafety 由此确定；其后安全度更低的
    // 盒不会改变 max）。
    std::size_t idx = 0;
    for (; idx < boxes.size(); ++idx) {
        const FrontierBox &box = boxes[idx];
        bool boxHasLiving = false;
        for (ObservedBoard::CellId cell : box.cells)
            if (observeCell(cell))
                boxHasLiving = true;
        if (boxHasLiving) {
            bestLivingSafety = (std::max)(bestLivingSafety, box.safety);
            bestSafety = (std::max)(bestSafety, box.safety);
        } else if (box.safety == 1.0L) {
            bestSafety = 1.0L;
        }
        if (boxHasLiving)
            break;
    }

    long double test = bestSafety == 1.0L ? 1.0L : bestLivingSafety - cfg.selectionThreshold1;
    long double test2 = bestSafety == 1.0L ? 1.0L : bestLivingSafety - cfg.selectionThreshold2;

    // 第二遍：候选窗（原逻辑；断点前的低安全度盒不 observe）。
    std::vector<ObservedBoard::CellId> onEdge;
    for (const FrontierBox &box : boxes) {
        if (box.safety < test && (onEdge.size() >= 2 || box.safety < test2))
            break;
        test = (std::min)(test, box.safety);
        for (ObservedBoard::CellId cell : box.cells) {
            const bool living = observeCell(cell);
            if (living || box.safety == 1.0L)
                onEdge.push_back(cell);
        }
    }

    // 第三遍：为 influencedTiles 补齐 influence>0 的死格（low-safety 但高影响的格）。
    for (ObservedBoard::CellId cell = 0; cell < cellCount; ++cell) {
        if (isDead[cell])
            continue;
        if (risk.tiles[cell] == 0.0L && risk.enablers[cell] == 0.0L)
            continue;
        observeCell(cell);
    }

    // 长期风险影响格（Java evaluateLocations 加入 tileOfInterestOn）。
    for (ObservedBoard::CellId cell : risk.influencedTiles(bestLivingSafety - cfg.selectionThreshold1, board, basic, structure, probability, deadSet))
        if (!evaluateContains(onEdge, cell))
            onEdge.push_back(cell);

    // ── 5. 离网候选（Java evaluateOffEdgeCandidates；高密度门控在 Java 恒假，
    //    故偏移恒为 4 向）──
    std::vector<ObservedBoard::CellId> offEdge;
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y) {
            const ObservedBoard::CellId cell = board.id(x, y);
            if (board.board[x][y] == ObservedBoard::CellState::Hidden && basic.marks[x][y] == Basic::Mark::T &&
                structure.cellLoc[cell].component == -1)
                offEdge.push_back(cell);
        }
    std::vector<ObservedBoard::CellId> offEdgeCandidates;
    const long double offEdgeSafetyValue = 1.0L - probability.tCellProbability();
    if (bestSafety != 1.0L && offEdgeSafetyValue > bestSafety * 0.95L) {
        if (offEdge.size() < 30) {
            offEdgeCandidates = offEdge;
        } else {
            // Java OFFSETS = {{2,0},{-2,0},{0,2},{0,-2}}（高密度门控恒假）。
            constexpr std::array<std::pair<int, int>, 4> offsets{std::pair{2, 0}, std::pair{-2, 0}, std::pair{0, 2}, std::pair{0, -2}};
            auto addOffEdge = [&](int nx, int ny) {
                if (nx < 0 || nx >= board.board.rows() || ny < 0 || ny >= board.board.cols())
                    return;
                const ObservedBoard::CellId cell = board.id(nx, ny);
                if (evaluateContains(offEdge, cell) && !evaluateContains(offEdgeCandidates, cell))
                    offEdgeCandidates.push_back(cell);
            };
            for (int x = 0; x < board.board.rows(); ++x)
                for (int y = 0; y < board.board.cols(); ++y) {
                    if (!isNumberState(board.board[x][y]))
                        continue; // Java 原始 witness
                    for (const auto [dx, dy] : offsets)
                        addOffEdge(x + dx, y + dy);
                }
            for (ObservedBoard::CellId cell : offEdge) {
                const auto [x, y] = board.pos(cell);
                int adjacent = 0;
                forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
                    if (board.board[nx][ny] == ObservedBoard::CellState::Hidden)
                        ++adjacent;
                });
                if (adjacent > 1 && adjacent < 4 && !evaluateContains(offEdgeCandidates, cell))
                    offEdgeCandidates.push_back(cell);
            }
        }
    }

    // ── 6. SpaceCounter：小区域推迟，大区域优先（Java evaluateLocations）──
    std::vector<ObservedBoard::CellId> notDeferred;
    std::vector<ObservedBoard::CellId> deferred;
    auto partition = [&](const std::vector<ObservedBoard::CellId> &tiles) {
        for (ObservedBoard::CellId cell : tiles) {
            const auto [x, y] = board.pos(cell);
            if (meetsSpaceThreshold(x, y, board, basic, cfg.spaceThreshold))
                notDeferred.push_back(cell);
            else
                deferred.push_back(cell);
        }
    };
    partition(onEdge);
    partition(offEdgeCandidates);
    const std::vector<ObservedBoard::CellId> &toEvaluate = notDeferred.empty() ? deferred : notDeferred;

    // ── 7. 逐格评估 ──
    std::vector<Eval> evaluated;
    Eval best;
    for (ObservedBoard::CellId cell : toEvaluate) {
        Eval r = evaluate(board, basic, structure, probability, shapes, distributions, risk, baseHotspot, observeOf(cell), cell, best,
                          cfg, workspace);
        if (best.cell == -1)
            best = r;
        else if (best.deferGuessing && !r.deferGuessing)
            best = r;
        else if (!best.deferGuessing && r.deferGuessing)
            ;
        else if (r.weight > best.weight)
            best = r;
        evaluated.push_back(std::move(r));
    }

    // ── 8. bestMove + 兜底 ──
    // 仍无候选则选全局最安全的未开非雷格（Java 残局 allDead / guess(wholeEdge)
    // 的参考语义）。
    if (evaluated.empty()) {
        ObservedBoard::CellId safest = -1;
        long double safestSafety = -1.0L;
        for (int x = 0; x < board.board.rows(); ++x)
            for (int y = 0; y < board.board.cols(); ++y) {
                if (board.board[x][y] != ObservedBoard::CellState::Hidden)
                    continue;
                if (basic.marks[x][y] == Basic::Mark::F)
                    continue;
                const ObservedBoard::CellId cell = board.id(x, y);
                const long double safety = 1.0L - probability.mineProbability(cell, board, basic, structure);
                if (safety > safestSafety) {
                    safestSafety = safety;
                    safest = cell;
                }
            }
        if (safest != -1) {
            Eval r = evaluate(board, basic, structure, probability, shapes, distributions, risk, baseHotspot, observeOf(safest), safest,
                              best, cfg, workspace);
            evaluated.push_back(std::move(r));
        }
    }

    // ── 9. 输出（finish 内部做排序与支配替换）──
    return finish(evaluated);
}
} // namespace mss
