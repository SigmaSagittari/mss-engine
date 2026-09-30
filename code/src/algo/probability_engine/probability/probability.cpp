#include "algo/probability_engine/probability/probability.h"

#include <span>
#include <utility>

namespace mss {

const std::vector<Probability::Result::Component> &Probability::Result::components() const {
    return components_;
}

const std::vector<long double> &Probability::Result::boxProbabilities() const {
    return boxProbabilities_;
}

long double Probability::Result::boxProbability(Structure::ComponentId component, Structure::BoxId box) const {
    return Probability::limitProbability(boxProbabilities_[components_[component].boxOffset + box]);
}

long double Probability::Result::mineProbability(ObservedBoard::CellId cell, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure) const {
    const std::pair<int, int> position = board.pos(cell);
    const Basic::Mark mark = basic.marks[position.first][position.second];
    const Structure::Result::CellLocation location = structure.cellLoc[cell];
    if (location.component < 0) {
        if (mark == Basic::Mark::F)
            return 1.0L;
        if (mark == Basic::Mark::T)
            return Probability::limitProbability(tCellProbability_);
        return 0.0L;
    }
    if (location.box < 0)
        return 0.0L;
    return Probability::limitProbability(boxProbabilities_[components_[location.component].boxOffset + location.box]);
}

long double Probability::Result::candidates() const {
    return candidates_;
}

long double Probability::Result::tCellProbability() const {
    return tCellProbability_;
}

long double Probability::countWeightedWays(const Poly &polynomial, int totalMines, int freeCells, Combination::Workspace &workspace) {
    // 每项前沿方案数再乘上恰好填入 freeMines 个自由格的组合数。
    if (freeCells < 0)
        return 0.0L;
    long double result = 0.0L;
    for (int i = 0; i < (int)polynomial.coefficients.size(); ++i) {
        const int freeMines = totalMines - polynomial.start - i;
        if (freeMines < 0 || freeMines > freeCells)
            continue;
        if (freeCells <= 9)
            result += polynomial.coefficients[i] * Combination::chooseSmall(freeCells, freeMines);
        else
            result += polynomial.coefficients[i] * Combination::chooseLarge(freeCells, freeMines, workspace);
    }
    return result;
}

long double Probability::limitProbability(long double probability) {
    // 分母归一化的舍入误差可能让本应为 1 的概率略低于 1；沿用 1.4 的阈值截断。
    return probability >= 1.0L - 1e-10L ? 1.0L : probability;
}

Probability::Result Probability::analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Workspace &workspace, const ShapeSolver::OrderAlgo &algo) {
    Result result;
    analyze(board, basic, structure, shapes, distributions, result, workspace, algo);
    return result;
}

void Probability::analyze(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, Result &result, Workspace &workspace, const ShapeSolver::OrderAlgo &algo) {
    Workspace &ws = workspace;
    ws.distributionIds.clear();
    ws.frontiers.clear();
    ws.distributionIds.reserve(structure.components.size());
    ws.frontiers.reserve(structure.components.size());
    // 先完成所有池插入，再借用 ways 向量；中途插入可能搬动池内的 DistributionResult。
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        ws.distributionIds.push_back(ShapeSolver::analyze(shapes.get(instance.shape), shapes, distributions, ws.shapeSolver, algo));
    }
    for (const ShapeSolver::DistributionId id : ws.distributionIds)
        if (distributions.get(id).ways().empty()) {
            result.components_.clear();
            result.boxProbabilities_.clear();
            result.candidates_ = 0.0L;
            result.tCellProbability_ = 0.0L;
            return;
        }

    result.components_.resize(structure.components.size());
    int boxOffset = 0;
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const Structure::Shape &shape = shapes.get(instance.shape);
        result.components_[component] = {boxOffset, shape.boxes.size};
        boxOffset += shape.boxes.size;
    }
    result.boxProbabilities_.assign(boxOffset, 0.0L);
    result.candidates_ = 0.0L;
    result.tCellProbability_ = 0.0L;
    for (int component = 0; component < (int)ws.distributionIds.size(); ++component) {
        const ShapeSolver::DistributionResult &distribution = distributions.get(ws.distributionIds[component]);
        ws.frontiers.push_back(Poly(distribution.start(), distribution.ways()));
    }

    const int totalMines = board.totalMines - basic.mineSum;
    const int tSum = basic.unknownSum;
    // outside 视图只在回调期间有效，因此在此直接累加未归一化的 Box 雷概率分子。
    const Poly all = ws.frontierProducts.forEachOutside(ws.frontiers, [&](int component, const Poly &outside) {
        const ShapeSolver::DistributionResult &distribution = distributions.get(ws.distributionIds[component]);
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const Structure::Shape &shape = shapes.get(instance.shape);
        const Probability::Result::Component &componentResult = result.components_[component];
        const std::vector<long double> &componentWays = distribution.ways();
        for (int i = 0; i < (int)componentWays.size(); ++i) {
            if (componentWays[i] == 0.0L)
                continue;
            const int componentMines = distribution.start() + i;
            const long double outsideWays = countWeightedWays(outside, totalMines - componentMines, tSum, ws.combination);
            if (outsideWays == 0.0L)
                continue;
            const long double totalWays = componentWays[i] * outsideWays;
            const std::span<const long double> expectations = distribution.perBoxExpectation(i);
            // expectation 是该雷数档下 Box 中的期望雷数，除以 Box 大小后才是任一格概率的分子。
            for (int box = 0; box < shape.boxes.size; ++box)
                result.boxProbabilities_[componentResult.boxOffset + box] += totalWays * expectations[box] / shape.boxes.span(shapes.boxes)[box].size;
        }
    });

    result.candidates_ = countWeightedWays(all, totalMines, tSum, ws.combination);
    if (result.candidates_ == 0.0L)
        return;
    result.tCellProbability_ = limitProbability(countWeightedWays(all, totalMines - 1, tSum - 1, ws.combination) / result.candidates_);
    for (int box = 0; box < (int)result.boxProbabilities_.size(); ++box)
        result.boxProbabilities_[box] /= result.candidates_;
}

long double Probability::analyzeLogicComponents(const std::vector<LogicComponentInput> &components, int totalMines, Workspace &workspace, const std::function<void(int, int, long double, long double)> &callback) {
    Workspace &ws = workspace;
    ws.frontiers.clear();
    ws.frontiers.reserve(components.size());
    ws.logicComponentValues.clear();
    int totalOffFrontier = 0;
    for (int component = 0; component < (int)components.size(); ++component) {
        ws.frontiers.push_back(components[component].frontier);
        totalOffFrontier += components[component].offFrontierCount;
    }

    const Poly all = ws.frontierProducts.forEachOutside(ws.frontiers, [&](int component, const Poly &outside) {
        const LogicComponentInput &input = components[component];
        const Poly &frontier = input.frontier;
        if (frontier.coefficients.empty())
            return;
        int maxMines = frontier.start + input.offFrontierCount;
        for (int i = 0; i < (int)frontier.coefficients.size(); ++i)
            if (frontier.coefficients[i] != 0.0L)
                maxMines = frontier.start + i + input.offFrontierCount;
        if (maxMines > totalMines)
            maxMines = totalMines;
        for (int mines = frontier.start; mines <= maxMines; ++mines) {
            const long double localWays = countWeightedWays(frontier, mines, input.offFrontierCount, ws.combination);
            if (localWays == 0.0L)
                continue;
            const long double outsideWays = countWeightedWays(outside, totalMines - mines, totalOffFrontier - input.offFrontierCount, ws.combination);
            const long double totalWays = localWays * outsideWays;
            if (totalWays != 0.0L)
                ws.logicComponentValues.push_back({component, mines, localWays, totalWays});
        }
    });

    const long double candidates = countWeightedWays(all, totalMines, totalOffFrontier, ws.combination);
    if (candidates == 0.0L)
        return candidates;
    for (const Workspace::LogicComponentValue &value : ws.logicComponentValues) {
        const long double probability = value.totalWays / candidates;
        if (probability != 0.0L)
            callback(value.component, value.mines, value.localWays, probability);
    }
    return candidates;
}

}
