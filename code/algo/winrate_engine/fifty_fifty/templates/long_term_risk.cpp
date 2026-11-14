#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"

#include <algorithm>
#include <cmath>

#include "core/algorithm/neighbors.h"

namespace mss {

struct TemplateLongTermRisk::Workspace {
    struct Force {
        ObservedBoard::CellId cell;
        signed char value; // 1 为雷，-1 为安。
        bool operator<(const Force &other) const { return cell == other.cell ? value < other.value : cell < other.cell; }
    };
    using Forces = vectorPool<Force>::vector;
    using Blockers = vectorPool<Forces>::vector;
    using Values = vectorPool<long double>::vector;
    struct Event { Forces base; Blockers blockers; bool restriction; };
    struct Ranked { Event event; long double ways; int order; };

    vectorPool<Force> forces;
    vectorPool<Forces> blockers;
    vectorPool<long double> values;
    vector<Force> base, assigned;
    vector<signed char> fixed;
    vector<Event> events;
    vector<Ranked> ranked;
    vector<int> signature, group;
    vector<unsigned char> eventMasks;
    vector<ObservedBoard::Delta::Change> changes;
};

struct TemplateLongTermRisk::Query {
    using Force = Workspace::Force;
    using Forces = Workspace::Forces;
    using Event = Workspace::Event;
    using Values = Workspace::Values;

    Result &result;
    Position::Result &position;
    Position::Pool &positions;
    const FiftyFiftyData::Pool &templates;
    vectorPool<Force> &forces;
    vectorPool<Forces> &blockers;
    vectorPool<long double> &values;
    vector<Force> &assigned;
    vector<signed char> &fixed;
    const vector<Event> &events;
    vector<int> &signature;

    bool valid(const Basic::Delta &delta) {
        const ObservedBoard::Result &board = position.board;
        const int rows = board.cells.rows(), cols = board.cells.cols();
        for (const Basic::Delta::Change &change : positions.basic.changes.span(delta.changes)) {
            if (change.previous == change.next) continue;
            const auto [row, col] = board.pos(change.cell);
            bool valid = true;
            forEachAdjacent(row, col, rows, cols, [&](int r, int c) {
                const ObservedBoard::CellState state = board.cells[r][c];
                if (!valid || !ObservedBoard().isNumber(state)) return;
                int remaining = ObservedBoard().numberValue(state), hidden = 0;
                forEachAdjacent(r, c, rows, cols, [&](int nr, int nc) {
                    const Basic::Mark mark = position.basic.marks[nr][nc];
                    remaining -= mark == Basic::Mark::F;
                    hidden += mark == Basic::Mark::H || mark == Basic::Mark::T;
                });
                valid = remaining >= 0 && remaining <= hidden;
            });
            if (!valid) return false;
        }
        return true;
    }

    long double count() {
        // 同一个 Box 内只需区分强制雷／安的数量，T 格同样可互换。
        signature.resize(assigned.size);
        for (int i = 0; i < (int)assigned.size; ++i)
            signature[i] = 2 * result.boxes[assigned[i].cell] + (assigned[i].value == 1);
        std::sort(signature.data, signature.data + signature.size);
        std::uint64_t hash = 1469598103934665603ULL;
        for (int i = 0; i < (int)signature.size; ++i) hash = (hash ^ signature[i]) * 1099511628211ULL;
        const int *found = result.countIndex.find(hash);
        const int head = found ? *found : -1;
        for (int i = head; i >= 0; i = result.counts[i].next) {
            const Result::Count &count = result.counts[i];
            if (count.key.count == signature.size &&
                std::equal(signature.data, signature.data + signature.size, result.keys.span(count.key).begin()))
                return count.ways;
        }

        using Cell = ObservedBoard::CellState;
        ObservedBoard::Result &board = position.board;
        ObservedBoard::Delta observed{positions.board.changes.push_back()};
        for (int i = 0; i < (int)assigned.size; ++i) {
            const Force force = assigned[i];
            const auto [row, col] = board.pos(force.cell);
            positions.board.changes.push_back(observed.changes, {force.cell, board.cells[row][col], force.value == 1 ? Cell::ForcedMine : Cell::ForcedSafe});
        }
        ObservedBoard().applyDelta(board, observed, positions.board);
        const Basic::Delta basic = Basic().update(position.basic, board, observed, positions.basic);
        long double ways = 0.0L;
        // 数字失去最后一个候选格时，先检查约束，再更新 Structure。
        if (valid(basic)) {
            const Structure::Delta structure = Structure().update(position.structure, board, position.basic, positions.structure, observed, basic, positions.basic);
            ways = Position().countWays(position, positions);
            // 直接撤回本次改动，原盘的 LogicComponent 结果可以继续使用。
            Structure().applyDelta(position.structure, structure, positions.structure, true);
            positions.structure.deltas.added.pop_back(structure.added);
            positions.structure.deltas.removed.pop_back(structure.removed);
        }
        Basic().applyDelta(position.basic, basic, positions.basic, true);
        ObservedBoard().applyDelta(board, observed, positions.board, true);
        positions.basic.changes.pop_back(basic.changes);
        positions.board.changes.pop_back(observed.changes);
        vectorPool<int>::vector key = result.keys.push_back();
        for (int i = 0; i < (int)signature.size; ++i) result.keys.push_back(key, signature[i]);
        result.countIndex[hash] = result.counts.size;
        result.counts.push_back({key, head, ways});
        return ways;
    }

    bool prepare(Event &event, const Result::Match &match, int direction, bool restriction, vector<Force> &base) {
        using Point = FiftyFiftyData::Point;
        const int rows = position.board.cells.rows(), cols = position.board.cells.cols();
        const FiftyFiftyData::Pattern &pattern = templates.patterns[match.pattern];
        const int coreCount = 2 * pattern.half;
        base.resize(0);
        for (int i = 0; i < (int)pattern.points.count; ++i) {
            const int id = templates.points.span(pattern.points)[i].cell(match.anchor, rows, cols);
            if (id < 0 || result.mine[id] == 1.0L) continue;
            const bool mine = i >= coreCount || ((i >= pattern.half) == direction);
            fixed[id] = mine ? 1 : -1;
            base.push_back({id, fixed[id]});
        }
        Workspace::Blockers clauses = blockers.push_back();
        bool possible = true;
        if (restriction) for (const FiftyFiftyData::Blocker &blocker : templates.blockers.span(pattern.blockers)) {
            const FiftyFiftyData::Pattern &child = templates.patterns[blocker.pattern];
            Forces allMines = forces.push_back();
            bool closed = true;
            const Point anchor = match.anchor + blocker.offset;
            for (int i = 2 * child.half; i < (int)child.points.count; ++i) {
                const int id = templates.points.span(child.points)[i].cell(anchor, rows, cols);
                if (id < 0 || result.mine[id] == 1.0L || fixed[id] == 1) continue;
                if (result.mine[id] == 0.0L || fixed[id] == -1) { closed = false; break; }
                forces.push_back(allMines, {id, 1});
            }
            if (!closed) forces.pop_back(allMines);
            else if (allMines.count == 0) { possible = false; break; }
            else if (allMines.count == 1) {
                const int id = forces.span(allMines)[0].cell;
                base.push_back({id, -1});
                fixed[id] = -1;
                forces.pop_back(allMines);
            } else {
                // 每条条件要求至少一格安。封口格按格号递增，保留更小的集合即可。
                const Force *first = forces.span(allMines).begin(), *last = first + allMines.count;
                std::size_t kept = 0;
                bool covered = false;
                for (Forces clause : blockers.span(clauses)) {
                    const Force *other = forces.span(clause).begin(), *end = other + clause.count;
                    if (clause.count <= allMines.count) covered |= std::includes(first, last, other, end);
                    else if (std::includes(other, end, first, last)) continue;
                    blockers.data[clauses.offset + kept++] = clause;
                }
                blockers.data.resize(clauses.offset + kept);
                clauses.count = kept;
                if (covered) forces.pop_back(allMines);
                else blockers.push_back(clauses, allMines);
            }
        }
        for (int i = 0; i < (int)base.size; ++i) fixed[base[i].cell] = 0;
        if (!possible) return false;
        std::sort(base.data, base.data + base.size);
        Forces required = forces.push_back();
        for (int i = 0; i < (int)base.size; ++i) forces.push_back(required, base[i]);
        event = {required, clauses, restriction};
        return true;
    }

    bool extend(Forces required) {
        for (const Force &force : forces.span(required)) {
            const signed char value = fixed[force.cell];
            if (value == -force.value) return false;
            if (value == 0) {
                fixed[force.cell] = force.value;
                assigned.push_back(force);
            }
        }
        return true;
    }

    void undo(int length) {
        for (int i = length; i < (int)assigned.size; ++i) fixed[assigned[i].cell] = 0;
        assigned.resize(length);
    }

    // 本体减掉交集。限制同样减掉交集，明细再把交集加到下一桶，标量只减一半。
    // 有待排除的全雷封口时，先消化这段封口条件，再继续原事件序列。
    void evaluate(int head, long double ways, Values output, Workspace::Blockers pending) {
        const bool excluding = pending.count != 0;
        if ((!excluding && head < 0) || ways == 0.0L) {
            for (int k = 0; k < (int)output.count; ++k) values.span(output)[k] = k == 0 ? ways : 0.0L;
            return;
        }
        const Event event = excluding ? Event{blockers.data[pending.offset + --pending.count], {}, false} : events[head--];
        const int length = assigned.size;
        bool possible = extend(event.base), certain = possible;
        if (possible) for (Forces clause : blockers.span(event.blockers)) {
            bool safe = false, unknown = false;
            for (const Force &force : forces.span(clause)) {
                safe |= fixed[force.cell] == -1;
                unknown |= fixed[force.cell] == 0;
            }
            if (!safe && !unknown) { possible = false; break; }
            certain &= safe;
        }
        if (!possible) {
            undo(length);
            evaluate(head, ways, output, pending);
            return;
        }
        if (certain && assigned.size == std::size_t(length)) {
            if (!event.restriction) {
                for (int k = 0; k < (int)output.count; ++k) values.span(output)[k] = 0.0L;
            } else {
                evaluate(head, ways, output, pending);
                if (output.count == 1) values.span(output)[0] *= 0.5L;
                else {
                    for (int k = (int)output.count - 1; k > 0; --k) values.span(output)[k] = values.span(output)[k - 1];
                    values.span(output)[0] = 0.0L;
                }
            }
            return;
        }
        const long double joined = assigned.size == std::size_t(length) ? ways : count();
        Values overlap = values.push_back();
        overlap.count = output.count;
        values.data.resize(overlap.offset + overlap.count);
        evaluate(head, joined, overlap, excluding ? pending : event.blockers);
        undo(length);
        evaluate(head, ways, output, pending);
        for (int k = 0; k < (int)output.count; ++k) {
            if (event.restriction && output.count == 1) values.span(output)[k] -= 0.5L * values.span(overlap)[k];
            else {
                values.span(output)[k] -= values.span(overlap)[k];
                if (event.restriction && k > 0) values.span(output)[k] += values.span(overlap)[k - 1];
            }
        }
        values.pop_back(overlap);
    }

    long double mass(Event event) {
        extend(event.base);
        const long double ways = count();
        const Values output{0, 1};
        values.data.resize(1);
        evaluate(-1, ways, output, event.blockers);
        undo(0);
        return values.span(output)[0];
    }
};

__declspec(noinline) TemplateLongTermRisk::Workspace &TemplateLongTermRisk::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void TemplateLongTermRisk::update(Result &result, const Position::Result &position, const Position::ProbabilityResult &probability,
                                  const Probability::Pool &probabilities, const FiftyFiftyData::Pool &templates, int limit) {
    using Point = FiftyFiftyData::Point;
    const ObservedBoard::Result &board = position.board;
    const int rows = board.cells.rows(), cols = board.cells.cols(), size = rows * cols;
    result.limit = limit;
    result.mine.resize(size);
    result.boxes.resize(size);
    result.compensation.resize(size);
    result.byCell.resize(size);
    result.matches.resize(0);
    result.countIndex.clear();
    result.counts.resize(0);
    result.keys.data.resize(0);
    const Structure::Pool::Instances &instances = probabilities.distributions.shapes.instances;
    for (int id = 0; id < size; ++id) {
        result.mine[id] = Probability().mineProbability(probability.boxes, probabilities, id, board, position.basic, position.structure);
        result.compensation[id] = -1.0L; // 当前盘面尚未查询。
        result.byCell[id] = {};
        const Structure::Result::CellLocation &where = position.structure.cellLoc[id];
        result.boxes[id] = size;
        if (where.box >= 0) {
            const Structure::Instance &instance = instances.list[position.structure.instanceIds[where.instanceIndex]];
            result.boxes[id] = instances.cells.span(instances.boxes.span(instance.boxes)[where.box])[0];
        }
    }
    auto hidden = [&](int cell) { return cell >= 0 && result.mine[cell] > 0.01L && result.mine[cell] < 0.99L; };
    for (int row = 0; row < rows; ++row) for (int col = 0; col < cols; ++col) {
        if (!hidden(board.id(row, col))) continue;
        const Point anchor{row, col};
        for (int i = 0; i < (int)templates.checks.size;) {
            const FiftyFiftyData::Check &check = templates.checks[i];
            const int r = row + check.point.row, c = col + check.point.col;
            if ((unsigned int)r >= (unsigned int)rows || (unsigned int)c >= (unsigned int)cols) {
                i = check.skip;
                continue;
            }
            const long double p = result.mine[r * cols + c];
            if (p <= 0.01L || p >= 0.99L) {
                i = check.skip;
                continue;
            }
            ++i;
            if (check.pattern < 0) continue;
            const FiftyFiftyData::Pattern &pattern = templates.patterns[check.pattern];
            const int coreCount = 2 * pattern.half;
            long double seal = 1.0L;
            int missing = 0;
            for (int j = coreCount; j < (int)pattern.points.count; ++j) {
                const int id = templates.points.span(pattern.points)[j].cell(anchor, rows, cols);
                if (id < 0 || result.mine[id] == 1.0L) continue;
                seal *= result.mine[id];
                if (seal < 0.01L) break;
                ++missing;
            }
            if (seal < 0.01L || pattern.half + missing > board.totalMines - position.basic.mineSum) continue;
            if (pattern.half >= 4 && position.basic.unknownSum >= 9) {
                int free[2] = {};
                for (int j = 0; j < coreCount; ++j) {
                    const Point point = anchor + templates.points.span(pattern.points)[j];
                    free[j >= pattern.half] += position.basic.marks[point.row][point.col] == Basic::Mark::T;
                }
                if (free[0] >= 4 && free[1] >= 4) continue;
            }
            result.matches.push_back({check.pattern, anchor});
        }
    }

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < (int)result.matches.size; ++i) {
            const Result::Match &match = result.matches[i];
            const FiftyFiftyData::Pattern &pattern = templates.patterns[match.pattern];
            for (int slot = 0; slot < (int)pattern.points.count; ++slot) {
                const int id = templates.points.span(pattern.points)[slot].cell(match.anchor, rows, cols);
                if (id < 0 || result.mine[id] == 1.0L) continue;
                vectorPool<Result::Link>::vector &links = result.byCell[id];
                if (pass != 0) result.links.data[links.offset + links.count] = {i, slot};
                ++links.count;
            }
        }
        if (pass == 0) {
            int length = 0;
            for (int id = 0; id < size; ++id) {
                const int count = result.byCell[id].count;
                result.byCell[id] = {std::size_t(length), 0};
                length += count;
            }
            result.links.data.resize(length);
        }
    }
}

template <TemplateLongTermRisk::DetailRequest request>
TemplateLongTermRisk::InfluenceResult<request> TemplateLongTermRisk::influence(
    ObservedBoard::CellId cell, Result &result, Position::Result &position, Position::Pool &positions,
    const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
    const FiftyFiftyData::Pool &templates) {
    InfluenceResult<request> answer{};
    if constexpr (request == DetailRequest::Details) {
        answer.restrictions.resize(1);
        answer.restrictions[0] = 1.0L;
    }
    const long double mine = result.mine[cell];
    if (mine == 0.0L || mine == 1.0L || result.matches.size == 0) return answer;
    if constexpr (request == DetailRequest::Score)
        if (result.compensation[cell] >= 0.0L) return {result.compensation[cell]};

    Workspace &workspace = getWorkspace();
    vector<int> &group = workspace.group;
    vector<ObservedBoard::Delta::Change> &changes = workspace.changes;
    const ObservedBoard::Result &board = position.board;
    group.resize(0);
    group.push_back(cell);
    changes.resize(0);
    changes.push_back({cell, ObservedBoard::CellState::Hidden, ObservedBoard::CellState::ForcedSafe});
    const Position::Delta delta = Position().updateWithDelta(position, changes, positions);
    const vectorPool<long double>::vector boxTail = probabilities.boxProbabilities.push_back();
    const Position::ProbabilityResult safe = Position().analyzeProbability(position, positions, probabilities);
    const long double tolerance = 1e-9L * (std::min)(mine, 1.0L - mine);
    for (int id = 0; id < (int)result.mine.size; ++id)
        if (id != cell && std::abs(result.mine[id] - mine) <= tolerance &&
            Probability().mineProbability(safe.boxes, probabilities, id, board, position.basic, position.structure) == 0.0L)
            group.push_back(id);
    probabilities.boxProbabilities.pop_back(boxTail);
    probabilities.instanceBoxes.pop_back(safe.boxes.instanceBoxes);
    Position().applyDelta(position, delta, positions, true);
    positions.popDelta(delta);

    vector<unsigned char> &eventMasks = workspace.eventMasks;
    eventMasks.resize(result.matches.size);
    std::fill_n(eventMasks.data, eventMasks.size, 0);
    for (int i = 0; i < (int)group.size; ++i) for (const Result::Link &link : result.links.span(result.byCell[group[i]])) {
        const int half = templates.patterns[result.matches[link.match].pattern].half;
        eventMasks[link.match] |= link.slot >= 2 * half ? 12 : (link.slot >= half ? 2 : 1);
    }

    vector<Workspace::Ranked> &ranked = workspace.ranked;
    ranked.resize(0);
    workspace.forces.data.resize(0);
    workspace.blockers.data.resize(0);
    vector<Workspace::Event> &events = workspace.events;
    events.resize(0);
    workspace.assigned.resize(0);
    workspace.fixed.resize(result.mine.size);
    std::fill_n(workspace.fixed.data, workspace.fixed.size, 0);
    Query query{result, position, positions, templates, workspace.forces, workspace.blockers, workspace.values,
                workspace.assigned, workspace.fixed, events, workspace.signature};
    for (int i = 0; i < (int)eventMasks.size; ++i) for (int direction = 0; direction < 4; ++direction) {
        if (!(eventMasks[i] & (1 << direction))) continue;
        Workspace::Event event;
        if (!query.prepare(event, result.matches[i], direction % 2, direction >= 2, workspace.base)) continue;
        const long double ways = query.mass(event);
        if (ways > 0.0L) ranked.push_back({event, ways, (int)ranked.size});
    }
    for (int i = 0; i < (int)ranked.size; ++i) if (!ranked[i].event.restriction) {
        const decltype(workspace.forces.span(ranked[i].event.base)) required = workspace.forces.span(ranked[i].event.base);
        for (int j = 0; j < (int)ranked.size; ++j) if (i != j && !ranked[j].event.restriction) {
            const decltype(workspace.forces.span(ranked[j].event.base)) other = workspace.forces.span(ranked[j].event.base);
            if ((ranked[j].event.base.count < ranked[i].event.base.count || (ranked[j].event.base.count == ranked[i].event.base.count && j < i)) &&
                std::includes(required.begin(), required.end(), other.begin(), other.end())) {
                ranked[i].ways = 0.0L;
                break;
            }
        }
    }
    const int selected = (std::min)(result.limit, (int)ranked.size);
    if (selected > 0)
        std::partial_sort(ranked.data, ranked.data + selected, ranked.data + ranked.size, [](const Workspace::Ranked &a, const Workspace::Ranked &b) {
            return a.ways == b.ways ? a.order < b.order : a.ways > b.ways;
        });
    int bins = 1;
    for (int i = selected - 1; i >= 0; --i) if (ranked[i].ways > 0.0L) {
        events.push_back(ranked[i].event);
        if constexpr (request == DetailRequest::Details) bins += ranked[i].event.restriction;
    }
    if (events.size == 0) {
        if constexpr (request == DetailRequest::Score)
            for (int i = 0; i < (int)group.size; ++i) result.compensation[group[i]] = 0.0L;
        return answer;
    }

    workspace.fixed[cell] = 1;
    workspace.assigned.push_back({cell, 1});
    const long double rootWays = mine * probability.totalWays;
    const Workspace::Values output{0, std::size_t(bins)};
    workspace.values.data.resize(bins);
    query.evaluate((int)events.size - 1, rootWays, output, {});
    long double loss = workspace.values.span(output)[0];
    if constexpr (request == DetailRequest::Details) {
        answer.restrictions.resize(bins);
        long double noBody = 0.0L, weight = 1.0L;
        loss = 0.0L;
        for (int k = 0; k < bins; ++k) {
            const long double ways = workspace.values.span(output)[k];
            answer.restrictions[k] = ways / rootWays;
            noBody += answer.restrictions[k];
            loss += weight * ways;
            weight *= 0.5L;
        }
        answer.body = 1.0L - noBody;
    }
    answer.compensation = (rootWays - loss) / probability.totalWays;
    if constexpr (request == DetailRequest::Score)
        for (int i = 0; i < (int)group.size; ++i) result.compensation[group[i]] = answer.compensation;
    return answer;
}

void TemplateLongTermRisk::coarse(Grid<long double> &output, Result &result, Position::Result &position, Position::Pool &positions,
                                 const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                 const FiftyFiftyData::Pool &templates) {
    output.resize(position.board.cells.rows(), position.board.cells.cols(), 0.0L);
    const int limit = result.limit;
    if (limit != 1) {
        result.limit = 1;
        std::fill_n(result.compensation.data, result.compensation.size, -1.0L);
    }
    long double *const scores = output[0];
    for (int cell = 0; cell < (int)result.mine.size; ++cell)
        scores[cell] = influence(cell, result, position, positions, probability, probabilities, templates).compensation;
    if (limit != 1) {
        result.limit = limit;
        std::fill_n(result.compensation.data, result.compensation.size, -1.0L);
    }
}

template TemplateLongTermRisk::Score TemplateLongTermRisk::influence<TemplateLongTermRisk::DetailRequest::Score>(
    ObservedBoard::CellId, Result &, Position::Result &, Position::Pool &, const Position::ProbabilityResult &,
    Probability::Pool &, const FiftyFiftyData::Pool &);
template TemplateLongTermRisk::Details TemplateLongTermRisk::influence<TemplateLongTermRisk::DetailRequest::Details>(
    ObservedBoard::CellId, Result &, Position::Result &, Position::Pool &, const Position::ProbabilityResult &,
    Probability::Pool &, const FiftyFiftyData::Pool &);

} // 命名空间 mss
