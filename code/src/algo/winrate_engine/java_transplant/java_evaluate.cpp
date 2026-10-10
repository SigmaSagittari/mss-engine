#include "java_evaluate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "core/utility/neighbors.h"

namespace mss {

// ── 小工具 ──

bool JavaEval::isNumberState(winrate::CellState state) {
    return (int)(state) <= (int)(winrate::CellState::Num8);
}

bool JavaEval::evaluateContains(winrate::Cells cells, winrate::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

// 单格雷迹 tally（Java Box.getTally() / offEdgeTally）。
long double JavaEval::evaluateBoxTally(winrate::CellId cell, const winrate::View &input) {
    const auto &[board, basic, structure, probability, shapes] = input;
    return probability.mineProbability(cell, board, basic, structure) * probability.candidates();
}

// 未指定候选时计入全部热点；指定候选时忽略其豁免热点。
// 热点惩罚为 (1 + P(安全)) × 0.5 的连乘。
long double JavaEval::hotspotSafety(const LongTermRisk::Analysis &risk, winrate::CellId candidate, const winrate::View &input) {
    const auto &[board, basic, structure, probability, shapes] = input;
    long double result = 1.0L;
    for (const LongTermRisk::RiskHotspot &hotspot : risk.hotspots) {
        if (candidate != -1 && hotspot.isExempt(candidate))
            continue;
        const long double safety = 1.0L - probability.mineProbability(hotspot.cell, board, basic, structure);
        result *= (1.0L + safety) * 0.5L;
    }
    return result;
}

// Java isTileExempt：候选格是否被某个热点豁免（点了它等于破 50/50，不惩罚）。
bool JavaEval::hotspotExempt(const LongTermRisk::Analysis &risk, winrate::CellId cell) {
    for (const LongTermRisk::RiskHotspot &hotspot : risk.hotspots)
        if (hotspot.isExempt(cell))
            return true;
    return false;
}

// Java SORT_BY_WEIGHT：defer 垫底，其次权重降序，其次 expectedClears 降序。
bool JavaEval::evalLess(const Candidate &lhs, const Candidate &rhs) {
    if (lhs.pruned != rhs.pruned)
        return !lhs.pruned;
    // 被裁项按原来的格号顺序排列。
    if (lhs.pruned) return lhs.cell < rhs.cell;
    if (lhs.deferGuessing != rhs.deferGuessing)
        return !lhs.deferGuessing;
    if (lhs.weight != rhs.weight)
        return lhs.weight > rhs.weight;
    if (lhs.expectedClears != rhs.expectedClears)
        return lhs.expectedClears > rhs.expectedClears;
    return lhs.cell < rhs.cell;
}

// Java getLivingClearCount / getEmptyBoxes / bestSafety 的一部分：
// 强制安全后的 tally-0 盒格数及 dominated 判定。

JavaEval::ForcedInfo JavaEval::inspectForced(const winrate::Structure &structure, const winrate::Probability &probability, winrate::CellId exclude, const winrate::Shapes &shapes, Workspace &workspace) {
    EmptyBoxes &boxes = workspace.forcedBoxes;
    collectEmptyBoxes(boxes, structure, probability, shapes);
    bool dominated = false;
    for (int i = 0; i + 1 < (int)boxes.offsets.size(); ++i) {
        const winrate::Cells cells(boxes.cells.data() + boxes.offsets[i], boxes.offsets[i + 1] - boxes.offsets[i]);
        if (cells.size() > 1 && !evaluateContains(cells, exclude)) {
            dominated = true;
            break;
        }
    }
    return ForcedInfo(boxes.cells.size(), dominated);
}

// 临时施加强制事实并回滚，复用父状态的 Board、Basic、Structure 容量。

// ── 新局面的摘要（Java ProbabilityEngineFast 的 bestSafety 族）──
//   blendedSafety   = (最安全活格×weight1 + 次安全活格×weight2) / 和
//   clears          = 已知安全格及 tally-0 前沿格数
//   singleSafest    = 唯一最安全格（无则为 -1）
//   emptyBoxes      = tally-0 盒格集（供 commonClears 交叉）

JavaEval::BoardSummary JavaEval::summarizeBoard(const winrate::View &input, const Config &cfg, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes] = input;
    long double clears = 0.0L;
    long double best = basic.unknownSum != 0 ? 1.0L - probability.tCellProbability() : 0.0L;
    long double second = best;
    // -1 表示尚无唯一最安全格。
    winrate::CellId bestCell = -1;

    // 延续原评分口径：已知安全格和前沿格参与 best/second，此处不做死格过滤。
    // 仅在存在 T 格时以离网安全率为基线；空候选集合从 0 开始取最大值。
    auto addItem = [&](winrate::CellId cell, long double safety, bool zeroMine) {
        if (zeroMine)
            clears += 1.0L;
        if (safety > best) {
            second = best;
            best = safety;
            bestCell = cell;
        } else if (safety > second) {
            second = safety;
        }
    };
    // 已知安全格先入列：Java 的 safest1/safest2 会把 safety 1.0 的活格算进去
    // （0 结局翻出的邻居全是 Safe，不泛洪也能确定全安全）。
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y)
            if (board.board[x][y] == winrate::CellState::Hidden && basic.marks[x][y] == Basic::Mark::S)
                addItem(board.id(x, y), 1.0L, true);
    probability.frontierCells(structure, shapes, [&](winrate::CellId cell, long double mine) {
        addItem(cell, 1.0L - mine, mine == 0.0L);
    });
    const long double blendedSafety = (best * cfg.weight1 + second * cfg.weight2) / (cfg.weight1 + cfg.weight2);
    collectEmptyBoxes(workspace.summaryBoxes, structure, probability, shapes);
    return BoardSummary(blendedSafety, clears, best > second ? bestCell : -1);
}

// 把安全盒格集复制到工作区连续存储，不持有会被池写入失效的视图。
void JavaEval::collectEmptyBoxes(EmptyBoxes &out, const winrate::Structure &structure, const winrate::Probability &probability, const winrate::Shapes &shapes) {
    out.cells.clear();
    out.offsets.clear();
    out.offsets.push_back(0);
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const std::span<const int> offsets = instance.boxes.boxOf.span(shapes.boxOf);
        const winrate::Cells cells = instance.boxes.cells.span(shapes.cells);
        for (int box = 0; box < (int)instance.boxes.count(); ++box) {
            if (probability.boxProbability(component, box) != 0.0L) continue;
            const int start = out.cells.size();
            out.cells.insert(out.cells.end(), cells.begin() + offsets[box], cells.begin() + offsets[box + 1]);
            std::sort(out.cells.begin() + start, out.cells.end());
            out.offsets.push_back(out.cells.size());
        }
    }
}

// 按格集完全相同保留所有已评估结局共有的安全盒。
void JavaEval::intersectBoxes(EmptyBoxes &out, const EmptyBoxes &next) {
    int cellCount = 0;
    int boxCount = 0;
    for (int i = 0; i + 1 < (int)out.offsets.size(); ++i) {
        const int start = out.offsets[i];
        const int end = out.offsets[i + 1];
        bool found = false;
        for (int j = 0; j + 1 < (int)next.offsets.size(); ++j) {
            if (end - start == next.offsets[j + 1] - next.offsets[j] && std::equal(out.cells.begin() + start, out.cells.begin() + end, next.cells.begin() + next.offsets[j])) {
                found = true;
                break;
            }
        }
        if (!found) continue;
        // 目标位置不超过源位置；逐格前移不会覆盖尚未比较的盒。
        for (int k = start; k < end; ++k) out.cells[cellCount++] = out.cells[k];
        out.offsets[++boxCount] = cellCount;
    }
    out.cells.resize(cellCount);
    out.offsets.resize(boxCount + 1);
}

// ── 单格评估（Java doFullEvaluateTile）──
// board/basic/structure 会被临时修改并回滚。observation 是该格的观测分布。
// best 为当前最高分；prune=false 不使用评分上界裁剪。

// ── 候选窗 / 离网候选 ──

// Java SpaceCounter.meetsThreshold：未开格连通区（可跳过已揭示格）≥ 阈值。
bool JavaEval::meetsSpaceThreshold(int startX, int startY, const winrate::Board &board, const winrate::Basic &basic, int threshold, Workspace &workspace) {
    RawGrid<char> &visited = workspace.visited;
    visited.resize(board.board.rows(), board.board.cols(), 0);
    std::vector<std::pair<int, int>> &stack = workspace.stack;
    stack.clear();
    stack.emplace_back(startX, startY);
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
            if (board.board[nx][ny] == winrate::CellState::Hidden && basic.marks[nx][ny] != Basic::Mark::F) {
                visited[nx][ny] = 1;
                stack.emplace_back(nx, ny);
                return;
            }
            if (isNumberState(board.board[nx][ny])) { // 已揭示 → 可跳过继续找连通区
                visited[nx][ny] = 1;
                forEachAdjacent(nx, ny, board.board.rows(), board.board.cols(), [&](int nx2, int ny2) {
                    if (!visited[nx2][ny2] && board.board[nx2][ny2] == winrate::CellState::Hidden && basic.marks[nx2][ny2] != Basic::Mark::F) {
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
const JavaEval::Candidate *JavaEval::findAlternativeMove(const Candidate &move, const std::vector<Candidate> &evaluated, const Config &cfg) {
    if (move.commonClears.empty())
        return nullptr;
    for (const Candidate &candidate : evaluated) {
        if (candidate.pruned || candidate.cell == move.cell)
            continue;
        if (candidate.safety - move.safety > cfg.equalityThreshold && evaluateContains(move.commonClears, candidate.cell))
            return &candidate;
    }
    return nullptr;
}

JavaEval::ForcedInfo JavaEval::analyzeForced(const winrate::Input &input, winrate::CellId cell, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    ObservedBoard::Delta &updates = workspace.boardDelta;
    const auto [row, col] = board.pos(cell);
    updates.clear();
    updates.changes.emplace_back(cell, board.board[row][col], winrate::CellState::ForcedSafe);
    ObservedBoard::update(board, updates);
    Basic::update(basic, workspace.basicDelta, board, updates, workspace.basic);
    Structure::update(structure, workspace.structureDelta, board, basic, shapes, std::span{&cell, 1}, workspace.structure);
    if (!workspace.nextProbability) workspace.nextProbability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability);
    else Probability::analyze(board, basic, structure, shapes, distributions, *workspace.nextProbability, workspace.probability);
    ForcedInfo info = inspectForced(structure, *workspace.nextProbability, cell, shapes, workspace);
    Structure::reverseDelta(structure, shapes, workspace.structureDelta);
    Basic::reverseDelta(basic, workspace.basicDelta);
    ObservedBoard::reverseDelta(board, updates);
    return info;
}

JavaEval::Candidate JavaEval::evaluate(const winrate::Input &input, LongTermRisk::Analysis &risk, winrate::CellId cell, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune, bool allowDominated, const Candidate *best, const Observe::Result &observation) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    const long double safety = 1.0L - observation.probability[9];
    if (safety == 0.0L)
        return Candidate(cell, safety, 0.0L, 0.0L, false, false, -1, {});
    const long double baseHotspot = hotspotSafety(risk, -1, input);

    // fiftyFiftyInfluence = (安全迹 + 0.9×影响度) / 安全迹（Java findInfluence(tile)）。
    const long double tally = LongTermRisk::influence(cell, input, risk, riskConfig, workspace.risk);
    const long double safetyTally = probability.candidates() - evaluateBoxTally(cell, input);
    const long double fiftyFiftyInfluence = (safetyTally + cfg.influenceScale * tally) / safetyTally;

    const bool hotSpotTile = hotspotExempt(risk, cell);
    const long double pruneFiftyFiftyInfluence = (hotSpotTile ? 1.0L : fiftyFiftyInfluence) / baseHotspot;

    // dominated 快路径（Java doFullEvaluateTile 前半段）：强制本格安全后，
    // 存在不含本格的全安全盒（size>1）→ 信息冗余，直接按一次翻开的收益记分。
    const ForcedInfo dominatedInfo = analyzeForced(input, cell, workspace);
    const long double linkedTilesCount = dominatedInfo.livingClears;
    if (allowDominated && dominatedInfo.dominated)
        return Candidate(cell, safety, safety * (1.0L + safety * cfg.progressContribution), linkedTilesCount, false, false, -1, workspace.forcedBoxes.cells);

    long double secondarySafety = 0.0L;
    long double progressProb = 0.0L;
    long double expectedClears = 0.0L;
    long double safetyThisTileLeft = safety;
    // -1 表示尚无共同的唯一最安全格。
    winrate::CellId singleSafestTile = -1;
    bool sameSingleSafestTile = true;
    int validValues = 0;
    // 全结局共有的安全盒（Java commonClears 的逐结局交集）。
    EmptyBoxes &commonBoxes = workspace.commonBoxes;
    commonBoxes.cells.clear();
    commonBoxes.offsets.clear();
    commonBoxes.offsets.push_back(0);
    bool haveCommon = false;

    ObservedBoard::Delta &updates = workspace.boardDelta;
    updates.clear();
    const auto [row, col] = board.pos(cell);
    const winrate::CellState previous = board.board[row][col];
    for (int value = 0; value <= 8; ++value) {
        const long double probV = observation.probability[value];
        if (probV == 0.0L)
            continue;
        // 乐观上界剪枝（Java 同款）：剩余安全度全按 100% 计也追不上第一名。
        const long double progressBonus = 1.0L + (progressProb + safetyThisTileLeft) * cfg.progressContribution;
        const long double optimistic = (secondarySafety + safetyThisTileLeft * pruneFiftyFiftyInfluence) * progressBonus;
        if (best && prune && optimistic < best->weight && !best->deferGuessing) {
            // 被裁项不参与推荐。
            return Candidate(cell, safety, -std::numeric_limits<long double>::infinity(), 0.0L, false, true, -1, {});
        }

        // 临时揭示本格为 value，随后回滚。
        updates.clear();
        updates.changes.emplace_back(cell, previous, (winrate::CellState)value);
        ObservedBoard::update(board, updates);
        Basic::update(basic, workspace.basicDelta, board, updates, workspace.basic);
        Structure::update(structure, workspace.structureDelta, board, basic, shapes, std::span{&cell, 1}, workspace.structure);
        std::optional<winrate::Probability> &nextProbability = workspace.nextProbability;
        if (!nextProbability) nextProbability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability);
        else Probability::analyze(board, basic, structure, shapes, distributions, *nextProbability, workspace.probability);
        const winrate::Probability &next = *nextProbability;
        const winrate::View nextInput(board, basic, structure, next, shapes);

        ++validValues;
        const long double prob = probV;
        const BoardSummary summary = summarizeBoard(nextInput, cfg, workspace);
        const long double clears = summary.clears;
        const long double nextMoveSafety = summary.blendedSafety;
        const long double nextHotspot = hotspotSafety(risk, cell, nextInput);
        const long double newFiftyFiftyInfluence = (hotSpotTile ? 1.0L : fiftyFiftyInfluence) * nextHotspot / baseHotspot;

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
            commonBoxes = workspace.summaryBoxes;
            haveCommon = true;
        } else if (!commonBoxes.cells.empty()) {
            intersectBoxes(commonBoxes, workspace.summaryBoxes);
        }
        safetyThisTileLeft -= prob;

        Structure::reverseDelta(structure, shapes, workspace.structureDelta);
        Basic::reverseDelta(basic, workspace.basicDelta);
        ObservedBoard::reverseDelta(board, updates);
    }

    winrate::CellId dominatingLocation = -1;
    if (sameSingleSafestTile && singleSafestTile != -1) {
        // 本格之后某格恒为最安全活格且更安全 → 它支配本格（Java 复杂支配）。
        const long double singleSafestSafety = 1.0L - probability.mineProbability(singleSafestTile, board, basic, structure);
        if (singleSafestSafety > safety)
            dominatingLocation = singleSafestTile;
    }
    // 唯一结局为死格，推荐时垫底；记录在全部分支计算完成后构造。
    return Candidate(cell, safety, secondarySafety * (1.0L + progressProb * cfg.progressContribution), expectedClears, validValues == 1, false, dominatingLocation, commonBoxes.cells);
}

winrate::Cells JavaEval::selectCandidates(const winrate::View &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // ── 3. 前沿盒（Java pe.getBestCandidates 的数据源）──
    std::vector<FrontierBox> &boxes = workspace.boxes;
    boxes.clear();
    const std::vector<winrate::Probability::Component> &componentProbabilities = probability.components();
    const std::vector<long double> &boxProbabilities = probability.boxProbabilities();
    for (int cid = 0; cid < (int)(structure.components.size()); ++cid) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[cid]);
        for (int bid = 0; bid < (int)(instance.boxes.count()); ++bid) {
            boxes.emplace_back(1.0L - boxProbabilities[componentProbabilities[cid].boxOffset + bid], structure.components[cid], bid);
        }
    }
    std::sort(boxes.begin(), boxes.end(), [](const FrontierBox &lhs, const FrontierBox &rhs) {
        return lhs.safety > rhs.safety;
    });

    // 原候选窗不启用根死格过滤；这里按盒安全度选取，deferGuessing 由单格数字分支评估决定。
    auto cellsOf = [&](const FrontierBox &box) {
        const Structure::Instance &instance = shapes.getInstance(box.instance);
        const std::span<const int> offsets = instance.boxes.boxOf.span(shapes.boxOf);
        return instance.boxes.cells.span(shapes.cells).subspan(offsets[box.box], offsets[box.box + 1] - offsets[box.box]);
    };

    // 没有 T 格时，离网候选集合为空，不能参与最高安全度比较。
    long double bestLivingSafety = basic.unknownSum != 0 ? 1.0L - probability.tCellProbability() : 0.0L;
    long double bestSafety = bestLivingSafety;

    if (!boxes.empty()) {
        bestLivingSafety = (std::max)(bestLivingSafety, boxes.front().safety);
        bestSafety = (std::max)(bestSafety, boxes.front().safety);
    }

    long double test = bestSafety == 1.0L ? 1.0L : bestLivingSafety - cfg.selectionThreshold1;
    long double test2 = bestSafety == 1.0L ? 1.0L : bestLivingSafety - cfg.selectionThreshold2;

    // 候选窗：优先最安全前沿盒，并保留原来的两个安全度门槛。
    std::vector<winrate::CellId> &onEdge = workspace.onEdge;
    onEdge.clear();
    for (const FrontierBox &box : boxes) {
        if (box.safety < test && (onEdge.size() >= 2 || box.safety < test2))
            break;
        test = (std::min)(test, box.safety);
        for (winrate::CellId cell : cellsOf(box)) onEdge.push_back(cell);
    }

    // 长期风险影响格（Java evaluateLocations 加入 tileOfInterestOn）。
    for (winrate::CellId cell : LongTermRisk::influencedTiles(risk, bestLivingSafety - cfg.selectionThreshold1, input, dead))
        if (!evaluateContains(onEdge, cell))
            onEdge.push_back(cell);

    // ── 5. 离网候选（Java evaluateOffEdgeCandidates；高密度门控在 Java 恒假，
    //    故偏移恒为 4 向）──
    std::vector<winrate::CellId> &offEdge = workspace.offEdge;
    offEdge.clear();
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y) {
            const winrate::CellId cell = board.id(x, y);
            if (board.board[x][y] == winrate::CellState::Hidden && basic.marks[x][y] == Basic::Mark::T && structure.cellLoc[cell].component == -1)
                offEdge.push_back(cell);
        }
    std::vector<winrate::CellId> &offEdgeCandidates = workspace.offEdgeCandidates;
    offEdgeCandidates.clear();
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
                const winrate::CellId cell = board.id(nx, ny);
                // Basic 的 T 已保证隐藏且无数字邻接，Structure 只纳入 H，与 offEdge 成员查询等价。
                if (basic.marks[nx][ny] == Basic::Mark::T && !evaluateContains(offEdgeCandidates, cell))
                    offEdgeCandidates.push_back(cell);
            };
            for (int x = 0; x < board.board.rows(); ++x)
                for (int y = 0; y < board.board.cols(); ++y) {
                    if (!isNumberState(board.board[x][y]))
                        continue; // Java 原始 witness
                    for (const std::pair<int, int> &offset : offsets) {
                        const auto [dx, dy] = offset;
                        addOffEdge(x + dx, y + dy);
                    }
                }
            for (winrate::CellId cell : offEdge) {
                const auto [x, y] = board.pos(cell);
                int adjacent = 0;
                forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
                    if (board.board[nx][ny] == winrate::CellState::Hidden)
                        ++adjacent;
                });
                if (adjacent > 1 && adjacent < 4 && !evaluateContains(offEdgeCandidates, cell))
                    offEdgeCandidates.push_back(cell);
            }
        }
    }

    // ── 6. SpaceCounter：小区域推迟，大区域优先（Java evaluateLocations）──
    std::vector<winrate::CellId> &notDeferred = workspace.notDeferred;
    notDeferred.clear();
    std::vector<winrate::CellId> &deferred = workspace.deferred;
    deferred.clear();
    auto partition = [&](const std::vector<winrate::CellId> &tiles) {
        for (winrate::CellId cell : tiles) {
            const auto [x, y] = board.pos(cell);
            if (meetsSpaceThreshold(x, y, board, basic, cfg.spaceThreshold, workspace))
                notDeferred.push_back(cell);
            else
                deferred.push_back(cell);
        }
    };
    partition(onEdge);
    partition(offEdgeCandidates);

    // 候选窗没有选中格时，沿原算法选择整盘最安全的隐藏格。
    if (notDeferred.empty() && deferred.empty()) {
        // -1 表示盘面已经没有可点击格。
        winrate::CellId safest = -1;
        long double safestSafety = 0.0L;
        for (int x = 0; x < board.board.rows(); ++x)
            for (int y = 0; y < board.board.cols(); ++y) {
                if (board.board[x][y] != winrate::CellState::Hidden)
                    continue;
                if (basic.marks[x][y] == Basic::Mark::F)
                    continue;
                const winrate::CellId cell = board.id(x, y);
                const long double safety = 1.0L - probability.mineProbability(cell, board, basic, structure);
                if (safest == -1 || safety > safestSafety) {
                    safestSafety = safety;
                    safest = cell;
                }
            }
        if (safest != -1) deferred.push_back(safest);
    }
    return notDeferred.empty() ? winrate::Cells(deferred) : winrate::Cells(notDeferred);
}

void JavaEval::evaluateCandidates(Scores &result, winrate::Cells cells, const winrate::Input &input, LongTermRisk::Analysis &risk, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune, bool allowDominated) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    std::vector<Candidate> &evaluated = result;
    // -1 表示还未评估任何候选；首格仍按原算法使用支配快路径。
    int best = -1;
    for (winrate::CellId cell : cells) {
        if (!workspace.observed[cell]) {
            workspace.observations[cell] = Observe::observe(board, basic, structure, shapes, probability, distributions, cell, workspace.probability);
            workspace.observed[cell] = 1;
        }
        Candidate candidate = evaluate(input, risk, cell, cfg, riskConfig, workspace, prune, allowDominated, best == -1 ? nullptr : &evaluated[best], workspace.observations[cell]);
        if (best == -1 || (!candidate.pruned && ((evaluated[best].deferGuessing && !candidate.deferGuessing) || (evaluated[best].deferGuessing == candidate.deferGuessing && candidate.weight > evaluated[best].weight)))) best = evaluated.size();
        evaluated.push_back(std::move(candidate));
    }
}

void JavaEval::selectRecommendation(Scores &result, const Config &cfg) {
    std::vector<Candidate> &evaluated = result;
    std::sort(evaluated.begin(), evaluated.end(), evalLess);
    const Candidate *chosen = &evaluated.front();
    const Candidate *alternative = findAlternativeMove(*chosen, evaluated, cfg);
    if (alternative) chosen = alternative;
    if (chosen->dominatingLocation != -1) {
        for (const Candidate &candidate : evaluated)
            if (!candidate.pruned && candidate.cell == chosen->dominatingLocation) {
                chosen = &candidate;
                break;
            }
    }
    std::rotate(evaluated.begin(), evaluated.begin() + (chosen - evaluated.data()), evaluated.begin() + (chosen - evaluated.data()) + 1);
}

void JavaEval::evaluateAutomatic(Scores &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune) {
    const auto &[board, basic, structure, probability, shapes, distributions] = input;
    result.clear();
    for (int x = 0; x < board.board.rows(); ++x)
        for (int y = 0; y < board.board.cols(); ++y)
            if (board.board[x][y] == winrate::CellState::Hidden && basic.marks[x][y] == Basic::Mark::S) {
                // 已知安全格直接推荐，不计算数字分支收益。
                result.emplace_back(board.id(x, y), 1.0L, 1.0L, 0.0L, false, false, -1, std::vector<winrate::CellId>{});
                return;
            }
    const int cellCount = board.board.rows() * board.board.cols();
    workspace.observations.resize(cellCount);
    workspace.observed.assign(cellCount, 0);
    LongTermRisk::Analysis &analysis = workspace.risk.analysis;
    Pseudo::Result &pseudo = workspace.pseudoResult;
    if (analysis.pseudos.empty()) Pseudo::update(pseudo, input, dead, pseudoConfig, workspace.pseudo);
    const std::vector<winrate::CellId> &pseudos = analysis.pseudos.empty() ? pseudo : analysis.pseudos;
    const winrate::Cells cells = !pseudos.empty() ? winrate::Cells(pseudos) : selectCandidates(input, risk, dead, cfg, workspace);
    // 没有可点击格是已完成盘面的正常结果。
    if (cells.empty()) return;
    // 自动候选不使用强制安全后的支配快路径；数字分支仍按 prune 裁剪。
    evaluateCandidates(result, cells, input, analysis, cfg, riskConfig, workspace, prune, false);
    selectRecommendation(result, cfg);
}

void JavaEval::exportScores(Result &result, const Scores &scores) {
    result.clear();
    result.reserve(scores.size());
    for (const Candidate &candidate : scores) result.emplace_back(candidate.cell, candidate.pruned ? -1.0L : candidate.weight);
}

void JavaEval::update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune) {
    evaluateAutomatic(workspace.scores, input, risk, dead, cfg, riskConfig, pseudoConfig, workspace, prune);
    exportScores(result, workspace.scores);
}

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune) {
    Result result;
    update(result, input, risk, dead, cfg, riskConfig, pseudoConfig, workspace, prune);
    return result;
}

void JavaEval::update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune) {
    const int cellCount = risk.rows() * risk.cols();
    workspace.observations.resize(cellCount);
    workspace.observed.assign(cellCount, 0);
    workspace.scores.clear();
    evaluateCandidates(workspace.scores, cells, input, workspace.risk.analysis, cfg, riskConfig, workspace, prune, prune);
    exportScores(result, workspace.scores);
}

JavaEval::Result JavaEval::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune) {
    Result result;
    update(result, input, risk, cells, cfg, riskConfig, workspace, prune);
    return result;
}
} // 命名空间 mss
