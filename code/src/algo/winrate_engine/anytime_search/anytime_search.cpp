#include "anytime_search.h"

#include <algorithm>
#include <numeric>

#include "core/assert.h"
#include "core/config.h"

namespace mss {

struct AnytimeSearch::State {
    struct Node;
    struct Move {
        winrate::CellId cellId;
        std::vector<MoveEval::Branch> branches;
        std::optional<U128> equivalence;
        std::vector<std::unique_ptr<Node>> children;
        long double value;
        bool finished;
        explicit Move(MoveEval::Analysis analysis) : cellId(analysis.cellId), branches(std::move(analysis.branches)), equivalence(analysis.equivalence), children(branches.size()), value(analysis.value), finished(branches.empty()) {}
    };
    struct Node {
        std::vector<Move> moves;
        bool ready;
        int depth;
        Node() : ready(false), depth(0) {}
        bool finished() const {
            return ready && std::all_of(moves.begin(), moves.end(), [](const Move &move) { return move.finished; });
        }
        long double best() const {
            return std::max_element(moves.begin(), moves.end(), [](const Move &lhs, const Move &rhs) { return lhs.value < rhs.value; })->value;
        }
    };
    struct Frame {
        Node &node;
        int horizon;
        std::vector<int> order;
        int move;
        int branch;
        long double value;
        bool settled;
        std::optional<LongTermRisk::Result> risk;
        std::vector<winrate::CellId> cells;
        std::vector<Observe::Result> observations;
        // 固定候选池；先评分，再按宽度保留最高分招法。
        std::vector<std::pair<long double, winrate::CellId>> pending;
        int next;
        std::optional<MoveEval::Cursor> cursor;
        bool safe;
        // 根帧没有入边；其余帧借用树中分支的固定 Delta。
        const MoveEval::Branch *transition;
        Frame(Node &node, int horizon, const MoveEval::Branch *transition) : node(node), horizon(horizon), move(0), branch(0), value(0.0L), settled(true), next(0), safe(false), transition(transition) {}
        void orderMoves() {
            order.resize(node.moves.size());
            std::iota(order.begin(), order.end(), 0);
            // 一层开始时冻结顺序；候选发布新分数不会改变本层游标。
            std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) { return node.moves[lhs].value > node.moves[rhs].value; });
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
    Node root;
    std::vector<std::unique_ptr<Frame>> frames;

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
        for (MoveEval::Analysis &analysis : analyses) root.moves.emplace_back(std::move(analysis));
        root.ready = true;
        frames.push_back(std::make_unique<Frame>(root, 1, nullptr));
        frames.back()->orderMoves();
    }

    winrate::Input input(Workspace &workspace) {
        // 只在准备新节点或新候选时重建工作概率；旧节点重访只重放 Delta。
        std::optional<winrate::Probability> &probability = workspace.moveEval.nextProbability;
        if (!probability) probability = Probability::analyze(board, basic, structure, shapes, distributions, workspace.moveEval.probability);
        else Probability::analyze(board, basic, structure, shapes, distributions, *probability, workspace.moveEval.probability);
        return winrate::Input(board, basic, structure, *probability, shapes, distributions);
    }

    void fill(Frame &frame, Workspace &workspace) {
        for (const std::pair<long double, winrate::CellId> &candidate : frame.pending) {
            const winrate::CellId cell = candidate.second;
            const Observe::Result observation = Observe::observe(board, basic, structure, shapes, *workspace.moveEval.nextProbability, distributions, cell, workspace.moveEval.probability);
            if (!frame.safe && MoveEval::isDead(observation)) continue;
            frame.cells.push_back(cell);
            frame.observations.push_back(observation);
        }
    }

    void prepare(Frame &frame, const mss::Config &cfg, Workspace &workspace) {
        const winrate::Input current = input(workspace);
        MoveEval::Workspace &evaluation = workspace.moveEval;
        winrate::CellId safe = -1;
        auto consider = [&](winrate::CellId cell) { if (safe == -1 || cell < safe) safe = cell; };
        auto basicSafe = [&](winrate::CellId cell) {
            const auto [row, col] = board.pos(cell);
            if (board.board[row][col] > winrate::CellState::Num8 && basic.marks[row][col] == Basic::Mark::S) consider(cell);
        };
        for (winrate::CellId cell : rootBasicSafe) basicSafe(cell);
        // 分支新增的 Basic 安全格都在路径 Delta 中；只筛掉已经揭开的项。
        for (const std::unique_ptr<Frame> &path : frames)
            if (path->transition) for (const Basic::Delta::Change &change : path->transition->basicDelta.changes) basicSafe(change.cell);
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
        if (!frame.safe) {
            frame.risk = LongTermRisk::analyze(current, {}, cfg.longTermRisk, evaluation.risk);
            // 根层由 JavaEval 交接；内层沿原搜索使用 Pseudo 的局部候选。
            Pseudo::update(workspace.pseudoResult, current, {}, cfg.pseudo, workspace.pseudo);
            frame.cells = workspace.pseudoResult;
            if (frame.cells.empty()) {
                frame.cells = rootWitnesses;
                for (const std::unique_ptr<Frame> &path : frames)
                    if (path->transition) for (const ObservedBoard::Delta::Change &change : path->transition->boardDelta.changes) frame.cells.push_back(change.cell);
                std::sort(frame.cells.begin(), frame.cells.end());
                winrate::CellId firstOffEdge = -1;
                if (basic.unknownSum != 0)
                    for (winrate::CellId cell : rootOffEdge) {
                        const auto [row, col] = board.pos(cell);
                        if (basic.marks[row][col] == Basic::Mark::T) { firstOffEdge = cell; break; }
                    }
                const winrate::Cells selected = MoveEval::Selection::select(current, *frame.risk, {}, basic.unknownSum < 30 ? rootOffEdge : rootOffEdgeCandidates, frame.cells, firstOffEdge, cfg.javaEval, workspace.selection);
                frame.cells.assign(selected.begin(), selected.end());
            }
        }
        auto allCells = [&]() {
            frame.pending.clear();
            // 安全分支不会走到全候选退路；剩余可点击格属于 H 或 T。
            current.probability.frontierCells(structure, shapes, [&](winrate::CellId cell, long double mine) { if (mine < 1.0L) frame.pending.emplace_back(mine, cell); });
            if (current.probability.tCellProbability() < 1.0L)
                for (winrate::CellId cell : rootOffEdge) {
                    const auto [row, col] = board.pos(cell);
                    if (basic.marks[row][col] == Basic::Mark::T) frame.pending.emplace_back(current.probability.tCellProbability(), cell);
                }
        };
        const bool global = frame.cells.empty();
        for (winrate::CellId cell : frame.cells) frame.pending.emplace_back(current.probability.mineProbability(cell, board, basic, structure), cell);
        frame.cells.clear();
        if (global) allCells();
        // 同概率按格子编号排序；组件重建后的存储顺序不参与候选截断。
        auto order = [&]() {
            std::stable_sort(frame.pending.begin(), frame.pending.end(), [](const std::pair<long double, winrate::CellId> &lhs, const std::pair<long double, winrate::CellId> &rhs) { return lhs.first != rhs.first ? lhs.first < rhs.first : lhs.second < rhs.second; });
            frame.pending.erase(std::unique(frame.pending.begin(), frame.pending.end(), [](const std::pair<long double, winrate::CellId> &lhs, const std::pair<long double, winrate::CellId> &rhs) { return lhs.second == rhs.second; }), frame.pending.end());
        };
        order();
        fill(frame, workspace);
        if (frame.cells.empty()) {
            // pseudo 窗口也可能全 dead；必猜格只能在全盘没有活格时成为候选。
            if (!global) {
                allCells();
                order();
                fill(frame, workspace);
            }
            if (frame.cells.empty()) {
                const winrate::CellId cell = frame.pending.front().second;
                frame.cells.push_back(cell);
                frame.observations.push_back(Observe::observe(board, basic, structure, shapes, current.probability, distributions, cell, evaluation.probability));
            }
        }
        assert_(!frame.cells.empty(), "AnytimeSearch: 非终局没有内层候选");
    }

    void finishBranch(Result &result, long double value, bool finished) {
        Frame &frame = *frames.back();
        const int index = frame.order[frame.move];
        const MoveEval::Branch &branch = frame.node.moves[index].branches[frame.branch];
        frame.value += branch.probability * value;
        frame.settled = frame.settled && finished;
        if (frames.size() == 1) result.candidates[index].branches[frame.branch] = Candidate::Branch(branch.digit, value, frame.horizon);
        ++frame.branch;
    }

    void returnChild(Result &result) {
        Frame &child = *frames.back();
        const long double value = child.node.best();
        const bool finished = child.node.finished();
        Structure::applyDelta(structure, shapes, child.transition->structureDelta, true);
        Basic::applyDelta(basic, child.transition->basicDelta, true);
        ObservedBoard::applyDelta(board, child.transition->boardDelta, true);
        frames.pop_back();
        finishBranch(result, value, finished);
    }

    bool advance(Result &result, const mss::Config &cfg, Workspace &workspace, long long &nodeBudget) {
        Frame &frame = *frames.back();
        if (!frame.node.ready) {
            if (nodeBudget == 0 && (frame.cells.empty() || frame.next != (int)frame.cells.size())) return false;
            const bool prepared = frame.cells.empty();
            if (prepared) prepare(frame, cfg, workspace);
            if (frame.next == (int)frame.cells.size()) {
                frame.node.ready = true;
                frame.orderMoves();
                if (frame.horizon == 0) { returnChild(result); return true; }
            } else {
                const int index = frame.next;
                if (!frame.cursor) {
                    const winrate::Input current = prepared ? winrate::Input(board, basic, structure, *workspace.moveEval.nextProbability, shapes, distributions) : input(workspace);
                    LongTermRisk::resetCache(workspace.moveEval.risk, board.board.rows() * board.board.cols());
                    frame.cursor = frame.safe ? MoveEval::Cursor(frame.cells[index], frame.observations[index], 1.0L, 1.0L, true, current.probability.candidates()) : MoveEval::begin(current, *frame.risk, frame.cells[index], frame.observations[index], cfg.moveEval, cfg.longTermRisk, workspace.moveEval);
                    MoveEval::prepareAlternatives(*frame.cursor, frame.cells, frame.pending, true);
                }
                const long long before = frame.cursor->analysis.node;
                // 路径每帧只揭开一格；有 ForcedSafe 时必走 safeSearch，不计算强制安全。
                const bool complete = MoveEval::advance(*frame.cursor, board, basic, structure, shapes, distributions, remainingSafe - (int)frames.size() + 1, 0, frame.risk ? &*frame.risk : nullptr, cfg.moveEval, workspace.moveEval);
                const long long expanded = frame.cursor->analysis.node - before;
                result.node += expanded;
                nodeBudget -= expanded;
                const bool equivalent = frame.cursor->analysis.equivalence && std::any_of(frame.node.moves.begin(), frame.node.moves.end(), [&](const Move &move) { return move.equivalence == frame.cursor->analysis.equivalence; });
                const int width = frame.safe ? 1 : cfg.anytimeSearch.width[frames.size() == 2 ? 1 : 2];
                const bool pruned = !complete && (int)frame.node.moves.size() == width && MoveEval::optimistic(*frame.cursor, cfg.moveEval) < frame.node.moves.back().value;
                if (equivalent || pruned || (complete && !frame.cursor->alternatives.empty())) {
                    ++frame.next;
                    frame.cursor.reset();
                } else if (complete) {
                    frame.node.moves.emplace_back(std::move(frame.cursor->analysis));
                    std::stable_sort(frame.node.moves.begin(), frame.node.moves.end(), [](const Move &lhs, const Move &rhs) { return lhs.value != rhs.value ? lhs.value > rhs.value : lhs.cellId < rhs.cellId; });
                    if ((int)frame.node.moves.size() > width) frame.node.moves.erase(frame.node.moves.begin() + width, frame.node.moves.end());
                    ++frame.next;
                    frame.cursor.reset();
                }
                return true;
            }
        }
        if (frame.move == (int)frame.order.size()) {
            frame.node.depth = frame.horizon;
            if (frames.size() != 1) { returnChild(result); return true; }
            result.completedDepth = frame.horizon;
            if (root.finished()) result.finished = true;
            else {
                const int horizon = frame.horizon + 1;
                frames.clear();
                frames.push_back(std::make_unique<Frame>(root, horizon, nullptr));
                frames.back()->orderMoves();
            }
            return true;
        }
        const int index = frame.order[frame.move];
        Move &move = frame.node.moves[index];
        if (move.finished) {
            if (frames.size() == 1) result.candidates[index].value.push_back(move.value);
            ++frame.move;
            return true;
        }
        if (frame.branch == (int)move.branches.size()) {
            move.value = frame.value;
            move.finished = frame.settled;
            if (frames.size() == 1) result.candidates[index].value.push_back(move.value);
            ++frame.move;
            frame.branch = 0;
            frame.value = 0.0L;
            frame.settled = true;
            return true;
        }
        const MoveEval::Branch &branch = move.branches[frame.branch];
        // 启发式只用于叶子评分；路径期望必须使用真实结局概率，不能反复乘奖励。
        const bool terminal = remainingSafe == (int)frames.size() || board.totalMines == basic.mineSum;
        // 第零层 javaLite 还要两步安全率；续搜 horizon 步后仍须留下两个安全格。
        if (terminal || branch.clears >= frame.horizon + 2) {
            finishBranch(result, 1.0L, terminal);
            return true;
        }
        std::unique_ptr<Node> &node = move.children[frame.branch];
        if (!node) node = std::make_unique<Node>();
        if (node->ready && (node->finished() || node->depth >= frame.horizon - 1)) {
            finishBranch(result, node->best(), node->finished());
            return true;
        }
        std::unique_ptr<Frame> child = std::make_unique<Frame>(*node, frame.horizon - 1, &branch);
        ObservedBoard::applyDelta(board, branch.boardDelta);
        Basic::applyDelta(basic, branch.basicDelta);
        Structure::applyDelta(structure, shapes, branch.structureDelta);
        if (node->ready) child->orderMoves();
        frames.push_back(std::move(child));
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

AnytimeSearch::Result AnytimeSearch::analyze(const winrate::Input &input, std::vector<MoveEval::Analysis> analyses, const mss::Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop) {
    Result result;
    assert_(cfg.anytimeSearch.width[0] > 0 && cfg.anytimeSearch.width[1] > 0 && cfg.anytimeSearch.width[2] > 0, "AnytimeSearch: 候选宽度须为正数");
    for (const MoveEval::Analysis &analysis : analyses) result.node += analysis.node;
    if (!cfg.anytimeSearch.fullAnalysis) {
        std::erase_if(analyses, [](const MoveEval::Analysis &analysis) { return analysis.value < 0.0L; });
        auto dead = [&](const MoveEval::Analysis &analysis) { return analysis.branches.size() == 1 && input.probability.mineProbability(analysis.cellId, input.board, input.basic, input.structure) != 0.0L; };
        if (std::any_of(analyses.begin(), analyses.end(), [&](const MoveEval::Analysis &analysis) { return !dead(analysis); })) std::erase_if(analyses, dead);
        else if (analyses.size() > 1) {
            std::stable_sort(analyses.begin(), analyses.end(), [&](const MoveEval::Analysis &lhs, const MoveEval::Analysis &rhs) { return input.probability.mineProbability(lhs.cellId, input.board, input.basic, input.structure) < input.probability.mineProbability(rhs.cellId, input.board, input.basic, input.structure); });
            analyses.erase(analyses.begin() + 1, analyses.end());
        }
        std::stable_sort(analyses.begin(), analyses.end(), [](const MoveEval::Analysis &lhs, const MoveEval::Analysis &rhs) { return lhs.value > rhs.value; });
        std::vector<U128> identities;
        // 先剔除 dead，再去重，再计宽度；G 指定根招法不进入此分支。
        std::erase_if(analyses, [&](const MoveEval::Analysis &analysis) {
            if (!analysis.equivalence) return false;
            if (std::find(identities.begin(), identities.end(), *analysis.equivalence) != identities.end()) return true;
            identities.push_back(*analysis.equivalence);
            return false;
        });
        if ((int)analyses.size() > cfg.anytimeSearch.width[0]) analyses.erase(analyses.begin() + cfg.anytimeSearch.width[0], analyses.end());
    }
    for (const MoveEval::Analysis &analysis : analyses) {
        assert_(analysis.value >= 0.0L, "AnytimeSearch: 输入候选尚未完成完整分析");
        result.candidates.emplace_back(analysis.cellId, analysis.value);
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
