#include "game/position.h"

namespace mss {

void Position::Config::hash(U128Hasher &hasher) const {
    shapeSolver.hash(hasher);
}

struct Position::Workspace {
    vector<ShapeSolver::DistributionId> distributionIds;
};

__declspec(noinline) Position::Workspace &Position::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void Position::Pool::clear() {
    probabilities.clear();
    distributions.clear();
    structure.clear();
    basic.clear();
    board.clear();
    ++probabilityVersion;
}

void Position::Pool::popDelta(const Delta &delta) {
    structure.deltas.added.pop_back(delta.structure.added);
    structure.deltas.removed.pop_back(delta.structure.removed);
    basic.changes.pop_back(delta.basic.changes);
    board.changes.pop_back(delta.board.changes);
}

Position::Result Position::analyze(const ObservedBoard::Result &board, Pool &pool, const Config &config) {
    Result result(board, config);
    Basic().analyze(result.basic, result.board);
    Structure().analyze(result.structure, result.board, result.basic, pool.structure);
    return result;
}

Position::CheckedResult Position::analyzeChecked(const ObservedBoard::Result &board, Pool &pool, const Config &config, Probability::Pool &probabilityPool) {
    CheckedResult checked{{board, config}, nullptr};
    Result &result = checked.result;
    Basic().analyze(result.basic, result.board);
    if (!Basic().checkValid(result.board, result.basic)) {
        checked.error = "基础逻辑冲突";
        return checked;
    }
    Structure().analyze(result.structure, result.board, result.basic, pool.structure);

    Workspace &workspace = getWorkspace();
    vector<ShapeSolver::DistributionId> &distributionIds = workspace.distributionIds;
    ShapeSolver::DistributionPool &distributions = probabilityPool.distributions;
    analyzeDistributions(result, distributions, distributionIds);
    const Probability::Result probability = Probability().analyze(distributionIds, result.board.totalMines - result.basic.mineSum, probabilityPool);
    probabilityPool.blockProbabilities.pop_back(probability.probabilities);
    probabilityPool.blocks.pop_back(probability.blocks);

    // Probability 遇到空分布会直接返回零，只在无解时区分连通块冲突和总雷数冲突。
    if (probability.totalWays == 0.0L) {
        checked.error = "连通块都有解，却与雷数冲突";
        for (std::size_t i = 0; i < result.structure.instanceIds.size; ++i) {
            if (distributions.list[distributionIds[i]].ways.count == 0) {
                checked.error = "某连通块无解";
                break;
            }
        }
    }
    return checked;
}

ObservedBoard::Delta Position::updateBoard(ObservedBoard::Result &board, const vector<ObservedBoard::Delta::Change> &changes, ObservedBoard::Pool &pool) {
    ObservedBoard::Delta observed{pool.changes.push_back()};
    for (int i = 0; i < (int)changes.size; ++i)
        pool.changes.push_back(observed.changes, changes[i]);
    ObservedBoard().applyDelta(board, observed, pool);
    return observed;
}

void Position::update(Result &result, const vector<ObservedBoard::Delta::Change> &changes, Pool &pool) {
    if (changes.size == 0) return;
    const ObservedBoard::Delta observed = updateBoard(result.board, changes, pool.board);
    const Basic::Delta basic = Basic().update(result.basic, result.board, observed, pool.basic);
    if (result.pendingBoard.changes.count == 0) {
        result.pendingBoard = observed;
        result.pendingBasic = basic;
    } else {
        result.pendingBoard.changes.count += observed.changes.count;
        result.pendingBasic.changes.count += basic.changes.count;
    }
    result.logicComponents.valid = false;
    result.probabilityVersion = 0;
}

void Position::updateStructure(Result &result, Pool &pool) {
    if (result.pendingBoard.changes.count == 0) return;
    const Structure::Delta structure = Structure().update(result.structure, result.board, result.basic, pool.structure,
                                                          result.pendingBoard, result.pendingBasic, pool.basic);
    pool.popDelta({result.pendingBoard, result.pendingBasic, structure});
    result.pendingBoard = {};
    result.pendingBasic = {};
}

Position::Delta Position::updateWithDelta(Result &result, const vector<ObservedBoard::Delta::Change> &changes, Pool &pool) {
    updateStructure(result, pool);
    const ObservedBoard::Delta observed = updateBoard(result.board, changes, pool.board);
    const Basic::Delta basic = Basic().update(result.basic, result.board, observed, pool.basic);
    const Structure::Delta structure = Structure().update(result.structure, result.board, result.basic, pool.structure, observed, basic, pool.basic);
    Delta delta{observed, basic, structure};
    if (observed.changes.count != 0) {
        delta.probabilityVersion = result.probabilityVersion;
        LogicComponentResult &components = result.logicComponents;
        if (components.valid) delta.logicComponentVersion = components.version;
        components.valid = false;
        result.probabilityVersion = 0;
    }
    return delta;
}

void Position::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    if (reverse) {
        Structure().applyDelta(result.structure, delta.structure, pool.structure, true);
        Basic().applyDelta(result.basic, delta.basic, pool.basic, true);
        ObservedBoard().applyDelta(result.board, delta.board, pool.board, true);
    } else {
        ObservedBoard().applyDelta(result.board, delta.board, pool.board);
        Basic().applyDelta(result.basic, delta.basic, pool.basic);
        Structure().applyDelta(result.structure, delta.structure, pool.structure);
    }
    if (delta.board.changes.count != 0) {
        LogicComponentResult &components = result.logicComponents;
        // 临时变更期间没有覆盖原结果时，撤回可以继续使用它。
        components.valid = reverse && delta.logicComponentVersion != 0 && components.version == delta.logicComponentVersion;
        result.probabilityVersion = reverse ? delta.probabilityVersion : 0;
    }
}

void Position::analyzeDistributions(const Result &result, ShapeSolver::DistributionPool &pool, vector<ShapeSolver::DistributionId> &distributionIds) {
    const Structure::Result &structure = result.structure;
    const Structure::Pool::Instances &instances = pool.shapes.instances;
    const Basic::Result &basic = result.basic;
    distributionIds.resize(structure.instanceIds.size + (basic.unknownSum != 0));
    for (int i = 0; i < (int)structure.instanceIds.size; ++i) {
        const Structure::Instance &instance = instances.list[structure.instanceIds[i]];
        distributionIds[i] = ShapeSolver().analyze(instance, result.config_.shapeSolver, pool);
    }
    if (basic.unknownSum != 0)
        distributionIds[structure.instanceIds.size] = ShapeSolver().analyzeFreeCells(basic.unknownSum, pool);
}

Position::ProbabilityResult Position::analyzeProbability(Result &result, Pool &pool) {
    if (result.probabilityVersion == 0 || result.probabilityVersion != pool.probabilityVersion) {
        pool.probabilities.clear();
        pool.cachedProbability = analyzeProbability(result, pool, pool.probabilities);
        result.probabilityVersion = ++pool.probabilityVersion;
    }
    return pool.cachedProbability;
}

Position::ProbabilityResult Position::analyzeProbability(Result &result, Pool &pool, Probability::Pool &probabilities) {
    updateStructure(result, pool);
    Workspace &workspace = getWorkspace();
    vector<ShapeSolver::DistributionId> &distributionIds = workspace.distributionIds;
    analyzeDistributions(result, probabilities.distributions, distributionIds);
    const Probability::Result probability = Probability().analyze(distributionIds, result.board.totalMines - result.basic.mineSum, probabilities);
    const Probability::BoxResult boxes = Probability().projectBoxes(probability, distributionIds, result.structure, probabilities);
    probabilities.blockProbabilities.pop_back(probability.probabilities);
    probabilities.blocks.pop_back(probability.blocks);
    return {boxes, probability.totalWays};
}

long double Position::countWays(Result &result, Pool &pool) {
    updateStructure(result, pool);
    Workspace &workspace = getWorkspace();
    vector<ShapeSolver::DistributionId> &distributionIds = workspace.distributionIds;
    analyzeDistributions(result, pool.distributions, distributionIds);
    return Probability().countWays(distributionIds, result.board.totalMines - result.basic.mineSum, pool.probabilities);
}

void Position::observe(const Result &position, const ProbabilityResult &probability, const Probability::Pool &probabilities,
                       ObservedBoard::CellId cell, Observe::Result &result) {
    Observe().observe(position.board, position.basic, position.structure, probability.boxes, probability.totalWays,
                      probabilities, position.config_.shapeSolver, cell, result);
}

const Position::LogicComponentResult &Position::analyzeLogicComponents(Result &result, Pool &pool) {
    updateStructure(result, pool);
    LogicComponentResult &components = result.logicComponents;
    if (!components.valid) {
        LogicComponent().analyze(components.result, result.board, result.basic, result.structure, pool.structure.instances, components.pool);
        components.version = ++pool.logicComponentVersion;
        components.valid = true;
    }
    return components;
}

} // 命名空间 mss
