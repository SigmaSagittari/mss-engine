#include "anytime_search.h"

#include <algorithm>
#include <numeric>

#include "core/assert.h"
#include "core/config.h"
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
        std::vector<winrate::CellId> cells;
        std::vector<Observe::Result> observations;
        // 固定候选池；先评分，再按宽度保留最高分招法。
        std::vector<std::pair<long double, winrate::CellId>> pending;
        // 候选先在临时区排序截断，定稿后一次写入三个池，避免修改非尾部切片。
        std::vector<MoveEval::Analysis> selected;
        int next;
        std::optional<MoveEval::Cursor> cursor;
        bool safe;
        // 根帧入边为空切片；其余帧保存一条分支的池下标，扩容后重新取 Delta。
        vectorPool<MoveEval::Branch>::vector transition;
        Frame(int node, int horizon, vectorPool<MoveEval::Branch>::vector transition) : node(node), horizon(horizon), move(0), branch(0), value(0.0L), settled(true), next(0), safe(false), transition(transition) {}
        void orderMoves(const Node &current, const vectorPool<Move> &pool) {
            const std::span<const Move> candidates = current.moves.span(pool);
            order.resize(candidates.size());
            std::iota(order.begin(), order.end(), 0);
            // 一层开始时冻结顺序；候选发布新分数不会改变本层游标。
            std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) { return candidates[lhs].value > candidates[rhs].value; });
        }
    };

    winrate::Board board;
    winrate::Basic basic;
    winrate::Structure structure;
    winrate::Shapes &shapes;
    winrate::Distributions &distributions;
    int remainingSafe;
    std::vector<winrate::CellId> rootBasicSafe;
    std::vector<winrate::CellId> rootOffEdge;
    std::vector<winrate::CellId> rootOffEdgeCandidates;
    std::vector<winrate::CellId> rootWitnesses;
    std::vector<Node> nodes;
    vectorPool<Move> moves;
    vectorPool<MoveEval::Branch> branches;
    MoveEval::Pool deltas;
    vectorPool<int> children;
    std::vector<Frame> frames;

    State(const winrate::Input &input, std::vector<MoveEval::Analysis> analyses) : board(input.board), basic(input.basic), structure(input.structure), shapes(input.shapes), distributions(input.distributions), remainingSafe(board.board.rows() * board.board.cols() - board.totalMines) {
        for (int row = 0; row < board.board.rows(); ++row)
            for (int col = 0; col < board.board.cols(); ++col)
                if (board.board[row][col] <= winrate::CellState::Num8) { --remainingSafe; rootWitnesses.push_back(board.id(row, col)); }
                else {
                    if (basic.marks[row][col] == Basic::Mark::S) rootBasicSafe.push_back(board.id(row, col));
                    else if (basic.marks[row][col] == Basic::Mark::T) {
                        rootOffEdge.push_back(board.id(row, col));
                        // 以后每次揭示会把邻接 T 移入 H，仍为 T 的格子不改变隐藏邻格数。
                        if (MoveEval::offEdgeCandidate(board, board.id(row, col))) rootOffEdgeCandidates.push_back(board.id(row, col));
                    }
                }
        nodes.emplace_back();
        store(nodes.front(), analyses);
        frames.emplace_back(0, 1, vectorPool<MoveEval::Branch>::vector(0, 0));
        frames.back().orderMoves(nodes.front(), moves);
    }

    void store(Node &node, std::vector<MoveEval::Analysis> &analyses) {
        node.moves = moves.push_back();
        for (const MoveEval::Analysis &analysis : analyses) {
            vectorPool<MoveEval::Branch>::vector outcomes = branches.push_back();
            vectorPool<int>::vector next = children.push_back();
            for (const MoveEval::Branch &branch : analysis.branches) {
                outcomes.push_back(branches, MoveEval::Branch(branch.digit, branch.clears, branch.value, branch.probability, branch.boardChanges.span(analysis.deltas.boardChanges), branch.basicChanges.span(analysis.deltas.basicChanges), branch.removed.span(analysis.deltas.removed), branch.added.span(analysis.deltas.added), deltas));
                next.push_back(children, -1);
            }
            node.moves.push_back(moves, Move(analysis.cellId, outcomes, next, analysis.value));
        }
        node.ready = true;
        analyses.clear();
    }

    winrate::Input input(Workspace &workspace) {
        // 只在准备新节点或新候选时重建工作概率；旧节点重访只重放 Delta。
        std::optional<winrate::Probability> &probability = workspace.moveEval.nextProbability;
        if (!probability) probability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.moveEval.probability);
        else Probability::analyze(board, basic, structure, shapes, distributions, *probability, workspace.moveEval.probability);
        return winrate::Input(board, basic, structure, *probability, shapes, distributions);
    }

    void prepare(Frame &frame, const mss::Config &cfg, Workspace &workspace) {
        const winrate::Input current = input(workspace);
        frame.candidates = current.probability.candidates();
        MoveEval::Workspace &evaluation = workspace.moveEval;
        winrate::CellId safe = -1;
        auto consider = [&](winrate::CellId cell) { if (safe == -1 || cell < safe) safe = cell; };
        auto basicSafe = [&](winrate::CellId cell) {
            const auto [row, col] = board.pos(cell);
            if (board.board[row][col] > winrate::CellState::Num8 && basic.marks[row][col] == Basic::Mark::S) consider(cell);
        };
        for (winrate::CellId cell : rootBasicSafe) basicSafe(cell);
        // 分支新增的 Basic 安全格都在路径 Delta 中；只筛掉已经揭开的项。
        for (const Frame &path : frames)
            if (path.transition.size != 0) for (const Basic::Delta::Change &change : path.transition.span(branches).front().basicChanges.span(deltas.basicChanges)) basicSafe(change.cell);
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
        if (safe != -1) frame.cells.push_back(safe);
        frame.safe = !frame.cells.empty();
        if (frame.safe) {
            const MoveEval::Selection::Result selected = MoveEval::Selection::specified(current, frame.cells, evaluation.probability, workspace.selection);
            frame.observations.assign(selected.observations.begin(), selected.observations.end());
        } else {
            frame.cells = rootWitnesses;
            for (const Frame &path : frames)
                if (path.transition.size != 0) for (const ObservedBoard::Delta::Change &change : path.transition.span(branches).front().boardChanges.span(deltas.boardChanges)) frame.cells.push_back(change.cell);
            std::sort(frame.cells.begin(), frame.cells.end());
            winrate::CellId firstOffEdge = -1;
            for (winrate::CellId cell : rootOffEdge) {
                const auto [row, col] = board.pos(cell);
                if (basic.marks[row][col] == Basic::Mark::T) { firstOffEdge = cell; break; }
            }
            const MoveEval::Selection::Result selected = MoveEval::Selection::select(current, nullptr, basic.unknownSum < 30 ? rootOffEdge : rootOffEdgeCandidates, frame.cells, firstOffEdge, cfg.javaEval, cfg.pseudo, evaluation.probability, workspace.pseudo, workspace.selection);
            frame.cells.assign(selected.cells.begin(), selected.cells.end());
            frame.observations.assign(selected.observations.begin(), selected.observations.end());
        }
        for (winrate::CellId cell : frame.cells) frame.pending.emplace_back(current.probability.mineProbability(cell, board, basic, structure), cell);
        int count = 0;
        for (int i = 0; i < (int)frame.cells.size(); ++i)
            if (std::none_of(frame.cells.begin(), frame.cells.begin() + count, [&](winrate::CellId cell) { return interchangeable(cell, frame.cells[i], current); })) {
                frame.cells[count] = frame.cells[i];
                frame.observations[count++] = frame.observations[i];
            }
        frame.cells.resize(count);
        frame.observations.resize(count);
        assert_(!frame.cells.empty(), "AnytimeSearch: 非终局没有内层候选");
    }

    void finishBranch(Result &result, long double value, bool finished) {
        Frame &frame = frames.back();
        const int index = frame.order[frame.move];
        const MoveEval::Branch &branch = nodes[frame.node].moves.span(moves)[index].branches.span(branches)[frame.branch];
        // 根层复用已有权重；已算完的分支退出启发式修正，树内始终使用真实概率。
        const long double weight = frames.size() == 1 && !finished ? branch.probability * result.candidates[index].multiplier : branch.probability;
        frame.value += weight * value;
        frame.settled = frame.settled && finished;
        if (frames.size() == 1) result.candidates[index].branches[frame.branch] = Candidate::Branch(branch.digit, value, frame.horizon);
        ++frame.branch;
    }

    void returnChild(Result &result) {
        Frame &child = frames.back();
        const long double value = nodes[child.node].best(moves);
        const bool finished = nodes[child.node].finished(moves);
        const MoveEval::Branch &transition = child.transition.span(branches).front();
        Structure::applyDelta(structure, shapes, transition.removed.span(deltas.removed), transition.added.span(deltas.added), true);
        Basic::applyDelta(basic, transition.basicChanges.span(deltas.basicChanges), true);
        ObservedBoard::applyDelta(board, transition.boardChanges.span(deltas.boardChanges), true);
        frames.pop_back();
        finishBranch(result, value, finished);
    }

    bool advance(Result &result, const mss::Config &cfg, Workspace &workspace, long long &nodeBudget) {
        Frame &frame = frames.back();
        Node &node = nodes[frame.node];
        if (!node.ready) {
            if (nodeBudget == 0 && (frame.cells.empty() || frame.next != (int)frame.cells.size())) return false;
            if (frame.cells.empty()) prepare(frame, cfg, workspace);
            if (frame.next == (int)frame.cells.size()) {
                store(node, frame.selected);
                frame.orderMoves(node, moves);
                if (frame.horizon == 0) { returnChild(result); return true; }
            } else {
                const int index = frame.next;
                if (!frame.cursor) {
                    frame.cursor.emplace(frame.cells[index], frame.observations[index], 1.0L, frame.safe, *frame.candidates);
                    MoveEval::prepareAlternatives(*frame.cursor, frame.pending);
                }
                const long long before = frame.cursor->analysis.node;
                // 路径每帧只揭开一格；有 ForcedSafe 时必走 safeSearch，不计算强制安全。
                const bool complete = MoveEval::advance(*frame.cursor, board, basic, structure, shapes, distributions, remainingSafe - (int)frames.size() + 1, 0, cfg.moveEval, workspace.moveEval);
                const long long expanded = frame.cursor->analysis.node - before;
                result.node += expanded;
                nodeBudget -= expanded;
                const bool equivalent = frame.cursor->analysis.equivalence && std::any_of(frame.selected.begin(), frame.selected.end(), [&](const MoveEval::Analysis &move) { return move.equivalence == frame.cursor->analysis.equivalence; });
                const int width = frame.safe ? 1 : cfg.anytimeSearch.width[frames.size() == 2 ? 1 : 2];
                const bool pruned = !complete && (int)frame.selected.size() == width && MoveEval::optimistic(*frame.cursor, cfg.moveEval) < frame.selected.back().value;
                if (equivalent || pruned || (complete && !frame.cursor->alternatives.empty())) {
                    ++frame.next;
                    frame.cursor.reset();
                } else if (complete) {
                    frame.selected.push_back(std::move(frame.cursor->analysis));
                    std::stable_sort(frame.selected.begin(), frame.selected.end(), [](const MoveEval::Analysis &lhs, const MoveEval::Analysis &rhs) { return lhs.value != rhs.value ? lhs.value > rhs.value : lhs.cellId < rhs.cellId; });
                    if ((int)frame.selected.size() > width) frame.selected.erase(frame.selected.begin() + width, frame.selected.end());
                    ++frame.next;
                    frame.cursor.reset();
                }
                return true;
            }
        }
        if (frame.move == (int)frame.order.size()) {
            node.depth = frame.horizon;
            if (frames.size() != 1) { returnChild(result); return true; }
            result.completedDepth = frame.horizon;
            if (nodes.front().finished(moves)) result.finished = true;
            else {
                const int horizon = frame.horizon + 1;
                frames.clear();
                frames.emplace_back(0, horizon, vectorPool<MoveEval::Branch>::vector(0, 0));
                frames.back().orderMoves(nodes.front(), moves);
            }
            return true;
        }
        const int index = frame.order[frame.move];
        Move &move = node.moves.span(moves)[index];
        if (move.finished) {
            if (frames.size() == 1) result.candidates[index].value.push_back(move.value);
            ++frame.move;
            return true;
        }
        if (frame.branch == move.branches.size) {
            move.value = frame.value;
            move.finished = frame.settled;
            if (frames.size() == 1) {
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
        const bool terminal = remainingSafe == (int)frames.size() || board.totalMines == basic.mineSum;
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
        ObservedBoard::applyDelta(board, branch.boardChanges.span(deltas.boardChanges));
        Basic::applyDelta(basic, branch.basicChanges.span(deltas.basicChanges));
        Structure::applyDelta(structure, shapes, branch.removed.span(deltas.removed), branch.added.span(deltas.added));
        // 节点和帧扩容后不保留旧引用，只按下标重新访问。
        frames.emplace_back(child, horizon, transition);
        if (nodes[child].ready) frames.back().orderMoves(nodes[child], moves);
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

AnytimeSearch::Result AnytimeSearch::analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const mss::Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop) {
    Result result;
    assert_(cfg.anytimeSearch.width[0] > 0 && cfg.anytimeSearch.width[1] > 0 && cfg.anytimeSearch.width[2] > 0, "AnytimeSearch: 候选宽度须为正数");
    LongTermRisk::resetCache(workspace.moveEval.risk, input.board.board.rows() * input.board.board.cols());
    int remainingSafe = input.board.board.rows() * input.board.board.cols() - input.board.totalMines;
    int forcedSafes = 0;
    for (int row = 0; row < input.board.board.rows(); ++row)
        for (int col = 0; col < input.board.board.cols(); ++col) {
            if (input.board.board[row][col] <= winrate::CellState::Num8) --remainingSafe;
            else if (input.board.board[row][col] == winrate::CellState::ForcedSafe) ++forcedSafes;
        }
    std::vector<std::pair<long double, winrate::CellId>> peers;
    for (winrate::CellId cell : candidates.cells) peers.emplace_back(input.probability.mineProbability(cell, input.board, input.basic, input.structure), cell);
    std::vector<MoveEval::Analysis> analyses;
    for (int i = 0; i < (int)candidates.cells.size(); ++i) {
        MoveEval::Cursor cursor = MoveEval::begin(input, risk, candidates.cells[i], candidates.observations[i], cfg.moveEval, cfg.longTermRisk, workspace.moveEval);
        if (!cfg.anytimeSearch.fullAnalysis) MoveEval::prepareAlternatives(cursor, peers);
        while (!MoveEval::advance(cursor, input.board, input.basic, input.structure, input.shapes, input.distributions, remainingSafe, forcedSafes, cfg.moveEval, workspace.moveEval)) {}
        result.node += cursor.analysis.node;
        if (cfg.anytimeSearch.fullAnalysis || cursor.alternatives.empty()) analyses.push_back(std::move(cursor.analysis));
    }
    if (!cfg.anytimeSearch.fullAnalysis) {
        for (int i = 0; i < (int)analyses.size(); ++i)
            for (int j = i + 1; j < (int)analyses.size();)
                if (State::interchangeable(analyses[i].cellId, analyses[j].cellId, input)) {
                    // 可对换格的进度相同，完整乘数的大小即 influence 的大小；相同时固定保留小编号。
                    if (analyses[j].multiplier > analyses[i].multiplier || (analyses[j].multiplier == analyses[i].multiplier && analyses[j].cellId < analyses[i].cellId)) std::swap(analyses[i], analyses[j]);
                    analyses.erase(analyses.begin() + j);
                } else ++j;
        std::stable_sort(analyses.begin(), analyses.end(), [](const MoveEval::Analysis &lhs, const MoveEval::Analysis &rhs) { return lhs.value > rhs.value; });
        std::vector<U128> identities;
        // 安全支配和对换剪枝后按分数保留等价类代表，再计宽度；G 指定根招法不进入此分支。
        std::erase_if(analyses, [&](const MoveEval::Analysis &analysis) {
            if (!analysis.equivalence) return false;
            if (std::find(identities.begin(), identities.end(), *analysis.equivalence) != identities.end()) return true;
            identities.push_back(*analysis.equivalence);
            return false;
        });
        if ((int)analyses.size() > cfg.anytimeSearch.width[0]) analyses.erase(analyses.begin() + cfg.anytimeSearch.width[0], analyses.end());
    }
    for (const MoveEval::Analysis &analysis : analyses) {
        result.candidates.emplace_back(analysis.cellId, analysis.value, analysis.multiplier);
        for (const MoveEval::Branch &branch : analysis.branches) result.candidates.back().branches.emplace_back(branch.digit, branch.value, 0);
    }
    result.finished = std::all_of(analyses.begin(), analyses.end(), [](const MoveEval::Analysis &analysis) { return analysis.branches.empty(); });
    if (!result.finished) result.state = std::make_unique<State>(input, std::move(analyses));
    update(result, cfg, workspace, nodeBudget, stop);
    return result;
}

void AnytimeSearch::update(Result &result, const mss::Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop) {
    assert_(nodeBudget >= 0, "AnytimeSearch: 节点预算须非负");
    while (!result.finished && !stop.stop_requested() && result.state->advance(result, cfg, workspace, nodeBudget)) {}
}

} // 命名空间 mss
