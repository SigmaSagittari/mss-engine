#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"

#include <algorithm>
#include <new>

#include "core/assert.h"

namespace mss {

struct AnytimeSearch::Pool::ProbabilityLayer {
    Probability::Pool pool;
    vector<Probability::Result> results;
    int remaining; // 还没有完成招法生成、也未永久终结的局面数。

    ProbabilityLayer(const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Combination::Pool &combinations) : pool(shapes, distributions, combinations), remaining(0) {}
};

AnytimeSearch::Pool::~Pool() {
    clearProbabilities();
}

void AnytimeSearch::Pool::clear() {
    nodes.data.resize(0);
    deltas.resize(0);
    clearProbabilities();
    for (Frame &frame : frames)
        frame.active = false;
}

void AnytimeSearch::Pool::clearProbabilities() {
    for (int depth = 0; depth < (int)probabilities.size; ++depth)
        delete probabilities.data[depth];
    probabilities.resize(0);
}

void AnytimeSearch::Pool::releaseProbabilities() {
    for (int depth = 1; depth < (int)probabilities.size; ++depth) {
        ProbabilityLayer *layer = probabilities.data[depth];
        if (layer == nullptr) continue;
        // 依次释放；前一层仍有待展开局面时，后一层还可能继续接收新结果。
        if (layer->remaining != 0) break;
        delete layer;
        probabilities.data[depth] = nullptr;
    }
}

ObservedBoard::CellId AnytimeSearch::Result::recommended(const Pool &pool) const {
    const Pool::Node *best = nullptr;
    long double bestScore;
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

void AnytimeSearch::selectRoot(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Selection::Result &selected = pool.selected;
    vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
    evaluatedMoves.resize(0);
    pool.pending.clear();
    pool.pending.probabilityPool.clear();
    result.nodes += Selection().begin(state, state.probabilityPool, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, Selection::Mode::Root, cells);
    result.reason = selected.reason();
    while (selected.next < (int)selected.cells.size) {
        const Selection::Evaluation evaluated = Selection().next(selected, state, state.probabilityPool, shapeConfig, config.selection, fiftyConfig, &pool.pending, -1.0L, details);
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

long long AnytimeSearch::prepareNode(PositionState &state, const Probability::Pool &probabilities, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, int width) {
    Selection::Result &selected = pool.selected;
    vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
    evaluatedMoves.resize(0);
    pool.pending.clear();
    pool.pending.probabilityPool.clear();
    long long nodes = Selection().begin(state, probabilities, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, Selection::Mode::Inner);
    assert_(selected.cells.size != 0, "AnytimeSearch: 非终局没有内层候选");
    const int limit = selected.source == Selection::Result::Source::Safe ? 1 : width;
    while (selected.next < (int)selected.cells.size) {
        const long double floor = (int)evaluatedMoves.size == limit ? evaluatedMoves.data[limit - 1].score() : -1.0L;
        const Selection::Evaluation evaluated = Selection().next(selected, state, probabilities, shapeConfig, config.selection, fiftyConfig, &pool.pending, floor);
        nodes += evaluated.nodes();
        if (!evaluated.javaLite.valid) continue;
        evaluatedMoves.push_back(evaluated);
        selectCandidates(evaluatedMoves, limit);
    }
    return nodes;
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

void AnytimeSearch::appendDeltas(Pool &pool, int index, const JavaLite::Result &evaluated, const JavaLite::Pool &values) {
    const int depth = pool.nodes.data.data[index].depth;
    vectorPool<Pool::Node>::vector branches = pool.nodes.push_back();
    if (evaluated.deltas.count != 0) {
        while ((int)pool.probabilities.size <= depth + 1) pool.probabilities.push_back(nullptr);
        if (pool.probabilities.data[depth + 1] == nullptr) pool.probabilities.data[depth + 1] = new Pool::ProbabilityLayer(pool.pending.probabilityPool.shapes, pool.pending.probabilityPool.distributions, pool.pending.probabilityPool.combinations);
    }
    for (int i = 0; i < (int)evaluated.deltas.count; ++i) {
        Pool::ProbabilityLayer &layer = *pool.probabilities.data[depth + 1];
        const int probabilityIndex = layer.results.size;
        const Probability::Result &source = values.probabilities.data[evaluated.deltas.offset + i];
        vectorPool<Probability::Result::Component>::vector components = layer.pool.components.push_back();
        for (int j = 0; j < (int)source.components.count; ++j)
            layer.pool.components.push_back(components, values.probabilityPool.components.data.data[source.components.offset + j]);
        vectorPool<long double>::vector probabilities = layer.pool.boxProbabilities.push_back();
        for (int j = 0; j < (int)source.boxProbabilities.count; ++j)
            layer.pool.boxProbabilities.push_back(probabilities, values.probabilityPool.boxProbabilities.data.data[source.boxProbabilities.offset + j]);
        layer.results.push_back({components, probabilities, source.candidates, source.tCellProbability});
        ++layer.remaining;
        const int delta = (int)pool.deltas.size;
        pool.deltas.push_back(values.deltas.data.data[evaluated.deltas.offset + i]);
        const int positionIndex = pool.nodes.data.size;
        pool.nodes.data.resize(positionIndex + 1);
        ++branches.count;
        new (pool.nodes.data.data + positionIndex) Pool::Node(delta, probabilityIndex, depth + 1);
    }
    Pool::Node &candidate = pool.nodes.data.data[index];
    candidate.child = branches;
}

AnytimeSearch::Result AnytimeSearch::buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Result result;
    pool.clear();
    vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
    AnytimeSearch().selectRoot(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, cells, details);
    if (cells == nullptr) {
        std::stable_sort(evaluatedMoves.data, evaluatedMoves.data + evaluatedMoves.size, [](const Selection::Evaluation &left, const Selection::Evaluation &right) { return left.score() > right.score(); });
        if (config.width[0] > 0 && (int)evaluatedMoves.size > config.width[0]) evaluatedMoves.size = config.width[0];
    }
    result.roots = pool.nodes.push_back();
    pool.rootCompensations.resize((int)result.roots.offset + evaluatedMoves.size);
    for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
        pool.nodes.push_back(result.roots, Pool::Node(evaluatedMoves.data[i], 0));
        Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
        pool.rootCompensations.data[result.roots.offset + i] = candidate.finished ? 0.0L : evaluatedMoves.data[i].compensation;
    }
    for (int i = 0; i < (int)evaluatedMoves.size; ++i)
        appendDeltas(pool, (int)result.roots.offset + i, evaluatedMoves.data[i].javaLite, pool.pending);
    result.finished = true;
    for (int i = 0; i < (int)result.roots.count; ++i)
        result.finished = result.finished && pool.nodes.data.data[result.roots.offset + i].finished;
    return result;
}

long long AnytimeSearch::extend(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long nodeBudget, int maxDepth, SuggestResult::Details *details) {
    if (pool.frames.empty()) pool.frames.emplace_back();
    Pool::Frame &frame = pool.frames[0];
    if (!frame.active) orderMoves(pool, result.roots, frame);
    long long budget = nodeBudget;
    bool complete = true;
    while (frame.next < (int)frame.order.size) {
        const int index = frame.order.data[frame.next];
        AnytimeSearch().extendCandidate(state, pool, index, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, budget, complete);
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
    result.nodes += nodeBudget - budget;
    return budget;
}

void AnytimeSearch::extendPosition(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long &budget, bool &complete) {
    const int depth = pool.nodes.data.data[index].depth;
    if (!pool.nodes.data.data[index].prepared) {
        const int width = config.width[depth == 1 ? 1 : 2];
        budget -= AnytimeSearch().prepareNode(state, pool.probabilities.data[depth]->pool, pool, shapeConfig, config, fiftyConfig, fiftyPool, width);
        const vector<Selection::Evaluation> &evaluatedMoves = pool.evaluatedMoves;
        vectorPool<Pool::Node>::vector candidates = pool.nodes.push_back();
        for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
            pool.nodes.push_back(candidates, Pool::Node(evaluatedMoves.data[i], depth));
        }
        pool.nodes.data.data[index].child = candidates;
        pool.nodes.data.data[index].prepared = true;
        pool.nodes.data.data[index].expandedDepth = depth;
        for (int i = 0; i < (int)evaluatedMoves.size; ++i)
            appendDeltas(pool, (int)candidates.offset + i, evaluatedMoves.data[i].javaLite, pool.pending);
        --pool.probabilities.data[depth]->remaining;
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
        AnytimeSearch().extendCandidate(state, pool, frame.order.data[frame.next], maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, budget, complete);
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

void AnytimeSearch::extendCandidate(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long &budget, bool &complete) {
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
        long double value;
        bool finished;
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
            ObservedBoard().applyDelta(state.board, delta.boardDelta, state.basicPool.board);
            Basic().applyDelta(state.basic, delta.basicDelta, state.basicPool);
            Structure().applyDelta(state.structure, delta.structureDelta, state.shapes);
            const Probability::Result previousProbability = state.probability;
            // 完整概率由评分时直接写池；生成招法前借用本层结果，重访旧节点只重放增量。
            if (needProbability) {
                const Pool::Node &position = pool.nodes.data.data[branchIndex];
                state.probability = pool.probabilities.data[position.depth]->results.data[position.probabilityIndex];
            }
            AnytimeSearch().extendPosition(state, pool, branchIndex, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, budget, complete);
            value = pool.nodes.data.data[branchIndex].value;
            finished = pool.nodes.data.data[branchIndex].finished;
            state.probability = previousProbability;
            Structure().applyDelta(state.structure, delta.structureDelta, state.shapes, true);
            Basic().applyDelta(state.basic, delta.basicDelta, state.basicPool, true);
            ObservedBoard().applyDelta(state.board, delta.boardDelta, state.basicPool.board, true);
            if (!complete) return;
        }
        frame.value += probability * value;
        frame.settled = frame.settled && finished;
        ++frame.nextOutcome;
        pool.nodes.data.data[branchIndex].value = value;
        // 永久终结的局面以后也不会生成招法，结束其完整概率的用途。
        if (finished && !pool.nodes.data.data[branchIndex].finished && !pool.nodes.data.data[branchIndex].prepared) --pool.probabilities.data[depth + 1]->remaining;
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

AnytimeSearch::Result AnytimeSearch::analyze(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    if (details) details->clear();
    Result result = AnytimeSearch().buildRoot(state, pool, shapeConfig, config, fiftyConfig, fiftyPool, cells, details);
    if (details)
        for (int i = 0; i < (int)result.roots.count; ++i) {
            const Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
            details->history.push_back(SuggestResult::Details::Layer(candidate.cell, 0, pool.score((int)result.roots.offset + i)));
        }
    AnytimeSearch().update(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, details);
    return result;
}

void AnytimeSearch::update(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, SuggestResult::Details *details) {
    if (config.nodeLimit == 0) return;
    long long budget = config.nodeLimit;
    while (!result.finished) {
        const int beforeDepth = result.completedDepth;
        budget = AnytimeSearch().extend(result, state, pool, shapeConfig, config, fiftyConfig, fiftyPool, budget, beforeDepth + 1, details);
        pool.releaseProbabilities();
        if (result.completedDepth == beforeDepth)
            break;
    }
    if (result.finished) pool.clearProbabilities();
}

} // 命名空间 mss
