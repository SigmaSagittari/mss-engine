#include "move_evaluate.h"

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

void MoveEval::Selection::window(std::vector<winrate::CellId> &result, const winrate::View &input, const LongTermRisk::Result *risk, winrate::Cells offEdge, winrate::Cells witnesses, winrate::CellId firstOffEdge, const Config &cfg, Workspace &workspace) {
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
    if (risk) {
        const long double cutoffTally = (bestSafety - cfg.selectionThreshold1) * probability.candidates();
        for (int row = 0; row < board.board.rows(); ++row)
            for (int col = 0; col < board.board.cols(); ++col) {
                const long double influence = risk->influences[row][col];
                if (influence == 0.0L) continue;
                const winrate::CellId cell = board.id(row, col);
                const long double mineTally = probability.mineProbability(cell, board, basic, structure) * probability.candidates();
                if (probability.candidates() - mineTally + influence > cutoffTally && !contains(onEdge, cell)) onEdge.push_back(cell);
            }
    }

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
    result.clear();
    bool hasLarge = false;
    auto partition = [&](const std::vector<winrate::CellId> &tiles) {
        for (winrate::CellId cell : tiles) {
            const auto [x, y] = board.pos(cell);
            const bool large = meetsSpaceThreshold(x, y, input, cfg.spaceThreshold, workspace);
            // 首个大区域出现后舍弃已收集的小区域；两类候选内部的原顺序不变。
            if (large && !hasLarge) { result.clear(); hasLarge = true; }
            if (large || !hasLarge) result.push_back(cell);
        }
    };
    partition(onEdge);
    partition(offEdgeCandidates);

    // 候选窗没有选中格时，沿原算法选择整盘最安全的隐藏格。
    if (result.empty()) {
        // -1 表示盘面已经没有可点击格。
        winrate::CellId safest = firstOffEdge;
        long double safestSafety = firstOffEdge == -1 ? 0.0L : offEdgeSafetyValue;
        for (const FrontierBox &box : boxes)
            for (winrate::CellId cell : cellsOf(box))
                if (safest == -1 || box.safety > safestSafety || (box.safety == safestSafety && cell < safest)) {
                    safestSafety = box.safety;
                    safest = cell;
                }
        if (safest != -1) result.push_back(safest);
    }
}


MoveEval::Selection::Result MoveEval::Selection::specified(const winrate::Input &input, winrate::Cells cells, Probability::Workspace &probability) {
    Result result({cells.begin(), cells.end()}, {}, Reason::Score);
    result.observations.reserve(cells.size());
    for (winrate::CellId cell : cells) result.observations.push_back(Observe::observe(input.board, input.basic, input.structure, input.shapes, input.probability, input.distributions, cell, probability));
    return result;
}

MoveEval::Selection::Result MoveEval::Selection::find(const winrate::Input &input, const LongTermRisk::Result *risk, const Config &cfg, Workspace &workspace) {
    const auto &[board, basic, structure, probabilities, shapes, distributions, boardPool, basicPool] = input;
    workspace.offEdge.clear();
    workspace.witnesses.clear();
    winrate::CellId firstOffEdge = -1;
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            const winrate::CellId cell = board.id(row, col);
            if (board.board[row][col] <= winrate::CellState::Num8) workspace.witnesses.push_back(cell);
            else if (board.board[row][col] != winrate::CellState::ForcedMine) {
                if (probabilities.mineProbability(cell, board, basic, structure) == 0.0L) return Result({cell}, {}, Reason::Safe);
                if (basic.marks[row][col] == Basic::Mark::T) {
                    if (firstOffEdge == -1) firstOffEdge = cell;
                    if (basic.unknownSum < 30 || offEdgeCandidate(board, cell)) workspace.offEdge.push_back(cell);
                }
            }
        }
    Result result({}, {}, Reason::Score);
    find(result, input, risk, workspace.offEdge, workspace.witnesses, firstOffEdge, cfg, workspace);
    return result;
}

void MoveEval::Selection::find(Result &result, const winrate::Input &input, const LongTermRisk::Result *risk, winrate::Cells offEdge, winrate::Cells witnesses, winrate::CellId firstOffEdge, const Config &cfg, Workspace &workspace) {
    const auto &[board, basic, structure, probabilities, shapes, distributions, boardPool, basicPool] = input;
    result.cells.clear(); result.observations.clear(); result.reason = Reason::Score;
    const Pseudo::Kind kind = risk && !risk->pseudos.empty() ? risk->pseudoKind : Pseudo::update(result.cells, input, {}, cfg.pseudo, workspace.pseudo);
    if (risk && !risk->pseudos.empty()) result.cells = risk->pseudos;
    // 概率相同按格子编号排序，避免组件紧凑搬移改变等价局面的截断结果。
    if (result.cells.empty()) window(result.cells, input, risk, offEdge, witnesses, firstOffEdge, cfg, workspace);
    else result.reason = kind == Pseudo::Kind::Forced ? Reason::ForcedFiftyFifty : Reason::FiftyFiftyOrSafe;
    std::sort(result.cells.begin(), result.cells.end(), [&](winrate::CellId lhs, winrate::CellId rhs) {
        const long double left = probabilities.mineProbability(lhs, board, basic, structure), right = probabilities.mineProbability(rhs, board, basic, structure);
        return left != right ? left < right : lhs < rhs;
    });
}

void MoveEval::Selection::removeDead(Result &result, const winrate::Input &input, Probability::Workspace &probability) {
    const auto &[board, basic, structure, probabilities, shapes, distributions, boardPool, basicPool] = input;
    result.observations.clear();
    result.observations.reserve(result.cells.size());
    int count = 0;
    for (winrate::CellId cell : result.cells) {
        const Observe::Result observation = Observe::observe(board, basic, structure, shapes, probabilities, distributions, cell, probability);
        if (result.reason != Reason::Score || !isDead(observation)) { result.cells[count++] = cell; result.observations.push_back(observation); }
    }
    result.cells.resize(count);
    if (!result.cells.empty() || result.reason != Reason::Score) return;
    // 普通候选全 dead 时只选一个非必雷格，不再扩大候选寻找活格。
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col)
            if (board.board[row][col] == winrate::CellState::Hidden && probabilities.mineProbability(board.id(row, col), board, basic, structure) != 1.0L) {
                const winrate::CellId cell = board.id(row, col);
                result.cells.push_back(cell);
                result.observations.push_back(Observe::observe(board, basic, structure, shapes, probabilities, distributions, cell, probability));
                result.reason = Reason::BlindGuess;
                return;
            }
    result.reason = Reason::BlindGuess;
}

MoveEval::Summary MoveEval::shortTerm(const winrate::View &input, int remainingSafe) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // 未揭示的 S = 总格数 - 已揭示数 - F - T - H；H 按 Box 大小扣除。
    int known = board.totalMines + remainingSafe - basic.mineSum - basic.unknownSum;
    int clears = 0;
    long double best = basic.unknownSum != 0 ? 1.0L - probability.tCellProbability() : 0.0L;
    long double second = best;
    auto add = [&](long double safety) {
        if (safety > best) { second = best; best = safety; }
        else if (safety > second) second = safety;
    };
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        for (int box = 0; box < (int)instance.boxes.count(); ++box) {
            const int count = instance.boxes.cellCount(shapes, box);
            const long double safety = 1.0L - probability.boxProbability(component, box);
            known -= count;
            if (safety == 1.0L) clears += count;
            add(safety);
            if (count > 1) add(safety);
        }
    }
    if (known != 0) add(1.0L);
    if (known > 1) add(1.0L);
    clears += known;
    // T 格并不在 frontierCells 中；零雷时它们同样能支持安全步截断。
    if (basic.unknownSum != 0 && probability.tCellProbability() == 0.0L) clears += basic.unknownSum;
    // 安全格已全部揭开就是获胜；空后续候选的价值为 1。
    return Summary(remainingSafe == 0 ? 1.0L : (4.0L * best + second) / 5.0L, clears);
}

MoveEval::Branch::Branch(int digit, int clears, long double value, long double probability, ObservedBoard::Delta boardDelta, Basic::Delta basicDelta, Structure::Delta structureDelta) : digit(digit), clears(clears), value(value), probability(probability), boardDelta(boardDelta), basicDelta(basicDelta), structureDelta(structureDelta) {}

bool MoveEval::Selection::isDead(const Observe::Result &observation) {
    int digits = 0;
    for (int digit = 0; digit <= 8; ++digit)
        if (observation.probability[digit] != 0.0L) ++digits;
    return digits == 1;
}

bool MoveEval::Selection::offEdgeCandidate(const winrate::Board &board, winrate::CellId cell) {
    const auto [x, y] = board.pos(cell);
    int adjacent = 0;
    forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) { if (board.board[nx][ny] == winrate::CellState::Hidden) ++adjacent; });
    return adjacent > 1 && adjacent < 4;
}

MoveEval::Cursor::Cursor(winrate::CellId cell, Observe::Result observation, long double influence, bool safeSearch, long double candidates) : analysis(cell, 0.0L), observation(observation), influence(influence), safeSearch(safeSearch), digit(0), progress(0.0L), remainingSafety(1.0L - observation.probability[9]), candidates(candidates) {
    if (safeSearch || remainingSafety == 0.0L) linkedClears = 0;
}

void MoveEval::Cursor::reset(winrate::CellId cell, Observe::Result observation, long double influence, bool safeSearch, long double candidates) {
    analysis.cellId = cell; analysis.value = 0.0L; analysis.multiplier = 1.0L; analysis.equivalence.reset(); analysis.node = 0; analysis.branches.clear();
    this->observation = observation; this->influence = influence; this->safeSearch = safeSearch; this->candidates = candidates;
    digit = 0; progress = 0.0L; remainingSafety = 1.0L - observation.probability[9]; linkedClears.reset();
    if (safeSearch || remainingSafety == 0.0L) linkedClears = 0;
    alternatives.clear(); peers = {};
}

MoveEval::Cursor MoveEval::begin(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::CellId cell, const Observe::Result &observation, const Config &cfg, Workspace &workspace) {
    if (observation.probability[9] == 1.0L) return Cursor(cell, observation, 1.0L, false, input.probability.candidates());
    const long double tally = LongTermRisk::influence(cell, input, risk, cfg.longTermRisk, workspace.longTermRisk);
    const long double safetyTally = input.probability.candidates() - observation.probability[9] * input.probability.candidates();
    const long double influence = (safetyTally + cfg.influenceScale * tally) / safetyTally;
    return Cursor(cell, observation, influence, false, input.probability.candidates());
}

void MoveEval::prepareAlternatives(Cursor &cursor, std::span<const std::pair<long double, winrate::CellId>> peers) {
    cursor.peers = peers;
    for (const std::pair<long double, winrate::CellId> &peer : peers)
        if (peer.first < cursor.observation.probability[9]) cursor.alternatives.push_back(peer.second);
}

long double MoveEval::optimistic(const Cursor &cursor, const Config &cfg) {
    const long double bonus = 1.0L + (cursor.progress + cursor.remainingSafety) * cfg.progressContribution;
    return (cursor.analysis.value + cursor.remainingSafety) * cursor.influence * bonus;
}

bool MoveEval::advance(Cursor &cursor, winrate::Board &board, winrate::Basic &basic, winrate::Structure &structure, winrate::Shapes &shapes, winrate::Distributions &distributions, ObservedBoard::Pool &boardPool, Basic::Pool &basicPool, int remainingSafe, int forcedSafes, const Config &cfg, Workspace &workspace) {
    const winrate::CellId cell = cursor.analysis.cellId;
    const auto [row, col] = board.pos(cell);
    while (cursor.digit <= 8 && cursor.observation.probability[cursor.digit] == 0.0L) ++cursor.digit;
    if (cursor.linkedClears && cursor.digit == 9) {
        ++cursor.analysis.node;
        return true;
    }
    const bool forced = !cursor.linkedClears;
    ObservedBoard::Delta delta(boardPool.changes.push_back());
    delta.changes.push_back(boardPool.changes, {cell, board.board[row][col], cursor.linkedClears ? (winrate::CellState)cursor.digit : winrate::CellState::ForcedSafe});
    ObservedBoard::applyDelta(board, delta, boardPool);
    const Basic::Delta basicDelta = Basic::update(basic, board, delta, boardPool, basicPool, workspace.basic);
    const Structure::Delta structureDelta = Structure::update(structure, board, basic, shapes, std::span{&cell, 1}, workspace.structure);
    if (!workspace.nextProbability) workspace.nextProbability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.probability);
    else Probability::analyze(board, basic, structure, shapes, distributions, *workspace.nextProbability, workspace.probability);
    const winrate::Probability &next = *workspace.nextProbability;
    const winrate::View nextInput(board, basic, structure, next, shapes);
    const Summary summary = shortTerm(nextInput, remainingSafe - (forced ? 0 : 1));
    const int offEdgeClears = basic.unknownSum != 0 && next.tCellProbability() == 0.0L ? basic.unknownSum : 0;
    if (forced) {
        cursor.linkedClears = summary.clears - offEdgeClears - forcedSafes - (delta.changes.span(boardPool.changes).front().previous == winrate::CellState::ForcedSafe ? 0 : 1);
        // 本招必在自己的必安子集中；两个子集相同就证明双方互相必安。
        if (!cursor.peers.empty()) {
            U128Hasher hash;
            for (const std::pair<long double, winrate::CellId> &peer : cursor.peers)
                if (next.mineProbability(peer.second, board, basic, structure) == 0.0L) hash.mix(peer.second);
            cursor.analysis.equivalence = hash.finalize();
        }
    } else {
        std::erase_if(cursor.alternatives, [&](winrate::CellId alternative) { return next.mineProbability(alternative, board, basic, structure) != 0.0L; });
        const long double probabilityDigit = cursor.observation.probability[cursor.digit];
        cursor.analysis.value += probabilityDigit * summary.blendedSafety;
        // T 零雷格用于安全步截断；Java 原进度奖励只计已知安全格和前沿安全格。
        if (summary.clears - offEdgeClears > *cursor.linkedClears) cursor.progress += probabilityDigit;
        cursor.remainingSafety -= probabilityDigit;
        cursor.analysis.branches.emplace_back(cursor.digit, summary.clears, summary.blendedSafety, next.candidates() / cursor.candidates, delta, basicDelta, structureDelta);
        ++cursor.digit;
    }
    Structure::applyDelta(structure, shapes, structureDelta, true);
    Basic::applyDelta(basic, basicDelta, basicPool, true);
    ObservedBoard::applyDelta(board, delta, boardPool, true);
    if (forced) {
        shapes.removed.pop_back(structureDelta.removed);
        shapes.added.pop_back(structureDelta.added);
        basicPool.changes.pop_back(basicDelta.changes);
        boardPool.changes.pop_back(delta.changes);
    }
    while (cursor.digit <= 8 && cursor.observation.probability[cursor.digit] == 0.0L) ++cursor.digit;
    if (cursor.digit != 9) return false;
    const long double bonus = 1.0L + (cursor.safeSearch ? 0.0L : cursor.progress * cfg.progressContribution);
    cursor.analysis.multiplier = cursor.influence * bonus;
    cursor.analysis.value *= cursor.analysis.multiplier;
    ++cursor.analysis.node;
    return true;
}

} // 命名空间 mss
