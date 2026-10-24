#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"

#include <algorithm>
#include <utility>

#include "core/algorithm/neighbors.h"
#include "core/assert.h"

namespace mss {

void AnytimeSearch::swapProbability(Probability::Result &left, Probability::Result &right) {
    left.components.swap(right.components);
    left.boxProbabilities.swap(right.boxProbabilities);
    std::swap(left.candidates, right.candidates);
    std::swap(left.tCellProbability, right.tCellProbability);
}

void AnytimeSearch::countSafe(const PositionState &state, int &remainingSafe, int &forcedSafes) {
    remainingSafe = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    forcedSafes = 0;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col) {
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8)
                --remainingSafe;
            else if (state.board.cells[row][col] == ObservedBoard::CellState::ForcedSafe)
                ++forcedSafes;
        }
}

ObservedBoard::CellId AnytimeSearch::Result::recommended(const Pool &pool) const {
    const Pool::Node *best = nullptr;
    for (int i = 0; i < (int)roots.count; ++i) {
        const Pool::Node &candidate = pool.nodes.data.data[roots.offset + i];
        if (best == nullptr || candidate.expandedDepth > best->expandedDepth || (candidate.expandedDepth == best->expandedDepth && candidate.value > best->value))
            best = &candidate;
    }
    return best != nullptr ? best->cell : -1;
}

SuggestResult AnytimeSearch::Result::snapshot(const Pool &pool) const {
    SuggestResult result(SuggestResult::Engine::Anytime, reason);
    result.nodes = node;
    for (int i = 0; i < (int)roots.count; ++i) {
        const Pool::Node &candidate = pool.nodes.data.data[roots.offset + i];
        result.candidates.push_back(SuggestResult::Candidate(candidate.cell, candidate.value));
    }
    const ObservedBoard::CellId cell = recommended(pool);
    if (cell >= 0) result.suggest.push_back(cell);
    return result;
}

bool AnytimeSearch::interchangeable(const PositionState &state, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs) {
    const std::pair<int, int> left = state.board.pos(lhs);
    const std::pair<int, int> right = state.board.pos(rhs);
    if (std::abs(left.first - right.first) > 1 || std::abs(left.second - right.second) > 1)
        return false;
    const Structure::Result::CellLocation &leftLocation = state.structure.cellLoc.data[lhs];
    const Structure::Result::CellLocation &rightLocation = state.structure.cellLoc.data[rhs];
    if (leftLocation.component != rightLocation.component || leftLocation.box != rightLocation.box || state.basic.marks[left.first][left.second] != state.basic.marks[right.first][right.second])
        return false;
    auto matches = [&](int row, int col, int otherRow, int otherCol, ObservedBoard::CellId other) {
        bool same = true;
        forEachAdjacent(row, col, state.board.cells.rows(), state.board.cells.cols(), [&](int neighborRow, int neighborCol) {
            const ObservedBoard::CellId cell = state.board.id(neighborRow, neighborCol);
            if (cell == other || state.board.cells[neighborRow][neighborCol] <= ObservedBoard::CellState::Num8 || Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) == 1.0L)
                return;
            if (std::abs(neighborRow - otherRow) > 1 || std::abs(neighborCol - otherCol) > 1)
                same = false;
        });
        return same;
    };
    return matches(left.first, left.second, right.first, right.second, rhs) && matches(right.first, right.second, left.first, left.second, lhs);
}

void AnytimeSearch::pruneRoot(vector<JavaEval::Analysis> &analyses, const PositionState &state, int width, Workspace &workspace) {
    for (int i = 0; i < (int)analyses.size; ++i)
        for (int j = i + 1; j < (int)analyses.size;)
            if (interchangeable(state, analyses.data[i].cell, analyses.data[j].cell)) {
                // 完整乘数包含 influence，必须评分后才选代表；相同时保留小编号。
                if (analyses.data[j].multiplier > analyses.data[i].multiplier || (analyses.data[j].multiplier == analyses.data[i].multiplier && analyses.data[j].cell < analyses.data[i].cell))
                    analyses.data[i] = analyses.data[j];
                for (int k = j + 1; k < (int)analyses.size; ++k)
                    analyses.data[k - 1] = analyses.data[k];
                --analyses.size;
            } else
                ++j;
    std::stable_sort(analyses.data, analyses.data + analyses.size, [](const JavaEval::Analysis &left, const JavaEval::Analysis &right) {
        return left.value > right.value;
    });
    vector<U128> &identities = workspace.identities;
    identities.resize(0);
    int count = 0;
    for (int i = 0; i < (int)analyses.size; ++i) {
        bool duplicate = false;
        if (analyses.data[i].hasEquivalence)
            for (int j = 0; j < (int)identities.size; ++j)
                if (identities.data[j] == analyses.data[i].equivalence)
                    duplicate = true;
        if (duplicate)
            continue;
        if (analyses.data[i].hasEquivalence)
            identities.push_back(analyses.data[i].equivalence);
        analyses.data[count++] = analyses.data[i];
    }
    analyses.size = count;
    if (width > 0 && (int)analyses.size > width)
        analyses.size = width;
}

long long AnytimeSearch::selectRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Selection::Result &selected = pool.selected;
    Selection::find(state, shapeConfig, config.javaEval.selection, fiftyConfig, fiftyPool, selected, workspace.javaEval.selection, true, cells);
    Selection::removeDead(selected, state, shapeConfig, workspace.javaEval.probability);
    int remaining = 0, forced = 0;
    countSafe(state, remaining, forced);
    vector<JavaEval::Peer> &peers = pool.peers;
    peers.resize(0);
    for (int i = 0; i < (int)selected.cells.size; ++i)
        peers.push_back(JavaEval::Peer(selected.cells.data[i], selected.observations.data[i].probability[9]));
    const span<const JavaEval::Peer> peerSpan = config.fullAnalysis || cells != nullptr ? span<const JavaEval::Peer>() : span<const JavaEval::Peer>(peers.data, (int)peers.size);
    vector<JavaEval::Analysis> &analyses = pool.analyses;
    analyses.resize(0);
    pool.branches.data.resize(0);
    long long nodeCount = 0;
    for (int i = 0; i < (int)selected.cells.size; ++i) {
        const ObservedBoard::CellId cell = selected.cells.data[i];
        const Observe::Result &observation = selected.observations.data[i];
        const bool safe = observation.probability[9] == 0.0L;
        const bool pseudo = selected.reason == Selection::Reason::Forced || selected.reason == Selection::Reason::Semi;
        const JavaEval::Analysis analysis = safe || pseudo ? JavaEval::javaLite(state, shapeConfig, config.javaEval.progressContribution, workspace.javaEval, pool.branches, cell, observation, remaining, forced, safe, peerSpan, -1.0L, details) : JavaEval::evaluate(state, shapeConfig, config.javaEval, workspace.javaEval, pool.branches, selected.fiftyFifty, fiftyConfig, cell, observation, remaining, forced, peerSpan, true, details);
        nodeCount += analysis.node;
        if (config.fullAnalysis || cells != nullptr || !analysis.hasAlternative)
            analyses.push_back(analysis);
    }
    return nodeCount;
}

void AnytimeSearch::selectCandidates(vector<JavaEval::Analysis> &analyses, int limit) {
    // 按 Selection 顺序结算：同必安子集只与当前宽度内的代表比较，宽度外的类不占用身份。
    int selectedCount = 0;
    if (limit <= 0) {
        analyses.size = 0;
        return;
    }
    for (int i = 0; i < (int)analyses.size; ++i) {
        const JavaEval::Analysis candidate = analyses.data[i];
        if (candidate.hasAlternative)
            continue;
        bool duplicate = false;
        if (candidate.hasEquivalence)
            for (int j = 0; j < selectedCount; ++j)
                if (analyses.data[j].hasEquivalence && analyses.data[j].equivalence == candidate.equivalence)
                    duplicate = true;
        if (duplicate)
            continue;
        int position = 0;
        while (position < selectedCount && (analyses.data[position].value > candidate.value || (analyses.data[position].value == candidate.value && analyses.data[position].cell < candidate.cell)))
            ++position;
        if (selectedCount < limit) {
            for (int j = selectedCount; j > position; --j)
                analyses.data[j] = analyses.data[j - 1];
            analyses.data[position] = candidate;
            ++selectedCount;
        } else if (position < limit) {
            for (int j = limit - 1; j > position; --j)
                analyses.data[j] = analyses.data[j - 1];
            analyses.data[position] = candidate;
        }
    }
    analyses.size = selectedCount;
}

bool AnytimeSearch::prepareNode(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, int width, long long &budget, long long &nodeCount) {
    Selection::Result &selected = pool.selected;
    vector<JavaEval::Analysis> &analyses = pool.analyses;
    vector<JavaEval::Peer> &peers = pool.peers;
    if (!pool.preparing) {
        Selection::find(state, shapeConfig, config.javaEval.selection, fiftyConfig, fiftyPool, selected, workspace.javaEval.selection, false);
        Selection::removeDead(selected, state, shapeConfig, workspace.javaEval.probability);
        pool.safe = false;
        for (int i = 0; i < (int)selected.cells.size; ++i)
            if (selected.observations.data[i].probability[9] == 0.0L)
                pool.safe = true;
        // peers 保留去重前的完整集合，强制安全签名和更安全替代格都以它为输入。
        peers.resize(0);
        for (int i = 0; i < (int)selected.cells.size; ++i)
            peers.push_back(JavaEval::Peer(selected.cells.data[i], selected.observations.data[i].probability[9]));
        int count = 0;
        for (int i = 0; i < (int)selected.cells.size; ++i) {
            bool duplicate = false;
            for (int j = 0; j < count; ++j)
                if (interchangeable(state, selected.cells.data[j], selected.cells.data[i]))
                    duplicate = true;
            if (duplicate)
                continue;
            selected.cells.data[count] = selected.cells.data[i];
            selected.observations.data[count] = selected.observations.data[i];
            ++count;
        }
        selected.cells.size = count;
        selected.observations.size = count;
        assert_(selected.cells.size != 0, "AnytimeSearch: 非终局没有内层候选");
        analyses.resize(0);
        pool.branches.data.resize(0);
        pool.next = 0;
        pool.preparing = true;
    }
    int remaining = 0, forced = 0;
    countSafe(state, remaining, forced);
    const int limit = pool.safe ? 1 : width;
    const span<const JavaEval::Peer> peerSpan(peers.data, (int)peers.size);
    while (pool.next < (int)selected.cells.size) {
        if (budget <= 0)
            return false;
        const int i = pool.next++;
        const long double floor = (int)analyses.size == limit ? analyses.data[limit - 1].value : -1.0L;
        const JavaEval::Analysis analysis = JavaEval::javaLite(state, shapeConfig, config.javaEval.progressContribution, workspace.javaEval, pool.branches, selected.cells.data[i], selected.observations.data[i], remaining, 0, pool.safe, peerSpan, floor);
        budget -= analysis.node;
        nodeCount += analysis.node;
        if (!analysis.valid)
            continue;
        analyses.push_back(analysis);
        selectCandidates(analyses, limit);
    }
    pool.preparing = false;
    return true;
}

void AnytimeSearch::orderMoves(Pool &pool, vectorPool<Pool::Node>::vector children, Pool::Frame &frame) {
    frame.order.resize(children.count);
    for (int i = 0; i < (int)children.count; ++i)
        frame.order.data[i] = (int)children.offset + i;
    std::sort(frame.order.data, frame.order.data + frame.order.size, [&](int left, int right) {
        const Pool::Node &lhs = pool.nodes.data.data[left], &rhs = pool.nodes.data.data[right];
        return lhs.value != rhs.value ? lhs.value > rhs.value : left < right;
    });
    frame.next = 0;
    frame.branch = 0;
    frame.value = 0.0L;
    frame.settled = true;
    frame.active = true;
}

void AnytimeSearch::appendBranches(Pool &pool, int index, const JavaEval::Analysis &analysis, const vectorPool<JavaEval::Branch> &values) {
    const int depth = pool.nodes.data.data[index].depth;
    vectorPool<Pool::Node>::vector branches = pool.nodes.push_back();
    for (int i = 0; i < (int)analysis.branches.count; ++i) {
        const int transition = (int)pool.transitions.size;
        pool.transitions.push_back(values.data.data[analysis.branches.offset + i]);
        pool.nodes.push_back(branches, Pool::Node(transition, depth + 1, true));
    }
    Pool::Node &candidate = pool.nodes.data.data[index];
    candidate.child = branches;
}

AnytimeSearch::Result AnytimeSearch::buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Result result;
    pool.nodes.data.resize(0);
    pool.transitions.resize(0);
    pool.preparing = false;
    for (Pool::Frame &frame : pool.frames)
        frame.active = false;
    vector<JavaEval::Analysis> &analyses = pool.analyses;
    const long long nodeCount = selectRoot(state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, cells, details);
    result.reason = pool.selected.reason;
    if (!config.fullAnalysis && cells == nullptr)
        pruneRoot(analyses, state, config.width[0], workspace);
    result.roots = pool.nodes.push_back();
    for (int i = 0; i < (int)analyses.size; ++i) {
        pool.nodes.push_back(result.roots, Pool::Node(analyses.data[i].cell, 0, false));
        Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
        candidate.root = true;
        candidate.value = analyses.data[i].value;
        candidate.multiplier = analyses.data[i].multiplier;
        candidate.finished = analyses.data[i].branches.count == 0;
    }
    for (int i = 0; i < (int)analyses.size; ++i)
        appendBranches(pool, (int)result.roots.offset + i, analyses.data[i], pool.branches);
    result.finished = true;
    for (int i = 0; i < (int)result.roots.count; ++i)
        result.finished = result.finished && pool.nodes.data.data[result.roots.offset + i].finished;
    result.node = nodeCount;
    return result;
}

long long AnytimeSearch::extend(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, Evaluation &evaluation, long long nodeBudget, int maxDepth, SuggestResult::Details *details) {
    if (pool.frames.empty()) pool.frames.emplace_back();
    Pool::Frame &frame = pool.frames[0];
    if (!frame.active) orderMoves(pool, result.roots, frame);
    long long budget = nodeBudget;
    long long nodeCount = 0;
    bool complete = true;
    while (frame.next < (int)frame.order.size) {
        const int index = frame.order.data[frame.next];
        extendCandidate(state, pool, index, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, workspace, evaluation, budget, nodeCount, complete);
        if (!complete) break;
        if (details) {
            const Pool::Node &candidate = pool.nodes.data.data[index];
            // 数字顺序与根分支顺序一致；只在整格完成后发布，保留上一完整层的子评分。
            int branch = 0;
            for (int i = 0; i < (int)details->branches.size; ++i)
                if (details->branches.data[i].cell == candidate.cell) details->branches.data[i].value = pool.nodes.data.data[candidate.child.offset + branch++].value;
            details->history.push_back(SuggestResult::Details::Layer(candidate.cell, maxDepth, candidate.value));
        }
        ++frame.next;
    }
    if (complete) {
        result.completedDepth = maxDepth;
        frame.active = false;
    }
    result.finished = complete;
    for (int i = 0; i < (int)result.roots.count; ++i)
        result.finished = result.finished && pool.nodes.data.data[result.roots.offset + i].finished;
    result.node += nodeCount;
    return budget;
}

void AnytimeSearch::extendPosition(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, Evaluation &evaluation, long long &budget, long long &nodeCount, bool &complete) {
    const int depth = pool.nodes.data.data[index].depth;
    if (!pool.nodes.data.data[index].prepared) {
        const int width = config.width[depth == 1 ? 1 : 2];
        if (budget <= 0 || !prepareNode(state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, width, budget, nodeCount)) {
            complete = false;
            return;
        }
        const vector<JavaEval::Analysis> &analyses = pool.analyses;
        vectorPool<Pool::Node>::vector candidates = pool.nodes.push_back();
        for (int i = 0; i < (int)analyses.size; ++i) {
            pool.nodes.push_back(candidates, Pool::Node(analyses.data[i].cell, depth, false));
            Pool::Node &candidate = pool.nodes.data.data[candidates.offset + i];
            candidate.value = analyses.data[i].value;
            candidate.multiplier = analyses.data[i].multiplier;
            candidate.finished = analyses.data[i].branches.count == 0;
        }
        pool.nodes.data.data[index].child = candidates;
        pool.nodes.data.data[index].prepared = true;
        pool.nodes.data.data[index].expandedDepth = depth;
        for (int i = 0; i < (int)analyses.size; ++i)
            appendBranches(pool, (int)candidates.offset + i, analyses.data[i], pool.branches);
        long double best = 0.0L;
        bool allFinished = true;
        for (int i = 0; i < (int)candidates.count; ++i) {
            const Pool::Node &candidate = pool.nodes.data.data[candidates.offset + i];
            if (i == 0 || candidate.value > best) best = candidate.value;
            allFinished = allFinished && candidate.finished;
        }
        pool.nodes.data.data[index].value = best;
        pool.nodes.data.data[index].finished = allFinished;
    }
    if (pool.nodes.data.data[index].expandedDepth >= maxDepth || pool.nodes.data.data[index].finished) return;
    if ((int)pool.frames.size() <= depth) pool.frames.resize(depth + 1);
    Pool::Frame &frame = pool.frames[depth];
    if (!frame.active) orderMoves(pool, pool.nodes.data.data[index].child, frame);
    while (frame.next < (int)frame.order.size) {
        extendCandidate(state, pool, frame.order.data[frame.next], maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, workspace, evaluation, budget, nodeCount, complete);
        if (!complete) return;
        ++frame.next;
    }
    const vectorPool<Pool::Node>::vector children = pool.nodes.data.data[index].child;
    long double best = 0.0L;
    bool allFinished = true;
    for (int i = 0; i < (int)children.count; ++i) {
        const Pool::Node &candidate = pool.nodes.data.data[children.offset + i];
        if (i == 0 || candidate.value > best) best = candidate.value;
        allFinished = allFinished && candidate.finished;
    }
    pool.nodes.data.data[index].value = best;
    pool.nodes.data.data[index].finished = allFinished;
    pool.nodes.data.data[index].expandedDepth = maxDepth;
    frame.active = false;
}

void AnytimeSearch::extendCandidate(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, Evaluation &evaluation, long long &budget, long long &nodeCount, bool &complete) {
    if (pool.nodes.data.data[index].finished) {
        pool.nodes.data.data[index].expandedDepth = maxDepth;
        return;
    }
    const int depth = pool.nodes.data.data[index].depth;
    const vectorPool<Pool::Node>::vector children = pool.nodes.data.data[index].child;
    const int horizon = maxDepth - depth;
    Pool::Frame &frame = pool.frames[depth];
    int safe = 0, forced = 0;
    countSafe(state, safe, forced);
    // 1.5 的 remainingSafe 是根状态总安全数，frameCount 是路径深度加一；换到当前盘面就是只剩一个安全格。
    const bool terminal = safe <= 1 || state.board.totalMines == state.basic.mineSum;
    while (frame.branch < (int)children.count) {
        const int i = frame.branch;
        const int branchIndex = (int)children.offset + i;
        const JavaEval::Branch &branch = pool.transitions.data[pool.nodes.data.data[branchIndex].transition];
        const int clears = branch.clears;
        const long double probability = branch.probability;
        long double value = 0.0L;
        bool finished = false;
        if (terminal) {
            value = 1.0L;
            finished = true;
        } else if (clears >= horizon + 2) {
            value = 1.0L;
            finished = false;
        } else if (pool.nodes.data.data[branchIndex].expandedDepth >= maxDepth || pool.nodes.data.data[branchIndex].finished) {
            value = pool.nodes.data.data[branchIndex].value;
            finished = pool.nodes.data.data[branchIndex].finished;
        } else {
            const JavaEval::Branch transition = branch;
            ObservedBoard::apply(state.board, transition.boardDelta, state.basicPool.board);
            Basic::applyDelta(state.basic, transition.basicDelta, state.basicPool);
            Structure::applyDelta(state.structure, state.shapes, transition.structureDelta);
            // 旧节点重访只重放增量；只有准备新节点才需要当前盘面的概率。
            const bool needProbability = !pool.nodes.data.data[branchIndex].prepared;
            if (needProbability) {
                if ((int)evaluation.probabilities.size() <= depth) evaluation.probabilities.resize(depth + 1);
                Probability::Result &nextProbability = evaluation.probabilities[depth];
                Probability::analyze(state.board, state.basic, state.structure, state.shapes, shapeConfig, state.distributions, nextProbability, workspace.javaEval.probability);
                swapProbability(state.probability, nextProbability);
            }
            extendPosition(state, pool, branchIndex, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, workspace, evaluation, budget, nodeCount, complete);
            value = pool.nodes.data.data[branchIndex].value;
            finished = pool.nodes.data.data[branchIndex].finished;
            if (needProbability) swapProbability(state.probability, evaluation.probabilities[depth]);
            Structure::applyDelta(state.structure, state.shapes, transition.structureDelta, true);
            Basic::applyDelta(state.basic, transition.basicDelta, state.basicPool, true);
            ObservedBoard::apply(state.board, transition.boardDelta, state.basicPool.board, true);
            if (!complete) return;
        }
        long double weight = probability;
        if (pool.nodes.data.data[index].root && !finished)
            weight *= pool.nodes.data.data[index].multiplier;
        frame.value += weight * value;
        frame.settled = frame.settled && finished;
        ++frame.branch;
        pool.nodes.data.data[branchIndex].value = value;
        pool.nodes.data.data[branchIndex].finished = finished;
    }
    pool.nodes.data.data[index].value = frame.value;
    pool.nodes.data.data[index].finished = frame.settled;
    pool.nodes.data.data[index].expandedDepth = maxDepth;
    if (pool.nodes.data.data[index].root && frame.settled)
        pool.nodes.data.data[index].multiplier = 1.0L;
    frame.branch = 0;
    frame.value = 0.0L;
    frame.settled = true;
}

AnytimeSearch::Result AnytimeSearch::analyze(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    if (details) details->clear();
    Result result = buildRoot(state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, cells, details);
    if (details)
        for (int i = 0; i < (int)result.roots.count; ++i) {
            const Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
            details->history.push_back(SuggestResult::Details::Layer(candidate.cell, 0, candidate.value));
        }
    update(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, details);
    return result;
}

void AnytimeSearch::update(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, SuggestResult::Details *details) {
    Evaluation evaluation;
    long long budget = config.nodeLimit;
    while (!result.finished) {
        const int beforeDepth = result.completedDepth;
        budget = extend(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, evaluation, budget, beforeDepth + 1, details);
        if (result.completedDepth == beforeDepth)
            break;
    }
}

} // 命名空间 mss
