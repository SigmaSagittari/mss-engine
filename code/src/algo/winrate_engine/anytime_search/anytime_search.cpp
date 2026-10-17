#include "anytime_search.h"

#include <algorithm>
#include <numeric>

#include "core/assert.h"
#include "core/utility/neighbors.h"
#include "core/utility/vector_pool.h"

namespace mss {

struct AnytimeSearch::State {
    static bool interchangeable(winrate::CellId lhs, winrate::CellId rhs, const winrate::View &input) {
        const auto &[board, basic, structure, probability, shapes] = input;
        const auto [lx, ly] = board.pos(lhs);
        const auto [rx, ry] = board.pos(rhs);
        if (lx + 1 < rx || rx + 1 < lx || ly + 1 < ry || ry + 1 < ly) return false;
        const Structure::Result::CellLocation &left = structure.cellLoc[lhs], &right = structure.cellLoc[rhs];
        // 同一约束盒（或同为 T）保证雷位可交换；仅安全率相同不能证明这一点。
        if (left.component != right.component || left.box != right.box || basic.marks[lx][ly] != basic.marks[rx][ry]) return false;
        auto matches = [&](int x, int y, int otherX, int otherY, winrate::CellId other) {
            bool same = true;
            forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
                const winrate::CellId cell = board.id(nx, ny);
                // 概率查询包含 Basic 已确认的雷；只比较互换两格以外、仍可揭开的隐藏邻格。
                if (cell == other || board.board[nx][ny] <= winrate::CellState::Num8 || probability.mineProbability(cell, board, basic, structure) == 1.0L) return;
                if (nx + 1 < otherX || otherX + 1 < nx || ny + 1 < otherY || otherY + 1 < ny) same = false;
            });
            return same;
        };
        return matches(lx, ly, rx, ry, rhs) && matches(rx, ry, lx, ly, lhs);
    }

    static void prune(std::vector<MoveEval::Analysis> &analyses, const winrate::Input &input, int width, std::vector<U128> &identities) {
        for (int i = 0; i < (int)analyses.size(); ++i)
            for (int j = i + 1; j < (int)analyses.size();)
                if (interchangeable(analyses[i].cellId, analyses[j].cellId, input)) {
                    // 完整乘数包含 influence，必须评分后才选代表；相同时保留小编号。
                    if (analyses[j].multiplier > analyses[i].multiplier || (analyses[j].multiplier == analyses[i].multiplier && analyses[j].cellId < analyses[i].cellId)) std::swap(analyses[i], analyses[j]);
                    analyses.erase(analyses.begin() + j);
                } else ++j;
        std::stable_sort(analyses.begin(), analyses.end(), [](const MoveEval::Analysis &lhs, const MoveEval::Analysis &rhs) { return lhs.value > rhs.value; });
        identities.clear();
        // 对换剪枝后按分数保留等价类代表，再计宽度；身份来自剪枝前的完整候选池。
        std::erase_if(analyses, [&](const MoveEval::Analysis &analysis) {
            if (!analysis.equivalence) return false;
            if (std::find(identities.begin(), identities.end(), *analysis.equivalence) != identities.end()) return true;
            identities.push_back(*analysis.equivalence);
            return false;
        });
        if ((int)analyses.size() > width) analyses.erase(analyses.begin() + width, analyses.end());
    }

    struct Move {
        winrate::CellId cellId;
        vectorPool<MoveEval::Branch>::vector branches;
        // 子节点存 nodes 下标，-1 表示尚未创建。
        vectorPool<int>::vector children;
        long double value;
        bool finished;
        Move(winrate::CellId cellId, vectorPool<MoveEval::Branch>::vector branches, vectorPool<int>::vector children, long double value) : cellId(cellId), branches(branches), children(children), value(value), finished(branches.size == 0) {}
    };
    struct Node {
        vectorPool<Move>::vector moves;
        bool ready;
        int depth;
        Node() : moves(0, 0), ready(false), depth(0) {}
        bool finished(const vectorPool<Move> &pool) const {
            const std::span<const Move> candidates = moves.span(pool);
            return ready && std::all_of(candidates.begin(), candidates.end(), [](const Move &move) { return move.finished; });
        }
        long double best(const vectorPool<Move> &pool) const {
            const std::span<const Move> candidates = moves.span(pool);
            return std::max_element(candidates.begin(), candidates.end(), [](const Move &lhs, const Move &rhs) { return lhs.value < rhs.value; })->value;
        }
    };
    struct Frame {
        int node;
        int horizon;
        std::vector<int> order;
        int move;
        int branch;
        long double value;
        bool settled;
        // 新节点准备时保存真实布局数；工作概率随后会被结局计算覆写。
        std::optional<long double> candidates;
        MoveEval::Selection::Result selection;
        // 固定候选池；先评分，再按宽度保留最高分招法。
        std::vector<std::pair<long double, winrate::CellId>> pending;
        // 候选先在临时区排序截断，定稿后一次写入三个池，避免修改非尾部切片。
        std::vector<MoveEval::Analysis> selected;
        int selectedCount;
        int next;
        std::optional<MoveEval::Cursor> cursor;
        bool evaluating;
        bool safe;
        // 根帧入边为空切片；其余帧保存一条分支的池下标，扩容后重新取 Delta。
        vectorPool<MoveEval::Branch>::vector transition;
        Frame(int node, int horizon, vectorPool<MoveEval::Branch>::vector transition) : node(node), horizon(horizon), move(0), branch(0), value(0.0L), settled(true), selection({}, {}, MoveEval::Selection::Reason::Score), selectedCount(0), next(0), evaluating(false), safe(false), transition(transition) {}
        void reset(int node, int horizon, vectorPool<MoveEval::Branch>::vector transition) {
            this->node = node; this->horizon = horizon; this->transition = transition;
            move = branch = selectedCount = next = 0; value = 0.0L; settled = true; evaluating = safe = false;
            candidates.reset(); selection.cells.clear(); selection.observations.clear(); pending.clear();
        }
        void orderMoves(const Node &current, const vectorPool<Move> &pool) {
            const std::span<const Move> candidates = current.moves.span(pool);
            order.resize(candidates.size());
            std::iota(order.begin(), order.end(), 0);
            // 一层开始时冻结顺序；候选发布新分数不会改变本层游标。
            std::sort(order.begin(), order.end(), [&](int lhs, int rhs) { return candidates[lhs].value != candidates[rhs].value ? candidates[lhs].value > candidates[rhs].value : lhs < rhs; });
        }
    };

    winrate::Board board;
    winrate::Basic basic;
    winrate::Structure structure;
    winrate::Shapes &shapes;
    winrate::Distributions &distributions;
    ObservedBoard::Pool &boardPool;
    mss::Basic::Pool &basicPool;
    int remainingSafe;
    std::vector<winrate::CellId> rootBasicSafe;
    std::vector<winrate::CellId> rootOffEdge;
    std::vector<winrate::CellId> rootOffEdgeCandidates;
    std::vector<winrate::CellId> rootWitnesses;
    std::vector<Node> nodes;
    vectorPool<Move> moves;
    vectorPool<MoveEval::Branch> branches;
    vectorPool<int> children;
    std::vector<Frame> frames;
    // 仅前 frameCount 个槽属于当前路径；退出帧保留容量，重用时覆写状态。
    int frameCount;
    std::optional<winrate::Probability> probability;

    State(const winrate::Input &input, std::vector<MoveEval::Analysis> analyses) : board(input.board), basic(input.basic), structure(input.structure), shapes(input.shapes), distributions(input.distributions), boardPool(input.boardPool), basicPool(input.basicPool), remainingSafe(board.board.rows() * board.board.cols() - board.totalMines), frameCount(0) {
        for (int row = 0; row < board.board.rows(); ++row)
            for (int col = 0; col < board.board.cols(); ++col)
                if (board.board[row][col] <= winrate::CellState::Num8) { --remainingSafe; rootWitnesses.push_back(board.id(row, col)); }
                else {
                    if (basic.marks[row][col] == Basic::Mark::S) rootBasicSafe.push_back(board.id(row, col));
                    else if (basic.marks[row][col] == Basic::Mark::T) {
                        rootOffEdge.push_back(board.id(row, col));
                        // 以后每次揭示会把邻接 T 移入 H，仍为 T 的格子不改变隐藏邻格数。
                        if (MoveEval::Selection::offEdgeCandidate(board, board.id(row, col))) rootOffEdgeCandidates.push_back(board.id(row, col));
                    }
                }
        nodes.emplace_back();
        store(nodes.front(), analyses);
        pushFrame(0, 1, vectorPool<MoveEval::Branch>::vector(0, 0));
        frames[frameCount - 1].orderMoves(nodes.front(), moves);
    }

    void pushFrame(int node, int horizon, vectorPool<MoveEval::Branch>::vector transition) {
        if (frameCount == (int)frames.size()) frames.emplace_back(node, horizon, transition);
        else frames[frameCount].reset(node, horizon, transition);
        ++frameCount;
    }

    void store(Node &node, std::span<const MoveEval::Analysis> analyses) {
        node.moves = moves.push_back();
        for (const MoveEval::Analysis &analysis : analyses) {
            vectorPool<MoveEval::Branch>::vector outcomes = branches.push_back();
            vectorPool<int>::vector next = children.push_back();
            for (const MoveEval::Branch &branch : analysis.branches) {
                outcomes.push_back(branches, branch);
                next.push_back(children, -1);
            }
            node.moves.push_back(moves, Move(analysis.cellId, outcomes, next, analysis.value));
        }
        node.ready = true;
    }

    winrate::Input input(Workspace &workspace) {
        // 只在准备新节点或新候选时重建工作概率；旧节点重访只重放 Delta。
        if (!probability) probability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.javaEval.moveEval.probability);
        else Probability::analyze(board, basic, structure, shapes, distributions, *probability, workspace.javaEval.moveEval.probability);
        return winrate::Input(board, basic, structure, *probability, shapes, distributions, boardPool, basicPool);
    }

    void prepare(Frame &frame, const Config &cfg, Workspace &workspace) {
        const winrate::Input current = input(workspace);
        frame.candidates = current.probability.candidates();
        MoveEval::Workspace &evaluation = workspace.javaEval.moveEval;
        winrate::CellId safe = -1;
        auto consider = [&](winrate::CellId cell) { if (safe == -1 || cell < safe) safe = cell; };
        auto basicSafe = [&](winrate::CellId cell) {
            const auto [row, col] = board.pos(cell);
            if (board.board[row][col] > winrate::CellState::Num8 && basic.marks[row][col] == Basic::Mark::S) consider(cell);
        };
        for (winrate::CellId cell : rootBasicSafe) basicSafe(cell);
        // 分支新增的 Basic 安全格都在路径 Delta 中；只筛掉已经揭开的项。
        for (int i = 0; i < frameCount; ++i) {
            const Frame &path = frames[i];
            if (path.transition.size != 0) for (const Basic::Delta::Change &change : path.transition.span(branches).front().basicDelta.changes.span(basicPool.changes)) basicSafe(change.cell);
        }
        for (int component = 0; component < (int)structure.components.size(); ++component) {
            const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
            const std::span<const int> offsets = instance.boxes.boxOf.span(shapes.boxOf);
            const winrate::Cells cells = instance.boxes.cells.span(shapes.cells);
            for (int box = 0; box < (int)instance.boxes.count(); ++box)
                if (current.probability.boxProbability(component, box) == 0.0L)
                    for (int i = offsets[box]; i < offsets[box + 1]; ++i) consider(cells[i]);
        }
        if (basic.unknownSum != 0 && current.probability.tCellProbability() == 0.0L)
            for (winrate::CellId cell : rootOffEdge) {
                const auto [row, col] = board.pos(cell);
                if (basic.marks[row][col] == Basic::Mark::T) { consider(cell); break; }
            }
        // 与原逐格扫描一样，始终选择行优先顺序的第一个安全格。
        if (safe != -1) frame.selection.cells.push_back(safe);
        frame.safe = !frame.selection.cells.empty();
        if (frame.safe) {
            frame.selection.reason = MoveEval::Selection::Reason::Safe;
            MoveEval::Selection::removeDead(frame.selection, current, evaluation.probability);
        } else {
            std::vector<winrate::CellId> &witnesses = workspace.witnesses;
            witnesses = rootWitnesses;
            for (int i = 0; i < frameCount; ++i) {
                const Frame &path = frames[i];
                if (path.transition.size != 0) for (const ObservedBoard::Delta::Change &change : path.transition.span(branches).front().boardDelta.changes.span(boardPool.changes)) witnesses.push_back(change.cell);
            }
            std::sort(witnesses.begin(), witnesses.end());
            winrate::CellId firstOffEdge = -1;
            for (winrate::CellId cell : rootOffEdge) {
                const auto [row, col] = board.pos(cell);
                if (basic.marks[row][col] == Basic::Mark::T) { firstOffEdge = cell; break; }
            }
            MoveEval::Selection::find(frame.selection, current, nullptr, basic.unknownSum < 30 ? rootOffEdge : rootOffEdgeCandidates, witnesses, firstOffEdge, cfg.javaEval.selection, workspace.javaEval.selection);
            MoveEval::Selection::removeDead(frame.selection, current, evaluation.probability);
        }
        // pending 必须保留去重前的完整集合，强制安全签名与安全支配都以它为输入。
        for (int i = 0; i < (int)frame.selection.cells.size(); ++i) frame.pending.emplace_back(frame.selection.observations[i].probability[9], frame.selection.cells[i]);
        int count = 0;
        for (int i = 0; i < (int)frame.selection.cells.size(); ++i)
            if (std::none_of(frame.selection.cells.begin(), frame.selection.cells.begin() + count, [&](winrate::CellId cell) { return interchangeable(cell, frame.selection.cells[i], current); })) {
                frame.selection.cells[count] = frame.selection.cells[i];
                frame.selection.observations[count++] = frame.selection.observations[i];
            }
        frame.selection.cells.resize(count);
        frame.selection.observations.resize(count);
        assert_(!frame.selection.cells.empty(), "AnytimeSearch: 非终局没有内层候选");
    }

    void finishBranch(Result &result, long double value, bool finished) {
        Frame &frame = frames[frameCount - 1];
        const int index = frame.order[frame.move];
        const MoveEval::Branch &branch = nodes[frame.node].moves.span(moves)[index].branches.span(branches)[frame.branch];
        // 根层复用已有权重；已算完的分支退出启发式修正，树内始终使用真实概率。
        const long double weight = frameCount == 1 && !finished ? branch.probability * result.candidates[index].multiplier : branch.probability;
        frame.value += weight * value;
        frame.settled = frame.settled && finished;
        if (frameCount == 1) result.candidates[index].branches[frame.branch] = Candidate::Branch(branch.digit, value, frame.horizon);
        ++frame.branch;
    }

    void returnChild(Result &result) {
        Frame &child = frames[frameCount - 1];
        const long double value = nodes[child.node].best(moves);
        const bool finished = nodes[child.node].finished(moves);
        const MoveEval::Branch &transition = child.transition.span(branches).front();
        Structure::applyDelta(structure, shapes, transition.structureDelta, true);
        Basic::applyDelta(basic, transition.basicDelta, basicPool, true);
        ObservedBoard::applyDelta(board, transition.boardDelta, boardPool, true);
        --frameCount;
        finishBranch(result, value, finished);
    }

    bool advance(Result &result, const Config &cfg, Workspace &workspace, long long &nodeBudget) {
        Frame &frame = frames[frameCount - 1];
        Node &node = nodes[frame.node];
        if (!node.ready) {
            if (nodeBudget == 0 && (frame.selection.cells.empty() || frame.next != (int)frame.selection.cells.size())) return false;
            if (frame.selection.cells.empty()) prepare(frame, cfg, workspace);
            if (frame.next == (int)frame.selection.cells.size()) {
                store(node, std::span(frame.selected).first(frame.selectedCount));
                frame.orderMoves(node, moves);
                if (frame.horizon == 0) { returnChild(result); return true; }
            } else {
                const int index = frame.next;
                if (!frame.evaluating) {
                    if (!frame.cursor) frame.cursor.emplace(frame.selection.cells[index], frame.selection.observations[index], 1.0L, frame.safe, *frame.candidates);
                    else frame.cursor->reset(frame.selection.cells[index], frame.selection.observations[index], 1.0L, frame.safe, *frame.candidates);
                    MoveEval::prepareAlternatives(*frame.cursor, frame.pending);
                    frame.evaluating = true;
                }
                const long long before = frame.cursor->analysis.node;
                // 路径每帧只揭开一格；有 ForcedSafe 时必走 safeSearch，不计算强制安全。
                const bool complete = MoveEval::advance(*frame.cursor, board, basic, structure, shapes, distributions, boardPool, basicPool, remainingSafe - frameCount + 1, 0, cfg.javaEval.moveEval, workspace.javaEval.moveEval);
                const bool equivalent = frame.cursor->analysis.equivalence && std::any_of(frame.selected.begin(), frame.selected.begin() + frame.selectedCount, [&](const MoveEval::Analysis &move) { return move.equivalence == frame.cursor->analysis.equivalence; });
                const int width = frame.safe ? 1 : cfg.width[frameCount == 2 ? 1 : 2];
                const bool pruned = !complete && frame.selectedCount == width && MoveEval::optimistic(*frame.cursor, cfg.javaEval.moveEval) < frame.selected[frame.selectedCount - 1].value;
                // 候选在这里结算；被剪掉或被去重的不再走 advance 的完成分支，也补计一次。
                if (!complete && (equivalent || pruned)) ++frame.cursor->analysis.node;
                const long long expanded = frame.cursor->analysis.node - before;
                result.node += expanded;
                nodeBudget -= expanded;
                if (equivalent || pruned || (complete && !frame.cursor->alternatives.empty())) {
                    ++frame.next;
                    frame.evaluating = false;
                } else if (complete) {
                    if (frame.selectedCount == (int)frame.selected.size()) frame.selected.emplace_back(frame.cursor->analysis.cellId, frame.cursor->analysis.value);
                    std::swap(frame.selected[frame.selectedCount++], frame.cursor->analysis);
                    std::sort(frame.selected.begin(), frame.selected.begin() + frame.selectedCount, [](const MoveEval::Analysis &lhs, const MoveEval::Analysis &rhs) { return lhs.value != rhs.value ? lhs.value > rhs.value : lhs.cellId < rhs.cellId; });
                    if (frame.selectedCount > width) --frame.selectedCount;
                    ++frame.next;
                    frame.evaluating = false;
                }
                return true;
            }
        }
        if (frame.move == (int)frame.order.size()) {
            node.depth = frame.horizon;
            if (frameCount != 1) { returnChild(result); return true; }
            result.completedDepth = frame.horizon;
            if (nodes.front().finished(moves)) result.finished = true;
            else {
                const int horizon = frame.horizon + 1;
                frameCount = 0;
                pushFrame(0, horizon, vectorPool<MoveEval::Branch>::vector(0, 0));
                frames[frameCount - 1].orderMoves(nodes.front(), moves);
            }
            return true;
        }
        const int index = frame.order[frame.move];
        Move &move = node.moves.span(moves)[index];
        if (move.finished) {
            if (frameCount == 1) result.candidates[index].value.push_back(move.value);
            ++frame.move;
            return true;
        }
        if (frame.branch == move.branches.size) {
            move.value = frame.value;
            move.finished = frame.settled;
            if (frameCount == 1) {
                result.candidates[index].value.push_back(move.value);
                if (move.finished) result.candidates[index].multiplier = 1.0L;
            }
            ++frame.move;
            frame.branch = 0;
            frame.value = 0.0L;
            frame.settled = true;
            return true;
        }
        const MoveEval::Branch &branch = move.branches.span(branches)[frame.branch];
        // 安全步截断尚未算完该分支，不能当成终局撤掉根层修正。
        const bool terminal = remainingSafe == frameCount || board.totalMines == basic.mineSum;
        // 第零层 javaLite 还要两步安全率；续搜 horizon 步后仍须留下两个安全格。
        if (terminal || branch.clears >= frame.horizon + 2) {
            finishBranch(result, 1.0L, terminal);
            return true;
        }
        int &child = move.children.span(children)[frame.branch];
        if (child == -1) { child = nodes.size(); nodes.emplace_back(); }
        if (nodes[child].ready && (nodes[child].finished(moves) || nodes[child].depth >= frame.horizon - 1)) {
            finishBranch(result, nodes[child].best(moves), nodes[child].finished(moves));
            return true;
        }
        const int horizon = frame.horizon - 1;
        const vectorPool<MoveEval::Branch>::vector transition(move.branches.idx + frame.branch, 1);
        ObservedBoard::applyDelta(board, branch.boardDelta, boardPool);
        Basic::applyDelta(basic, branch.basicDelta, basicPool);
        Structure::applyDelta(structure, shapes, branch.structureDelta);
        // 节点和帧扩容后不保留旧引用，只按下标重新访问。
        pushFrame(child, horizon, transition);
        if (nodes[child].ready) frames[frameCount - 1].orderMoves(nodes[child], moves);
        return true;
    }
};

AnytimeSearch::Result::Result() : node(0), completedDepth(0), finished(false) {}
AnytimeSearch::Result::~Result() = default;
AnytimeSearch::Result::Result(Result &&other) noexcept = default;
AnytimeSearch::Result &AnytimeSearch::Result::operator=(Result &&other) noexcept = default;

winrate::CellId AnytimeSearch::Result::recommended() const {
    const Candidate *best = nullptr;
    for (const Candidate &candidate : candidates)
        if (!best || candidate.value.size() > best->value.size() || (candidate.value.size() == best->value.size() && candidate.value.back() > best->value.back())) best = &candidate;
    return best ? best->cellId : -1;
}

AnytimeSearch::Result AnytimeSearch::analyze(const winrate::Input &input, const Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop, winrate::Cells cells) {
    const LongTermRisk::Result risk = LongTermRisk::analyze(input, {}, cfg.javaEval.moveEval.longTermRisk, workspace.javaEval.moveEval.longTermRisk);
    MoveEval::Selection::Result selected = cells.empty() ? MoveEval::Selection::find(input, &risk, cfg.javaEval.selection, workspace.javaEval.selection) : MoveEval::Selection::specified(input, cells, workspace.javaEval.moveEval.probability);
    if (cells.empty()) MoveEval::Selection::removeDead(selected, input, workspace.javaEval.moveEval.probability);
    return analyze(input, risk, selected, cfg, workspace, nodeBudget, stop);
}

AnytimeSearch::Result AnytimeSearch::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop) {
    Result result;
    assert_(cfg.width[0] > 0 && cfg.width[1] > 0 && cfg.width[2] > 0, "AnytimeSearch: 候选宽度须为正数");
    int remainingSafe = input.board.board.rows() * input.board.board.cols() - input.board.totalMines;
    int forcedSafes = 0;
    for (int row = 0; row < input.board.board.rows(); ++row)
        for (int col = 0; col < input.board.board.cols(); ++col) {
            if (input.board.board[row][col] <= winrate::CellState::Num8) --remainingSafe;
            else if (input.board.board[row][col] == winrate::CellState::ForcedSafe) ++forcedSafes;
        }
    std::vector<std::pair<long double, winrate::CellId>> peers;
    for (int i = 0; i < (int)candidates.cells.size(); ++i) peers.emplace_back(candidates.observations[i].probability[9], candidates.cells[i]);
    std::vector<MoveEval::Analysis> analyses;
    for (int i = 0; i < (int)candidates.cells.size(); ++i) {
        MoveEval::Cursor cursor = MoveEval::begin(input, risk, candidates.cells[i], candidates.observations[i], cfg.javaEval.moveEval, workspace.javaEval.moveEval);
        if (!cfg.fullAnalysis) MoveEval::prepareAlternatives(cursor, peers);
        while (!MoveEval::advance(cursor, input.board, input.basic, input.structure, input.shapes, input.distributions, input.boardPool, input.basicPool, remainingSafe, forcedSafes, cfg.javaEval.moveEval, workspace.javaEval.moveEval)) {}
        result.node += cursor.analysis.node;
        if (cfg.fullAnalysis || cursor.alternatives.empty()) analyses.push_back(std::move(cursor.analysis));
    }
    if (!cfg.fullAnalysis) State::prune(analyses, input, cfg.width[0], workspace.identities);
    for (const MoveEval::Analysis &analysis : analyses) {
        result.candidates.emplace_back(analysis.cellId, analysis.value, analysis.multiplier);
        for (const MoveEval::Branch &branch : analysis.branches) result.candidates.back().branches.emplace_back(branch.digit, branch.value, 0);
    }
    result.finished = std::all_of(analyses.begin(), analyses.end(), [](const MoveEval::Analysis &analysis) { return analysis.branches.empty(); });
    if (!result.finished) result.state = std::make_unique<State>(input, std::move(analyses));
    update(result, cfg, workspace, nodeBudget, stop);
    return result;
}

void AnytimeSearch::update(Result &result, const Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop) {
    assert_(nodeBudget >= 0, "AnytimeSearch: 节点预算须非负");
    while (!result.finished && !stop.stop_requested() && result.state->advance(result, cfg, workspace, nodeBudget)) {}
    if (result.finished) result.state.reset();
}

} // 命名空间 mss
