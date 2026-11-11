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

bool Semi::semiContains(const vector<CellId> &cells, CellId cell) {
    for (std::size_t i = 0; i < cells.size; ++i)
        if (cells[i] == cell)
            return true;
    return false;
}

bool Semi::adjacent(const ObservedBoard::Result &board, CellId lhs, CellId rhs) {
    const std::pair<int, int> left = board.pos(lhs);
    const std::pair<int, int> right = board.pos(rhs);
    return lhs != rhs && std::abs(left.first - right.first) <= 1 && std::abs(left.second - right.second) <= 1;
}

void Semi::assess(Link &link, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities) {
    const CellId tiles[2] = {link.first, link.second};
    for (int index = 0; index < 2; ++index) {
        const CellId tile = tiles[index];
        const std::pair<int, int> position = state.board.pos(tile);
        forEachAdjacent(position.first, position.second, state.board.cells.rows(), state.board.cells.cols(), [&](int row, int col) {
            const CellId info = state.board.id(row, col);
            if (info == link.first || info == link.second)
                return;
            if (Probability().mineProbability(probability.boxes, probabilities, info, state.board, state.basic, state.structure) == 1.0L)
                return;
            const CellId other = tile == link.first ? link.second : link.first;
            if (!adjacent(state.board, info, other)) {
                link.trouble[link.troubleCount++] = info;
                if (tile == link.first)
                    link.closedFirst = false;
                else
                    link.closedSecond = false;
                link.unavoidable = false;
            }
        });
    }
}

void Semi::findRootedLinks(const vector<CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<CellId> &dead, int witnessSize, vector<Link> &links, int &linkCount) {
    const int offsets[2][2] = {{-1, 0}, {0, 1}};
    for (std::size_t index = 0; index < cells.size; ++index) {
        const CellId tile1 = cells[index];
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
                links[linkCount].reset(tile1, tile2, semiContains(dead, tile1), semiContains(dead, tile2), true, witnessSize);
            Link &link = links[linkCount];
            assess(link, state, probability, probabilities);
            if ((link.closedFirst || link.closedSecond) && (!link.deadFirst || !link.deadSecond))
                ++linkCount;
        }
    }
}

bool Semi::noTrouble(const ObservedBoard::Result &board, const vector<CellId> &trouble, const vector<CellId> &chain) {
    for (std::size_t i = 0; i < trouble.size; ++i) {
        const CellId cell = trouble[i];
        if (semiContains(chain, cell))
            continue;
        int count = 0;
        for (std::size_t j = 0; j < chain.size; ++j)
            if (adjacent(board, cell, chain[j]))
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

void Semi::collectLinks(const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<CellId> &dead, vector<Link> &links, int &linkCount, std::deque<vector<CellId>> &witnesses, int &witnessCount) {
    Workspace &workspace = getWorkspace();
    vector<CellId> &candidates = workspace.cells;
    const Structure::Pool &pool = probabilities.distributions.shapes;
    for (std::size_t i = 0; i < state.structure.instanceIds.size; ++i) {
        const Structure::Instance &instance = pool.instances.list[state.structure.instanceIds[i]];
        const Structure::Shape &shape = pool.shapes.list[instance.shape];
        for (const Structure::Shape::Constraint &constraint : pool.shapes.constraints.span(shape.constraints)) {
            int sum = constraint.sum;
            candidates.resize(0);
            for (Structure::BoxId id : pool.shapes.boxIds.span(constraint.boxIds)) {
                const vectorPool<CellId>::vector members = pool.instances.boxes.span(instance.boxes)[id];
                const CellId first = pool.instances.cells.span(members)[0];
                const long double mine = Probability().mineProbability(probability.boxes, probabilities, first, state.board, state.basic, state.structure);
                if (mine == 1.0L)
                    sum -= (int)members.count;
                else if (mine != 0.0L)
                    for (CellId cell : pool.instances.cells.span(members))
                        candidates.push_back(cell);
            }
            if (sum != 1 || candidates.size < 2)
                continue;
            if (candidates.size == 2) {
                const CellId first = candidates[0];
                const CellId second = candidates[1];
                bool duplicate = false;
                for (int link = 0; link < linkCount; ++link) {
                    const Link &current = links[link];
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
                    links[linkCount].reset(first, second, semiContains(dead, first), semiContains(dead, second), false, 2);
                assess(links[linkCount], state, probability, probabilities);
                ++linkCount;
            } else {
                const int previous = linkCount;
                findRootedLinks(candidates, state, probability, probabilities, dead, (int)candidates.size, links, linkCount);
                if (linkCount == previous) {
                    if (witnessCount == (int)witnesses.size())
                        witnesses.emplace_back();
                    witnesses[witnessCount] = candidates;
                    ++witnessCount;
                }
            }
        }
    }
}

bool Semi::extendChain(const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, span<Semi::Link> links, Semi::Chain &chain, std::deque<Semi::Chain> &chains, int &chainCount, CellId target) {
    bool extended = false;
    for (int index = 0; index < links.size; ++index) {
        Semi::Link &link = links[index];
        if (link.processed || (chain.semi && link.semi) || (link.first != chain.openTile && link.second != chain.openTile))
            continue;
        const bool first = link.first == chain.openTile;
        const CellId next = first ? link.second : link.first;
        if (semiContains(chain.whole5050, next))
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
        const bool hit = !closed || target < 0 || (semiContains(chain.whole5050, target) && (!chain.semi || Probability().mineProbability(probability.boxes, probabilities, target, state.board, state.basic, state.structure) <= 0.5L));
        if (closed ? hit && chain.whole5050.size % 2 == 0 && noTrouble(state.board, chain.trouble, chain.whole5050) : extendChain(state, probability, probabilities, links, chain, chains, chainCount, target))
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

Semi::Kind Semi::extendChains(vector<CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, span<Link> links, std::deque<Chain> &chains, int &chainCount, CellId target) {
    for (int startIndex = 0; startIndex < links.size; ++startIndex) {
        Link &start = links[startIndex];
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
        if (extendChain(state, probability, probabilities, links, chain, chains, chainCount, target)) {
            cells.resize(0);
            for (int i = 0; i < (int)chain.whole5050.size; ++i) {
                const CellId cell = chain.whole5050[i];
                if (!chain.semi || Probability().mineProbability(probability.boxes, probabilities, cell, state.board, state.basic, state.structure) <= 0.5L)
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

Semi::Kind Semi::connectChains(vector<CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<CellId> &dead, std::deque<Chain> &chains, int chainCount, const std::deque<vector<CellId>> &witnesses, int witnessCount, CellId target) {
    Workspace &workspace = getWorkspace();
    vector<CellId> &combined = workspace.combined;
    vector<CellId> &trouble = workspace.trouble;
    for (int witnessIndex = 0; witnessIndex < witnessCount; ++witnessIndex) {
        const vector<CellId> &witness = witnesses[witnessIndex];
        for (int first = 0; first < chainCount; ++first) {
            const Chain &chain1 = chains[first];
            if (!semiContains(witness, chain1.openTile))
                continue;
            const long double tally1 = Probability().mineProbability(probability.boxes, probabilities, chain1.openTile, state.board, state.basic, state.structure) * probability.totalWays;
            for (int second = first + 1; second < chainCount; ++second) {
                const Chain &chain2 = chains[second];
                if (!semiContains(witness, chain2.openTile))
                    continue;
                bool overlap = false;
                for (int i = 0; i < (int)chain1.whole5050.size; ++i)
                    if (semiContains(chain2.whole5050, chain1.whole5050[i]))
                        overlap = true;
                if (overlap)
                    continue;
                const long double tally2 = Probability().mineProbability(probability.boxes, probabilities, chain2.openTile, state.board, state.basic, state.structure) * probability.totalWays;

                Link linker(chain1.openTile, chain2.openTile, semiContains(dead, chain1.openTile), semiContains(dead, chain2.openTile), false, (int)witness.size);
                assess(linker, state, probability, probabilities);

                combined.resize(0);
                for (int i = 0; i < (int)chain1.whole5050.size; ++i)
                    combined.push_back(chain1.whole5050[i]);
                for (int i = 0; i < (int)chain2.whole5050.size; ++i)
                    combined.push_back(chain2.whole5050[i]);

                trouble.resize(0);
                for (int i = 0; i < linker.troubleCount; ++i)
                    trouble.push_back(linker.trouble[i]);
                for (int i = 0; i < (int)chain1.trouble.size; ++i)
                    trouble.push_back(chain1.trouble[i]);
                for (int i = 0; i < (int)chain2.trouble.size; ++i)
                    trouble.push_back(chain2.trouble[i]);
                if (combined.size % 2 != 0 || !noTrouble(state.board, trouble, combined))
                    continue;

                if (chain1.semi && chain2.semi) {
                    const long double lowest = (std::min)(tally1, tally2);
                    bool broken = false;
                    for (int i = 0; i < (int)combined.size; ++i) {
                        const long double tally = Probability().mineProbability(probability.boxes, probabilities, combined[i], state.board, state.basic, state.structure) * probability.totalWays;
                        if (tally < lowest) {
                            broken = true;
                            break;
                        }
                    }
                    if (broken)
                        continue;
                }
                const int comparison = tally1 < tally2 ? -1 : (tally1 > tally2 ? 1 : 0);
                if (target >= 0 && !((comparison <= 0 && chain1.openTile == target) || (comparison >= 0 && chain2.openTile == target))) continue;
                cells.resize(0);
                if (comparison == 0) {
                    cells.push_back(chain1.openTile);
                    cells.push_back(chain2.openTile);
                } else
                    cells.push_back(comparison < 0 ? chain1.openTile : chain2.openTile);
                // 连接约束含两格之外的可能雷位，两端可以同时安全。
                return Kind::Semi;
            }
        }
    }
    return Kind::None;
}

Semi::Kind Semi::update(vector<CellId> &cells, const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<CellId> &dead, CellId target) {
    Workspace &workspace = getWorkspace();
    cells.resize(0);
    int linkCount = 0;
    int chainCount = 0;
    int witnessCount = 0;
    collectLinks(state, probability, probabilities, dead, workspace.links, linkCount, workspace.witnesses, witnessCount);
    const span<Link> links(workspace.links.data, linkCount);
    std::sort(links.begin(), links.end(), [this](const Link &left, const Link &right) { return linkLess(left, right); });
    int unavoidable = -1;
    for (int i = 0; i < links.size; ++i) {
        if (!links[i].unavoidable)
            continue;
        if (target >= 0 && links[i].first != target && links[i].second != target) continue;
        unavoidable = i;
        if (!links[i].semi)
            break;
    }
    if (unavoidable >= 0) {
        const Link &link = links[unavoidable];
        cells.push_back(link.first);
        cells.push_back(link.second);
        // 重叠的封闭 Semi 边属于同一组，沿共享端点收齐候选。
        if (link.semi) {
            for (int i = 0; i < (int)cells.size; ++i) {
                for (int j = 0; j < links.size; ++j) {
                    const Link &link = links[j];
                    if (!link.semi || !link.unavoidable || (link.first != cells[i] && link.second != cells[i]))
                        continue;
                    const CellId cell = link.first == cells[i] ? link.second : link.first;
                    if (!semiContains(cells, cell))
                        cells.push_back(cell);
                }
            }
        }
        return link.semi ? Kind::Semi : Kind::Forced;
    }
    const Kind kind = extendChains(cells, state, probability, probabilities, links, workspace.chains, chainCount, target);
    if (kind != Kind::None) return kind;
    return connectChains(cells, state, probability, probabilities, dead, workspace.chains, chainCount, workspace.witnesses, witnessCount, target);
}

Semi::Result Semi::analyze(const Position::Result &state, const Position::ProbabilityResult &probability, const Probability::Pool &probabilities, const vector<CellId> &dead) {
    Result result;
    result.kind = update(result.cells, state, probability, probabilities, dead);
    return result;
}

} // 命名空间 mss
