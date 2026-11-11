#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"

#include <algorithm>
#include <utility>

#include "core/assert.h"

namespace mss {

struct AnytimeSearch::Outcome {
    Position::Delta delta;
    Position::ProbabilityResult probability;
    long double chance, value;
    int digit, knownSafe;
};

struct AnytimeSearch::Evaluation {
    ObservedBoard::CellId cell;
    long double value, progressMultiplier, compensation;
    vectorPool<Outcome>::vector outcomes;
    int nodes;
    bool valid;

    long double score() const { return value * progressMultiplier + compensation; }
};

struct AnytimeSearch::Workspace {
    struct FrontierCell {
        ObservedBoard::CellId cell;
        long double mine;
    };

    vector<Selection::Cell> cells;
    vector<ObservedBoard::CellId> accepted;
    Selection::Frontier exchangeFrontier;
    vector<ObservedBoard::Delta::Change> changes;
    vector<FrontierCell> frontier;
    vector<long double> lowest;
    vector<Evaluation> evaluations;
    vectorPool<Outcome> outcomes;
    vectorPool<int> order;
    vector<int> path;
};

__declspec(noinline) AnytimeSearch::Workspace &AnytimeSearch::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

struct AnytimeSearch::Pool::ProbabilityLayer {
    Probability::Pool pool;
    vector<Position::ProbabilityResult> results;
    int pending; // 尚未生成候选或永久终结的局面数。

    ProbabilityLayer(ShapeSolver::DistributionPool &distributions, Combination::Pool &combinations)
        : pool(distributions, combinations), pending(0) {}
};

AnytimeSearch::Pool::~Pool() {
    clearProbabilities();
}

void AnytimeSearch::Pool::clear(Position::Pool &positions) {
    AnytimeSearch().releaseSearchData(positions, *this);
    moves.data.resize(0);
    branches.data.resize(0);
    rootCompensations.resize(0);
    remainingSafe = 0;
    nextBranch = -1;
}

void AnytimeSearch::Pool::clearProbabilities() {
    for (int i = 0; i < (int)probabilities.size; ++i) delete probabilities[i];
    probabilities.resize(0);
}

long double AnytimeSearch::rootScore(const vector<Pool::Move> &moves, const vector<long double> &compensations, int index) {
    return moves[index].value + compensations[index];
}

ObservedBoard::CellId AnytimeSearch::Result::recommended(const Pool &pool) const {
    if (roots.count == 0) return -1;
    const vector<Pool::Move> &moves = pool.moves.data;
    const vector<long double> &compensations = pool.rootCompensations;
    AnytimeSearch search;
    int best = (int)roots.offset;
    for (int i = 1; i < (int)roots.count; ++i) {
        const int index = (int)roots.offset + i;
        const int depth = moves[index].expandedDepth, bestDepth = moves[best].expandedDepth;
        if (depth > bestDepth || (depth == bestDepth && search.rootScore(moves, compensations, index) > search.rootScore(moves, compensations, best))) best = index;
    }
    return moves[best].cell;
}

SuggestResult AnytimeSearch::Result::snapshot(const Pool &pool) const {
    if (roots.count == 0) return {};
    SuggestResult result(SuggestResult::Engine::Anytime, reason);
    result.nodes = nodes;
    const vector<Pool::Move> &moves = pool.moves.data;
    const vector<long double> &compensations = pool.rootCompensations;
    AnytimeSearch search;
    for (int i = 0; i < (int)roots.count; ++i) {
        const int index = (int)roots.offset + i;
        result.candidates.push_back({moves[index].cell, search.rootScore(moves, compensations, index)});
    }
    result.suggest.push_back(recommended(pool));
    return result;
}

AnytimeSearch::Evaluation AnytimeSearch::evaluate(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                                   Probability::Pool &probabilities, const Config &config, const Selection::Candidate &candidate,
                                                   int remaining, long double floor, vectorPool<Outcome> &outcomes, long double compensation, bool scaleCompensation) {
    using Cell = ObservedBoard::CellState;
    using CellId = ObservedBoard::CellId;
    const long double *digitProbability = candidate.observation.probability;
    const long double mine = digitProbability[9];
    Evaluation result{candidate.cell, 0.0L, 1.0L, 0.0L, outcomes.push_back(), 0, true};
    if (mine == 1.0L) return result;

    long double contributions[9]{}, progressByDigit[9]{};
    unsigned evaluated = 0;
    auto score = [&](bool bound) {
        long double value = 0.0L, progress = 0.0L;
        // 固定求和顺序；估算上界时，未算的数字按满分和有推进计。
        for (int digit = 0; digit <= 8; ++digit) {
            if (evaluated & (1u << digit)) {
                value += contributions[digit];
                progress += progressByDigit[digit];
            } else if (bound) {
                value += digitProbability[digit];
                progress += digitProbability[digit];
            }
        }
        return std::pair{value, 1.0L + (mine == 0.0L ? 0.0L : progress * config.progressContribution)};
    };
    auto pruned = [&]() {
        if (floor < 0.0L) return false;
        const auto [value, multiplier] = score(true);
        return value * multiplier < floor;
    };
    Workspace &workspace = getWorkspace();
    vector<ObservedBoard::Delta::Change> &changes = workspace.changes;
    vector<Workspace::FrontierCell> &frontier = workspace.frontier;
    vector<long double> &lowest = workspace.lowest;
    const auto [row, col] = position.board.pos(candidate.cell);
    const Cell previous = position.board.cells[row][col];
    auto reveal = [&](Cell next) {
        changes.resize(0);
        changes.push_back({candidate.cell, previous, next});
        return Position().updateWithDelta(position, changes, positions);
    };
    const vectorPool<long double>::vector boxTail = probabilities.boxProbabilities.push_back();
    const vectorPool<vectorPool<long double>::vector>::vector instanceTail = probabilities.instanceBoxes.push_back();
    int linkedClears = candidate.linkedClears;
    if (mine != 0.0L && linkedClears < 0) {
        linkedClears = 0;
        position.basic.safeCells([&](CellId cell) {
            const auto [r, c] = position.board.pos(cell);
            linkedClears += position.board.cells[r][c] != Cell::ForcedSafe;
        });
        frontier.resize(0);
        probability.boxes.frontierCells(probabilities, position.structure, [&](CellId cell, long double otherMine) {
            if (cell != candidate.cell) frontier.push_back({cell, otherMine});
        });
        const Position::Delta delta = reveal(Cell::ForcedMine);
        const Position::ProbabilityResult forced = Position().analyzeProbability(position, positions, probabilities);
        ++result.nodes;
        for (int i = 0; i < (int)frontier.size; ++i) {
            const Workspace::FrontierCell &other = frontier[i];
            const long double forcedMine = Probability().mineProbability(forced.boxes, probabilities, other.cell, position.board, position.basic, position.structure);
            linkedClears += (other.mine - mine * forcedMine) / (1.0L - mine) <= 1e-10L;
        }
        probabilities.instanceBoxes.pop_back(instanceTail);
        probabilities.boxProbabilities.pop_back(boxTail);
        Position().applyDelta(position, delta, positions, true);
        positions.popDelta(delta);
    }

    int order[9], count = 0;
    for (int digit = 0; digit <= 8; ++digit) {
        if (digitProbability[digit] == 0.0L) continue;
        int i = count++;
        while (i > 0 && digitProbability[order[i - 1]] < digitProbability[digit]) {
            order[i] = order[i - 1];
            --i;
        }
        order[i] = digit;
    }
    for (int i = 0; i < count; ++i) {
        const int digit = order[i];
        const Position::Delta delta = reveal(Cell(digit));
        const Position::ProbabilityResult child = Position().analyzeProbability(position, positions, probabilities);
        ++result.nodes;
        child.boxes.lowestProbabilities(probabilities, position.structure, 2, lowest);
        const long double offEdge = position.basic.unknownSum != 0 ? 1.0L - child.boxes.tCellProbability : 0.0L;
        const long double best = (std::max)(lowest.size > 0 ? 1.0L - lowest[0] : 0.0L, offEdge);
        const long double second = (std::max)(lowest.size > 1 ? 1.0L - lowest[1] : 0.0L, offEdge);
        int known = 0;
        position.basic.safeCells([&](CellId) { ++known; });
        const long double value = remaining == 1 || known > 1 ? 1.0L : known == 1 ? (4.0L + best) / 5.0L : (4.0L * best + second) / 5.0L;
        child.boxes.frontierCells(probabilities, position.structure, [&](CellId, long double p) { known += p == 0.0L; });
        contributions[digit] = digitProbability[digit] * value;
        if (mine != 0.0L && known > linkedClears) progressByDigit[digit] = digitProbability[digit];
        evaluated |= 1u << digit;
        const int offEdgeClears = child.boxes.tCellProbability == 0.0L ? position.basic.unknownSum : 0;
        outcomes.push_back(result.outcomes, {delta, child, child.totalWays / probability.totalWays, value, digit, known + offEdgeClears});
        Position().applyDelta(position, delta, positions, true);
        if (pruned()) {
            for (const Outcome &outcome : outcomes.reverseSpan(result.outcomes)) positions.popDelta(outcome.delta);
            outcomes.pop_back(result.outcomes);
            probabilities.instanceBoxes.pop_back(instanceTail);
            probabilities.boxProbabilities.pop_back(boxTail);
            result.valid = false;
            return result;
        }
    }
    const auto [value, multiplier] = score(false);
    result.value = value;
    result.progressMultiplier = multiplier;
    result.compensation = scaleCompensation ? value / (1.0L - mine) * compensation : compensation;
    return result;
}

vectorPool<AnytimeSearch::Pool::Move>::vector AnytimeSearch::appendMoves(Pool &pool, int parent, int depth, const vector<Evaluation> &evaluations,
                                                                         const vectorPool<Outcome> &outcomes, Probability::Pool &probabilities) {
    vectorPool<Pool::Move>::vector moves = pool.moves.push_back();
    for (int i = 0; i < (int)evaluations.size; ++i) {
        const Evaluation &evaluated = evaluations[i];
        const int index = (int)pool.moves.data.size;
        vectorPool<Pool::Branch>::vector branches = pool.branches.push_back();
        if (evaluated.outcomes.count != 0) {
            while (depth >= (int)pool.probabilities.size) pool.probabilities.push_back(nullptr);
            Pool::ProbabilityLayer *&slot = pool.probabilities[depth];
            if (!slot) slot = new Pool::ProbabilityLayer(probabilities.distributions, probabilities.combinations);
            Pool::ProbabilityLayer &layer = *slot;
            for (const Outcome &outcome : outcomes.span(evaluated.outcomes)) {
                Position::ProbabilityResult child = outcome.probability;
                child.boxes.instanceBoxes = layer.pool.instanceBoxes.push_back();
                for (const vectorPool<long double>::vector &box : probabilities.instanceBoxes.span(outcome.probability.boxes.instanceBoxes)) {
                    vectorPool<long double>::vector copy = layer.pool.boxProbabilities.push_back();
                    for (long double p : probabilities.boxProbabilities.span(box)) layer.pool.boxProbabilities.push_back(copy, p);
                    layer.pool.instanceBoxes.push_back(child.boxes.instanceBoxes, copy);
                }
                const int probabilityIndex = (int)layer.results.size;
                layer.results.push_back(child);
                ++layer.pending;
                pool.branches.push_back(branches, {outcome.delta, outcome.chance, outcome.value, outcome.digit, outcome.knownSafe,
                                                  index, depth, probabilityIndex, false, {}});
            }
        }
        const bool finished = branches.count == 0;
        pool.moves.push_back(moves, {evaluated.cell, depth, parent, depth, finished, evaluated.value, evaluated.progressMultiplier, branches});
        if (parent < 0) pool.rootCompensations.push_back(finished ? 0.0L : evaluated.compensation);
    }
    return moves;
}


AnytimeSearch::Result AnytimeSearch::buildRoot(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                               Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                                               int remainingSafe, const Config &config, Pool &pool, bool scaleCompensation) {
    pool.clear(positions);
    Result result;
    result.nodes = selected.nodes;
    if (selected.candidates.size == 0) return result;

    const ObservedBoard::Result &board = position.board;
    pool.remainingSafe = remainingSafe;
    if (pool.remainingSafe == 0) return result;
    result.reason = SuggestResult::Reason::ScoreWins;

    pool.deltaStart = {{positions.board.changes.push_back()}, {positions.basic.changes.push_back()},
                       {positions.structure.deltas.removed.push_back(), positions.structure.deltas.added.push_back()}};
    pool.hasDeltas = true;
    Workspace &workspace = getWorkspace();
    const vectorPool<long double>::vector boxTail = probabilities.boxProbabilities.push_back();
    const vectorPool<vectorPool<long double>::vector>::vector instanceTail = probabilities.instanceBoxes.push_back();
    vector<Evaluation> &evaluations = workspace.evaluations;
    vectorPool<Outcome> &outcomes = workspace.outcomes;
    evaluations.resize(0);
    outcomes.data.resize(0);
    for (int i = 0; i < (int)selected.candidates.size; ++i) {
        const Selection::Candidate &candidate = selected.candidates[i];
        const auto [row, col] = board.pos(candidate.cell);
        const Evaluation evaluated = evaluate(position, positions, probability, probabilities, config, candidate, pool.remainingSafe, -1.0L,
                                              outcomes, longTermRisk[row][col], scaleCompensation);
        result.nodes += evaluated.nodes;
        evaluations.push_back(evaluated);
    }
    if (config.width[0] > 0 && evaluations.size > 1) {
        const int keep = (std::min)((int)evaluations.size, config.width[0]);
        std::partial_sort(evaluations.data, evaluations.data + keep, evaluations.data + evaluations.size,
                          [](const Evaluation &left, const Evaluation &right) {
            const long double a = left.score(), b = right.score();
            return a != b ? a > b : left.cell < right.cell;
        });
        evaluations.resize(keep);
    }
    result.roots = appendMoves(pool, -1, 0, evaluations, outcomes, probabilities);
    probabilities.instanceBoxes.pop_back(instanceTail);
    probabilities.boxProbabilities.pop_back(boxTail);
    for (const Pool::Move &move : pool.moves.span(result.roots)) result.finished = result.finished && move.finished;
    if (result.finished) releaseSearchData(positions, pool);
    return result;
}

void AnytimeSearch::select(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                           Probability::Pool &probabilities, const Config &config, int depth, int remaining, long long &budget,
                           vector<Evaluation> &evaluations, vectorPool<Outcome> &outcomes) {
    using CellId = ObservedBoard::CellId;
    using Range = Selection::Range;

    CellId safe = -1;
    auto considerSafe = [&](CellId cell) { if (safe < 0 || cell < safe) safe = cell; };
    position.basic.safeCells(considerSafe);
    probability.boxes.frontierCells(probabilities, position.structure, [&](CellId cell, long double mine) {
        if (mine == 0.0L) considerSafe(cell);
    });
    if (position.basic.unknownSum != 0 && probability.boxes.tCellProbability == 0.0L)
        considerSafe(position.basic.firstT());
    const int limit = safe >= 0 ? 1 : config.width[depth == 1 ? 1 : 2];
    const span<const CellId> requested = safe >= 0 ? span<const CellId>(&safe, 1) : span<const CellId>();

    Workspace &workspace = getWorkspace();
    vector<Selection::Cell> &cells = workspace.cells;
    vector<CellId> &accepted = workspace.accepted;
    Selection::Frontier &frontier = workspace.exchangeFrontier;
    accepted.resize(0);
    frontier.knownSafe = -1;
    Selection selection;
    selection.collect(cells, position, positions, probability, probabilities, config.selection, requested, nullptr, false);
    const bool exchange = safe < 0 && (config.selection.equivalence || config.selection.dominance);
    auto score = [&](Selection::Candidate &candidate, int index, Range range) {
        const long double floor = (int)evaluations.size == limit ? evaluations[limit - 1].score() : -1.0L;
        if (floor >= 0.0L) {
            long double safety = 0.0L;
            for (int digit = 0; digit <= 8; ++digit) safety += candidate.observation.probability[digit];
            if (safety * (1.0L + safety * config.progressContribution) < floor) return;
        }
        if (exchange && range != Range::Fallback) {
            const Range lastRange = accepted.size >= 2 ? range : Range::Fallback;
            budget -= selection.removeExchange(candidate, {cells.data + index + 1, (int)cells.size - index - 1}, lastRange,
                                               frontier, position, positions, probability, probabilities, config.selection);
        }
        const Evaluation evaluated = evaluate(position, positions, probability, probabilities, config, candidate, remaining, floor, outcomes);
        budget -= evaluated.nodes;
        if (!evaluated.valid) return;
        evaluations.push_back(evaluated);
        for (int j = (int)evaluations.size - 1; j > 0; --j) {
            const Evaluation &before = evaluations[j - 1];
            if (before.score() > evaluated.score() || (before.score() == evaluated.score() && before.cell < evaluated.cell)) break;
            std::swap(evaluations[j - 1], evaluations[j]);
        }
        if ((int)evaluations.size > limit) evaluations.resize(limit);
    };

    Selection::Candidate dead{-1, {}};
    Range range = Range::First;
    for (int i = 0; i < (int)cells.size; ++i) {
        const Selection::Cell &cell = cells[i];
        if (cell.range != range && accepted.size >= 2) break;
        range = cell.range;
        if (cell.removed && accepted.size >= 2) {
            // 已经数够两格。后面若有活格与本格位置互换，本格也是活格，可以直接比较。
            if (config.selection.interchangeable) accepted.push_back(cell.cell);
            continue;
        }
        Selection::Candidate candidate{cell.cell, {}};
        Position().observe(position, probability, probabilities, cell.cell, candidate.observation);
        --budget;
        if (safe < 0 && selection.isDead(candidate.observation)) {
            if (accepted.size == 0 && dead.cell < 0) dead = candidate;
            continue;
        }
        if (config.selection.interchangeable && std::any_of(accepted.data, accepted.data + accepted.size, [&](CellId other) {
            return selection.interchangeable(position, probability, probabilities, cell.cell, other);
        })) continue;
        accepted.push_back(cell.cell);
        if (!cell.removed) score(candidate, i, range);
        if (range == Range::Fallback) break;
    }
    if (accepted.size == 0) {
        assert_(dead.cell >= 0, "Anytime 未结束的子局面应有候选格");
        const Evaluation evaluated = evaluate(position, positions, probability, probabilities, config, dead, remaining, -1.0L, outcomes);
        budget -= evaluated.nodes;
        evaluations.push_back(evaluated);
    }
}

void AnytimeSearch::preparePosition(Position::Result &position, Position::Pool &positions, const Config &config,
                                    Pool &pool, int index, long long &budget) {
    const Pool::Branch &branch = pool.branches.data[index];
    const int depth = pool.moves.data[branch.parent].depth + 1;
    Pool::ProbabilityLayer &layer = *pool.probabilities[depth - 1];
    const Position::ProbabilityResult probability = layer.results[branch.probabilityIndex];
    Probability::Pool &probabilities = layer.pool;

    const vectorPool<long double>::vector boxTail = probabilities.boxProbabilities.push_back();
    const vectorPool<vectorPool<long double>::vector>::vector instanceTail = probabilities.instanceBoxes.push_back();
    Workspace &workspace = getWorkspace();
    vector<Evaluation> &evaluations = workspace.evaluations;
    vectorPool<Outcome> &outcomes = workspace.outcomes;
    evaluations.resize(0);
    outcomes.data.resize(0);
    select(position, positions, probability, probabilities, config, depth, pool.remainingSafe - depth, budget, evaluations, outcomes);
    const vectorPool<Pool::Move>::vector moves = appendMoves(pool, index, depth, evaluations, outcomes, probabilities);
    probabilities.instanceBoxes.pop_back(instanceTail);
    probabilities.boxProbabilities.pop_back(boxTail);
    --layer.pending;

    Pool::Branch &prepared = pool.branches.data[index];
    prepared.children = moves;
    prepared.probabilityIndex = -1;
    prepared.expandedDepth = depth;
    prepared.value = 0.0L;
    prepared.finished = true;
    for (const Pool::Move &move : pool.moves.span(moves)) {
        prepared.value = (std::max)(prepared.value, move.value);
        prepared.finished = prepared.finished && move.finished;
    }
}

vectorPool<int>::vector AnytimeSearch::orderMoves(const Pool &pool, vectorPool<Pool::Move>::vector children, int depth,
                                                  int resumeMove, vectorPool<int> &order) {
    vectorPool<int>::vector moves = order.push_back();
    if (resumeMove >= 0) order.push_back(moves, resumeMove);
    for (int i = 0; i < (int)children.count; ++i) {
        const int index = (int)children.offset + i;
        const Pool::Move &move = pool.moves.data[index];
        if (index != resumeMove && !move.finished && move.expandedDepth < depth) order.push_back(moves, index);
    }
    const int first = resumeMove >= 0 ? 1 : 0;
    if ((int)moves.count > first + 1) {
        auto priority = [&](int index) {
            const Pool::Move &move = pool.moves.data[index];
            return move.value * move.progressMultiplier + (move.parent < 0 ? pool.rootCompensations[index] : 0.0L);
        };
        std::sort(order.data.data + moves.offset + first, order.data.data + moves.offset + moves.count, [&](int left, int right) {
            const long double a = priority(left), b = priority(right);
            return a != b ? a > b : left < right;
        });
    }
    return moves;
}

int AnytimeSearch::extendPosition(Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool,
                                  int index, int depth, long long &budget, span<const int> resume) {
    if (pool.branches.data[index].probabilityIndex >= 0) preparePosition(position, positions, config, pool, index, budget);
    const Pool::Branch &branch = pool.branches.data[index];
    if (branch.finished || branch.expandedDepth >= depth) return -1;
    const vectorPool<Pool::Move>::vector children = branch.children;
    const int resumeMove = resume.size != 0 ? resume.data[--resume.size] : -1;
    vectorPool<int> &order = getWorkspace().order;
    const vectorPool<int>::vector moves = orderMoves(pool, children, depth, resumeMove, order);
    for (int i = 0; i < (int)moves.count; ++i) {
        const int move = order.data[moves.offset + i];
        const int next = extendMove(position, positions, config, pool, move, depth, budget, move == resumeMove ? resume : span<const int>());
        if (next >= 0) {
            order.pop_back(moves);
            return next;
        }
    }
    order.pop_back(moves);
    Pool::Branch &expanded = pool.branches.data[index];
    expanded.value = 0.0L;
    expanded.finished = true;
    for (const Pool::Move &move : pool.moves.span(children)) {
        expanded.value = (std::max)(expanded.value, move.value);
        expanded.finished = expanded.finished && move.finished;
    }
    expanded.expandedDepth = depth;
    return -1;
}

int AnytimeSearch::extendMove(Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool,
                              int index, int depth, long long &budget, span<const int> resume) {
    const Pool::Move &move = pool.moves.data[index];
    const int moveDepth = move.depth;
    const vectorPool<Pool::Branch>::vector children = move.children;
    const int horizon = depth - moveDepth;
    const int resumeBranch = resume.size != 0 ? resume.data[--resume.size] : -1;
    const bool terminal = pool.remainingSafe - moveDepth <= 1 || position.board.totalMines == position.basic.mineSum;
    long double contributions[9]{};
    bool finished = true;
    for (int i = 0; i < (int)children.count; ++i) {
        const int branchIndex = (int)children.offset + i;
        const Pool::Branch &branch = pool.branches.data[branchIndex];
        if (terminal) {
            Pool::Branch &settled = pool.branches.data[branchIndex];
            if (settled.probabilityIndex >= 0) {
                --pool.probabilities[moveDepth]->pending;
                settled.probabilityIndex = -1;
            }
            settled.value = 1.0L;
            settled.finished = true;
        } else if (branch.knownSafe >= horizon + 2) {
            pool.branches.data[branchIndex].value = 1.0L;
        } else if (branchIndex >= resumeBranch && !branch.finished && branch.expandedDepth < depth) {
            if (branch.probabilityIndex >= 0 && budget <= 0) return branchIndex;
            const Position::Delta delta = branch.delta;
            Position().applyDelta(position, delta, positions);
            const int next = extendPosition(position, positions, config, pool, branchIndex, depth, budget,
                                            branchIndex == resumeBranch ? resume : span<const int>());
            Position().applyDelta(position, delta, positions, true);
            if (next >= 0) return next;
        }
        const Pool::Branch &expanded = pool.branches.data[branchIndex];
        contributions[expanded.digit] = expanded.probability * expanded.value;
        finished = finished && expanded.finished;
    }
    Pool::Move &expanded = pool.moves.data[index];
    expanded.value = 0.0L;
    for (long double contribution : contributions) expanded.value += contribution;
    expanded.finished = finished;
    expanded.expandedDepth = depth;
    return -1;
}

void AnytimeSearch::record(const Pool &pool, int index, SuggestResult::Details &details) {
    const Pool::Move &move = pool.moves.data[index];
    for (const Pool::Branch &branch : pool.branches.span(move.children)) {
        int i = 0;
        while (i < (int)details.outcomes.size && (details.outcomes[i].cell != move.cell || details.outcomes[i].digit != branch.digit)) ++i;
        if (i == (int)details.outcomes.size) details.outcomes.push_back({move.cell, branch.digit, branch.value});
        else details.outcomes[i].value = branch.value;
    }
    details.history.push_back({move.cell, move.expandedDepth, rootScore(pool.moves.data, pool.rootCompensations, index)});
}

void AnytimeSearch::releaseSearchData(Position::Pool &positions, Pool &pool) {
    pool.clearProbabilities();
    if (pool.hasDeltas) {
        positions.popDelta(pool.deltaStart);
        pool.hasDeltas = false;
    }
}

void AnytimeSearch::releaseProbabilities(vector<Pool::ProbabilityLayer *> &probabilities) {
    // 上一层仍有局面待展开时，下一层还会收到新的概率结果。
    for (int i = 0; i < (int)probabilities.size; ++i) {
        Pool::ProbabilityLayer *layer = probabilities[i];
        if (!layer) continue;
        if (layer->pending != 0) break;
        delete layer;
        probabilities[i] = nullptr;
    }
}

template <typename Record>
void AnytimeSearch::updateImpl(Result &result, Position::Result &position, Position::Pool &positions, const Config &config,
                               Pool &pool, Record recordMove) {
    if (config.nodeLimit == 0 || result.finished) return;
    Workspace &workspace = getWorkspace();
    vector<int> &path = workspace.path;
    vectorPool<int> &order = workspace.order;
    path.resize(0);
    for (int branch = pool.nextBranch; branch >= 0;) {
        const int move = pool.branches.data[branch].parent;
        path.push_back(branch);
        path.push_back(move);
        branch = pool.moves.data[move].parent;
    }

    long long budget = config.nodeLimit;
    int next = -1;
    while (!result.finished) {
        const int depth = result.completedDepth + 1;
        span<const int> resume(path.data, (int)path.size);
        const int resumeMove = resume.size != 0 ? resume.data[--resume.size] : -1;
        if (resumeMove < 0) {
            for (int i = 0; i < (int)result.roots.count; ++i) {
                const int index = (int)result.roots.offset + i;
                Pool::Move &move = pool.moves.data[index];
                if (!move.finished) continue;
                move.expandedDepth = depth;
                recordMove(index);
            }
        }
        const vectorPool<int>::vector moves = orderMoves(pool, result.roots, depth, resumeMove, order);
        for (int i = 0; i < (int)moves.count; ++i) {
            const int index = order.data[moves.offset + i];
            next = extendMove(position, positions, config, pool, index, depth, budget, index == resumeMove ? resume : span<const int>());
            if (next >= 0) break;
            if (pool.moves.data[index].finished) pool.rootCompensations[index] = 0.0L;
            recordMove(index);
        }
        order.pop_back(moves);
        path.resize(0);
        releaseProbabilities(pool.probabilities);
        if (next >= 0) break;
        result.completedDepth = depth;
        result.finished = true;
        for (const Pool::Move &move : pool.moves.span(result.roots)) result.finished = result.finished && move.finished;
    }
    pool.nextBranch = next;
    result.nodes += config.nodeLimit - budget;
    if (result.finished) releaseSearchData(positions, pool);
}

AnytimeSearch::Result AnytimeSearch::analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                             Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                                             int remainingSafe, const Config &config, Pool &pool, bool scaleCompensation) {
    Result result = buildRoot(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, config, pool, scaleCompensation);
    update(result, position, positions, config, pool);
    return result;
}

AnytimeSearch::Result AnytimeSearch::analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                                             Probability::Pool &probabilities, const Selection::Result &selected, const Grid<long double> &longTermRisk,
                                             int remainingSafe, const Config &config, Pool &pool, SuggestResult::Details &details, bool scaleCompensation) {
    details.clear();
    Result result = buildRoot(position, positions, probability, probabilities, selected, longTermRisk, remainingSafe, config, pool, scaleCompensation);
    for (int i = 0; i < (int)result.roots.count; ++i) record(pool, (int)result.roots.offset + i, details);
    update(result, position, positions, config, pool, details);
    return result;
}

void AnytimeSearch::update(Result &result, Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool) {
    updateImpl(result, position, positions, config, pool, [](int) {});
}

void AnytimeSearch::update(Result &result, Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool,
                            SuggestResult::Details &details) {
    updateImpl(result, position, positions, config, pool, [&](int index) { record(pool, index, details); });
}

} // 命名空间 mss
