#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"

#include <algorithm>
#include <utility>

#include "core/assert.h"

namespace mss {

void AnytimeSearch::swapProbability(Probability::Result &left, Probability::Result &right) {
    left.components.swap(right.components);
    left.boxProbabilities.swap(right.boxProbabilities);
    std::swap(left.candidates, right.candidates);
    std::swap(left.tCellProbability, right.tCellProbability);
}

ObservedBoard::CellId AnytimeSearch::Result::recommended(const Pool &pool) const {
    const Pool::Node *best = nullptr;
    long double bestScore = 0.0L;
    for (int i = 0; i < (int)roots.count; ++i) {
        const int index = (int)roots.offset + i;
        const Pool::Node &candidate = pool.nodes.data.data[index];
        const long double score = pool.score(index);
        if (best == nullptr || candidate.expandedDepth > best->expandedDepth || (candidate.expandedDepth == best->expandedDepth && score > bestScore)) {
            best = &candidate;
            bestScore = score;
        }
    }
    return best != nullptr ? best->cell : -1;
}

SuggestResult AnytimeSearch::Result::snapshot(const Pool &pool) const {
    SuggestResult result(SuggestResult::Engine::Anytime, reason);
    result.nodes = nodes;
    for (int i = 0; i < (int)roots.count; ++i) {
        const Pool::Node &candidate = pool.nodes.data.data[roots.offset + i];
        result.candidates.push_back(SuggestResult::Candidate(candidate.cell, pool.score((int)roots.offset + i)));
    }
    const ObservedBoard::CellId cell = recommended(pool);
    if (cell >= 0) result.suggest.push_back(cell);
    return result;
}

void AnytimeSearch::selectRoot(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Selection::Result &selected = pool.selected;
    vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
    evaluatedMoves.resize(0);
    pool.pendingDeltas.data.resize(0);
    result.nodes += Selection::begin(state, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, workspace.selection, Selection::Mode::Root, &pool.pendingDeltas, cells);
    result.reason = selected.reason();
    while (selected.next < (int)selected.cells.size) {
        const Selection::Evaluation evaluated = Selection::next(selected, state, shapeConfig, config.selection, fiftyConfig, workspace.selection, -1.0L, details);
        result.nodes += evaluated.nodes();
        if (evaluated.javaLite.valid) evaluatedMoves.push_back(evaluated);
    }
}

void AnytimeSearch::selectCandidates(vector<Selection::Evaluation> &evaluatedMoves, int limit) {
    int selectedCount = 0;
    if (limit <= 0) {
        evaluatedMoves.size = 0;
        return;
    }
    for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
        const Selection::Evaluation candidate = evaluatedMoves.data[i];
        int position = 0;
        while (position < selectedCount && (evaluatedMoves.data[position].score() > candidate.score() || (evaluatedMoves.data[position].score() == candidate.score() && evaluatedMoves.data[position].cell < candidate.cell)))
            ++position;
        if (selectedCount < limit) {
            for (int j = selectedCount; j > position; --j)
                evaluatedMoves.data[j] = evaluatedMoves.data[j - 1];
            evaluatedMoves.data[position] = candidate;
            ++selectedCount;
        } else if (position < limit) {
            for (int j = limit - 1; j > position; --j)
                evaluatedMoves.data[j] = evaluatedMoves.data[j - 1];
            evaluatedMoves.data[position] = candidate;
        }
    }
    evaluatedMoves.size = selectedCount;
}

bool AnytimeSearch::prepareNode(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, int width, long long &budget, long long &nodeCount) {
    Selection::Result &selected = pool.selected;
    vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
    if (!pool.preparing) {
        evaluatedMoves.resize(0);
        pool.pendingDeltas.data.resize(0);
        const int nodes = Selection::begin(state, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, workspace.selection, Selection::Mode::Inner, &pool.pendingDeltas);
        budget -= nodes;
        nodeCount += nodes;
        assert_(selected.cells.size != 0, "AnytimeSearch: 非终局没有内层候选");
        pool.preparing = true;
    }
    const int limit = selected.source == Selection::Result::Source::Safe ? 1 : width;
    while (selected.next < (int)selected.cells.size) {
        if (budget <= 0) return false;
        const long double floor = (int)evaluatedMoves.size == limit ? evaluatedMoves.data[limit - 1].score() : -1.0L;
        const Selection::Evaluation evaluated = Selection::next(selected, state, shapeConfig, config.selection, fiftyConfig, workspace.selection, floor);
        budget -= evaluated.nodes();
        nodeCount += evaluated.nodes();
        if (!evaluated.javaLite.valid) continue;
        evaluatedMoves.push_back(evaluated);
        selectCandidates(evaluatedMoves, limit);
    }
    pool.preparing = false;
    return true;
}

void AnytimeSearch::orderMoves(Pool &pool, vectorPool<Pool::Node>::vector children, Pool::Frame &frame) {
    frame.order.resize(children.count);
    for (int i = 0; i < (int)children.count; ++i)
        frame.order.data[i] = (int)children.offset + i;
    std::sort(frame.order.data, frame.order.data + frame.order.size, [&](int left, int right) {
        return pool.score(left) != pool.score(right) ? pool.score(left) > pool.score(right) : left < right;
    });
    frame.next = 0;
    frame.nextOutcome = 0;
    frame.value = 0.0L;
    frame.settled = true;
    frame.active = true;
}

void AnytimeSearch::appendDeltas(Pool &pool, int index, const JavaLite::Result &evaluated, const vectorPool<JavaLite::Delta> &values) {
    const int depth = pool.nodes.data.data[index].depth;
    vectorPool<Pool::Node>::vector branches = pool.nodes.push_back();
    for (int i = 0; i < (int)evaluated.deltas.count; ++i) {
        const int delta = (int)pool.deltas.size;
        pool.deltas.push_back(values.data.data[evaluated.deltas.offset + i]);
        pool.nodes.push_back(branches, Pool::Node(delta, depth + 1, true));
    }
    Pool::Node &candidate = pool.nodes.data.data[index];
    candidate.child = branches;
}

AnytimeSearch::Result AnytimeSearch::buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Result result;
    pool.nodes.data.resize(0);
    pool.deltas.resize(0);
    pool.preparing = false;
    for (Pool::Frame &frame : pool.frames)
        frame.active = false;
    vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
    selectRoot(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, cells, details);
    if (cells == nullptr) {
        std::stable_sort(evaluatedMoves.data, evaluatedMoves.data + evaluatedMoves.size, [](const Selection::Evaluation &left, const Selection::Evaluation &right) { return left.score() > right.score(); });
        if (config.width[0] > 0 && (int)evaluatedMoves.size > config.width[0]) evaluatedMoves.size = config.width[0];
    }
    result.roots = pool.nodes.push_back();
    pool.rootCompensations.resize((int)result.roots.offset + evaluatedMoves.size);
    for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
        pool.nodes.push_back(result.roots, Pool::Node(evaluatedMoves.data[i].cell, 0, false));
        Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
        candidate.root = true;
        candidate.value = evaluatedMoves.data[i].javaLite.value;
        candidate.progressMultiplier = evaluatedMoves.data[i].javaLite.progressMultiplier;
        candidate.finished = evaluatedMoves.data[i].javaLite.deltas.count == 0;
        pool.rootCompensations.data[result.roots.offset + i] = candidate.finished ? 0.0L : evaluatedMoves.data[i].compensation;
    }
    for (int i = 0; i < (int)evaluatedMoves.size; ++i)
        appendDeltas(pool, (int)result.roots.offset + i, evaluatedMoves.data[i].javaLite, pool.pendingDeltas);
    result.finished = true;
    for (int i = 0; i < (int)result.roots.count; ++i)
        result.finished = result.finished && pool.nodes.data.data[result.roots.offset + i].finished;
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
            for (int i = 0; i < (int)details->outcomes.size; ++i)
                if (details->outcomes.data[i].cell == candidate.cell) details->outcomes.data[i].value = pool.nodes.data.data[candidate.child.offset + branch++].value;
            details->history.push_back(SuggestResult::Details::Layer(candidate.cell, maxDepth, pool.score(index)));
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
    result.nodes += nodeCount;
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
        const vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
        vectorPool<Pool::Node>::vector candidates = pool.nodes.push_back();
        for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
            pool.nodes.push_back(candidates, Pool::Node(evaluatedMoves.data[i].cell, depth, false));
            Pool::Node &candidate = pool.nodes.data.data[candidates.offset + i];
            candidate.value = evaluatedMoves.data[i].javaLite.value;
            candidate.progressMultiplier = evaluatedMoves.data[i].javaLite.progressMultiplier;
            candidate.finished = evaluatedMoves.data[i].javaLite.deltas.count == 0;
        }
        pool.nodes.data.data[index].child = candidates;
        pool.nodes.data.data[index].prepared = true;
        pool.nodes.data.data[index].expandedDepth = depth;
        for (int i = 0; i < (int)evaluatedMoves.size; ++i)
            appendDeltas(pool, (int)candidates.offset + i, evaluatedMoves.data[i].javaLite, pool.pendingDeltas);
        long double best = 0.0L, bestScore = 0.0L;
        bool allFinished = true;
        for (int i = 0; i < (int)candidates.count; ++i) {
            const Pool::Node &candidate = pool.nodes.data.data[candidates.offset + i];
            if (i == 0 || candidate.score() > bestScore) { best = candidate.value; bestScore = candidate.score(); }
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
    long double best = 0.0L, bestScore = 0.0L;
    bool allFinished = true;
    for (int i = 0; i < (int)children.count; ++i) {
        const Pool::Node &candidate = pool.nodes.data.data[children.offset + i];
        if (i == 0 || candidate.score() > bestScore) { best = candidate.value; bestScore = candidate.score(); }
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
    int safe = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col)
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) --safe;
    // 1.5 的 remainingSafe 是根状态总安全数，frameCount 是路径深度加一；换到当前盘面就是只剩一个安全格。
    const bool terminal = safe <= 1 || state.board.totalMines == state.basic.mineSum;
    while (frame.nextOutcome < (int)children.count) {
        const int i = frame.nextOutcome;
        const int branchIndex = (int)children.offset + i;
        const JavaLite::Delta &branch = pool.deltas.data[pool.nodes.data.data[branchIndex].deltaIndex];
        const int clears = branch.knownSafeCount;
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
            const bool needProbability = !pool.nodes.data.data[branchIndex].prepared;
            if (needProbability && budget <= 0) {
                complete = false;
                return;
            }
            const JavaLite::Delta delta = branch;
            ObservedBoard::apply(state.board, delta.boardDelta, state.basicPool.board);
            Basic::applyDelta(state.basic, delta.basicDelta, state.basicPool);
            Structure::applyDelta(state.structure, state.shapes, delta.structureDelta);
            // 旧节点重访只重放增量；只有准备新节点才需要当前盘面的概率。
            if (needProbability) {
                if ((int)evaluation.probabilities.size() <= depth) evaluation.probabilities.resize(depth + 1);
                Probability::Result &nextProbability = evaluation.probabilities[depth];
                Probability::analyze(state.board, state.basic, state.structure, state.shapes, shapeConfig, state.distributions, nextProbability, workspace.selection.probability);
                ++nodeCount;
                swapProbability(state.probability, nextProbability);
            }
            extendPosition(state, pool, branchIndex, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, workspace, evaluation, budget, nodeCount, complete);
            value = pool.nodes.data.data[branchIndex].value;
            finished = pool.nodes.data.data[branchIndex].finished;
            if (needProbability) {
                // 与后续准备一起结算，预算为 1 时仍能推进，避免只重算局面而无法前进。
                --budget;
                swapProbability(state.probability, evaluation.probabilities[depth]);
            }
            Structure::applyDelta(state.structure, state.shapes, delta.structureDelta, true);
            Basic::applyDelta(state.basic, delta.basicDelta, state.basicPool, true);
            ObservedBoard::apply(state.board, delta.boardDelta, state.basicPool.board, true);
            if (!complete) return;
        }
        frame.value += probability * value;
        frame.settled = frame.settled && finished;
        ++frame.nextOutcome;
        pool.nodes.data.data[branchIndex].value = value;
        pool.nodes.data.data[branchIndex].finished = finished;
    }
    pool.nodes.data.data[index].value = frame.value;
    pool.nodes.data.data[index].finished = frame.settled;
    pool.nodes.data.data[index].expandedDepth = maxDepth;
    if (pool.nodes.data.data[index].root && frame.settled) {
        pool.nodes.data.data[index].progressMultiplier = 1.0L;
        pool.rootCompensations.data[index] = 0.0L;
    }
    frame.nextOutcome = 0;
    frame.value = 0.0L;
    frame.settled = true;
}

AnytimeSearch::Result AnytimeSearch::analyze(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    if (details) details->clear();
    Result result = buildRoot(state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, cells, details);
    if (details)
        for (int i = 0; i < (int)result.roots.count; ++i) {
            const Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
            details->history.push_back(SuggestResult::Details::Layer(candidate.cell, 0, pool.score((int)result.roots.offset + i)));
        }
    update(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, workspace, details);
    return result;
}

void AnytimeSearch::update(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, SuggestResult::Details *details) {
    if (config.nodeLimit == 0) return;
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
