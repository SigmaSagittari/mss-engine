#include "algo/winrate_engine/fifty_fifty/old_java/semi.h"

#include <algorithm>
#include <cmath>

#include "core/algorithm/neighbors.h"

namespace mss {

// 一条待连接的交换边，以及两端尚未被雷墙挡住的信息位置。
struct Semi::Link {
    ObservedBoard::CellId first;
    ObservedBoard::CellId second;
    bool closedFirst, closedSecond; // 该端独占的信息位是否已封口。
    bool deadFirst, deadSecond; // 调用方传入的死格集合是否包含该端。
    bool processed; // 拼链时是否已经使用这条边。
    bool semi; // 从较大的候选组中取出的 Semi 边。
    bool unavoidable; // 两端所有独占信息位都被封住，不需要再接边。
    int witnessSize; // 来源约束的候选格数；用于优先处理较小的结构。
    // 每条 link 最多两个格的邻居；固定容量避免热路径堆分配。
    ObservedBoard::CellId trouble[16];
    int troubleCount;

    Link(ObservedBoard::CellId first, ObservedBoard::CellId second, bool deadFirst, bool deadSecond, bool semi, int witnessSize);
    void reset(ObservedBoard::CellId first, ObservedBoard::CellId second, bool deadFirst, bool deadSecond, bool semi, int witnessSize);
    void addTrouble(ObservedBoard::CellId cell);
};

struct Semi::Chain {
    vector<ObservedBoard::CellId> whole5050; // 已连入这条链的本体格。
    // -1 表示链已封口且未满足不可避免条件，不再参与后续连接。
    ObservedBoard::CellId openTile;
    bool semi; // 链中是否含有 Semi 边。
    vector<ObservedBoard::CellId> trouble; // 尚待后续连接平衡的信息格。

    Chain(ObservedBoard::CellId openTile, bool semi) : openTile(openTile), semi(semi) {}
};

// 链接和链保留容量；每次 update 从零计数，阶段间只传本次有效范围。
struct Semi::Workspace {
    vector<Link> links;
    std::deque<Chain> chains;
    std::deque<vector<ObservedBoard::CellId>> witnesses;
    vector<ObservedBoard::CellId> cells;
    vector<ObservedBoard::CellId> combined;
    vector<ObservedBoard::CellId> trouble;
};

__declspec(noinline) Semi::Workspace &Semi::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

using CellId = ObservedBoard::CellId;

const int *Semi::shapeBoxSizes(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.boxSizes.data.data + shape.boxSizes.offset; }
const Structure::Shape::Constraint *Semi::shapeConstraints(const Structure::Shape &shape, const Structure::Pool &shapes) { return shapes.shapes.constraints.data.data + shape.constraints.offset; }
const Structure::BoxId *Semi::constraintMembers(const Structure::Shape::Constraint &constraint, const Structure::Pool &shapes) { return shapes.shapes.boxIds.data.data + constraint.boxIds.offset; }

Semi::Link::Link(CellId first, CellId second, bool deadFirst, bool deadSecond, bool semi, int witnessSize) : first(first), second(second), closedFirst(true), closedSecond(true), deadFirst(deadFirst), deadSecond(deadSecond), processed(false), semi(semi), unavoidable(true), witnessSize(witnessSize), trouble{}, troubleCount(0) {}

void Semi::Link::reset(CellId first, CellId second, bool deadFirst, bool deadSecond, bool semi, int witnessSize) {
    this->first = first;
    this->second = second;
    this->deadFirst = deadFirst;
    this->deadSecond = deadSecond;
    this->semi = semi;
    this->witnessSize = witnessSize;
    closedFirst = true;
    closedSecond = true;
    unavoidable = true;
    processed = false;
    troubleCount = 0;
}

void Semi::Link::addTrouble(CellId cell) {
    if (troubleCount < 16)
        trouble[troubleCount++] = cell;
}

bool Semi::semiContains(span<const CellId> cells, CellId cell) {
    for (int i = 0; i < cells.size; ++i)
        if (cells.data[i] == cell)
            return true;
    return false;
}

bool Semi::adjacent(const ObservedBoard::Result &board, CellId lhs, CellId rhs) {
    const std::pair<int, int> left = board.pos(lhs);
    const std::pair<int, int> right = board.pos(rhs);
    return lhs != rhs && std::abs(left.first - right.first) <= 1 && std::abs(left.second - right.second) <= 1;
}

void Semi::assess(Link &link, const PositionState &state, const Probability::Pool &probabilities) {
    const CellId tiles[2] = {link.first, link.second};
    for (int index = 0; index < 2; ++index) {
        const CellId tile = tiles[index];
        const std::pair<int, int> position = state.board.pos(tile);
        forEachAdjacent(position.first, position.second, state.board.cells.rows(), state.board.cells.cols(), [&](int row, int col) {
            const CellId info = state.board.id(row, col);
            if (info == link.first || info == link.second)
                return;
            if (Probability().mineProbability(state.probability, probabilities, info, state.board, state.basic, state.structure) == 1.0L)
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

void Semi::findRootedLinks(span<const CellId> cells, const PositionState &state, const Probability::Pool &probabilities, span<const CellId> dead, int witnessSize, vector<Link> &links, int &linkCount) {
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
            if (!semiContains(cells, tile2))
                continue;
            if (linkCount == (int)links.size)
                links.push_back(Link(tile1, tile2, semiContains(dead, tile1), semiContains(dead, tile2), true, witnessSize));
            else
                links.data[linkCount].reset(tile1, tile2, semiContains(dead, tile1), semiContains(dead, tile2), true, witnessSize);
            Link &link = links.data[linkCount];
            assess(link, state, probabilities);
            if ((link.closedFirst || link.closedSecond) && (!link.deadFirst || !link.deadSecond))
                ++linkCount;
        }
    }
}

bool Semi::noTrouble(const ObservedBoard::Result &board, span<const CellId> trouble, span<const CellId> chain) {
    for (int i = 0; i < trouble.size; ++i) {
        const CellId cell = trouble.data[i];
        if (semiContains(chain, cell))
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

bool Semi::linkLess(const Link &lhs, const Link &rhs) {
    if (lhs.semi != rhs.semi)
        return !lhs.semi;
    return lhs.witnessSize < rhs.witnessSize;
}

void Semi::collectLinks(const PositionState &state, const Probability::Pool &probabilities, span<const CellId> dead, vector<Link> &links, int &linkCount, std::deque<vector<CellId>> &witnesses, int &witnessCount) {
    Workspace &workspace = getWorkspace();
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
                const long double mine = Probability().mineProbability(state.probability, probabilities, instanceCells[boxOf[id]], state.board, state.basic, state.structure);
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
                    if (!current.semi && ((current.first == first && current.second == second) || (current.first == second && current.second == first))) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                    continue;
                if (linkCount == (int)links.size)
                    links.push_back(Link(first, second, semiContains(dead, first), semiContains(dead, second), false, 2));
                else
                    links.data[linkCount].reset(first, second, semiContains(dead, first), semiContains(dead, second), false, 2);
                assess(links.data[linkCount], state, probabilities);
                ++linkCount;
            } else {
                const int previous = linkCount;
                findRootedLinks(span<const CellId>(workspace.cells.data, (int)workspace.cells.size), state, probabilities, dead, (int)workspace.cells.size, links, linkCount);
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

bool Semi::extendChain(const PositionState &state, const Probability::Pool &probabilities, span<Semi::Link> links, Semi::Chain &chain, std::deque<Semi::Chain> &chains, int &chainCount, CellId target) {
    bool extended = false;
    for (int index = 0; index < links.size; ++index) {
        Semi::Link &link = links.data[index];
        if (link.processed || (chain.semi && link.semi) || (link.first != chain.openTile && link.second != chain.openTile))
            continue;
        const bool first = link.first == chain.openTile;
        const CellId next = first ? link.second : link.first;
        if (std::find(chain.whole5050.data, chain.whole5050.data + chain.whole5050.size, next) != chain.whole5050.data + chain.whole5050.size)
            continue;
        extended = true;
        const CellId previous = chain.openTile;
        const bool semi = chain.semi;
        const bool closed = first ? link.closedSecond : link.closedFirst;
        chain.openTile = next;
        chain.semi = semi || link.semi;
        chain.whole5050.push_back(next);
        for (int i = 0; i < link.troubleCount; ++i)
            chain.trouble.push_back(link.trouble[i]);
        // processed 只属于当前路径；失败后撤销，不能吃掉其它闭链分支。
        link.processed = true;
        const bool hit = !closed || target < 0 || (std::find(chain.whole5050.data, chain.whole5050.data + chain.whole5050.size, target) != chain.whole5050.data + chain.whole5050.size && (!chain.semi || Probability().mineProbability(state.probability, probabilities, target, state.board, state.basic, state.structure) <= 0.5L));
        if (closed ? hit && chain.whole5050.size % 2 == 0 && noTrouble(state.board, span<const CellId>(chain.trouble.data, (int)chain.trouble.size), span<const CellId>(chain.whole5050.data, (int)chain.whole5050.size)) : extendChain(state, probabilities, links, chain, chains, chainCount, target))
            return true;
        link.processed = false;
        chain.openTile = previous;
        chain.semi = semi;
        --chain.whole5050.size;
        chain.trouble.size -= link.troubleCount;
    }
    if (!extended) {
        if (++chainCount == (int)chains.size())
            chains.emplace_back(chain.openTile, chain.semi);
        chains[chainCount] = chain;
    }
    return false;
}

Semi::Kind Semi::extendChains(vector<CellId> &cells, const PositionState &state, const Probability::Pool &probabilities, span<Link> links, std::deque<Chain> &chains, int &chainCount, CellId target) {
    for (int startIndex = 0; startIndex < links.size; ++startIndex) {
        Link &start = links.data[startIndex];
        if (start.closedFirst == start.closedSecond)
            continue;
        const CellId openTile = start.closedFirst ? start.second : start.first;
        if (chains.empty()) chains.emplace_back(openTile, start.semi);
        else { chains[0].openTile = openTile; chains[0].semi = start.semi; }
        Chain &chain = chains[0];
        chain.whole5050.resize(0);
        chain.trouble.resize(0);
        chain.whole5050.push_back(start.first);
        chain.whole5050.push_back(start.second);
        for (int i = 0; i < start.troubleCount; ++i)
            chain.trouble.push_back(start.trouble[i]);
        start.processed = true;
        if (extendChain(state, probabilities, links, chain, chains, chainCount, target)) {
            cells.resize(0);
            for (int i = 0; i < (int)chain.whole5050.size; ++i) {
                const CellId cell = chain.whole5050.data[i];
                if (!chain.semi || Probability().mineProbability(state.probability, probabilities, cell, state.board, state.basic, state.structure) <= 0.5L)
                    cells.push_back(cell);
            }
            return chain.semi ? Kind::Semi : Kind::Forced;
        }
        start.processed = false;
    }
    // 第 0 槽供路径复用；把最后一条开放链移入，输出保持连续。
    if (chainCount != 0) {
        Chain &chain = chains[0];
        chain.whole5050.swap(chains[chainCount].whole5050);
        chain.trouble.swap(chains[chainCount].trouble);
        std::swap(chain.openTile, chains[chainCount].openTile);
        std::swap(chain.semi, chains[chainCount].semi);
    }
    return Kind::None;
}

Semi::Kind Semi::connectChains(vector<CellId> &cells, const PositionState &state, const Probability::Pool &probabilities, span<const CellId> dead, std::deque<Chain> &chains, int chainCount, const std::deque<vector<CellId>> &witnesses, int witnessCount, CellId target) {
    Workspace &workspace = getWorkspace();
    for (int witnessIndex = 0; witnessIndex < witnessCount; ++witnessIndex) {
        const vector<CellId> &witness = witnesses[witnessIndex];
        for (int first = 0; first < chainCount; ++first) {
            Chain *chain1 = &chains[first];
            if (!semiContains(span<const CellId>(witness.data, (int)witness.size), chain1->openTile))
                continue;
            const long double tally1 = Probability().mineProbability(state.probability, probabilities, chain1->openTile, state.board, state.basic, state.structure) * state.probability.candidates;
            for (int second = first + 1; second < chainCount; ++second) {
                Chain *chain2 = &chains[second];
                if (!semiContains(span<const CellId>(witness.data, (int)witness.size), chain2->openTile))
                    continue;
                bool overlap = false;
                for (int i = 0; i < (int)chain1->whole5050.size; ++i)
                    if (semiContains(span<const CellId>(chain2->whole5050.data, (int)chain2->whole5050.size), chain1->whole5050.data[i]))
                        overlap = true;
                if (overlap)
                    continue;
                const long double tally2 = Probability().mineProbability(state.probability, probabilities, chain2->openTile, state.board, state.basic, state.structure) * state.probability.candidates;

                Link linker(chain1->openTile, chain2->openTile, semiContains(dead, chain1->openTile), semiContains(dead, chain2->openTile), false, (int)witness.size);
                assess(linker, state, probabilities);

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

                if (chain1->semi && chain2->semi) {
                    const long double lowest = (std::min)(tally1, tally2);
                    bool broken = false;
                    for (int i = 0; i < (int)workspace.combined.size; ++i) {
                        const long double tally = Probability().mineProbability(state.probability, probabilities, workspace.combined.data[i], state.board, state.basic, state.structure) * state.probability.candidates;
                        if (tally < lowest) {
                            broken = true;
                            break;
                        }
                    }
                    if (broken)
                        continue;
                }
                const int comparison = tally1 < tally2 ? -1 : (tally1 > tally2 ? 1 : 0);
                if (target >= 0 && !((comparison <= 0 && chain1->openTile == target) || (comparison >= 0 && chain2->openTile == target))) continue;
                cells.resize(0);
                if (comparison == 0) {
                    cells.push_back(chain1->openTile);
                    cells.push_back(chain2->openTile);
                } else
                    cells.push_back(comparison < 0 ? chain1->openTile : chain2->openTile);
                // 连接约束含两格之外的可能雷位，两端可以同时安全。
                return Kind::Semi;
            }
        }
    }
    return Kind::None;
}

Semi::Kind Semi::update(vector<CellId> &cells, const PositionState &state, const Probability::Pool &probabilities, span<const CellId> dead, CellId target) {
    Workspace &workspace = getWorkspace();
    cells.resize(0);
    int linkCount = 0;
    int chainCount = 0;
    int witnessCount = 0;
    collectLinks(state, probabilities, dead, workspace.links, linkCount, workspace.witnesses, witnessCount);
    const span<Link> links(workspace.links.data, linkCount);
    std::sort(links.begin(), links.end(), [this](const Link &left, const Link &right) { return linkLess(left, right); });
    const Link *unavoidable = nullptr;
    for (int i = 0; i < links.size; ++i) {
        if (!links.data[i].unavoidable)
            continue;
        if (target >= 0 && links.data[i].first != target && links.data[i].second != target) continue;
        unavoidable = &links.data[i];
        if (!links.data[i].semi)
            break;
    }
    if (unavoidable != nullptr) {
        cells.resize(0);
        cells.push_back(unavoidable->first);
        cells.push_back(unavoidable->second);
        // 重叠的封闭 Semi 边属于同一组，沿共享端点收齐候选。
        if (unavoidable->semi) {
            for (int i = 0; i < (int)cells.size; ++i) {
                for (int j = 0; j < links.size; ++j) {
                    const Link &link = links.data[j];
                    if (!link.semi || !link.unavoidable || (link.first != cells.data[i] && link.second != cells.data[i]))
                        continue;
                    const CellId cell = link.first == cells.data[i] ? link.second : link.first;
                    if (!semiContains(span<const CellId>(cells.data, (int)cells.size), cell))
                        cells.push_back(cell);
                }
            }
        }
        return unavoidable->semi ? Kind::Semi : Kind::Forced;
    }
    const Kind kind = extendChains(cells, state, probabilities, links, workspace.chains, chainCount, target);
    if (kind != Kind::None) return kind;
    return connectChains(cells, state, probabilities, dead, workspace.chains, chainCount, workspace.witnesses, witnessCount, target);
}

Semi::Result Semi::analyze(const PositionState &state, const Probability::Pool &probabilities, span<const CellId> dead) {
    Result result;
    result.kind = update(result.cells, state, probabilities, dead);
    return result;
}

} // 命名空间 mss
