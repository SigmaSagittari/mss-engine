#include "algo/probability_engine/probability/probability.h"

#include <algorithm>
#include <utility>

#include "core/algorithm/rng.h"
#include "core/assert.h"

namespace mss {

// 组件分布、多项式组合与抽样所需的临时存储。
struct Probability::Workspace {
    // 本次 analyze 的组件分布 ID。
    vector<ShapeSolver::DistributionId> distributionIds;

    // 多项式乘积树的临时存储：系数池、节点和 outside 缓冲。
    struct Products {
        vectorPool<long double> values;
        struct Node {
            int start;
            vectorPool<long double>::vector coefficients;
        };
        // 叶子补到 2 的幂，按二叉树下标存放。
        vector<Node> nodes;
        // 自顶向下求 outside 时的临时节点。
        vector<Node> outside;
    } frontierProducts;
    // 各组件按雷数分布的多项式，系数借用 distributions.values。
    vector<Poly> frontiers;
    // 乘积树先回调、后返回全盘分母；暂存各档分子以便统一归一化。
    struct LogicValue {
        int component;
        int mines;
        long double localWays;
        long double totalWays;
    };
    vector<LogicValue> logicValues;

    struct Sample {
        // 组件后缀多项式的系数池；suffix[i] 是第 i 个后缀，suffixStart 是它的起点。
        vectorPool<long double> values;
        vector<vectorPool<long double>::vector> suffix;
        vector<int> suffixStart;
        vector<ObservedBoard::CellId> freeCells;
    } sample;
};

// 多项式乘积树；临时节点和系数使用本文件工作区的 frontierProducts。
struct PolyProductTree {
    using Node = Probability::Workspace::Products::Node;

    // 每个因子回调一次，最后回调全乘积；所有视图只在对应回调期间有效。
    template <typename Callback, typename TotalCallback> void forEachOutside(span<const Poly> factors, const Callback &callback, const TotalCallback &totalCallback) {
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
        // 每个子树的 outside 等于父 outside 乘兄弟子树；不做多项式除法，零因子也能正确处理。
        visitOutside(1, 0, factors, leafBase, unit, nodes, outside, values, callback);
        totalCallback(leafBase == 1 ? factors[0] : polyOf(nodes.data[1], values));
    }

  private:
    Poly polyOf(const Node &node, const vectorPool<long double> &values) {
        if (node.coefficients.count == 0)
            return Poly{node.start, {}};
        return Poly{node.start, span<const long double>(values.data.data + node.coefficients.offset, (int)node.coefficients.count)};
    }

    Poly nodeOf(int index, span<const Poly> factors, int leafBase, const Node &unit, const vector<Node> &nodes, const vectorPool<long double> &values) {
        if (index >= leafBase) {
            const int factorIndex = index - leafBase;
            return factorIndex < factors.size ? factors[factorIndex] : polyOf(unit, values);
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

    template <typename Callback> void visitOutside(int index, int depth, span<const Poly> factors, int leafBase, const Node &unit, const vector<Node> &nodes, vector<Node> &outside, vectorPool<long double> &values, const Callback &callback) {
        const Node &currentOutside = outside.data[depth];
        if (index >= leafBase) {
            const int factorIndex = index - leafBase;
            if (factorIndex < factors.size)
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

// 赋值的每档总权重按绝对雷数索引。
const long double *Probability::assignmentWays(const ShapeSolver::AssignmentPool &pool, const ShapeSolver::Assignments &assignments) {
    return pool.storage.ways.data.data + assignments.ways.offset;
}

long double Probability::countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Pool &combinations) {
    // 每项前沿方案数再乘上恰好填入 freeMines 个自由格的组合数。
    if (freeCells < 0)
        return 0.0L;
    long double result = 0.0L;
    const long double *coefficients = polynomial.data();
    for (int i = 0; i < polynomial.size(); ++i) {
        const int freeMines = totalMines - polynomial.start - i;
        if (freeMines < 0 || freeMines > freeCells)
            continue;
        result += coefficients[i] * pickFreeWays(freeCells, freeMines, combinations);
    }
    return result;
}

long double Probability::limitProbability(long double probability) {
    // 分母归一化的舍入误差可能让本应为 1 的概率略低于 1；沿用 1.4 的阈值截断。
    return probability >= 1.0L - 1e-10L ? 1.0L : probability;
}

void Probability::Result::lowestProbabilities(const Pool &probabilities, const Structure::Result &structure, int count, vector<long double> &output) const {
    output.resize(0);
    if (count == 0) return;
    for (int component = 0; component < (int)components.count; ++component) {
        const Component &layout = probabilities.components.data.data[components.offset + component];
        const Structure::Instance &instance = probabilities.shapes.instances.list.data[structure.components.data[component]];
        const int *offsets = probabilities.shapes.instances.boxOf.data.data + instance.boxOf.offset;
        for (int box = 0; box < layout.boxCount; ++box) {
            const int copies = (std::min)(count, offsets[box + 1] - offsets[box]);
            const long double probability = Probability().limitProbability(probabilities.boxProbabilities.data.data[boxProbabilities.offset + layout.boxOffset + box]);
            for (int i = 0; i < copies; ++i) {
                if ((int)output.size == count && probability >= output.data[output.size - 1]) break;
                int position = output.size;
                if (position < count) output.resize(position + 1);
                else --position;
                while (position > 0 && probability < output.data[position - 1]) {
                    output.data[position] = output.data[position - 1];
                    --position;
                }
                output.data[position] = probability;
            }
        }
    }
}

void Probability::Pool::clear() {
    components.data.resize(0);
    boxProbabilities.data.resize(0);
}

Probability::Result Probability::analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const ShapeSolver::Config &config, Pool &results) {
    Workspace &workspace = getWorkspace();
    const Structure::Pool &pool = results.shapes;
    ShapeSolver::DistributionPool &distributions = results.distributions;
    vector<ShapeSolver::DistributionId> &distributionIds = workspace.distributionIds;
    vector<Poly> &frontiers = workspace.frontiers;
    distributionIds.resize(0);
    frontiers.resize(0);
    const int componentCount = (int)structure.components.size;
    // 先完成所有池插入，再借用系数；中途插入可能搬动池内的结果与系数数组。
    for (int component = 0; component < componentCount; ++component) {
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        distributionIds.push_back(ShapeSolver().analyze(shape, config, distributions));
    }
    for (int component = 0; component < componentCount; ++component)
        if (distributions.list.data[distributionIds.data[component]].ways.count == 0)
            return {results.components.push_back(), results.boxProbabilities.push_back(), 0.0L, 0.0L};

    vectorPool<Result::Component>::vector components = results.components.push_back();
    int boxOffset = 0;
    for (int component = 0; component < componentCount; ++component) {
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        results.components.push_back(components, {boxOffset, (int)shape.boxSizes.count});
        boxOffset += (int)shape.boxSizes.count;
    }
    const vectorPool<long double>::vector boxProbabilities{results.boxProbabilities.data.size, (std::size_t)boxOffset};
    results.boxProbabilities.data.resize(boxProbabilities.offset + boxProbabilities.count);
    long double *probabilities = results.boxProbabilities.data.data + boxProbabilities.offset;
    std::fill(probabilities, probabilities + boxOffset, 0.0L);
    long double candidates = 0.0L;
    long double tCellProbability = 0.0L;
    for (int component = 0; component < componentCount; ++component) {
        const ShapeSolver::Distribution &distribution = distributions.list.data[distributionIds.data[component]];
        frontiers.push_back(Poly{distribution.start, span<const long double>(distributions.values.data.data + distribution.ways.offset, (int)distribution.ways.count)});
    }

    const int totalMines = board.totalMines - basic.mineSum;
    const int tSum = basic.unknownSum;
    // outside 视图只在回调期间有效，因此在此直接累加未归一化的 Box 雷概率分子。
    PolyProductTree().forEachOutside(span<const Poly>(frontiers.data, (int)frontiers.size), [&](int component, const Poly &outside) {
        const ShapeSolver::Distribution &distribution = distributions.list.data[distributionIds.data[component]];
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        const int boxCount = (int)shape.boxSizes.count;
        const int *boxSizes = pool.shapes.boxSizes.data.data + shape.boxSizes.offset;
        const long double *ways = distributions.values.data.data + distribution.ways.offset;
        const long double *expectations = distributions.values.data.data + distribution.expectations.offset;
        const Result::Component &componentResult = results.components.data.data[components.offset + component];
        for (int i = 0; i < (int)distribution.ways.count; ++i) {
            if (ways[i] == 0.0L)
                continue;
            const int componentMines = distribution.start + i;
            const long double outsideWays = countWeightedWays(outside, totalMines - componentMines, tSum, results.combinations);
            if (outsideWays == 0.0L)
                continue;
            const long double totalWays = ways[i] * outsideWays;
            // expectation 是该雷数档下 Box 中的期望雷数，除以 Box 大小后才是任一格概率的分子。
            for (int box = 0; box < boxCount; ++box)
                probabilities[componentResult.boxOffset + box] += totalWays * expectations[i * boxCount + box] / boxSizes[box];
        }
    }, [&](const Poly &all) {
        candidates = countWeightedWays(all, totalMines, tSum, results.combinations);
        if (candidates == 0.0L)
            return;
        tCellProbability = limitProbability(countWeightedWays(all, totalMines - 1, tSum - 1, results.combinations) / candidates);
        for (int box = 0; box < boxOffset; ++box)
            probabilities[box] /= candidates;
    });
    return {components, boxProbabilities, candidates, tCellProbability};
}

void Probability::sampleMineBoard(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, ShapeSolver::AssignmentPool &assignments, Combination::Pool &combinations, U128 seed, Grid<char> &result) {
    Workspace &workspace = getWorkspace();
    const Structure::Pool &pool = assignments.shapes;
    const int componentCount = (int)structure.components.size;
    for (int component = 0; component < componentCount; ++component) {
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const Structure::Shape &shape = pool.shapes.list.data[instance.shape];
        ShapeSolver().assignments(instance.shape, shape, assignments);
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
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
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
        const long double *leftData = assignmentWays(assignments, *assignment);
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
        const Structure::Instance &instance = pool.instances.list.data[structure.components.data[component]];
        const ShapeSolver::Assignments *assignment = assignments.find(instance.shape);
        const long double *ways = assignmentWays(assignments, *assignment);
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
        const int *boxOf = pool.instances.boxOf.data.data + instance.boxOf.offset;
        const Structure::CellId *cells = pool.instances.cells.data.data + instance.cells.offset;
        ObservedBoard::CellId choices[9];
        for (int box = 0; box < assignment->boxCount; ++box) {
            const int size = boxOf[box + 1] - boxOf[box];
            for (int i = 0; i < size; ++i)
                choices[i] = cells[boxOf[box] + i];
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

long double Probability::analyzeLogicComponents(span<const LogicComponentInput> components, int totalMines, Combination::Pool &combinations, const std::function<void(int, int, long double, long double)> &callback) {
    Workspace &workspace = getWorkspace();
    vector<Poly> &frontiers = workspace.frontiers;
    vector<Workspace::LogicValue> &logicValues = workspace.logicValues;
    frontiers.resize(0);
    logicValues.resize(0);
    int totalOffFrontier = 0;
    for (int component = 0; component < components.size; ++component) {
        frontiers.push_back(components.data[component].frontier);
        totalOffFrontier += components.data[component].offFrontierCount;
    }

    long double candidates = 0.0L;
    PolyProductTree().forEachOutside(span<const Poly>(frontiers.data, (int)frontiers.size), [&](int component, const Poly &outside) {
        const LogicComponentInput &input = components.data[component];
        const Poly &frontier = input.frontier;
        if (frontier.size() == 0)
            return;
        int maxMines = frontier.start + input.offFrontierCount;
        for (int i = 0; i < frontier.size(); ++i)
            if (frontier.data()[i] != 0.0L)
                maxMines = frontier.start + i + input.offFrontierCount;
        if (maxMines > totalMines)
            maxMines = totalMines;
        for (int mines = frontier.start; mines <= maxMines; ++mines) {
            const long double localWays = countWeightedWays(frontier, mines, input.offFrontierCount, combinations);
            if (localWays == 0.0L)
                continue;
            const long double outsideWays = countWeightedWays(outside, totalMines - mines, totalOffFrontier - input.offFrontierCount, combinations);
            const long double totalWays = localWays * outsideWays;
            if (totalWays != 0.0L)
                logicValues.push_back({component, mines, localWays, totalWays});
        }
    }, [&](const Poly &all) {
        candidates = countWeightedWays(all, totalMines, totalOffFrontier, combinations);
    });
    if (candidates == 0.0L)
        return candidates;
    for (int i = 0; i < (int)logicValues.size; ++i) {
        const Workspace::LogicValue &value = logicValues.data[i];
        const long double probability = value.totalWays / candidates;
        if (probability != 0.0L)
            callback(value.component, value.mines, value.localWays, probability);
    }
    return candidates;
}

long double Probability::mineProbability(const Result &result, const Pool &pool, ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure) {
    const std::pair<int, int> position = board.pos(cell);
    const Basic::Mark mark = basic.marks[position.first][position.second];
    const Structure::Result::CellLocation &location = structure.cellLoc.data[cell];
    if (location.component < 0) {
        if (mark == Basic::Mark::F)
            return 1.0L;
        if (mark == Basic::Mark::T)
            return limitProbability(result.tCellProbability);
        return 0.0L;
    }
    if (location.box < 0)
        return 0.0L;
    const Result::Component &component = pool.components.data.data[result.components.offset + location.component];
    return limitProbability(pool.boxProbabilities.data.data[result.boxProbabilities.offset + component.boxOffset + location.box]);
}

} // 命名空间 mss
