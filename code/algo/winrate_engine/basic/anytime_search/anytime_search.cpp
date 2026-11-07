#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"

#include <algorithm>
#include <new>

#include "core/assert.h"

namespace mss {

struct AnytimeSearch::Workspace {
    vectorPool<int> order;
    vector<int> path;
    Selection::Result selected;
    vector<Selection::Evaluation> evaluatedMoves;
};

__declspec(noinline) AnytimeSearch::Workspace &AnytimeSearch::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

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
    nextPosition = -1;
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

void AnytimeSearch::selectRoot(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Selection::Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Selection::Result &selected, vector<Selection::Evaluation> &evaluatedMoves, JavaLite::Pool &pending, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    evaluatedMoves.resize(0);
    pending.clear();
    pending.probabilityPool.clear();
    result.nodes += Selection().begin(state, state.probabilityPool, shapeConfig, config, fiftyConfig, fiftyPool, selected, Selection::Mode::Root, cells);
    result.reason = selected.reason();
    while (selected.next < (int)selected.cells.size) {
        const Selection::Evaluation evaluated = Selection().next(selected, state, state.probabilityPool, shapeConfig, config, fiftyConfig, &pending, -1.0L, details);
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

long long AnytimeSearch::prepareNode(PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Selection::Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Selection::Result &selected, vector<Selection::Evaluation> &evaluatedMoves, JavaLite::Pool &pending, int width) {
    evaluatedMoves.resize(0);
    pending.clear();
    pending.probabilityPool.clear();
    long long nodes = Selection().begin(state, probabilities, shapeConfig, config, fiftyConfig, fiftyPool, selected, Selection::Mode::Inner);
    assert_(selected.cells.size != 0, "AnytimeSearch: 非终局没有内层候选");
    const int limit = selected.source == Selection::Result::Source::Safe ? 1 : width;
    while (selected.next < (int)selected.cells.size) {
        const long double floor = (int)evaluatedMoves.size == limit ? evaluatedMoves.data[limit - 1].score() : -1.0L;
        const Selection::Evaluation evaluated = Selection().next(selected, state, probabilities, shapeConfig, config, fiftyConfig, &pending, floor);
        nodes += evaluated.nodes();
        if (!evaluated.javaLite.valid) continue;
        evaluatedMoves.push_back(evaluated);
        selectCandidates(evaluatedMoves, limit);
    }
    return nodes;
}

vectorPool<int>::vector AnytimeSearch::orderMoves(const Pool &pool, vectorPool<Pool::Node>::vector children, int maxDepth, int resumeMove, vectorPool<int> &order) {
    vectorPool<int>::vector moves = order.push_back();
    if (resumeMove >= 0) order.push_back(moves, resumeMove);
    for (int i = 0; i < (int)children.count; ++i) {
        const int index = (int)children.offset + i;
        const Pool::Node &candidate = pool.nodes.data.data[index];
        if (index != resumeMove && !candidate.finished && candidate.expandedDepth < maxDepth)
            order.push_back(moves, index);
    }
    const int first = resumeMove >= 0 ? 1 : 0;
    if ((int)moves.count > first + 1) {
        auto priority = [&](int index) {
            const Pool::Node &candidate = pool.nodes.data.data[index];
            return candidate.value * candidate.progressMultiplier + (candidate.root ? pool.rootCompensations.data[index] : 0.0L);
        };
        std::sort(order.data.data + moves.offset + first, order.data.data + moves.offset + moves.count, [&](int left, int right) {
            const long double leftPriority = priority(left), rightPriority = priority(right);
            return leftPriority != rightPriority ? leftPriority > rightPriority : left < right;
        });
    }
    return moves;
}

void AnytimeSearch::appendDeltas(Pool &pool, int index, const JavaLite::Result &evaluated, const JavaLite::Pool &values) {
    const int depth = pool.nodes.data.data[index].depth;
    vectorPool<Pool::Node>::vector branches = pool.nodes.push_back();
    if (evaluated.deltas.count != 0) {
        while ((int)pool.probabilities.size <= depth + 1) pool.probabilities.push_back(nullptr);
        if (pool.probabilities.data[depth + 1] == nullptr) pool.probabilities.data[depth + 1] = new Pool::ProbabilityLayer(values.probabilityPool.shapes, values.probabilityPool.distributions, values.probabilityPool.combinations);
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
        new (pool.nodes.data.data + positionIndex) Pool::Node(delta, probabilityIndex, depth + 1, index);
    }
    Pool::Node &candidate = pool.nodes.data.data[index];
    candidate.child = branches;
}

AnytimeSearch::Result AnytimeSearch::buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details) {
    Result result;
    pool.clear();
    Selection::Result &selected = getWorkspace().selected;
    vector<Selection::Evaluation> &evaluatedMoves = getWorkspace().evaluatedMoves;
    selectRoot(result, state, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, evaluatedMoves, pool.pending, cells, details);
    if (cells == nullptr) {
        std::stable_sort(evaluatedMoves.data, evaluatedMoves.data + evaluatedMoves.size, [](const Selection::Evaluation &left, const Selection::Evaluation &right) { return left.score() > right.score(); });
        if (config.width[0] > 0 && (int)evaluatedMoves.size > config.width[0]) evaluatedMoves.size = config.width[0];
    }
    result.roots = pool.nodes.push_back();
    pool.rootCompensations.resize((int)result.roots.offset + evaluatedMoves.size);
    for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
        pool.nodes.push_back(result.roots, Pool::Node(evaluatedMoves.data[i], 0, -1));
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

int AnytimeSearch::extendPosition(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long &budget, span<const int> resume) {
    const int depth = pool.nodes.data.data[index].depth;
    if (!pool.nodes.data.data[index].prepared) {
        const int width = config.width[depth == 1 ? 1 : 2];
        Selection::Result &selected = getWorkspace().selected;
        vector<Selection::Evaluation> &evaluatedMoves = getWorkspace().evaluatedMoves;
        budget -= prepareNode(state, pool.probabilities.data[depth]->pool, shapeConfig, config.selection, fiftyConfig, fiftyPool, selected, evaluatedMoves, pool.pending, width);
        vectorPool<Pool::Node>::vector candidates = pool.nodes.push_back();
        for (int i = 0; i < (int)evaluatedMoves.size; ++i) {
            pool.nodes.push_back(candidates, Pool::Node(evaluatedMoves.data[i], depth, index));
        }
        pool.nodes.data.data[index].child = candidates;
        pool.nodes.data.data[index].prepared = true;
        pool.nodes.data.data[index].expandedDepth = depth;
        for (int i = 0; i < (int)evaluatedMoves.size; ++i)
            appendDeltas(pool, (int)candidates.offset + i, evaluatedMoves.data[i].javaLite, pool.pending);
        --pool.probabilities.data[depth]->remaining;
        long double best = 0.0L;
        bool allFinished = true;
        for (int i = 0; i < (int)candidates.count; ++i) {
            const Pool::Node &candidate = pool.nodes.data.data[candidates.offset + i];
            best = (std::max)(best, candidate.value);
            allFinished = allFinished && candidate.finished;
        }
        pool.nodes.data.data[index].value = best;
        pool.nodes.data.data[index].finished = allFinished;
    }
    if (pool.nodes.data.data[index].expandedDepth >= maxDepth || pool.nodes.data.data[index].finished) return -1;
    const vectorPool<Pool::Node>::vector children = pool.nodes.data.data[index].child;
    const int resumeMove = resume.size != 0 ? resume.data[--resume.size] : -1;
    vectorPool<int> &order = getWorkspace().order;
    const vectorPool<int>::vector moves = orderMoves(pool, children, maxDepth, resumeMove, order);
    for (int i = 0; i < (int)moves.count; ++i) {
        const int move = order.data.data[moves.offset + i];
        const int nextPosition = extendCandidate(state, pool, move, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, budget, move == resumeMove ? resume : span<const int>());
        if (nextPosition >= 0) {
            order.pop_back(moves);
            return nextPosition;
        }
    }
    order.pop_back(moves);
    long double best = 0.0L;
    bool allFinished = true;
    for (int i = 0; i < (int)children.count; ++i) {
        const Pool::Node &candidate = pool.nodes.data.data[children.offset + i];
        best = (std::max)(best, candidate.value);
        allFinished = allFinished && candidate.finished;
    }
    pool.nodes.data.data[index].value = best;
    pool.nodes.data.data[index].finished = allFinished;
    pool.nodes.data.data[index].expandedDepth = maxDepth;
    return -1;
}

int AnytimeSearch::extendCandidate(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long &budget, span<const int> resume) {
    const int depth = pool.nodes.data.data[index].depth;
    const vectorPool<Pool::Node>::vector children = pool.nodes.data.data[index].child;
    const int horizon = maxDepth - depth;
    const int resumePosition = resume.size != 0 ? resume.data[--resume.size] : -1;
    long double sum = 0.0L;
    bool settled = true;
    int safe = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col)
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) --safe;
    // 1.5 的 remainingSafe 是根状态总安全数，frameCount 是路径深度加一；换到当前盘面就是只剩一个安全格。
    const bool terminal = safe <= 1 || state.board.totalMines == state.basic.mineSum;
    for (int i = 0; i < (int)children.count; ++i) {
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
        } else if (branchIndex < resumePosition || pool.nodes.data.data[branchIndex].expandedDepth >= maxDepth || pool.nodes.data.data[branchIndex].finished) {
            value = pool.nodes.data.data[branchIndex].value;
            finished = pool.nodes.data.data[branchIndex].finished;
        } else {
            const bool needProbability = !pool.nodes.data.data[branchIndex].prepared;
            if (needProbability && budget <= 0) return branchIndex;
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
            const int nextPosition = extendPosition(state, pool, branchIndex, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, budget, branchIndex == resumePosition ? resume : span<const int>());
            state.probability = previousProbability;
            Structure().applyDelta(state.structure, delta.structureDelta, state.shapes, true);
            Basic().applyDelta(state.basic, delta.basicDelta, state.basicPool, true);
            ObservedBoard().applyDelta(state.board, delta.boardDelta, state.basicPool.board, true);
            if (nextPosition >= 0) return nextPosition;
            value = pool.nodes.data.data[branchIndex].value;
            finished = pool.nodes.data.data[branchIndex].finished;
        }
        sum += probability * value;
        settled = settled && finished;
        pool.nodes.data.data[branchIndex].value = value;
        // 永久终结的局面以后也不会生成招法，结束其完整概率的用途。
        if (finished && !pool.nodes.data.data[branchIndex].finished && !pool.nodes.data.data[branchIndex].prepared) --pool.probabilities.data[depth + 1]->remaining;
        pool.nodes.data.data[branchIndex].finished = finished;
    }
    pool.nodes.data.data[index].value = sum;
    pool.nodes.data.data[index].finished = settled;
    pool.nodes.data.data[index].expandedDepth = maxDepth;
    return -1;
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
    if (config.nodeLimit == 0 || result.finished) return;
    vector<int> &path = getWorkspace().path;
    vectorPool<int> &order = getWorkspace().order;
    for (int index = pool.nextPosition; index >= 0; index = pool.nodes.data.data[index].parent)
        path.push_back(index);
    int nextPosition = -1;
    long long budget = config.nodeLimit;
    while (!result.finished) {
        const int maxDepth = result.completedDepth + 1;
        span<const int> resume(path.data, (int)path.size);
        const int resumeMove = resume.size != 0 ? resume.data[--resume.size] : -1;
        if (resumeMove < 0) {
            for (int i = 0; i < (int)result.roots.count; ++i) {
                const int index = (int)result.roots.offset + i;
                Pool::Node &candidate = pool.nodes.data.data[index];
                if (!candidate.finished) continue;
                candidate.expandedDepth = maxDepth;
                if (details) details->history.push_back(SuggestResult::Details::Layer(candidate.cell, maxDepth, pool.score(index)));
            }
        }
        const vectorPool<int>::vector moves = orderMoves(pool, result.roots, maxDepth, resumeMove, order);
        for (int i = 0; i < (int)moves.count; ++i) {
            const int index = order.data.data[moves.offset + i];
            nextPosition = extendCandidate(state, pool, index, maxDepth, shapeConfig, config, fiftyConfig, fiftyPool, budget, index == resumeMove ? resume : span<const int>());
            if (nextPosition >= 0) break;
            const Pool::Node &candidate = pool.nodes.data.data[index];
            if (candidate.finished) pool.rootCompensations.data[index] = 0.0L;
            if (details) {
                // 只在整招完成后发布数字分，未完成时保留上一层。
                int branch = 0;
                for (int j = 0; j < (int)details->outcomes.size; ++j)
                    if (details->outcomes.data[j].cell == candidate.cell) details->outcomes.data[j].value = pool.nodes.data.data[candidate.child.offset + branch++].value;
                details->history.push_back(SuggestResult::Details::Layer(candidate.cell, maxDepth, pool.score(index)));
            }
        }
        order.pop_back(moves);
        path.resize(0);
        pool.releaseProbabilities();
        if (nextPosition >= 0) break;
        result.completedDepth = maxDepth;
        result.finished = true;
        for (int i = 0; i < (int)result.roots.count; ++i)
            result.finished = result.finished && pool.nodes.data.data[result.roots.offset + i].finished;
    }
    pool.nextPosition = nextPosition;
    result.nodes += config.nodeLimit - budget;
    if (result.finished) pool.clearProbabilities();
}

} // 命名空间 mss
