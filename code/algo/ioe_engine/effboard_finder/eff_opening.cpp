#include "algo/ioe_engine/effboard_finder/eff_opening.h"

#include <algorithm>
#include <bit>
#include <cstring>

#include "algo/ioe_engine/dom/dom.h"
#include "core/algorithm/neighbors.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct EffOpening::Cell {
    std::int16_t zeroRegion, previousRegion;
    std::int8_t digit, adjacentRegions, score;
    bool independent;
};

struct EffOpening::Workspace {
    vector<Cell> cells;
    vector<std::uint8_t> priority, upper;
    vector<char> revealed;
    vectorPool<int> zeros, boundary;
    vector<vectorPool<int>::vector> regions;
};

__declspec(noinline) EffOpening::Workspace &EffOpening::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void EffOpening::prepare(int rows, int cols, Geometry &geometry) {
    const int count = rows * cols;
    vector<Geometry::Neighbors> &adjacent = geometry.adjacent;
    adjacent.resize(count);
    geometry.rank.resize(8 * count);
    geometry.byRank.resize(8 * count);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int index = row * cols + col;
            Geometry::Neighbors &neighbors = adjacent[index];
            neighbors.count = 0;
            forEachAdjacent(row, col, rows, cols, [&](int r, int c) { neighbors.cells[neighbors.count++] = r * cols + c; });
            for (int direction = 0; direction < 8; ++direction) {
                const int r = direction & 1 ? rows - 1 - row : row;
                const int c = direction & 2 ? cols - 1 - col : col;
                const int rank = direction & 4 ? c * rows + r : r * cols + c;
                geometry.rank[direction * count + index] = rank;
                geometry.byRank[direction * count + rank] = index;
            }
        }
    }
}

int EffOpening::build(const Grid<char> &mines, const Geometry &geometry, vector<Cell> &cells, vectorPool<int> &zeros,
                      vectorPool<int> &boundary, vector<vectorPool<int>::vector> &regions, vectorPool<int>::vector &largest) {
    const int count = geometry.adjacent.size;
    const vector<Geometry::Neighbors> &adjacent = geometry.adjacent;
    cells.resize(count);
    std::fill_n(cells.data, count, Cell{-1, -1, 0, 0, 0, false});
    regions.size = zeros.data.size = boundary.data.size = 0;
    const char *mine = mines[0];
    for (int index = 0; index < count; ++index) {
        if (!mine[index]) continue;
        // 此后的邻雷计数最多加八次，雷格的数字始终为负。
        cells[index].digit = -9;
        const Geometry::Neighbors &near = adjacent[index];
        for (int j = 0; j < near.count; ++j) ++cells[near.cells[j]].digit;
    }

    largest = {0, 0};
    for (int index = 0; index < count; ++index) {
        if (cells[index].digit || cells[index].zeroRegion >= 0) continue;
        const int id = regions.size;
        vectorPool<int>::vector region = zeros.push_back();
        vectorPool<int>::vector edge = boundary.push_back();
        zeros.push_back(region, index);
        cells[index].zeroRegion = id;
        for (std::size_t i = 0; i < region.count; ++i) {
            const Geometry::Neighbors &near = adjacent[zeros.data[region.offset + i]];
            for (int j = 0; j < near.count; ++j) {
                const int next = near.cells[j];
                Cell &cell = cells[next];
                if (!cell.digit && cell.zeroRegion < 0) {
                    zeros.push_back(region, next);
                    cell.zeroRegion = id;
                } else if (cell.digit > 0 && cell.previousRegion != id) {
                    cell.previousRegion = id;
                    ++cell.adjacentRegions;
                    boundary.push_back(edge, next);
                }
            }
        }
        regions.push_back(edge);
        if (region.count > largest.count) largest = region;
    }
    if (!regions.size) return 0;

    int threeBV = regions.size;
    for (int i = 0; i < count; ++i) {
        Cell &cell = cells[i];
        if (cell.digit <= 0) continue;
        cell.independent = cell.adjacentRegions == 0;
        threeBV += cell.independent;
        cell.score += std::max(0, cell.adjacentRegions - 1) - 1 - cell.digit;
        if (!cell.independent) continue;
        const Geometry::Neighbors &near = adjacent[i];
        for (int j = 0; j < near.count; ++j) ++cells[near.cells[j]].score;
    }

    return threeBV;
}

int EffOpening::highest(const std::uint8_t *values, int size) {
    std::uint8_t best = 0;
    for (int i = 0; i < size; ++i) best = std::max(best, values[i]);
    return best;
}

int EffOpening::firstAtLeast(const std::uint8_t *data, int size, int score) {
    constexpr std::uint64_t high = 0x8080808080808080ULL;
    const std::uint64_t threshold = 0x0101010101010101ULL * score;
    for (int i = 0; i < size; i += 8) {
        std::uint64_t packed;
        std::memcpy(&packed, data + i, sizeof(packed));
        // 各字节的高位单独承接借位，保留下来的高位对应达到分数的格。
        const std::uint64_t matches = ((packed | high) - threshold) & high;
        if (matches) return i + (std::endian::native == std::endian::little ? std::countr_zero(matches) : std::countl_zero(matches)) / 8;
    }
    return size;
}

template <bool first>
void EffOpening::change(int index, int delta, const std::uint16_t *rank, vector<std::uint8_t> &priority, vector<std::uint8_t> &upper) {
    constexpr int blockSize = 128;
    const int order = first ? index : rank[index];
    std::uint8_t &value = priority[order];
    value += delta;
    if (delta > 0) upper[order / blockSize] = std::max(upper[order / blockSize], value);
}

template <bool first>
void EffOpening::reveal(int index, const Geometry &geometry, const vector<Cell> &cells, const vectorPool<int> &boundary,
                        const vector<vectorPool<int>::vector> &regions, const std::uint16_t *rank,
                        vector<std::uint8_t> &priority, vector<std::uint8_t> &upper, vector<char> &revealed, int &remaining) {
    const int count = geometry.adjacent.size;
    const vector<Geometry::Neighbors> &adjacent = geometry.adjacent;
    const Cell &cell = cells[index];
    char &seen = revealed[cell.digit ? index : count + cell.zeroRegion];
    if (seen) return;
    seen = 1;
    if (cell.digit > 0) {
        if (cell.independent) {
            --remaining;
            const Geometry::Neighbors &near = adjacent[index];
            for (int j = 0; j < near.count; ++j) change<first>(near.cells[j], -1, rank, priority, upper);
        } else change<first>(index, 1, rank, priority, upper);
        return;
    }
    --remaining;
    for (int edge : boundary.span(regions[cell.zeroRegion])) {
        if (revealed[edge]) change<first>(edge, -1, rank, priority, upper);
        revealed[edge] = 1;
    }
}

template <bool first>
int EffOpening::simulate(const Geometry &geometry, const vector<Cell> &cells, const vectorPool<int> &boundary,
                          const vector<vectorPool<int>::vector> &regions, int threeBV, int direction,
                          vector<std::uint8_t> &priority, vector<std::uint8_t> &upper, vector<char> &revealed) {
    const int count = geometry.adjacent.size;
    const vector<Geometry::Neighbors> &adjacent = geometry.adjacent;
    constexpr int blockSize = 128;
    const int blocks = (count + blockSize - 1) / blockSize;
    const int paddedBlocks = (blocks + 15) & ~15;
    const std::uint16_t *rank = geometry.rank.data + direction * count;
    const std::uint16_t *ordered = geometry.byRank.data + direction * count;
    std::fill_n(revealed.data, revealed.size, 0);
    int clicks = 0, remaining = threeBV;
    // 可选分数从 64 起。停用格设为 32，后续最多增加九分。
    constexpr int bias = 64, inactive = 32;
    for (int order = 0; order < count; ++order) {
        const Cell &cell = cells[first ? order : ordered[order]];
        priority[order] = cell.digit > 0 ? bias + cell.score : inactive;
    }
    for (int block = 0; block < blocks; ++block) upper[block] = highest(priority.data + block * blockSize, blockSize);
    for (;;) {
        const int best = highest(upper.data, paddedBlocks);
        if (best < bias) break;
        const int block = firstAtLeast(upper.data, paddedBlocks, best);
        const std::uint8_t *values = priority.data + block * blockSize;
        const int offset = firstAtLeast(values, blockSize, best);
        // 这一段的最高分已经下降，更新上界后重新按方向次序选格。
        if (offset == blockSize) {
            upper[block] = highest(values, blockSize);
            continue;
        }
        const int order = block * blockSize + offset;
        const int index = first ? order : ordered[order];
        priority[order] = inactive;
        if (!revealed[index]) {
            ++clicks;
            reveal<first>(index, geometry, cells, boundary, regions, rank, priority, upper, revealed, remaining);
        }
        const Geometry::Neighbors &near = adjacent[index];
        for (int j = 0; j < near.count; ++j) {
            const int flag = near.cells[j];
            if (cells[flag].digit >= 0 || revealed[flag]) continue;
            revealed[flag] = 1;
            ++clicks;
            const Geometry::Neighbors &around = adjacent[flag];
            for (int k = 0; k < around.count; ++k) change<first>(around.cells[k], 1, rank, priority, upper);
        }
        ++clicks;
        for (int j = 0; j < near.count; ++j)
            if (cells[near.cells[j]].digit >= 0) reveal<first>(near.cells[j], geometry, cells, boundary, regions, rank, priority, upper, revealed, remaining);
    }
    return clicks + remaining;
}

EffOpening::Result EffOpening::select(const Grid<char> &mines, const Geometry &geometry, const Config &config, vector<int> &opening) {
    const int count = geometry.adjacent.size;
    const double target = config.target;
    const int extraClicks = config.extraClicks;
    Workspace &workspace = getWorkspace();
    vector<Cell> &cells = workspace.cells;
    vector<vectorPool<int>::vector> &regions = workspace.regions;
    vectorPool<int> &zeros = workspace.zeros, &boundary = workspace.boundary;
    vectorPool<int>::vector largest;
    opening.size = 0;
    const int threeBV = build(mines, geometry, cells, zeros, boundary, regions, largest);
    if (!threeBV) return {false, false};

    vector<std::uint8_t> &priority = workspace.priority, &upper = workspace.upper;
    vector<char> &revealed = workspace.revealed;
    constexpr int blockSize = 128;
    const int blocks = (count + blockSize - 1) / blockSize;
    const int paddedBlocks = (blocks + 15) & ~15;
    upper.resize(paddedBlocks);
    priority.resize(blocks * blockSize);
    std::fill(upper.data + blocks, upper.data + paddedBlocks, 0);
    std::fill(priority.data + count, priority.data + priority.size, 0);
    // 前段按格号记录揭开或插旗，后段按零区编号记录展开。
    revealed.resize(count + regions.size);

    const double scaled = 100.0 * threeBV;
    int clicks = simulate<true>(geometry, cells, boundary, regions, threeBV, 0, priority, upper, revealed);
    const int ceiling = (int)std::min<double>(count, scaled / target) + extraClicks;
    if (extraClicks) {
        const int adjusted = 115 * clicks - 15 * threeBV - 200;
        if (clicks > ceiling && (adjusted < 0 || adjusted > 100 * ceiling)) return {false, false};
    } else if (scaled < target * clicks && scaled / (threeBV - 1.15 * (threeBV - clicks) - 2) < target) return {false, false};

    for (int direction = 1; direction < 8 && scaled < target * clicks; ++direction) clicks = std::min(clicks, simulate<false>(geometry, cells, boundary, regions, threeBV, direction, priority, upper, revealed));
    bool checked = false;
    if (scaled < target * clicks && extraClicks && clicks <= ceiling) {
        checked = true;
        clicks = IoeDom().minimum(mines, (int)std::min<double>(count, scaled / target));
    }
    if (clicks < 0 || scaled < target * clicks) return {false, checked};
    opening.resize(largest.count);
    std::copy_n(zeros.data.data + largest.offset, largest.count, opening.data);
    return {true, checked};
}

} // 命名空间 mss
