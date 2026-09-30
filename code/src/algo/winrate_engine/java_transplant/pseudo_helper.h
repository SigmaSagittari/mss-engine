#pragma once

#include <span>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/utility/neighbors.h"

namespace mss {

// ─────────────────────────────────────────────────────────────
// algo/winrate_engine/java_transplant/pseudo_helper.h — Java PseudoHelper 的忠实移植。
//
// 找"伪 50/50"（pseudo）：一个 witness（数字约束）还差一雷时，若它不被墙或
// 雷线锚定，就能把两条独立的猜链接起来拼成一个不可避免的 50/50。返回应优先
// 点击的格（可能是一对中的一格、整条链、或 pseudo witness 连成的两格）。
//
// 使用方式与 Java Solver 一致：LongTermRiskReference::findInfluence 的
// pseudos 为空时调用它补充查找。
// ─────────────────────────────────────────────────────────────

struct PseudoReference {
    // 查找 pseudo 50/50 候选。dead：已知"死格"（单结局格），Java 即 PE 的
    // deadLocations。返回空 = 无 pseudo。
    static std::vector<ObservedBoard::CellId> findPseudo5050(const ObservedBoard::Result &board, const Basic::Result &basic,
                                              const Structure::Result &structure, const Structure::Pool &shapes,
                                              const Probability::Result &probability, std::span<const ObservedBoard::CellId> dead = {});
};

} // namespace mss

//==============================================================================

#include <algorithm>
#include <array>
#include <cmath>

namespace mss {
namespace PseudoReferenceInternal {

constexpr long double kEqEps = 1e-9L;

inline bool pseudoContains(std::span<const ObservedBoard::CellId> cells, ObservedBoard::CellId cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

inline bool adjacent(const ObservedBoard::Result &board, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs) {
    const auto [lx, ly] = board.pos(lhs);
    const auto [rx, ry] = board.pos(rhs);
    return lhs != rhs && std::abs(lx - rx) <= 1 && std::abs(ly - ry) <= 1;
}

// Java isConfirmedMine / isMineInPe：确定雷（basic Mine 或 PE 新雷）。
inline bool isMine(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
            const Probability::Result &probability, ObservedBoard::CellId cell) {
    const auto [x, y] = board.pos(cell);
    return basic.marks[x][y] == Basic::Mark::F || probability.mineProbability(cell, board, basic, structure) >= 1.0L - kEqEps;
}

// Java Link（Java 的 witness 即"还差一雷"的约束，squares = 约束内的隐藏格）。
struct Link {
    ObservedBoard::CellId first = -1;
    ObservedBoard::CellId second = -1;
    bool closedFirst = true;
    bool closedSecond = true;
    bool deadFirst = false;
    bool deadSecond = false;
    bool processed = false;
    bool pseudo = false;
    bool unavoidable = true;     // Java 默认 true，发现"能单独提供信息"的邻格即 false
    std::vector<ObservedBoard::CellId> trouble; // 只监控到链中一格的格（会打破封闭性）
    int witnessSize = 0;         // Java Link 排序的次级键（witness squares 数）
};

// Java Chain。
struct Chain {
    std::vector<ObservedBoard::CellId> whole5050;  // 全部组成格
    std::vector<ObservedBoard::CellId> living5050; // 其中非死格
    std::vector<ObservedBoard::CellId> pseudoTiles;
    ObservedBoard::CellId openTile = -1;
    bool pseudo = false;
    std::vector<ObservedBoard::CellId> trouble;
};

// Java assessLink：检查链两端是否"封闭"（每个相邻信息格要么是链格、要么同时
// 监控两格），并标记死格。
inline void assess(Link &link, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure,
            const Probability::Result &probability, std::span<const ObservedBoard::CellId> dead) {
    for (ObservedBoard::CellId tile : {link.first, link.second}) {
        const auto [x, y] = board.pos(tile);
        forEachAdjacent(x, y, board.board.rows(), board.board.cols(), [&](int nx, int ny) {
            const ObservedBoard::CellId info = board.id(nx, ny);
            if (info == link.first || info == link.second)
                return;
            if (isMine(board, basic, structure, probability, info))
                return;
            const ObservedBoard::CellId other = tile == link.first ? link.second : link.first;
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
    link.deadFirst = pseudoContains(dead, link.first);
    link.deadSecond = pseudoContains(dead, link.second);
}

// Java findRootedLinks：大 witness（>2 格）内相邻且至少一端封闭、至少一格活的
// 相邻对 → 根化 pseudo link。
inline std::vector<Link> findRootedLinks(std::span<const ObservedBoard::CellId> cells, const ObservedBoard::Result &board, const Basic::Result &basic,
                                  const Structure::Result &structure, const Probability::Result &probability, std::span<const ObservedBoard::CellId> dead,
                                  int witnessSize) {
    std::vector<Link> links;
    // Java 只检查"正上方"与"正右方"两个相对位置（x 为列、y 为行的坐标约定）。
    constexpr std::array<std::pair<int, int>, 2> offsets{
        std::pair{-1, 0}, // 上方（C++ row-1）
        std::pair{0, 1},  // 右方（C++ col+1）
    };
    for (ObservedBoard::CellId tile1 : cells) {
        const auto [x, y] = board.pos(tile1);
        for (const auto [dx, dy] : offsets) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || nx >= board.board.rows() || ny < 0 || ny >= board.board.cols())
                continue;
            const ObservedBoard::CellId tile2 = board.id(nx, ny);
            if (!pseudoContains(cells, tile2))
                continue;
            Link link;
            link.first = tile1;
            link.second = tile2;
            link.pseudo = true;
            link.witnessSize = witnessSize;
            assess(link, board, basic, structure, probability, dead);
            if ((link.closedFirst || link.closedSecond) && // 至少一端封闭
                (!link.deadFirst || !link.deadSecond))     // 至少一格活
                links.push_back(std::move(link));
        }
    }
    return links;
}

// Java noTrouble：每个 trouble 格必须邻接偶数个链格（否则能单独影响局面）。
inline bool noTrouble(const ObservedBoard::Result &board, std::span<const ObservedBoard::CellId> trouble, std::span<const ObservedBoard::CellId> chain) {
    for (ObservedBoard::CellId cell : trouble) {
        if (pseudoContains(chain, cell))
            continue;
        int count = 0;
        for (ObservedBoard::CellId member : chain)
            if (adjacent(board, cell, member))
                ++count;
        if (count % 2 != 0)
            return false;
    }
    return true;
}

// Java preferLiving：优先返回非死格；全是死格才原样返回。
inline std::vector<ObservedBoard::CellId> preferLiving(std::vector<ObservedBoard::CellId> cells, std::span<const ObservedBoard::CellId> dead) {
    std::vector<ObservedBoard::CellId> living;
    for (ObservedBoard::CellId cell : cells)
        if (!pseudoContains(dead, cell))
            living.push_back(cell);
    return living.empty() ? std::move(cells) : living;
}

// 排序键：Java Link.compareTo——非 pseudo 在前，其次 witness squares 数。
inline bool linkLess(const Link &lhs, const Link &rhs) {
    if (lhs.pseudo != rhs.pseudo)
        return !lhs.pseudo;
    return lhs.witnessSize < rhs.witnessSize;
}

} // namespace PseudoReferenceInternal

inline std::vector<ObservedBoard::CellId> PseudoReference::findPseudo5050(const ObservedBoard::Result &board, const Basic::Result &basic,
                                                    const Structure::Result &structure, const Structure::Pool &shapes,
                                                    const Probability::Result &probability, std::span<const ObservedBoard::CellId> dead) {
    using namespace PseudoReferenceInternal;
    std::vector<Link> links;
    std::vector<std::vector<ObservedBoard::CellId>> pseudoWitnesses;

    // 遍历全部"还差一雷"的 witness（约束 sum == 1）。
    for (const Structure::InstanceId instanceId : structure.components) {
        const Structure::Instance &instance = shapes.getInstance(instanceId);
        const Structure::Shape &shape = shapes.get(instance.shape);
        for (int i = 0; i < (int)(shape.constraintCount()); ++i) {
            const Structure::Shape::ConstraintView constraint = shape.constraint(shapes, i);
            if (constraint.sum != 1)
                continue;
            std::vector<ObservedBoard::CellId> cells;
            for (Structure::BoxId box : constraint.boxIds)
                for (int k = instance.boxes.boxOf.span(shapes.boxOf)[box]; k < instance.boxes.boxOf.span(shapes.boxOf)[box + 1]; ++k)
                    cells.push_back(instance.boxes.cells.span(shapes.cells)[k]);
            if (cells.size() < 2)
                continue;
            if (cells.size() == 2) {
                Link link;
                link.first = cells[0];
                link.second = cells[1];
                link.witnessSize = 2;
                assess(link, board, basic, structure, probability, dead);
                links.push_back(std::move(link));
            } else {
                std::vector<Link> rooted = findRootedLinks(cells, board, basic, structure, probability, dead, cells.size());
                if (rooted.empty())
                    pseudoWitnesses.push_back(std::move(cells));
                links.insert(links.end(), std::make_move_iterator(rooted.begin()), std::make_move_iterator(rooted.end()));
            }
        }
    }

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
    if (unavoidable != nullptr)
        return preferLiving({unavoidable->first, unavoidable->second}, dead);

    // 链：从一个只封闭一端的 link 出发，沿开口端扩展，封口且步数为偶 + 无 trouble
    // 即成不可避免的 50/50。
    std::vector<Chain> chains;
    for (Link &start : links) {
        if (start.processed || start.closedFirst == start.closedSecond)
            continue; // 恰一端封闭
        start.processed = true;
        Chain chain;
        chain.whole5050.push_back(start.first);
        chain.whole5050.push_back(start.second);
        chain.trouble.insert(chain.trouble.end(), start.trouble.begin(), start.trouble.end());
        chain.pseudo = start.pseudo;
        chain.openTile = start.closedFirst ? start.second : start.first;
        if (!start.deadFirst)
            chain.living5050.push_back(start.first);
        if (!start.deadSecond)
            chain.living5050.push_back(start.second);
        if (start.pseudo) {
            if (!start.deadFirst)
                chain.pseudoTiles.push_back(start.first);
            if (!start.deadSecond)
                chain.pseudoTiles.push_back(start.second);
        }

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
                    if (ext.pseudo) {
                        chain.pseudo = true;
                        if (!ext.deadFirst)
                            chain.pseudoTiles.push_back(ext.first);
                        if (!ext.deadSecond)
                            chain.pseudoTiles.push_back(ext.second);
                    }
                    chain.trouble.insert(chain.trouble.end(), ext.trouble.begin(), ext.trouble.end());
                    chain.whole5050.push_back(ext.second);
                    if (!ext.deadSecond)
                        chain.living5050.push_back(ext.second);
                    if (ext.closedSecond) {
                        if (extensions % 2 == 0 && noTrouble(board, chain.trouble, chain.whole5050)) {
                            if (ext.pseudo)
                                return preferLiving({ext.first, ext.second}, dead);
                            return preferLiving(chain.living5050, dead);
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
                    if (ext.pseudo) {
                        chain.pseudo = true;
                        if (!ext.deadFirst)
                            chain.pseudoTiles.push_back(ext.first);
                        if (!ext.deadSecond)
                            chain.pseudoTiles.push_back(ext.second);
                    }
                    chain.trouble.insert(chain.trouble.end(), ext.trouble.begin(), ext.trouble.end());
                    chain.whole5050.push_back(ext.first);
                    if (!ext.deadFirst)
                        chain.living5050.push_back(ext.first);
                    if (ext.closedFirst) {
                        if (extensions % 2 == 0 && noTrouble(board, chain.trouble, chain.whole5050)) {
                            if (ext.pseudo)
                                return preferLiving({ext.first, ext.second}, dead);
                            return preferLiving(chain.living5050, dead);
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

    // 用 pseudo witness 连接两条链成不可避免的 50/50。
    for (const std::vector<ObservedBoard::CellId> &witness : pseudoWitnesses) {
        Chain *chain1 = nullptr;
        Chain *chain2 = nullptr;
        long double tally1 = 0.0L;
        long double tally2 = 0.0L;
        bool found = false;
        for (ObservedBoard::CellId tile : witness) {
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

        Link linker;
        linker.first = chain1->openTile;
        linker.second = chain2->openTile;
        assess(linker, board, basic, structure, probability, dead);

        std::vector<ObservedBoard::CellId> combined = chain1->whole5050;
        combined.insert(combined.end(), chain2->whole5050.begin(), chain2->whole5050.end());
        std::vector<ObservedBoard::CellId> trouble = linker.trouble;
        trouble.insert(trouble.end(), chain1->trouble.begin(), chain1->trouble.end());
        trouble.insert(trouble.end(), chain2->trouble.begin(), chain2->trouble.end());
        if (combined.size() % 2 != 0 || !noTrouble(board, trouble, combined))
            continue;

        // 两条链都是 pseudo 时，组合链里不能有比最弱点更安全的格（否则不成立）。
        if (chain1->pseudo && chain2->pseudo) {
            const long double lowest = (std::min)(tally1, tally2);
            bool broken = false;
            for (ObservedBoard::CellId tile : combined) {
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
            return preferLiving({chain1->openTile, chain2->openTile}, dead);
        return preferLiving({c < 0 ? chain1->openTile : chain2->openTile}, dead);
    }
    return {};
}

} // namespace mss
