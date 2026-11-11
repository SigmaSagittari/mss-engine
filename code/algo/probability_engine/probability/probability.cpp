#include "algo/probability_engine/probability/probability.h"

#include <algorithm>
#include <utility>

#include "core/algorithm/poly.h"
#include "core/algorithm/rng.h"
#include "core/assert.h"

namespace mss {

// 组件分布、多项式组合与抽样所需的临时存储。
struct Probability::Workspace {
    // 多项式乘积树的临时存储：系数池、节点和 outside 缓冲。
    struct Products {
        vectorPool<long double> values;
        struct Node {
            int start;
            vectorPool<long double>::vector coefficients; // 来自 Products::values，第 i 项对应 start + i 个前沿雷。
        };
        // 叶子补到 2 的幂，按二叉树下标存放。
        vector<Node> nodes;
        // 自顶向下求 outside 时的临时节点。
        vector<Node> outside;
    } frontierProducts;
    // 各组件按雷数分布的多项式，系数借用 distributions.values。
    vector<Poly> frontiers;

    struct Sample {
        vectorPool<long double> values;
        // suffix[i] 切自 Sample::values，存从实例 i 起的后缀多项式，首项次数为 suffixStart[i]。
        vector<vectorPool<long double>::vector> suffix;
        vector<int> suffixStart;
        vector<ObservedBoard::CellId> freeCells;
    } sample;
};

// 多项式乘积树；临时节点和系数使用本文件工作区的 frontierProducts。
struct PolyProductTree {
    using Node = Probability::Workspace::Products::Node;

    // 先回调全乘积，返回真时再回调每个因子的块外乘积。视图只在对应回调期间有效。
    template <typename Callback, typename TotalCallback> void forEachOutside(const vector<Poly> &factors, const Callback &callback, const TotalCallback &totalCallback) {
        Probability::Workspace &workspace = Probability().getWorkspace();
        vectorPool<long double> &values = workspace.frontierProducts.values;
        vector<Node> &nodes = workspace.frontierProducts.nodes;
        vector<Node> &outside = workspace.frontierProducts.outside;
        const int factorCount = factors.size;
        values.data.resize(0);
        Node unit{0, values.push_back()};
        values.push_back(unit.coefficients, 1.0L);
        if (factorCount == 0) {
            totalCallback(polyOf(unit, values));
            return;
        }
        int leafBase = 1;
        int treeHeight = 0;
        while (leafBase < factorCount) {
            leafBase *= 2;
            ++treeHeight;
        }
        nodes.resize(leafBase);
        outside.resize(treeHeight + 1);
        outside.data[0] = unit;
        // 叶数补到 2 的幂；越界叶子由 unit 作为乘法单位元提供。
        for (int index = leafBase - 1; index > 0; --index) {
            const int left = index << 1;
            const int right = left | 1;
            Node &product = nodes.data[index];
            product = allocateProduct(nodeOf(left, factors, leafBase, unit, nodes, values), nodeOf(right, factors, leafBase, unit, nodes, values), values);
            multiply(nodeOf(left, factors, leafBase, unit, nodes, values), nodeOf(right, factors, leafBase, unit, nodes, values), product, values);
        }
        if (totalCallback(leafBase == 1 ? factors[0] : polyOf(nodes.data[1], values))) {
            // 每个子树的 outside 等于父 outside 乘兄弟子树。
            visitOutside(1, 0, factors, leafBase, unit, nodes, outside, values, callback);
        }
    }

  private:
    Poly polyOf(const Node &node, const vectorPool<long double> &values) {
        if (node.coefficients.count == 0)
            return Poly{node.start, {}};
        return Poly{node.start, span<const long double>(values.data.data + node.coefficients.offset, (int)node.coefficients.count)};
    }

    Poly nodeOf(int index, const vector<Poly> &factors, int leafBase, const Node &unit, const vector<Node> &nodes, const vectorPool<long double> &values) {
        if (index >= leafBase) {
            const int factorIndex = index - leafBase;
            return factorIndex < (int)factors.size ? factors[factorIndex] : polyOf(unit, values);
        }
        return polyOf(nodes.data[index], values);
    }

    // 分配可能搬动池；调用方随后重新按节点编号取得输入视图。
    Node allocateProduct(const Poly &left, const Poly &right, vectorPool<long double> &values) {
        if (left.size() == 0 || right.size() == 0)
            return {left.start + right.start, {}};
        const int size = left.size() + right.size() - 1;
        Node result{left.start + right.start, values.push_back()};
        for (int i = 0; i < size; ++i)
            values.push_back(result.coefficients, 0.0L);
        return result;
    }

    void multiply(const Poly &left, const Poly &right, const Node &result, vectorPool<long double> &values) {
        if (result.coefficients.count == 0)
            return;
        long double *output = values.data.data + result.coefficients.offset;
        for (int i = 0; i < left.size(); ++i)
            for (int j = 0; j < right.size(); ++j)
                output[i + j] += left.data()[i] * right.data()[j];
    }

    template <typename Callback> void visitOutside(int index, int depth, const vector<Poly> &factors, int leafBase, const Node &unit, const vector<Node> &nodes, vector<Node> &outside, vectorPool<long double> &values, const Callback &callback) {
        const Node &currentOutside = outside.data[depth];
        if (index >= leafBase) {
            const int factorIndex = index - leafBase;
            if (factorIndex < (int)factors.size)
                callback(factorIndex, polyOf(currentOutside, values));
            return;
        }
        Node &nextOutside = outside.data[depth + 1];
        nextOutside = allocateProduct(polyOf(currentOutside, values), nodeOf(index << 1 | 1, factors, leafBase, unit, nodes, values), values);
        multiply(polyOf(currentOutside, values), nodeOf(index << 1 | 1, factors, leafBase, unit, nodes, values), nextOutside, values);
        visitOutside(index << 1, depth + 1, factors, leafBase, unit, nodes, outside, values, callback);
        nextOutside = allocateProduct(polyOf(currentOutside, values), nodeOf(index << 1, factors, leafBase, unit, nodes, values), values);
        multiply(polyOf(currentOutside, values), nodeOf(index << 1, factors, leafBase, unit, nodes, values), nextOutside, values);
        visitOutside(index << 1 | 1, depth + 1, factors, leafBase, unit, nodes, outside, values, callback);
    }
};

__declspec(noinline) Probability::Workspace &Probability::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

// 自由格填 freeMines 个雷的组合数；小规模查表，大规模走缓存。
long double Probability::pickFreeWays(int freeCells, int freeMines, Combination::Pool &combinations) {
    if (freeCells <= 9)
        return Combination().chooseSmall(freeCells, freeMines);
    return Combination().chooseLarge(freeCells, freeMines, combinations);
}

long double Probability::countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Pool &combinations) {
    // 每项前沿方案数再乘上恰好填入 freeMines 个自由格的组合数。
    const int excess = totalMines - polynomial.start;
    const int first = (std::max)(0, excess - freeCells);
    const int last = (std::min)(excess, polynomial.size() - 1);
    long double result = 0.0L;
    const long double *coefficients = polynomial.data();
    for (int i = first; i <= last; ++i)
        result += coefficients[i] * pickFreeWays(freeCells, excess - i, combinations);
    return result;
}

long double Probability::limitProbability(long double probability) {
    // 实战研究已确认，使用此阈值绝对不会误判。
    // 截断集中在查询出口，调用方按 BoxResult 的精确值约定判定。
    return probability >= 1.0L - 1e-10L ? 1.0L : probability;
}

void Probability::BoxResult::lowestProbabilities(const Pool &probabilities, const Structure::Result &structure, int count, vector<long double> &output) const {
    output.resize(0);
    if (count == 0) return;
    const Structure::Pool::Instances &instances = probabilities.distributions.shapes.instances;
    for (std::size_t component = 0; component < instanceBoxes.count; ++component) {
        const vectorPool<long double>::vector boxes = probabilities.instanceBoxes.span(instanceBoxes)[component];
        const Structure::Instance &instance = instances.list[structure.instanceIds[component]];
        for (std::size_t box = 0; box < boxes.count; ++box) {
            const int size = (int)instances.boxes.span(instance.boxes)[box].count;
            const int copies = (std::min)(count, size);
            const long double probability = Probability().limitProbability(probabilities.boxProbabilities.span(boxes)[box]);
            for (int i = 0; i < copies; ++i) {
                if ((int)output.size == count && probability >= output[output.size - 1]) break;
                int position = output.size;
                if (position < count) output.resize(position + 1);
                else --position;
                while (position > 0 && probability < output[position - 1]) {
                    output[position] = output[position - 1];
                    --position;
                }
                output[position] = probability;
            }
        }
    }
}

void Probability::Pool::clear() {
    blocks.data.resize(0);
    blockProbabilities.data.resize(0);
    instanceBoxes.data.resize(0);
    boxProbabilities.data.resize(0);
}

Probability::Result Probability::analyze(const vector<ShapeSolver::DistributionId> &distributionIds, int totalMines, Pool &results) {
    Workspace &workspace = getWorkspace();
    const ShapeSolver::DistributionPool &distributions = results.distributions;
    vector<Poly> &frontiers = workspace.frontiers;
    frontiers.resize(0);
    const int componentCount = (int)distributionIds.size;
    int tSum = 0;
    Result result{results.blocks.push_back(), results.blockProbabilities.push_back(), 0.0L};
    for (int component = 0; component < componentCount; ++component) {
        const ShapeSolver::Distribution &distribution = distributions.list.data[distributionIds.data[component]];
        if (distribution.ways.count == 0)
            return result;
        frontiers.push_back(Poly{distribution.start, span<const long double>(distributions.values.data.data + distribution.ways.offset, (int)distribution.ways.count)});
        tSum += distribution.tCount;
    }

    PolyProductTree().forEachOutside(frontiers, [&](int component, const Poly &outside) {
        const ShapeSolver::Distribution &distribution = distributions.list.data[distributionIds.data[component]];
        const Poly &frontier = frontiers.data[component];
        const int tCount = distribution.tCount;
        const int outsideTCount = tSum - tCount;
        const int maxLocalMines = frontier.start + frontier.size() - 1 + tCount;
        const int maxOutsideMines = outside.start + outside.size() - 1 + outsideTCount;
        const int start = (std::max)(frontier.start, totalMines - maxOutsideMines);
        const int end = (std::min)(maxLocalMines, totalMines - outside.start);
        vectorPool<long double>::vector probabilities = results.blockProbabilities.push_back();
        for (int mines = start; mines <= end; ++mines) {
            const long double localWays = countWeightedWays(frontier, mines, tCount, results.combinations);
            const long double outsideWays = localWays == 0.0L ? 0.0L : countWeightedWays(outside, totalMines - mines, outsideTCount, results.combinations);
            const long double chance = outsideWays == 0.0L ? 0.0L : localWays * outsideWays / result.totalWays;
            results.blockProbabilities.push_back(probabilities, chance);
        }
        results.blocks.push_back(result.blocks, {start, probabilities});
    }, [&](const Poly &all) {
        result.totalWays = countWeightedWays(all, totalMines, tSum, results.combinations);
        return result.totalWays != 0.0L;
    });
    result.probabilities.count = results.blockProbabilities.data.size - result.probabilities.offset;
    return result;
}

Probability::BoxResult Probability::projectBoxes(const Result &result, const vector<ShapeSolver::DistributionId> &distributionIds,
                                                 const Structure::Result &structure, Pool &results) {
    BoxResult boxes{results.instanceBoxes.push_back(), 0.0L};
    if (result.totalWays == 0.0L)
        return boxes;

    const ShapeSolver::DistributionPool &distributions = results.distributions;
    const Structure::Pool::Instances &instances = distributions.shapes.instances;
    const int instanceCount = (int)structure.instanceIds.size;
    for (int component = 0; component < instanceCount; ++component) {
        const ShapeSolver::Distribution &dist = distributions.list[distributionIds[component]];
        const Result::Block &bands = results.blocks.span(result.blocks)[component];
        const Structure::Instance &instance = instances.list[structure.instanceIds[component]];
        const int boxCount = dist.boxCount;
        vectorPool<long double>::vector boxProbabilities = results.boxProbabilities.push_back();
        for (int box = 0; box < boxCount; ++box)
            results.boxProbabilities.push_back(boxProbabilities, 0.0L);
        int mines = bands.start;
        for (long double chance : results.blockProbabilities.span(bands.probabilities)) {
            const int mineIndex = mines++ - dist.start;
            if (chance == 0.0L)
                continue;
            const std::size_t row = (std::size_t)mineIndex * boxCount;
            for (int box = 0; box < boxCount; ++box)
                results.boxProbabilities.span(boxProbabilities)[box] += chance * distributions.values.span(dist.expectations)[row + box];
        }
        for (int box = 0; box < boxCount; ++box)
            results.boxProbabilities.span(boxProbabilities)[box] /= instances.boxes.span(instance.boxes)[box].count;
        results.instanceBoxes.push_back(boxes.instanceBoxes, boxProbabilities);
    }
    if (instanceCount < (int)distributionIds.size) {
        const int tCount = distributions.list[distributionIds[instanceCount]].tCount;
        if (tCount > 0) {
            const Result::Block &bands = results.blocks.span(result.blocks)[instanceCount];
            long double expected = 0.0L;
            int mines = bands.start;
            for (long double chance : results.blockProbabilities.span(bands.probabilities))
                expected += mines++ * chance;
            boxes.tCellProbability = limitProbability(expected / tCount);
        }
    }
    return boxes;
}

void Probability::sampleMineBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, ShapeSolver::AssignmentPool &assignments, Combination::Pool &combinations, U128 seed, Grid<char> &result) {
    Workspace &workspace = getWorkspace();
    const Structure::Pool &pool = assignments.shapes;
    const int componentCount = (int)structure.instanceIds.size;
    for (int component = 0; component < componentCount; ++component) {
        const Structure::Instance &instance = pool.instances.list.data[structure.instanceIds.data[component]];
        ShapeSolver().assignments(instance, assignments);
    }

    // 后缀多项式：suffix[c] 是组件 c..n-1 的雷数分布乘积；suffix[n] 为常数 1。
    vectorPool<long double> &sampleValues = workspace.sample.values;
    vector<vectorPool<long double>::vector> &suffix = workspace.sample.suffix;
    vector<int> &suffixStart = workspace.sample.suffixStart;
    vector<ObservedBoard::CellId> &freeCells = workspace.sample.freeCells;
    sampleValues.data.resize(0);
    suffix.resize(componentCount + 1);
    suffixStart.resize(componentCount + 1);
    suffixStart.data[componentCount] = 0;
    suffix.data[componentCount] = sampleValues.push_back();
    sampleValues.push_back(suffix.data[componentCount], 1.0L);
    for (int component = componentCount - 1; component >= 0; --component) {
        const Structure::Instance &instance = pool.instances.list.data[structure.instanceIds.data[component]];
        const ShapeSolver::Assignments *assignment = assignments.find(instance.shape);
        const int leftCount = (int)assignment->ways.count;
        const vectorPool<long double>::vector right = suffix.data[component + 1];
        const int rightCount = (int)right.count;
        suffixStart.data[component] = suffixStart.data[component + 1];
        suffix.data[component] = sampleValues.push_back();
        if (leftCount == 0 || rightCount == 0)
            continue;
        const int size = leftCount + rightCount - 1;
        for (int i = 0; i < size; ++i)
            sampleValues.push_back(suffix.data[component], 0.0L);
        const long double *leftData = assignments.storage.ways.data.data + assignment->ways.offset;
        const long double *rightData = sampleValues.data.data + right.offset;
        long double *output = sampleValues.data.data + suffix.data[component].offset;
        for (int i = 0; i < leftCount; ++i)
            for (int j = 0; j < rightCount; ++j)
                output[i + j] += leftData[i] * rightData[j];
    }

    const vectorPool<long double>::vector &frontier = suffix.data[0];
    const int frontierStart = suffixStart.data[0];
    const long double *frontierWays = sampleValues.data.data + frontier.offset;
    const int remainingMines = board.totalMines - basic.mineSum;
    const int freeCount = basic.unknownSum;
    long double totalWays = 0.0L;
    for (int i = 0; i < (int)frontier.count; ++i) {
        const int freeMines = remainingMines - frontierStart - i;
        if (freeMines < 0 || freeMines > freeCount)
            continue;
        totalWays += frontierWays[i] * pickFreeWays(freeCount, freeMines, combinations);
    }
    assert_(totalWays != 0.0L, "Probability::sampleMineBoard: 没有合法全盘雷分布");

    Random random(seed.lo, seed.hi);
    const long double frontierDraw = random.nextUnit();
    long double accumulated = 0.0L;
    int frontierMines = -1;
    for (int i = 0; i < (int)frontier.count; ++i) {
        const int freeMines = remainingMines - frontierStart - i;
        if (freeMines < 0 || freeMines > freeCount)
            continue;
        accumulated += frontierWays[i] * pickFreeWays(freeCount, freeMines, combinations);
        if (frontierDraw < accumulated / totalWays) {
            frontierMines = frontierStart + i;
            break;
        }
    }
    assert_(frontierMines >= 0, "Probability::sampleMineBoard: 全盘抽样未命中雷数档");

    result.resize(board.cells.rows(), board.cells.cols(), 0);
    freeCells.resize(0);
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col) {
            const Basic::Mark mark = basic.marks[row][col];
            if (mark == Basic::Mark::F)
                result[row][col] = 1;
            else if (mark == Basic::Mark::T)
                freeCells.push_back(board.id(row, col));
        }

    int remainingFrontier = frontierMines;
    for (int component = 0; component < componentCount; ++component) {
        const Structure::Instance &instance = pool.instances.list.data[structure.instanceIds.data[component]];
        const ShapeSolver::Assignments *assignment = assignments.find(instance.shape);
        const long double *ways = assignments.storage.ways.data.data + assignment->ways.offset;
        const vectorPool<long double>::vector &outside = suffix.data[component + 1];
        const int outsideStart = suffixStart.data[component + 1];
        const long double *outsideWays = sampleValues.data.data + outside.offset;
        const long double componentTotal = sampleValues.data.data[suffix.data[component].offset + (remainingFrontier - suffixStart.data[component])];
        const long double draw = random.nextUnit();
        long double sum = 0.0L;
        int componentMines = -1;
        for (int mines = 0; mines < (int)assignment->ways.count; ++mines) {
            const int index = remainingFrontier - mines - outsideStart;
            if (index < 0 || index >= (int)outside.count)
                continue;
            sum += ways[mines] * outsideWays[index];
            if (draw < sum / componentTotal) {
                componentMines = mines;
                break;
            }
        }
        assert_(componentMines >= 0, "Probability::sampleMineBoard: 组件抽样未命中雷数档");
        remainingFrontier -= componentMines;
        const span<const char> boxMines = ShapeSolver().sampleAssignment(assignments, *assignment, componentMines, random.nextUnit());
        ObservedBoard::CellId choices[9];
        for (int box = 0; box < assignment->boxCount; ++box) {
            const vectorPool<Structure::CellId>::vector cells = pool.instances.boxes.data.data[instance.boxes.offset + box];
            const int size = (int)cells.count;
            for (int i = 0; i < size; ++i)
                choices[i] = pool.instances.cells.data.data[cells.offset + i];
            for (int i = 0; i < boxMines[box]; ++i) {
                const int chosen = i + (int)(random.nextUnit() * (size - i));
                std::swap(choices[i], choices[chosen]);
                const std::pair<int, int> position = board.pos(choices[i]);
                result[position.first][position.second] = 1;
            }
        }
    }

    const int freeMines = remainingMines - frontierMines;
    for (int i = 0; i < freeMines; ++i) {
        const int chosen = i + (int)(random.nextUnit() * (freeCount - i));
        std::swap(freeCells.data[i], freeCells.data[chosen]);
        const std::pair<int, int> position = board.pos(freeCells.data[i]);
        result[position.first][position.second] = 1;
    }
}

long double Probability::mineProbability(const BoxResult &result, const Pool &pool, ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure) {
    const std::pair<int, int> position = board.pos(cell);
    const Basic::Mark mark = basic.marks[position.first][position.second];
    const Structure::Result::CellLocation &location = structure.cellLoc[cell];
    if (location.instanceIndex < 0) {
        if (mark == Basic::Mark::F)
            return 1.0L;
        if (mark == Basic::Mark::T)
            return limitProbability(result.tCellProbability);
        return 0.0L;
    }
    if (location.box < 0)
        return 0.0L;
    const vectorPool<long double>::vector boxes = pool.instanceBoxes.span(result.instanceBoxes)[location.instanceIndex];
    return limitProbability(pool.boxProbabilities.span(boxes)[location.box]);
}

} // 命名空间 mss
