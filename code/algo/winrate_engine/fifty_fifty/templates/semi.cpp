#include "algo/winrate_engine/fifty_fifty/templates/semi.h"

#include <bit>
#include <cmath>

#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "core/algorithm/neighbors.h"

namespace mss {

struct TemplateSemi::Facts {
    struct Constraint { int cells[8]; int count, sum; };
    struct Adjacent { int indices[8]; int count; };
    // 第一维为推导出的格号，每个位对应一个假设为雷的候选。
    Grid<std::uint64_t> mines, safes;
    vector<Basic::Mark> marks;
    vector<Constraint> constraints;
    vector<Adjacent> adjacent;
    vector<std::uint64_t> active;
    int rows, cols, words;
};

struct TemplateSemi::Workspace {
    Facts facts;
    vector<std::uint64_t> found;
    vector<int> pending;
    vector<unsigned char> queued;
};

__declspec(noinline) TemplateSemi::Workspace &TemplateSemi::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void TemplateSemi::propagate(Facts &facts, vector<int> &pending, vector<unsigned char> &queued) {
    pending.resize(0);
    queued.resize(facts.constraints.size);
    for (int i = 0; i < (int)facts.constraints.size; ++i) {
        pending.push_back(i);
        queued[i] = 1;
    }
    for (int head = 0; head < (int)pending.size; ++head) {
        const int index = pending[head];
        queued[index] = 0;
        const Facts::Constraint &constraint = facts.constraints[index];
        for (int word = 0; word < facts.words; ++word) {
            if (!facts.active[word]) continue;
            std::uint64_t mineCount[4] = {}, safeCount[4] = {};
            for (int i = 0; i < constraint.count; ++i) {
                const int id = constraint.cells[i];
                std::uint64_t mines = facts.mines[id][word], safes = facts.safes[id][word];
                for (int bit = 0; bit < 4; ++bit) {
                    const std::uint64_t nextMines = mineCount[bit] & mines, nextSafes = safeCount[bit] & safes;
                    mineCount[bit] ^= mines;
                    safeCount[bit] ^= safes;
                    mines = nextMines;
                    safes = nextSafes;
                }
            }
            std::uint64_t forceSafe = facts.active[word], forceMine = facts.active[word];
            for (int bit = 0; bit < 4; ++bit) {
                forceSafe &= constraint.sum & (1 << bit) ? mineCount[bit] : ~mineCount[bit];
                forceMine &= (constraint.count - constraint.sum) & (1 << bit) ? safeCount[bit] : ~safeCount[bit];
            }
            for (int i = 0; i < constraint.count; ++i) {
                const int id = constraint.cells[i];
                const std::uint64_t unknown = ~(facts.mines[id][word] | facts.safes[id][word]);
                const std::uint64_t mines = forceMine & unknown, safes = forceSafe & unknown;
                if (!(mines | safes)) continue;
                facts.mines[id][word] |= mines;
                facts.safes[id][word] |= safes;
                const Facts::Adjacent &adjacent = facts.adjacent[id];
                for (int j = 0; j < adjacent.count; ++j) {
                    const int neighbor = adjacent.indices[j];
                    if (!queued[neighbor]) {
                        queued[neighbor] = 1;
                        pending.push_back(neighbor);
                    }
                }
            }
        }
    }
}

bool TemplateSemi::matches(const FiftyFiftyData::Pool &templates, const FiftyFiftyData::Rooted &rooted,
                           const Facts &facts, ObservedBoard::CellId root) {
    using Point = FiftyFiftyData::Point;
    const FiftyFiftyData::Pattern &pattern = templates.patterns[rooted.pattern];
    const decltype(templates.points.span(pattern.points)) points = templates.points.span(pattern.points);
    const Point anchor = Point{root / facts.cols, root % facts.cols} - points[rooted.slot];
    const int word = root / 64, coreCount = 2 * pattern.half;
    const std::uint64_t bit = 1ULL << (root % 64);
    for (int i = 0; i < coreCount; ++i) {
        if (i == rooted.slot || i == (rooted.slot < pattern.half ? pattern.half : 0)) continue;
        const int id = points[i].cell(anchor, facts.rows, facts.cols);
        if (id < 0 || facts.marks[id] != Basic::Mark::H) return false;
        const bool mine = (i < pattern.half) == (rooted.slot < pattern.half);
        if (!((mine ? facts.mines : facts.safes)[id][word] & bit)) return false;
    }
    for (int i = coreCount; i < (int)pattern.points.count; ++i) {
        const int id = points[i].cell(anchor, facts.rows, facts.cols);
        if (id >= 0 && facts.marks[id] != Basic::Mark::F && !(facts.marks[id] == Basic::Mark::H && (facts.mines[id][word] & bit)))
            return false;
    }
    return true;
}

FiftyFiftyKind TemplateSemi::detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position,
                                const Position::ProbabilityResult &probability, const Probability::Pool &probabilities,
                                const FiftyFiftyData::Pool &templates) {
    Workspace &workspace = getWorkspace();
    Facts &facts = workspace.facts;
    vector<std::uint64_t> &found = workspace.found;
    const ObservedBoard::Result &board = position.board;
    facts.rows = board.cells.rows();
    facts.cols = board.cells.cols();
    const int size = facts.rows * facts.cols;
    facts.words = (size + 63) / 64;
    facts.marks.resize(size);
    facts.adjacent.resize(size);
    facts.constraints.resize(0);
    facts.mines.resize(size, facts.words, 0);
    facts.safes.resize(size, facts.words, 0);
    facts.active.resize(facts.words);
    found.resize(facts.words);
    std::fill_n(facts.active.data, facts.words, 0);
    std::fill_n(found.data, facts.words, 0);
    for (int id = 0; id < size; ++id) {
        const long double p = Probability().mineProbability(probability.boxes, probabilities, id, board, position.basic, position.structure);
        facts.marks[id] = p == 1.0L ? Basic::Mark::F : p == 0.0L ? Basic::Mark::S : position.basic.marks[id / facts.cols][id % facts.cols];
        facts.adjacent[id].count = 0;
        if (facts.marks[id] == Basic::Mark::H) {
            facts.mines[id][id / 64] = 1ULL << (id % 64);
            facts.active[id / 64] |= facts.mines[id][id / 64];
        }
    }
    for (int row = 0; row < facts.rows; ++row) for (int col = 0; col < facts.cols; ++col) {
        if (!ObservedBoard().isNumber(board.cells[row][col])) continue;
        Facts::Constraint constraint{};
        constraint.sum = ObservedBoard().numberValue(board.cells[row][col]);
        forEachAdjacent(row, col, facts.rows, facts.cols, [&](int r, int c) {
            const int id = board.id(r, c);
            if (facts.marks[id] == Basic::Mark::F) --constraint.sum;
            if (facts.marks[id] == Basic::Mark::H) constraint.cells[constraint.count++] = id;
        });
        if (constraint.count == 0) continue;
        for (int i = 0; i < constraint.count; ++i) {
            Facts::Adjacent &adjacent = facts.adjacent[constraint.cells[i]];
            adjacent.indices[adjacent.count++] = facts.constraints.size;
        }
        facts.constraints.push_back(constraint);
    }
    propagate(facts, workspace.pending, workspace.queued);

    const int radius = templates.starts.rows() / 2;
    for (int id = 0; id < size; ++id) if (facts.marks[id] == Basic::Mark::H)
        for (int word = 0; word < facts.words; ++word)
            for (std::uint64_t bits = facts.safes[id][word] & ~found[word]; bits; bits &= bits - 1) {
                const int root = word * 64 + std::countr_zero(bits);
                const int row = id / facts.cols - root / facts.cols, col = id % facts.cols - root % facts.cols;
                if (row < -radius || row > radius || col < -radius || col > radius) continue;
                const vectorPool<FiftyFiftyData::Rooted>::vector range = templates.starts[row + radius][col + radius];
                for (const FiftyFiftyData::Rooted &rooted : templates.roots.span(range)) if (matches(templates, rooted, facts, root)) {
                    found[word] |= 1ULL << (root % 64);
                    break;
                }
            }
    cells.resize(0);
    FiftyFiftyKind kind = FiftyFiftyKind::None;
    for (int root = 0; root < size; ++root) if (found[root / 64] & (1ULL << (root % 64))) {
        const long double p = Probability().mineProbability(probability.boxes, probabilities, root, board, position.basic, position.structure);
        const bool forced = std::abs(p - 0.5L) <= 1e-9L;
        if (forced && kind != FiftyFiftyKind::Forced) {
            cells.resize(0);
            kind = FiftyFiftyKind::Forced;
        }
        if (kind != FiftyFiftyKind::Forced) kind = FiftyFiftyKind::Semi;
        if (forced || kind == FiftyFiftyKind::Semi) cells.push_back(root);
    }
    return kind;
}

} // 命名空间 mss
