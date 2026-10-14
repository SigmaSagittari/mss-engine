#include "move_evaluate.h"
#include "java_lite.h"

#include <algorithm>
#include <array>

#include "core/utility/neighbors.h"

namespace mss {

bool MoveEval::Selection::contains(winrate::Cells cells, winrate::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

bool MoveEval::Selection::meetsSpaceThreshold(int startX, int startY, const winrate::View &input, int threshold, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // 概率引擎确认的雷也不可连通；仅查 Basic 会把尚未标 F 的必雷算进区域。
    auto available = [&](int row, int col) { return board.board[row][col] == winrate::CellState::Hidden && probability.mineProbability(board.id(row, col), board, basic, structure) != 1.0L; };
    RawGrid<unsigned> &visited = workspace.visited;
    std::vector<std::pair<int, int>> &stack = workspace.stack;
    if (visited.rows() != board.board.rows() || visited.cols() != board.board.cols()) visited.resize(board.board.rows(), board.board.cols(), 0);
    // 每次查询换访问序号；仅序号溢出时清除历史标记。
    if (++workspace.visit == 0) { visited.fill(0); ++workspace.visit; }
    const unsigned visit = workspace.visit;
    stack.clear();
    stack.emplace_back(startX, startY);
    visited[startX][startY] = visit;
    int count = 0;
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        ++count;
        if (count >= threshold)
            return true;
        forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
            if (visited[nx][ny] == visit)
                return;
            if (available(nx, ny)) {
                visited[nx][ny] = visit;
                stack.emplace_back(nx, ny);
                return;
            }
            if (board.board[nx][ny] <= winrate::CellState::Num8) { // 已揭示 → 可跳过继续找连通区
                visited[nx][ny] = visit;
                forEachAdjacent(nx, ny, board.board.rows(), board.board.cols(), [&](int nx2, int ny2) {
                    if (visited[nx2][ny2] != visit && available(nx2, ny2)) {
                        visited[nx2][ny2] = visit;
                        stack.emplace_back(nx2, ny2);
                    }
                });
            }
        });
    }
    return count >= threshold;
}

winrate::Cells MoveEval::Selection::select(const winrate::View &input, const LongTermRisk::Result *risk, winrate::Cells dead, winrate::Cells offEdge, winrate::Cells witnesses, winrate::CellId firstOffEdge, const Config &cfg, Workspace &workspace) {
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

    // 先按盒安全度准备候选窗，随后用观测分布排除死格。
    auto cellsOf = [&](const FrontierBox &box) {
        const Structure::Instance &instance = shapes.getInstance(box.instance);
        const std::span<const int> offsets = instance.boxes.boxOf.span(shapes.boxOf);
        return instance.boxes.cells.span(shapes.cells).subspan(offsets[box.box], offsets[box.box + 1] - offsets[box.box]);
    };

    // 没有 T 格时，离网候选集合为空，不能参与最高安全度比较。
    long double bestSafety = basic.unknownSum != 0 ? 1.0L - probability.tCellProbability() : 0.0L;

    if (!boxes.empty())
        bestSafety = (std::max)(bestSafety, boxes.front().safety);

    long double test = bestSafety == 1.0L ? 1.0L : bestSafety - cfg.selectionThreshold1;
    long double test2 = bestSafety == 1.0L ? 1.0L : bestSafety - cfg.selectionThreshold2;

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
    if (risk)
        for (winrate::CellId cell : LongTermRisk::influencedTiles(*risk, bestSafety - cfg.selectionThreshold1, input, dead))
            if (!contains(onEdge, cell))
                onEdge.push_back(cell);

    // ── 5. 离网候选（Java evaluateOffEdgeCandidates；高密度门控在 Java 恒假，
    //    故偏移恒为 4 向）──
    std::vector<winrate::CellId> &offEdgeCandidates = workspace.offEdgeCandidates;
    offEdgeCandidates.clear();
    const long double offEdgeSafetyValue = 1.0L - probability.tCellProbability();
    if (bestSafety != 1.0L && offEdgeSafetyValue > bestSafety * 0.95L) {
        if (basic.unknownSum < 30) {
            for (winrate::CellId cell : offEdge) {
                const auto [x, y] = board.pos(cell);
                if (basic.marks[x][y] == Basic::Mark::T) offEdgeCandidates.push_back(cell);
            }
        } else {
            // Java OFFSETS = {{2,0},{-2,0},{0,2},{0,-2}}（高密度门控恒假）。
            constexpr std::array<std::pair<int, int>, 4> offsets{std::pair{2, 0}, std::pair{-2, 0}, std::pair{0, 2}, std::pair{0, -2}};
            auto addOffEdge = [&](int nx, int ny) {
                if (nx < 0 || nx >= board.board.rows() || ny < 0 || ny >= board.board.cols())
                    return;
                const winrate::CellId cell = board.id(nx, ny);
                // Basic 的 T 已保证隐藏且无数字邻接，Structure 只纳入 H，与 offEdge 成员查询等价。
                if (basic.marks[nx][ny] == Basic::Mark::T && !contains(offEdgeCandidates, cell))
                    offEdgeCandidates.push_back(cell);
            };
            for (winrate::CellId cell : witnesses) {
                const auto [x, y] = board.pos(cell);
                for (const std::pair<int, int> &offset : offsets) {
                    const auto [dx, dy] = offset;
                    addOffEdge(x + dx, y + dy);
                }
            }
            for (winrate::CellId cell : offEdge) {
                const auto [x, y] = board.pos(cell);
                if (basic.marks[x][y] == Basic::Mark::T && !contains(offEdgeCandidates, cell))
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
            if (meetsSpaceThreshold(x, y, input, cfg.spaceThreshold, workspace))
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
        winrate::CellId safest = firstOffEdge;
        long double safestSafety = firstOffEdge == -1 ? 0.0L : offEdgeSafetyValue;
        for (const FrontierBox &box : boxes)
            for (winrate::CellId cell : cellsOf(box))
                if (safest == -1 || box.safety > safestSafety || (box.safety == safestSafety && cell < safest)) {
                    safestSafety = box.safety;
                    safest = cell;
                }
        if (safest != -1) deferred.push_back(safest);
    }
    return notDeferred.empty() ? winrate::Cells(deferred) : winrate::Cells(notDeferred);
}


// 未指定候选时计入全部热点；指定候选时忽略其豁免热点。
// 热点惩罚为 (1 + P(安全)) × 0.5 的连乘。
long double MoveEval::hotspotSafety(const LongTermRisk::Result &risk, winrate::CellId candidate, const winrate::View &input) {
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
bool MoveEval::hotspotExempt(const LongTermRisk::Result &risk, winrate::CellId cell) {
    for (const LongTermRisk::RiskHotspot &hotspot : risk.hotspots)
        if (hotspot.isExempt(cell))
            return true;
    return false;
}

bool MoveEval::isDead(const Observe::Result &observation) {
    int digits = 0;
    for (int digit = 0; digit <= 8; ++digit)
        if (observation.probability[digit] != 0.0L) ++digits;
    return digits == 1;
}

bool MoveEval::offEdgeCandidate(const winrate::Board &board, winrate::CellId cell) {
    const auto [x, y] = board.pos(cell);
    int adjacent = 0;
    forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) { if (board.board[nx][ny] == winrate::CellState::Hidden) ++adjacent; });
    return adjacent > 1 && adjacent < 4;
}

MoveEval::Cursor::Cursor(winrate::CellId cell, Observe::Result observation, long double influence, long double baseHotspot, bool safeSearch, long double candidates) : analysis(cell, 0.0L), observation(observation), influence(influence), baseHotspot(baseHotspot), safeSearch(safeSearch), digit(0), progress(0.0L), remainingSafety(1.0L - observation.probability[9]), candidates(candidates) {
    if (safeSearch || remainingSafety == 0.0L) linkedClears = 0;
}

MoveEval::Cursor MoveEval::begin(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::CellId cell, const Observe::Result &observation, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace) {
    if (observation.probability[9] == 1.0L) return Cursor(cell, observation, 1.0L, 1.0L, false, input.probability.candidates());
    const long double tally = LongTermRisk::influence(cell, input, risk, riskConfig, workspace.risk);
    const long double safetyTally = input.probability.candidates() - observation.probability[9] * input.probability.candidates();
    const long double influence = hotspotExempt(risk, cell) ? 1.0L : (safetyTally + cfg.influenceScale * tally) / safetyTally;
    return Cursor(cell, observation, influence, hotspotSafety(risk, -1, input), false, input.probability.candidates());
}

void MoveEval::prepareAlternatives(Cursor &cursor, winrate::Cells cells, std::span<const std::pair<long double, winrate::CellId>> peers, bool prune) {
    cursor.peers = peers;
    if (prune)
        for (const std::pair<long double, winrate::CellId> &peer : peers)
            if (peer.first < cursor.observation.probability[9] && std::find(cells.begin(), cells.end(), peer.second) != cells.end()) cursor.alternatives.push_back(peer.second);
}

long double MoveEval::optimistic(const Cursor &cursor, const Config &cfg) {
    const long double bonus = 1.0L + (cursor.progress + cursor.remainingSafety) * cfg.progressContribution;
    return (cursor.analysis.value + cursor.remainingSafety * cursor.influence / cursor.baseHotspot) * bonus;
}

bool MoveEval::advance(Cursor &cursor, winrate::Board &board, winrate::Basic &basic, winrate::Structure &structure, winrate::Shapes &shapes, winrate::Distributions &distributions, int remainingSafe, int forcedSafes, const LongTermRisk::Result *risk, const Config &cfg, Workspace &workspace) {
    const winrate::CellId cell = cursor.analysis.cellId;
    const auto [row, col] = board.pos(cell);
    while (cursor.digit <= 8 && cursor.observation.probability[cursor.digit] == 0.0L) ++cursor.digit;
    if (cursor.linkedClears && cursor.digit == 9) return true;
    const bool forced = !cursor.linkedClears;
    ObservedBoard::Delta &delta = workspace.boardDelta;
    delta.clear();
    delta.changes.emplace_back(cell, board.board[row][col], cursor.linkedClears ? (winrate::CellState)cursor.digit : winrate::CellState::ForcedSafe);
    ObservedBoard::applyDelta(board, delta);
    Basic::update(basic, workspace.basicDelta, board, delta, workspace.basic);
    Structure::update(structure, workspace.structureDelta, board, basic, shapes, std::span{&cell, 1}, workspace.structure);
    if (!workspace.nextProbability) workspace.nextProbability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability);
    else Probability::analyze(board, basic, structure, shapes, distributions, *workspace.nextProbability, workspace.probability);
    ++cursor.analysis.node;
    const winrate::Probability &next = *workspace.nextProbability;
    const winrate::View nextInput(board, basic, structure, next, shapes);
    const JavaLite::Summary summary = JavaLite::evaluate(nextInput, remainingSafe - (forced ? 0 : 1));
    const int offEdgeClears = basic.unknownSum != 0 && next.tCellProbability() == 0.0L ? basic.unknownSum : 0;
    if (forced) {
        cursor.linkedClears = summary.clears - offEdgeClears - forcedSafes - (delta.changes.front().previous == winrate::CellState::ForcedSafe ? 0 : 1);
        // 本招必在自己的必安子集中；两个子集相同就证明双方互相必安。
        U128Hasher hash;
        for (const std::pair<long double, winrate::CellId> &peer : cursor.peers)
            if (next.mineProbability(peer.second, board, basic, structure) == 0.0L) hash.mix(peer.second);
        cursor.analysis.equivalence = hash.finalize();
    } else {
        std::erase_if(cursor.alternatives, [&](winrate::CellId alternative) { return next.mineProbability(alternative, board, basic, structure) != 0.0L; });
        const long double probabilityDigit = cursor.observation.probability[cursor.digit];
        const long double weight = probabilityDigit * (risk ? cursor.influence * hotspotSafety(*risk, cell, nextInput) / cursor.baseHotspot : 1.0L);
        cursor.analysis.value += weight * summary.blendedSafety;
        // T 零雷格用于安全步截断；Java 原进度奖励只计已知安全格和前沿安全格。
        if (summary.clears - offEdgeClears > *cursor.linkedClears) cursor.progress += probabilityDigit;
        cursor.remainingSafety -= probabilityDigit;
        cursor.analysis.branches.emplace_back(cursor.digit, summary.clears, summary.blendedSafety, next.candidates() / cursor.candidates, delta, workspace.basicDelta, workspace.structureDelta);
        ++cursor.digit;
    }
    Structure::applyDelta(structure, shapes, workspace.structureDelta, true);
    Basic::applyDelta(basic, workspace.basicDelta, true);
    ObservedBoard::applyDelta(board, delta, true);
    while (cursor.digit <= 8 && cursor.observation.probability[cursor.digit] == 0.0L) ++cursor.digit;
    if (cursor.digit != 9) return false;
    const long double bonus = 1.0L + (cursor.safeSearch ? 0.0L : cursor.progress * cfg.progressContribution);
    cursor.analysis.value *= bonus;
    return true;
}

} // 命名空间 mss
