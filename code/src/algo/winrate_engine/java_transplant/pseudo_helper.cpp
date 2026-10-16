#include "pseudo_helper.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/utility/neighbors.h"

namespace mss {

bool Pseudo::pseudoContains(winrate::Cells cells, winrate::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

bool Pseudo::adjacent(const winrate::Board &board, winrate::CellId lhs, winrate::CellId rhs) {
    const auto [lx, ly] = board.pos(lhs);
    const auto [rx, ry] = board.pos(rhs);
    return lhs != rhs && std::abs(lx - rx) <= 1 && std::abs(ly - ry) <= 1;
}

// Java isConfirmedMine / isMineInPe：确定雷（basic Mine 或 PE 新雷）。
bool Pseudo::isMine(const winrate::View &input, winrate::CellId cell, long double eqEps) {
    const auto &[board, basic, structure, probability, shapes] = input;
    const auto [x, y] = board.pos(cell);
    return basic.marks[x][y] == Basic::Mark::F || probability.mineProbability(cell, board, basic, structure) >= 1.0L - eqEps;
}

// Java assessLink：检查链两端是否"封闭"（每个相邻信息格要么是链格、要么同时
// 监控两格），并标记死格。
void Pseudo::assess(Link &link, const winrate::View &input, long double eqEps) {
    const auto &[board, basic, structure, probability, shapes] = input;
    for (winrate::CellId tile : {link.first, link.second}) {
        const auto [x, y] = board.pos(tile);
        forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
            const winrate::CellId info = board.id(nx, ny);
            if (info == link.first || info == link.second)
                return;
            if (isMine(input, info, eqEps))
                return;
            const winrate::CellId other = tile == link.first ? link.second : link.first;
            if (!adjacent(board, info, other)) { // 只监控到一格 → 可单独提供信息
                link.trouble.push_back(info);
                if (tile == link.first)
                    link.closedFirst = false;
                else
                    link.closedSecond = false;
                link.unavoidable = false;
            }
        });
    }
}

// Java findRootedLinks：大 witness（>2 格）内相邻且至少一端封闭、至少一格活的
// 相邻对 → 根化 pseudo link。
void Pseudo::findRootedLinks(winrate::Cells cells, const winrate::View &input, winrate::Cells dead, int witnessSize, long double eqEps, std::vector<Link> &links) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // Java 只检查"正上方"与"正右方"两个相对位置（x 为列、y 为行的坐标约定）。
    constexpr std::array<std::pair<int, int>, 2> offsets{
        std::pair{-1, 0}, // 上方（C++ row-1）
        std::pair{0, 1},  // 右方（C++ col+1）
    };
    for (winrate::CellId tile1 : cells) {
        const auto [x, y] = board.pos(tile1);
        for (const std::pair<int, int> &offset : offsets) {
            const auto [dx, dy] = offset;
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || nx >= board.board.rows() || ny < 0 || ny >= board.board.cols())
                continue;
            const winrate::CellId tile2 = board.id(nx, ny);
            if (!pseudoContains(cells, tile2))
                continue;
            Link link(tile1, tile2, pseudoContains(dead, tile1), pseudoContains(dead, tile2), true, witnessSize);
            assess(link, input, eqEps);
            if ((link.closedFirst || link.closedSecond) && // 至少一端封闭
                (!link.deadFirst || !link.deadSecond))     // 至少一格活
                links.push_back(std::move(link));
        }
    }
}

// Java noTrouble：每个 trouble 格必须邻接偶数个链格（否则能单独影响局面）。
bool Pseudo::noTrouble(const winrate::Board &board, winrate::Cells trouble, winrate::Cells chain) {
    for (winrate::CellId cell : trouble) {
        if (pseudoContains(chain, cell))
            continue;
        int count = 0;
        for (winrate::CellId member : chain)
            if (adjacent(board, cell, member))
                ++count;
        if (count % 2 != 0)
            return false;
    }
    return true;
}

// 排序键：Java Link.compareTo——非 pseudo 在前，其次 witness squares 数。
bool Pseudo::linkLess(const Link &lhs, const Link &rhs) {
    if (lhs.pseudo != rhs.pseudo)
        return !lhs.pseudo;
    return lhs.witnessSize < rhs.witnessSize;
}

void Pseudo::collectLinks(const winrate::View &input, winrate::Cells dead, long double eqEps, std::vector<Link> &links, std::vector<std::vector<winrate::CellId>> &pseudoWitnesses, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // 先按完整概率扣除必雷，再找还差一雷的约束；仅用 Basic 会使等价分支产生不同的链。
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const Structure::Shape &shape = shapes.get(instance.shape);
        for (int i = 0; i < (int)(shape.constraintCount()); ++i) {
            const Structure::Shape::ConstraintView constraint = shape.constraint(shapes, i);
            int sum = constraint.sum;
            std::vector<winrate::CellId> &cells = workspace.cells;
            cells.clear();
            for (Structure::BoxId box : constraint.boxIds) {
                const long double mine = probability.boxProbability(component, box);
                if (mine == 1.0L) { sum -= instance.boxes.cellCount(shapes, box); continue; }
                if (mine == 0.0L) continue;
                for (int k = instance.boxes.boxOf.span(shapes.boxOf)[box]; k < instance.boxes.boxOf.span(shapes.boxOf)[box + 1]; ++k)
                    cells.push_back(instance.boxes.cells.span(shapes.cells)[k]);
            }
            if (sum != 1) continue;
            if (cells.size() < 2)
                continue;
            if (cells.size() == 2) {
                // 不同数字可给出同一条边；重复加入会让链折返并错误封口。
                if (std::any_of(links.begin(), links.end(), [&](const Link &link) { return !link.pseudo && ((link.first == cells[0] && link.second == cells[1]) || (link.first == cells[1] && link.second == cells[0])); })) continue;
                Link link(cells[0], cells[1], pseudoContains(dead, cells[0]), pseudoContains(dead, cells[1]), false, 2);
                assess(link, input, eqEps);
                links.push_back(std::move(link));
            } else {
                const std::size_t previous = links.size();
                findRootedLinks(cells, input, dead, cells.size(), eqEps, links);
                if (links.size() == previous) pseudoWitnesses.push_back(cells);
            }
        }
    }

}

Pseudo::Kind Pseudo::extendChains(Result &result, const winrate::Board &board, std::span<Link> links, std::vector<Chain> &chains) {
    // 链：从一个只封闭一端的 link 出发，沿开口端扩展，封口且步数为偶 + 无 trouble
    // 即成不可避免的 50/50。
    for (Link &start : links) {
        if (start.processed || start.closedFirst == start.closedSecond)
            continue; // 恰一端封闭
        start.processed = true;
        Chain chain(start.closedFirst ? start.second : start.first, start.pseudo);
        chain.whole5050.push_back(start.first);
        chain.whole5050.push_back(start.second);
        chain.trouble.insert(chain.trouble.end(), start.trouble.begin(), start.trouble.end());

        int extensions = 0;
        bool noMatch = false;
        while (chain.openTile != -1 && !noMatch) {
            noMatch = true;
            for (Link &ext : links) {
                if (ext.processed || (chain.pseudo && ext.pseudo))
                    continue; // pseudo 链不再接 pseudo
                if (ext.first == chain.openTile) {
                    ++extensions;
                    ext.processed = true;
                    noMatch = false;
                    if (ext.pseudo) chain.pseudo = true;
                    chain.trouble.insert(chain.trouble.end(), ext.trouble.begin(), ext.trouble.end());
                    chain.whole5050.push_back(ext.second);
                    if (ext.closedSecond) {
                        if (extensions % 2 == 0 && noTrouble(board, chain.trouble, chain.whole5050)) {
                            if (ext.pseudo) result = {ext.first, ext.second};
                            else result = chain.whole5050;
                            return chain.pseudo ? Kind::OrSafe : Kind::Forced;
                        }
                        chain.openTile = -1; // 封口但条件不符 → 本链作废
                    } else {
                        chain.openTile = ext.second;
                    }
                    break;
                }
                if (ext.second == chain.openTile) {
                    ++extensions;
                    ext.processed = true;
                    noMatch = false;
                    if (ext.pseudo) chain.pseudo = true;
                    chain.trouble.insert(chain.trouble.end(), ext.trouble.begin(), ext.trouble.end());
                    chain.whole5050.push_back(ext.first);
                    if (ext.closedFirst) {
                        if (extensions % 2 == 0 && noTrouble(board, chain.trouble, chain.whole5050)) {
                            if (ext.pseudo) result = {ext.first, ext.second};
                            else result = chain.whole5050;
                            return chain.pseudo ? Kind::OrSafe : Kind::Forced;
                        }
                        chain.openTile = -1;
                    } else {
                        chain.openTile = ext.first;
                    }
                    break;
                }
            }
        }
        if (noMatch)
            chains.push_back(std::move(chain)); // 未封口的链可被 pseudo witness 连接
    }

    return Kind::None;
}

Pseudo::Kind Pseudo::connectChains(Result &result, const winrate::View &input, winrate::Cells dead, long double eqEps, std::span<Chain> chains, std::span<const std::vector<winrate::CellId>> pseudoWitnesses, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // 用 pseudo witness 连接两条链成不可避免的 50/50。
    for (const std::vector<winrate::CellId> &witness : pseudoWitnesses) {
        Chain *chain1 = nullptr;
        Chain *chain2 = nullptr;
        long double tally1 = 0.0L;
        long double tally2 = 0.0L;
        bool found = false;
        for (winrate::CellId tile : witness) {
            for (Chain &chain : chains) {
                if (chain.openTile != tile)
                    continue;
                const long double tally = probability.mineProbability(tile, board, basic, structure) * probability.candidates();
                if (chain1 == nullptr) {
                    chain1 = &chain;
                    tally1 = tally;
                    break;
                }
                chain2 = &chain;
                tally2 = tally;
                found = true;
                break;
            }
            if (found)
                break;
        }
        if (chain1 == nullptr || chain2 == nullptr)
            continue;

        Link linker(chain1->openTile, chain2->openTile, pseudoContains(dead, chain1->openTile), pseudoContains(dead, chain2->openTile), false, witness.size());
        assess(linker, input, eqEps);

        std::vector<winrate::CellId> &combined = workspace.combined;
        combined = chain1->whole5050;
        combined.insert(combined.end(), chain2->whole5050.begin(), chain2->whole5050.end());
        std::vector<winrate::CellId> &trouble = workspace.trouble;
        trouble = linker.trouble;
        trouble.insert(trouble.end(), chain1->trouble.begin(), chain1->trouble.end());
        trouble.insert(trouble.end(), chain2->trouble.begin(), chain2->trouble.end());
        if (combined.size() % 2 != 0 || !noTrouble(board, trouble, combined))
            continue;

        // 两条链都是 pseudo 时，组合链里不能有比最弱点更安全的格（否则不成立）。
        if (chain1->pseudo && chain2->pseudo) {
            const long double lowest = (std::min)(tally1, tally2);
            bool broken = false;
            for (winrate::CellId tile : combined) {
                const long double tally = probability.mineProbability(tile, board, basic, structure) * probability.candidates();
                if (tally < lowest) { // 有更安全的格 → 此 pseudo 不成立
                    broken = true;
                    break;
                }
            }
            if (broken)
                continue;
        }
        const int c = tally1 < tally2 ? -1 : (tally1 > tally2 ? 1 : 0);
        if (c == 0)
            result = {chain1->openTile, chain2->openTile};
        else result = {c < 0 ? chain1->openTile : chain2->openTile};
        return chain1->pseudo || chain2->pseudo ? Kind::OrSafe : Kind::Forced;
    }
    return Kind::None;
}

Pseudo::Kind Pseudo::update(Result &result, const winrate::View &input, winrate::Cells dead, const Config &config, Workspace &workspace) {
    const auto &[board, basic, structure, probability, shapes] = input;
    result.clear();
    std::vector<Link> &links = workspace.links; links.clear();
    std::vector<Chain> &chains = workspace.chains; chains.clear();
    std::vector<std::vector<winrate::CellId>> &witnesses = workspace.witnesses; witnesses.clear();
    collectLinks(input, dead, config.eqEps, links, witnesses, workspace);
    // Java：优先返回非 pseudo 的不可避免 link。
    std::sort(links.begin(), links.end(), linkLess);
    const Link *unavoidable = nullptr;
    for (const Link &link : links) {
        if (link.unavoidable) {
            unavoidable = &link;
            if (!link.pseudo)
                break; // 优先真 50/50，其次 pseudo
        }
    }
    if (unavoidable != nullptr) {
        result = {unavoidable->first, unavoidable->second};
        return unavoidable->pseudo ? Kind::OrSafe : Kind::Forced;
    }
    const Kind kind = extendChains(result, board, links, chains);
    if (kind != Kind::None) return kind;
    return connectChains(result, input, dead, config.eqEps, chains, witnesses, workspace);
}

Pseudo::Result Pseudo::analyze(const winrate::View &input, winrate::Cells dead, const Config &config, Workspace &workspace) {
    Result result;
    update(result, input, dead, config, workspace);
    return result;
}
} // 命名空间 mss
