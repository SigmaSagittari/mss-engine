#include "algo/ioe_engine/eight_way/eight_way.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

#include "algo/ioe_engine/ref_dom/ref_dom.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct EightWay::NeighborOffsets {
    int value[8];

    NeighborOffsets(int rowStep, int colStep)
        : value{-rowStep - colStep, -rowStep, -rowStep + colStep, -colStep, colStep,
                rowStep - colStep, rowStep, rowStep + colStep} {}

    int operator[](int bit) const { return value[bit]; }
};

struct EightWay::Prepared {
    // 危险：为复用存储，邻格掩码跳过逐格初始化。prepare() 先写完前 rows * cols 项，再读取它们。
    struct Neighbors {
        std::uint8_t valid;
        std::uint8_t mines;
    };

    vector<Neighbors> neighbors;
    vector<std::uint8_t> numbers;
    vector<bool> isolated;
    vector<int> priority;
    vector<int> openingId;
    vector<vectorPool<int>::vector> openings;
    vectorPool<int> openingCells;
};

struct EightWay::Order {
    enum { ReverseRows = 1, ReverseCols = 2, Transpose = 4 };

    int rows, cols;
    int offset, rowStep, colStep;
    NeighborOffsets rankOffsets;
    int firstCell, majorCellStep, minorCellStep;
    int groupSize;
    int groupBits; // 边长为二次幂时存位移位数，否则为 -1，改用除法。
    bool straight;

    Order(int rows, int cols, int direction)
        : rows(rows), cols(cols),
          offset((direction & ReverseRows ? rows - 1 : 0) * (direction & Transpose ? 1 : cols) +
                 (direction & ReverseCols ? cols - 1 : 0) * (direction & Transpose ? rows : 1)),
          rowStep((direction & ReverseRows ? -1 : 1) * (direction & Transpose ? 1 : cols)),
          colStep((direction & ReverseCols ? -1 : 1) * (direction & Transpose ? rows : 1)), rankOffsets(rowStep, colStep),
          firstCell((direction & ReverseRows ? rows - 1 : 0) * cols + (direction & ReverseCols ? cols - 1 : 0)),
          majorCellStep(direction & Transpose ? (direction & ReverseCols ? -1 : 1) : (direction & ReverseRows ? -cols : cols)),
          minorCellStep(direction & Transpose ? (direction & ReverseRows ? -cols : cols) : (direction & ReverseCols ? -1 : 1)),
          groupSize(direction & Transpose ? rows : cols), groupBits(std::has_single_bit((unsigned)groupSize) ? std::countr_zero((unsigned)groupSize) : -1),
          straight(direction == 0 || direction == (ReverseRows | ReverseCols)) {}

    int rank(int row, int col) const { return offset + row * rowStep + col * colStep; }

    int cellAtRank(int rank) const {
        if (straight) return firstCell + rank * minorCellStep;
        // 名次的商、余数各对应一个坐标轴，步长已经包含翻转和转置。
        if (groupBits >= 0)
            return firstCell + (rank >> groupBits) * majorCellStep + (rank & (groupSize - 1)) * minorCellStep;
        const int major = rank / groupSize;
        const int minor = rank - major * groupSize;
        return firstCell + major * majorCellStep + minor * minorCellStep;
    }
};

struct EightWay::Queue {
    enum { levels = 10 };
    vector<std::uint64_t> bits;
    int firstWord[levels];
    int words;
    int maximum;

    Queue() : firstWord{}, words(0), maximum(-1) {}

    void clear(int size) {
        words = (size - 1) / 64 + 1;
        bits.resize(words * levels);
        std::fill_n(bits.data, bits.size, std::uint64_t{0});
        std::fill_n(firstWord, levels, words);
        maximum = -1;
    }

    void insert(int rank, int priority) {
        const int word = rank / 64;
        bits[priority * words + word] |= std::uint64_t{1} << (rank % 64);
        firstWord[priority] = std::min(firstWord[priority], word);
        maximum = std::max(maximum, priority);
    }

    // 同一优先级先取当前遍历顺序最靠前的格。队列空时返回 -1。
    int pop() {
        while (maximum >= 0) {
            int &word = firstWord[maximum];
            while (word < words) {
                std::uint64_t &value = bits[maximum * words + word];
                if (value) {
                    const int rank = word * 64 + std::countr_zero(value);
                    value &= value - 1;
                    return rank;
                }
                ++word;
            }
            --maximum;
        }
        return -1;
    }
};

struct EightWay::Workspace {
    Prepared board;
    struct {
        vector<Cell> cells;
        vector<int> priority;
        Queue queue;
    } simulation;
};

__declspec(noinline) EightWay::Workspace &EightWay::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

int EightWay::prepare(const Grid<char> &mines, const NeighborOffsets &offsets, Prepared &board) {
    const int rows = mines.rows();
    const int cols = mines.cols();
    const int size = rows * cols;
    const char *mine = mines[0];
    vector<Prepared::Neighbors> &neighbors = board.neighbors;
    neighbors.resize(size);
    vector<std::uint8_t> &numbers = board.numbers;
    vector<bool> &isolated = board.isolated;
    vector<int> &priority = board.priority;
    vector<int> &openingId = board.openingId;
    vector<vectorPool<int>::vector> &openings = board.openings;
    vectorPool<int> &openingCells = board.openingCells;
    numbers.resize(size);
    isolated.resize(size);
    priority.resize(size);
    openingId.resize(size);
    std::fill_n(numbers.data, size, 0);
    std::fill_n(isolated.data, size, true);
    std::fill_n(priority.data, size, 0);
    std::fill_n(openingId.data, size, -1); // 尚未归入零区。
    openings.size = 0;
    openingCells.data.size = 0;

    // 八位按左上到右下排列。边界掩码只保留盘面内的邻格。
    for (int row = 0; row < rows; ++row) {
        std::uint8_t rowMask = 0xff;
        if (row == 0) rowMask &= 0xf8;
        if (row + 1 == rows) rowMask &= 0x1f;
        for (int col = 0; col < cols; ++col) {
            std::uint8_t mask = rowMask;
            if (col == 0) mask &= 0xd6;
            if (col + 1 == cols) mask &= 0x6b;
            neighbors[row * cols + col] = {mask, 0};
        }
    }
    for (int cell = 0; cell < size; ++cell) {
        if (!mine[cell]) continue;
        isolated[cell] = false;
        for (unsigned bits = neighbors[cell].valid; bits; bits &= bits - 1) {
            const int bit = std::countr_zero(bits);
            const int next = cell + offsets[bit];
            ++numbers[next];
            // 一条邻接边的反向位置是 7 - bit。
            neighbors[next].mines |= 1u << (7 - bit);
        }
    }

    for (int first = 0; first < size; ++first) {
        if (mine[first] || numbers[first] || openingId[first] >= 0) continue;
        const int id = openings.size;
        vectorPool<int>::vector part = openingCells.push_back();
        openingId[first] = id;
        openingCells.push_back(part, first);
        for (int index = 0; index < (int)part.count; ++index) {
            const int cell = openingCells.data[part.offset + index];
            // 同一个数字格每多接壤一个空白区，chord 就能多省一次单独点开的点击。
            if (numbers[cell] && !isolated[cell]) ++priority[cell];
            isolated[cell] = false;
            if (numbers[cell]) continue;
            for (unsigned bits = neighbors[cell].valid; bits; bits &= bits - 1) {
                const int next = cell + offsets[std::countr_zero(bits)];
                // 数字边缘可属于多个空白区。只有零格的编号会在展开时用于定位空白区。
                if (openingId[next] == id) continue;
                openingId[next] = id;
                openingCells.push_back(part, next);
            }
        }
        openings.push_back(part);
    }

    int threeBV = openings.size;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const int cell = row * cols + col;
            if (mine[cell]) {
                priority[cell] = disabledPriority;
                for (unsigned bits = neighbors[cell].valid; bits; bits &= bits - 1)
                    --priority[cell + offsets[std::countr_zero(bits)]];
            } else if (!numbers[cell]) {
                priority[cell] = disabledPriority;
            } else {
                --priority[cell];
                if (isolated[cell]) {
                    ++threeBV;
                    for (unsigned bits = neighbors[cell].valid; bits; bits &= bits - 1)
                        ++priority[cell + offsets[std::countr_zero(bits)]];
                }
            }
        }
    return threeBV;
}

void EightWay::open(int cell, int rank, const Prepared &board, const NeighborOffsets &offsets,
                    vector<Cell> &cells, vector<int> &priority, Queue &queue, int &remainingThreeBV) {
    if (cells[cell] == Cell::Open) return;
    cells[cell] = Cell::Open;
    if (board.numbers[cell] == 0) {
        --remainingThreeBV;
        const vectorPool<int>::vector part = board.openings[board.openingId[cell]];
        for (int next : board.openingCells.span(part)) {
            if (board.numbers[next] == 0) priority[next] = disabledPriority;
            else if (cells[next] == Cell::Open) --priority[next];
            cells[next] = Cell::Open;
        }
    } else if (board.isolated[cell]) {
        --remainingThreeBV;
        for (unsigned bits = board.neighbors[cell].valid; bits; bits &= bits - 1)
            --priority[cell + offsets[std::countr_zero(bits)]];
    } else {
        const int next = ++priority[cell];
        if (next >= 0) queue.insert(rank, next);
    }
}

int EightWay::simulate(const Prepared &board, const NeighborOffsets &offsets, const Order &order, int threeBV) {
    const int cols = order.cols;
    const int rows = order.rows;
    const int size = rows * cols;
    const vector<Prepared::Neighbors> &neighbors = board.neighbors;
    const NeighborOffsets &rankOffsets = order.rankOffsets;
    Workspace &workspace = getWorkspace();
    vector<Cell> &cells = workspace.simulation.cells;
    vector<int> &priority = workspace.simulation.priority;
    Queue &queue = workspace.simulation.queue;

    cells.resize(size);
    std::fill_n(cells.data, size, Cell::Hidden);
    priority = board.priority;
    int remainingThreeBV = threeBV;
    queue.clear(size);
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const int level = priority[row * cols + col];
            if (level >= 0) queue.insert(order.rank(row, col), level);
        }

    int clicks = 0;
    for (int rank = queue.pop(); rank >= 0; rank = queue.pop()) {
        const int cell = order.cellAtRank(rank);
        const int current = priority[cell];
        if (current != queue.maximum) {
            if (current >= 0) queue.insert(rank, current);
            continue;
        }
        if (cells[cell] == Cell::Hidden) {
            open(cell, rank, board, offsets, cells, priority, queue, remainingThreeBV);
            ++clicks;
        }

        for (unsigned bits = neighbors[cell].mines; bits; bits &= bits - 1) {
            const int bit = std::countr_zero(bits);
            const int next = cell + offsets[bit];
            if (cells[next] == Cell::Flag) continue;
            cells[next] = Cell::Flag;
            ++clicks;
            const int nextRank = rank + rankOffsets[bit];
            for (unsigned adjacent = neighbors[next].valid; adjacent; adjacent &= adjacent - 1) {
                const int neighborBit = std::countr_zero(adjacent);
                const int level = ++priority[next + offsets[neighborBit]];
                if (level >= 0) queue.insert(nextRank + rankOffsets[neighborBit], level);
            }
        }
        // 同一个相对位移可以同时换算成格号和当前方向的遍历名次。
        for (unsigned bits = neighbors[cell].valid & ~neighbors[cell].mines; bits; bits &= bits - 1) {
            const int bit = std::countr_zero(bits);
            open(cell + offsets[bit], rank + rankOffsets[bit], board, offsets, cells, priority, queue, remainingThreeBV);
        }
        priority[cell] = disabledPriority;
        ++clicks;
    }

    // 每个尚未打开的独立数字格或空白区各补点一次。
    return clicks + remainingThreeBV;
}

void EightWay::maximumOpening(const Prepared &board, vector<int> &opening) {
    const vectorPool<int> &cells = board.openingCells;
    const vectorPool<int>::vector *largest = &board.openings[0];
    int largestCount = 0;
    // 零区切片也含边缘数字格。按零格数比较，同大小取第一个。
    for (std::size_t index = 0; index < board.openings.size; ++index) {
        const vectorPool<int>::vector &part = board.openings[index];
        int count = 0;
        for (int cell : cells.span(part))
            count += board.numbers[cell] == 0;
        if (count > largestCount) {
            largest = &part;
            largestCount = count;
        }
    }
    opening.size = 0;
    for (int cell : cells.span(*largest))
        if (board.numbers[cell] == 0) opening.push_back(cell);
}

EightWay::Filter EightWay::filterOpening(const Grid<char> &mines, double target, int domExtraClicks, vector<int> &opening) {
    const int rows = mines.rows(), cols = mines.cols();
    const NeighborOffsets offsets(cols, 1);
    Workspace &workspace = getWorkspace();
    Prepared &board = workspace.board;
    const int threeBV = prepare(mines, offsets, board);
    if (board.openings.size == 0) return {false, false};
    int best = simulate(board, offsets, Order(rows, cols, 0), threeBV);
    const double domBudget = domExtraClicks ? std::floor(100.0 * threeBV / target) + domExtraClicks : 0;
    if (domExtraClicks) {
        const int estimated = 115 * best - 15 * threeBV - 200;
        if (best > domBudget && (estimated < 0 || estimated > 100 * domBudget))
            return {false, false};
    } else {
        const double estimated = threeBV - 1.15 * (threeBV - best) - 2;
        if (100.0 * threeBV < target * best && 100.0 * threeBV / estimated < target)
            return {false, false};
    }
    for (int direction = 1; direction < 8; ++direction)
        best = std::min(best, simulate(board, offsets, Order(rows, cols, direction), threeBV));

    const bool needsDom = 100.0 * threeBV < target * best;
    if (needsDom) {
        if (!domExtraClicks || best > domBudget) return {false, false};
        const int exactClicks = ioe::ref_dom::RefDom().remainingClicks(mines, {}, {});
        if (100.0 * threeBV < target * exactClicks) return {false, true};
    }
    maximumOpening(board, opening);
    return {true, needsDom};
}

} // 命名空间 mss
