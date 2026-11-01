#include "algo/probability_engine/probability/observe.h"

#include <algorithm>
#include <utility>

#include "core/algorithm/combination.h"
#include "core/algorithm/neighbors.h"
#include "core/algorithm/poly.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector.h"

namespace mss {

struct Observe::Workspace {
    struct Components {
        vector<Structure::ComponentId> captured;
        vector<char> seen;
        vector<int> adjacentBoxCells;
        // 每个被捕获组件复用同一份观测结果；下一次 observe 会覆盖它。
        ShapeSolver::Observed observed;
    } components;
    struct Convolution {
        // dp[邻居雷数][已处理组件与邻居 T 格雷数]；两张表交替累积。
        Grid<long double> dp;
        Grid<long double> nextDp;
    } convolution;
    struct Rest {
        // ways 按绝对剩余雷数索引；其余是未捕获组件的多项式卷积暂存。
        vector<long double> ways;
        vector<long double> coefficients;
        vector<long double> scratch;
    } rest;
};

__declspec(noinline) Observe::Workspace &Observe::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

// 两个多项式卷积到调用方缓冲区；输出不与输入重叠。
void Observe::multiplyPoly(const Poly &left, const Poly &right, vector<long double> &output) {
    output.resize(0);
    if (left.size() == 0 || right.size() == 0)
        return;
    const int size = left.size() + right.size() - 1;
    output.resize(size);
    std::fill(output.data, output.data + size, 0.0L);
    const long double *leftData = left.data();
    const long double *rightData = right.data();
    for (int i = 0; i < left.size(); ++i)
        for (int j = 0; j < right.size(); ++j)
            output.data[i + j] += leftData[i] * rightData[j];
}

// 交换两个系数数组的存储，避免整份复制。
void Observe::swapValues(vector<long double> &left, vector<long double> &right) {
    std::swap(left.data, right.data);
    std::swap(left.size, right.size);
    std::swap(left.capacity, right.capacity);
}

long double Observe::choose(int n, int k, Combination::Pool &combinations) {
    if (n <= 9)
        return Combination().chooseSmall(n, k);
    return Combination().chooseLarge(n, k, combinations);
}

void Observe::observe(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability, const Probability::Pool &probabilities, const ShapeSolver::Config &config, ObservedBoard::CellId cell, Result &result) {
    Workspace &workspace = getWorkspace();
    const Structure::Pool &pool = probabilities.shapes;
    ShapeSolver::DistributionPool &distributions = probabilities.distributions;
    Workspace::Components &components = workspace.components;
    Workspace::Convolution &convolution = workspace.convolution;
    Workspace::Rest &rest = workspace.rest;
    const std::pair<int, int> position = board.pos(cell);
    const int row = position.first;
    const int col = position.second;
    const Basic::Mark mark = basic.marks[row][col];
    const int totalMines = board.totalMines - basic.mineSum;
    result.probability[9] = Probability().mineProbability(probability, probabilities, cell, board, basic, structure);
    if (mark == Basic::Mark::F)
        return;

    const Structure::Result::CellLocation &clicked = structure.cellLoc.data[cell];
    const bool clickedT = mark == Basic::Mark::T;
    const Structure::ComponentId clickedComponent = clicked.component;
    const Structure::BoxId clickedBox = clicked.box;
    const int componentCount = (int)structure.components.size;

    int fixedMines = 0;
    int tNeighbors = 0;
    components.captured.resize(0);
    components.seen.resize(componentCount);
    std::fill(components.seen.data, components.seen.data + componentCount, 0);
    // 固定雷只平移数字下标；只捕获邻接组件，所有 T 邻格稍后按组合数合并。
    forEachAdjacent(row, col, board.cells.rows(), board.cells.cols(), [&](int neighborRow, int neighborCol) {
        const Basic::Mark neighborMark = basic.marks[neighborRow][neighborCol];
        if (neighborMark == Basic::Mark::F) {
            ++fixedMines;
            return;
        }
        if (neighborMark == Basic::Mark::T) {
            ++tNeighbors;
            return;
        }
        const Structure::ComponentId component = structure.cellLoc.data[board.id(neighborRow, neighborCol)].component;
        if (component < 0 || components.seen.data[component])
            return;
        components.seen.data[component] = 1;
        components.captured.push_back(component);
    });

    int maxCapturedMines = tNeighbors;
    for (int index = 0; index < (int)components.captured.size; ++index) {
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[components.captured.data[index]]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        const int *boxSizes = pool.shapes.boxSizes.data.data + shape.boxSizes.offset;
        for (int box = 0; box < (int)shape.boxSizes.count; ++box)
            maxCapturedMines += boxSizes[box];
    }
    const int stride = maxCapturedMines + 1;

    // 两张表逐个组件交替累积；指针交换，避免整表复制。
    Grid<long double> *current = &convolution.dp;
    Grid<long double> *next = &convolution.nextDp;
    current->resize(1, stride, 0.0L);
    (*current)[0][0] = 1.0L;
    // 只遍历已合并区域可能占用的行列，尚未加入的邻格和组件对应整片零值。
    int neighborLimit = 0;
    int capturedLimit = 0;
    for (int index = 0; index < (int)components.captured.size; ++index) {
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[components.captured.data[index]]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        const int boxCount = (int)shape.boxSizes.count;
        components.adjacentBoxCells.resize(boxCount);
        std::fill(components.adjacentBoxCells.data, components.adjacentBoxCells.data + boxCount, 0);
        const int *boxOf = pool.instances.boxOf.data.data + instance.boxOf.offset;
        const Structure::CellId *cells = pool.instances.cells.data.data + instance.cells.offset;
        for (int box = 0; box < boxCount; ++box)
            for (int i = boxOf[box]; i < boxOf[box + 1]; ++i) {
                const std::pair<int, int> boxPosition = board.pos(cells[i]);
                const int rowDistance = boxPosition.first - row;
                const int colDistance = boxPosition.second - col;
                if (rowDistance >= -1 && rowDistance <= 1 && colDistance >= -1 && colDistance <= 1 && (rowDistance != 0 || colDistance != 0))
                    ++components.adjacentBoxCells.data[box];
            }

        int addedLimit = 0;
        for (int box = 0; box < boxCount; ++box) addedLimit += components.adjacentBoxCells.data[box];
        const Structure::BoxId xBox = components.captured.data[index] == clickedComponent ? clickedBox : -1;
        ShapeSolver().observe(shape, pool, span<const int>(components.adjacentBoxCells.data, boxCount), xBox, config, components.observed);
        next->resize(neighborLimit + addedLimit + 1, stride, 0.0L);
        for (int currentNeighborMines = 0; currentNeighborMines <= neighborLimit; ++currentNeighborMines)
            for (int addedNeighborMines = 0; addedNeighborMines <= addedLimit; ++addedNeighborMines) {
                const vectorPool<long double>::vector &segment = components.observed.ways[addedNeighborMines];
                const long double *segmentWays = components.observed.values.data.data + segment.offset;
                for (int capturedMines = 0; capturedMines <= capturedLimit; ++capturedMines) {
                    const long double base = (*current)[currentNeighborMines][capturedMines];
                    if (base == 0.0L)
                        continue;
                    for (int i = 0; i < (int)segment.count; ++i)
                        (*next)[currentNeighborMines + addedNeighborMines][capturedMines + components.observed.start[addedNeighborMines] + i] += base * segmentWays[i];
                }
            }
        capturedLimit += boxOf[boxCount];
        neighborLimit += addedLimit;
        std::swap(current, next);
    }

    // 邻接 T 格彼此独立，组合数同时增加邻居雷数和捕获区域雷数。
    if (tNeighbors > 0) {
        next->resize(neighborLimit + tNeighbors + 1, stride, 0.0L);
        for (int neighborMines = 0; neighborMines <= neighborLimit; ++neighborMines)
            for (int capturedMines = 0; capturedMines <= capturedLimit; ++capturedMines) {
                const long double base = (*current)[neighborMines][capturedMines];
                if (base == 0.0L)
                    continue;
                for (int tMines = 0; tMines <= tNeighbors && neighborMines + tMines <= 8; ++tMines)
                    (*next)[neighborMines + tMines][capturedMines + tMines] += base * Combination().chooseSmall(tNeighbors, tMines);
            }
        neighborLimit += tNeighbors;
        std::swap(current, next);
    }

    // 剩余 T 格与未捕获组件只影响总雷数，不影响点开数字；将其合成 rest.ways。
    const int tPool = basic.unknownSum - tNeighbors - (clickedT ? 1 : 0);
    int restStart = 0;
    rest.coefficients.resize(1);
    rest.coefficients.data[0] = 1.0L;
    for (int component = 0; component < componentCount; ++component) {
        if (components.seen.data[component])
            continue;
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        const ShapeSolver::DistributionId id = ShapeSolver().analyze(shape, config, distributions);
        const ShapeSolver::Distribution &distribution = distributions.list.data[id];
        const Poly left{restStart, span<const long double>(rest.coefficients.data, (int)rest.coefficients.size)};
        const Poly right{distribution.start, span<const long double>(distributions.values.data.data + distribution.ways.offset, (int)distribution.ways.count)};
        const int nextStart = restStart + distribution.start;
        multiplyPoly(left, right, rest.scratch);
        swapValues(rest.coefficients, rest.scratch);
        restStart = nextStart;
    }

    if (rest.coefficients.size == 0) {
        rest.ways.resize(0);
    } else {
        rest.ways.resize((std::min)(totalMines + 1, restStart + tPool + (int)rest.coefficients.size));
        std::fill(rest.ways.data, rest.ways.data + rest.ways.size, 0.0L);
        // 合并时只会查询 totalMines - capturedMines，其余总雷数无需计算。
        const int minRestMines = (std::max)(0, totalMines - maxCapturedMines);
        for (int i = 0; i < (int)rest.coefficients.size; ++i) {
            const long double ways = rest.coefficients.data[i];
            const int componentMines = restStart + i;
            for (int tMines = (std::max)(0, minRestMines - componentMines); tMines <= (std::min)(tPool, totalMines - componentMines); ++tMines)
                rest.ways.data[componentMines + tMines] += ways * choose(tPool, tMines, probabilities.combinations);
        }
    }

    long double neighborWays[9] = {};
    // 分母沿用全盘 Probability 结果；分子只累计点击安全且邻居数字确定的布局。
    for (int neighborMines = 0; neighborMines <= neighborLimit; ++neighborMines)
        for (int capturedMines = 0; capturedMines <= maxCapturedMines; ++capturedMines) {
            const long double ways = (*current)[neighborMines][capturedMines];
            const int restMines = totalMines - capturedMines;
            if (ways == 0.0L || restMines < 0 || restMines >= (int)rest.ways.size)
                continue;
            neighborWays[neighborMines] += ways * rest.ways.data[restMines];
        }

    for (int neighborMines = 0; neighborMines + fixedMines <= 8; ++neighborMines)
        result.probability[fixedMines + neighborMines] = neighborWays[neighborMines] / probability.candidates;
}

} // 命名空间 mss
