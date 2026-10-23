#include "algo/winrate_engine/fifty_fifty/old_java/pseudo.h"

#include <algorithm>
#include <cmath>

#include "core/algorithm/neighbors.h"

namespace mss {

using CellId = ObservedBoard::CellId;

static const int *shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.boxSizes.data.data + shape.boxSizes.offset; }
static const Structure::Shape::Constraint *shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.constraints.data.data + shape.constraints.offset; }
static const Structure::BoxId *constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes) { return shapes.shapes.boxIds.data.data + constraint.boxIds.offset; }

Pseudo::Link::Link() : first(-1), second(-1), closedFirst(true), closedSecond(true), deadFirst(false), deadSecond(false), processed(false), pseudo(false), unavoidable(true), witnessSize(0), trouble{}, troubleCount(0) {}

Pseudo::Link::Link(CellId first, CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize) : Link() {
    reset(first, second, deadFirst, deadSecond, pseudo, witnessSize);
}

void Pseudo::Link::reset(CellId first, CellId second, bool deadFirst, bool deadSecond, bool pseudo, int witnessSize) {
    this->first = first;
    this->second = second;
    this->deadFirst = deadFirst;
    this->deadSecond = deadSecond;
    this->pseudo = pseudo;
    this->witnessSize = witnessSize;
    closedFirst = true;
    closedSecond = true;
    unavoidable = true;
    processed = false;
    troubleCount = 0;
}

void Pseudo::Link::addTrouble(CellId cell) {
    if (troubleCount < 16)
        trouble[troubleCount++] = cell;
}

bool Pseudo::pseudoContains(span<const CellId> cells, CellId cell) {
    for (int i = 0; i < cells.size; ++i)
        if (cells.data[i] == cell)
            return true;
    return false;
}

bool Pseudo::adjacent(const ObservedBoard::Result &board, CellId lhs, CellId rhs) {
    const std::pair<int, int> left = board.pos(lhs);
    const std::pair<int, int> right = board.pos(rhs);
    return lhs != rhs && std::abs(left.first - right.first) <= 1 && std::abs(left.second - right.second) <= 1;
}

bool Pseudo::isMine(const PositionState &state, CellId cell, long double eqEps) {
    const std::pair<int, int> position = state.board.pos(cell);
    if (state.basic.marks[position.first][position.second] == Basic::Mark::F)
        return true;
    return Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) >= 1.0L - eqEps;
}

void Pseudo::assess(Link &link, const PositionState &state, long double eqEps) {
    const CellId tiles[2] = {link.first, link.second};
    for (int index = 0; index < 2; ++index) {
        const CellId tile = tiles[index];
        const std::pair<int, int> position = state.board.pos(tile);
        forEachAdjacent(position.first, position.second, state.board.cells.rows(), state.board.cells.cols(), [&](int row, int col) {
            const CellId info = state.board.id(row, col);
            if (info == link.first || info == link.second)
                return;
            if (isMine(state, info, eqEps))
                return;
            const CellId other = tile == link.first ? link.second : link.first;
            if (!adjacent(state.board, info, other)) {
                link.addTrouble(info);
                if (tile == link.first)
                    link.closedFirst = false;
                else
                    link.closedSecond = false;
                link.unavoidable = false;
            }
        });
    }
}

void Pseudo::findRootedLinks(span<const CellId> cells, const PositionState &state, span<const CellId> dead, int witnessSize, long double eqEps, vector<Link> &links, int &linkCount) {
    const int offsets[2][2] = {{-1, 0}, {0, 1}};
    for (int index = 0; index < cells.size; ++index) {
        const CellId tile1 = cells.data[index];
        const std::pair<int, int> position = state.board.pos(tile1);
        for (int offset = 0; offset < 2; ++offset) {
            const int row = position.first + offsets[offset][0];
            const int col = position.second + offsets[offset][1];
            if (row < 0 || row >= state.board.cells.rows() || col < 0 || col >= state.board.cells.cols())
                continue;
            const CellId tile2 = state.board.id(row, col);
            if (!pseudoContains(cells, tile2))
                continue;
            if (linkCount == (int)links.size)
                links.push_back(Link(tile1, tile2, pseudoContains(dead, tile1), pseudoContains(dead, tile2), true, witnessSize));
            else
                links.data[linkCount].reset(tile1, tile2, pseudoContains(dead, tile1), pseudoContains(dead, tile2), true, witnessSize);
            Link &link = links.data[linkCount];
            assess(link, state, eqEps);
            if ((link.closedFirst || link.closedSecond) && (!link.deadFirst || !link.deadSecond))
                ++linkCount;
        }
    }
}

bool Pseudo::noTrouble(const ObservedBoard::Result &board, span<const CellId> trouble, span<const CellId> chain) {
    for (int i = 0; i < trouble.size; ++i) {
        const CellId cell = trouble.data[i];
        if (pseudoContains(chain, cell))
            continue;
        int count = 0;
        for (int j = 0; j < chain.size; ++j)
            if (adjacent(board, cell, chain.data[j]))
                ++count;
        if (count % 2 != 0)
            return false;
    }
    return true;
}

bool Pseudo::linkLess(const Link &lhs, const Link &rhs) {
    if (lhs.pseudo != rhs.pseudo)
        return !lhs.pseudo;
    return lhs.witnessSize < rhs.witnessSize;
}

void Pseudo::collectLinks(const PositionState &state, span<const CellId> dead, long double eqEps, vector<Link> &links, int &linkCount, std::deque<vector<CellId>> &witnesses, int &witnessCount, Workspace &workspace) {
    for (int component = 0; component < (int)state.structure.components.size; ++component) {
        const Structure::Instance &instance = state.shapes.instances.list.data[state.structure.components.data[component]];
        const Structure::Shape &shape = state.shapes.shapes.list.data[instance.shape];
        const int *boxSizes = shapeBoxSizes(shape, state.shapes);
        const int *boxOf = state.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        const CellId *instanceCells = state.shapes.instances.cells.data.data + instance.cells.offset;
        const Structure::Shape::Constraint *constraints = shapeConstraints(shape, state.shapes);
        for (int i = 0; i < (int)shape.constraints.count; ++i) {
            const Structure::Shape::Constraint &constraint = constraints[i];
            int sum = constraint.sum;
            workspace.cells.resize(0);
            const Structure::BoxId *members = constraintMembers(constraint, state.shapes);
            for (int box = 0; box < (int)constraint.boxIds.count; ++box) {
                const Structure::BoxId id = members[box];
                const long double mine = Probability::mineProbability(state.probability, instanceCells[boxOf[id]], state.board, state.basic, state.structure);
                if (mine == 1.0L) {
                    sum -= boxSizes[id];
                    continue;
                }
                if (mine == 0.0L)
                    continue;
                for (int k = boxOf[id]; k < boxOf[id + 1]; ++k)
                    workspace.cells.push_back(instanceCells[k]);
            }
            if (sum != 1 || workspace.cells.size < 2)
                continue;
            if (workspace.cells.size == 2) {
                const CellId first = workspace.cells.data[0];
                const CellId second = workspace.cells.data[1];
                bool duplicate = false;
                for (int link = 0; link < linkCount; ++link) {
                    const Link &current = links.data[link];
                    if (!current.pseudo && ((current.first == first && current.second == second) || (current.first == second && current.second == first))) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                    continue;
                if (linkCount == (int)links.size)
                    links.push_back(Link(first, second, pseudoContains(dead, first), pseudoContains(dead, second), false, 2));
                else
                    links.data[linkCount].reset(first, second, pseudoContains(dead, first), pseudoContains(dead, second), false, 2);
                assess(links.data[linkCount], state, eqEps);
                ++linkCount;
            } else {
                const int previous = linkCount;
                findRootedLinks(span<const CellId>(workspace.cells.data, (int)workspace.cells.size), state, dead, (int)workspace.cells.size, eqEps, links, linkCount);
                if (linkCount == previous) {
                    if (witnessCount == (int)witnesses.size())
                        witnesses.emplace_back();
                    witnesses[witnessCount].resize(0);
                    for (int cell = 0; cell < (int)workspace.cells.size; ++cell)
                        witnesses[witnessCount].push_back(workspace.cells.data[cell]);
                    ++witnessCount;
                }
            }
        }
    }
}

static bool extendChain(const PositionState &state, span<Pseudo::Link> links, Pseudo::Chain &chain, std::deque<Pseudo::Chain> &chains, int &chainCount, bool (*noTrouble)(const ObservedBoard::Result &, span<const CellId>, span<const CellId>)) {
    bool extended = false;
    for (int index = 0; index < links.size; ++index) {
        Pseudo::Link &link = links.data[index];
        if (link.processed || (chain.pseudo && link.pseudo) || (link.first != chain.openTile && link.second != chain.openTile))
            continue;
        const bool first = link.first == chain.openTile;
        const CellId next = first ? link.second : link.first;
        if (std::find(chain.whole5050.data, chain.whole5050.data + chain.whole5050.size, next) != chain.whole5050.data + chain.whole5050.size)
            continue;
        extended = true;
        const CellId previous = chain.openTile;
        const bool pseudo = chain.pseudo;
        const bool closed = first ? link.closedSecond : link.closedFirst;
        chain.openTile = next;
        chain.pseudo = pseudo || link.pseudo;
        chain.whole5050.push_back(next);
        for (int i = 0; i < link.troubleCount; ++i)
            chain.trouble.push_back(link.trouble[i]);
        // processed 只属于当前路径；失败后撤销，不能吃掉其它闭链分支。
        link.processed = true;
        if (closed ? chain.whole5050.size % 2 == 0 && noTrouble(state.board, span<const CellId>(chain.trouble.data, (int)chain.trouble.size), span<const CellId>(chain.whole5050.data, (int)chain.whole5050.size)) : extendChain(state, links, chain, chains, chainCount, noTrouble))
            return true;
        link.processed = false;
        chain.openTile = previous;
        chain.pseudo = pseudo;
        --chain.whole5050.size;
        chain.trouble.size -= link.troubleCount;
    }
    if (!extended) {
        if (++chainCount == (int)chains.size())
            chains.emplace_back(chain.openTile, chain.pseudo);
        chains[chainCount] = chain;
    }
    return false;
}

Pseudo::Kind Pseudo::extendChains(Result &result, const PositionState &state, span<Link> links, std::deque<Chain> &chains, int &chainCount) {
    if (chains.empty())
        chains.emplace_back(-1, false);
    Chain &chain = chains[0];
    for (int startIndex = 0; startIndex < links.size; ++startIndex) {
        Link &start = links.data[startIndex];
        if (start.closedFirst == start.closedSecond)
            continue;
        chain.openTile = start.closedFirst ? start.second : start.first;
        chain.pseudo = start.pseudo;
        chain.whole5050.resize(0);
        chain.trouble.resize(0);
        chain.whole5050.push_back(start.first);
        chain.whole5050.push_back(start.second);
        for (int i = 0; i < start.troubleCount; ++i)
            chain.trouble.push_back(start.trouble[i]);
        start.processed = true;
        if (extendChain(state, links, chain, chains, chainCount, noTrouble)) {
            result.cells.resize(0);
            for (int i = 0; i < (int)chain.whole5050.size; ++i) {
                const CellId cell = chain.whole5050.data[i];
                if (!chain.pseudo || Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) <= 0.5L)
                    result.cells.push_back(cell);
            }
            return chain.pseudo ? Kind::Semi : Kind::Forced;
        }
        start.processed = false;
    }
    // 第 0 槽供路径复用；把最后一条开放链移入，输出保持连续。
    if (chainCount != 0) {
        chain.whole5050.swap(chains[chainCount].whole5050);
        chain.trouble.swap(chains[chainCount].trouble);
        std::swap(chain.openTile, chains[chainCount].openTile);
        std::swap(chain.pseudo, chains[chainCount].pseudo);
    }
    return Kind::None;
}

Pseudo::Kind Pseudo::connectChains(Result &result, const PositionState &state, span<const CellId> dead, long double eqEps, std::deque<Chain> &chains, int chainCount, const std::deque<vector<CellId>> &witnesses, int witnessCount, Workspace &workspace) {
    for (int witnessIndex = 0; witnessIndex < witnessCount; ++witnessIndex) {
        const vector<CellId> &witness = witnesses[witnessIndex];
        for (int first = 0; first < chainCount; ++first) {
            Chain *chain1 = &chains[first];
            if (!pseudoContains(span<const CellId>(witness.data, (int)witness.size), chain1->openTile))
                continue;
            const long double tally1 = Probability::mineProbability(state.probability, chain1->openTile, state.board, state.basic, state.structure) * state.probability.candidates;
            for (int second = first + 1; second < chainCount; ++second) {
                Chain *chain2 = &chains[second];
                if (!pseudoContains(span<const CellId>(witness.data, (int)witness.size), chain2->openTile))
                    continue;
                bool overlap = false;
                for (int i = 0; i < (int)chain1->whole5050.size; ++i)
                    if (pseudoContains(span<const CellId>(chain2->whole5050.data, (int)chain2->whole5050.size), chain1->whole5050.data[i]))
                        overlap = true;
                if (overlap)
                    continue;
                const long double tally2 = Probability::mineProbability(state.probability, chain2->openTile, state.board, state.basic, state.structure) * state.probability.candidates;

                Link linker(chain1->openTile, chain2->openTile, pseudoContains(dead, chain1->openTile), pseudoContains(dead, chain2->openTile), false, (int)witness.size);
                assess(linker, state, eqEps);

                workspace.combined.resize(0);
                for (int i = 0; i < (int)chain1->whole5050.size; ++i)
                    workspace.combined.push_back(chain1->whole5050.data[i]);
                for (int i = 0; i < (int)chain2->whole5050.size; ++i)
                    workspace.combined.push_back(chain2->whole5050.data[i]);

                workspace.trouble.resize(0);
                for (int i = 0; i < linker.troubleCount; ++i)
                    workspace.trouble.push_back(linker.trouble[i]);
                for (int i = 0; i < (int)chain1->trouble.size; ++i)
                    workspace.trouble.push_back(chain1->trouble.data[i]);
                for (int i = 0; i < (int)chain2->trouble.size; ++i)
                    workspace.trouble.push_back(chain2->trouble.data[i]);
                if (workspace.combined.size % 2 != 0 || !noTrouble(state.board, span<const CellId>(workspace.trouble.data, (int)workspace.trouble.size), span<const CellId>(workspace.combined.data, (int)workspace.combined.size)))
                    continue;

                if (chain1->pseudo && chain2->pseudo) {
                    const long double lowest = (std::min)(tally1, tally2);
                    bool broken = false;
                    for (int i = 0; i < (int)workspace.combined.size; ++i) {
                        const long double tally = Probability::mineProbability(state.probability, workspace.combined.data[i], state.board, state.basic, state.structure) * state.probability.candidates;
                        if (tally < lowest) {
                            broken = true;
                            break;
                        }
                    }
                    if (broken)
                        continue;
                }
                const int comparison = tally1 < tally2 ? -1 : (tally1 > tally2 ? 1 : 0);
                result.cells.resize(0);
                if (comparison == 0) {
                    result.cells.push_back(chain1->openTile);
                    result.cells.push_back(chain2->openTile);
                } else
                    result.cells.push_back(comparison < 0 ? chain1->openTile : chain2->openTile);
                // 连接约束含两格之外的可能雷位，两端可以同时安全。
                return Kind::Semi;
            }
        }
    }
    return Kind::None;
}

Pseudo::Kind Pseudo::update(Result &result, const PositionState &state, span<const CellId> dead, const Config &config, Workspace &workspace) {
    result.kind = Kind::None;
    result.cells.resize(0);
    int linkCount = 0;
    int chainCount = 0;
    int witnessCount = 0;
    collectLinks(state, dead, config.eqEps, workspace.links, linkCount, workspace.witnesses, witnessCount, workspace);
    const span<Link> links(workspace.links.data, linkCount);
    std::sort(links.begin(), links.end(), linkLess);
    const Link *unavoidable = nullptr;
    for (int i = 0; i < links.size; ++i) {
        if (!links.data[i].unavoidable)
            continue;
        unavoidable = &links.data[i];
        if (!links.data[i].pseudo)
            break;
    }
    if (unavoidable != nullptr) {
        result.cells.resize(0);
        result.cells.push_back(unavoidable->first);
        result.cells.push_back(unavoidable->second);
        // 重叠的封闭 Semi 边属于同一组，沿共享端点收齐候选。
        if (unavoidable->pseudo) {
            for (int i = 0; i < (int)result.cells.size; ++i) {
                for (int j = 0; j < links.size; ++j) {
                    const Link &link = links.data[j];
                    if (!link.pseudo || !link.unavoidable || (link.first != result.cells.data[i] && link.second != result.cells.data[i]))
                        continue;
                    const CellId cell = link.first == result.cells.data[i] ? link.second : link.first;
                    if (!pseudoContains(span<const CellId>(result.cells.data, (int)result.cells.size), cell))
                        result.cells.push_back(cell);
                }
            }
        }
        result.kind = unavoidable->pseudo ? Kind::Semi : Kind::Forced;
        return result.kind;
    }
    const Kind kind = extendChains(result, state, links, workspace.chains, chainCount);
    if (kind != Kind::None) {
        result.kind = kind;
        return kind;
    }
    const Kind connected = connectChains(result, state, dead, config.eqEps, workspace.chains, chainCount, workspace.witnesses, witnessCount, workspace);
    if (connected != Kind::None) {
        result.kind = connected;
        return connected;
    }
    return result.kind;
}

Pseudo::Result Pseudo::analyze(const PositionState &state, span<const CellId> dead, const Config &config, Workspace &workspace) {
    Result result;
    update(result, state, dead, config, workspace);
    return result;
}

} // 命名空间 mss
