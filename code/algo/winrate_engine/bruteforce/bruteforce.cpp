#include "algo/winrate_engine/bruteforce/bruteforce.h"

#include <algorithm>
#include <cstddef>

#include "algo/probability_engine/shape_solver/dfs_solver.h"
#include "algo/winrate_engine/bruteforce/bruteforce_multimask.h"
#include "algo/winrate_engine/bruteforce/bruteforce_normal.h"
#include "core/algorithm/neighbors.h"
#include "core/algorithm/rng.h"
#include "core/datastructure/bit_mask.h"

namespace mss {

// 构建过程复用的临时数组；每次构建覆写有效范围，Session 不借用它们。
struct BruteForce::Build {
    vector<Structure::InstanceId> instanceIds;
    vector<ObservedBoard::CellId> candidateCells;
    vector<int> candidateAt; // 棋盘 CellId → 候选下标，-1 表示不在本次候选里。
    vector<BruteForceType::CandidateId> tCells; // 待分配雷数的非前沿格。
    vector<BruteForceType::CandidateId> placed; // 当前正在拼接的完整雷盘中的雷位。
    // 与 instanceIds 对齐，记录各实例的 Box 赋值起点、数量和连续赋值。
    vector<int> assignmentOffsets;
    vector<int> assignmentCounts;
    vector<char> assignments;
};

// Session 由 solve 局部持有并显式传给后端；数组借用本线程工作区，仅在本次 solve 内有效。
struct BruteForce::Workspace {
    Build buildStorage;
    vector<BruteForceType::Session::Candidate> candidates;
    vector<BruteForceType::CandidateId> links;
    Grid<BruteForceType::CandidateId> mineCandidateIds;
    vector<std::uint64_t> configHashes;
};

__declspec(noinline) BruteForce::Workspace &BruteForce::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void BruteForce::writeConfig(BruteForceType::Session &session, Build &build, bool writeConfigs) {
    if (writeConfigs)
        for (int i = 0; i < session.minesPerConfig; ++i)
            session.mineCandidateIds[session.possibilityCount][i] = build.placed.data[i];
    ++session.possibilityCount;
}

void BruteForce::enumerateTCells(BruteForceType::Session &session, Build &build, int start, int remaining, bool writeConfigs) {
    if (remaining == 0) {
        writeConfig(session, build, writeConfigs);
        return;
    }
    for (int i = start; i <= (int)build.tCells.size - remaining; ++i) {
        build.placed.push_back(build.tCells.data[i]);
        enumerateTCells(session, build, i + 1, remaining - 1, writeConfigs);
        build.placed.size -= 1;
    }
}


void BruteForce::enumerateBoxCells(BruteForceType::Session &session, Build &build, const Structure::Pool::Instances &instances, int instanceIndex, int assignmentStart, int box, int cellStart, int remaining, int instanceMines, int usedMines, bool writeConfigs) {
    const Structure::Instance &instance = instances.list.data[build.instanceIds.data[instanceIndex]];
    const int boxCount = (int)instance.boxes.count;
    if (remaining == 0) {
        if (box + 1 == boxCount) {
            enumerateInstances(session, build, instances, instanceIndex + 1, usedMines + instanceMines, writeConfigs);
            return;
        }
        enumerateBoxCells(session, build, instances, instanceIndex, assignmentStart, box + 1, 0, build.assignments.data[assignmentStart + box + 1], instanceMines, usedMines, writeConfigs);
        return;
    }
    const vectorPool<Structure::CellId>::vector cells = instances.boxes.data.data[instance.boxes.offset + box];
    const int cellCount = (int)cells.count;
    for (int i = cellStart; i <= cellCount - remaining; ++i) {
        const Structure::CellId cell = instances.cells.data.data[cells.offset + i];
        build.placed.push_back((BruteForceType::CandidateId)build.candidateAt.data[cell]);
        enumerateBoxCells(session, build, instances, instanceIndex, assignmentStart, box, i + 1, remaining - 1, instanceMines, usedMines, writeConfigs);
        build.placed.size -= 1;
    }
}

void BruteForce::enumerateInstances(BruteForceType::Session &session, Build &build, const Structure::Pool::Instances &instances, int instanceIndex, int usedMines, bool writeConfigs) {
    if (instanceIndex == (int)build.instanceIds.size) {
        const int remaining = session.minesPerConfig - usedMines;
        if (remaining >= 0 && remaining <= (int)build.tCells.size)
            enumerateTCells(session, build, 0, remaining, writeConfigs);
        return;
    }
    const Structure::Instance &instance = instances.list.data[build.instanceIds.data[instanceIndex]];
    const int boxCount = (int)instance.boxes.count;
    const int assignmentOffset = build.assignmentOffsets.data[instanceIndex];
    for (int assignment = 0; assignment < build.assignmentCounts.data[instanceIndex]; ++assignment) {
        const int assignmentStart = assignmentOffset + assignment * boxCount;
        int instanceMines = 0;
        for (int box = 0; box < boxCount; ++box)
            instanceMines += build.assignments.data[assignmentStart + box];
        if (usedMines + instanceMines > session.minesPerConfig)
            continue;
        enumerateBoxCells(session, build, instances, instanceIndex, assignmentStart, 0, 0, build.assignments.data[assignmentStart], instanceMines, usedMines, writeConfigs);
    }
}

void BruteForce::buildSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic,
                              const vector<LogicComponent::Result::Component> &components, const LogicComponent::Pool &componentPool,
                              const Structure::Pool &shapes) {
    const Structure::Pool::Instances &instances = shapes.instances;
    build.instanceIds.resize(0);
    build.candidateCells.resize(0);
    for (std::size_t i = 0; i < components.size; ++i) {
        const LogicComponent::Result::Component &component = components.data[i];
        for (Structure::InstanceId id : componentPool.instanceIds.span(component.instanceIds)) {
            build.instanceIds.push_back(id);
            const Structure::Instance &instance = instances.list.data[id];
            for (const vectorPool<Structure::CellId>::vector &box : instances.boxes.span(instance.boxes))
                for (Structure::CellId cell : instances.cells.span(box))
                    build.candidateCells.push_back(cell);
        }
        for (ObservedBoard::CellId cell : componentPool.tCells.span(component.tCells))
            build.candidateCells.push_back(cell);
    }

    // 按原盘格号排序，沿用整盘求解的候选编号顺序。
    if (build.candidateCells.size > 1)
        std::sort(build.candidateCells.data, build.candidateCells.data + build.candidateCells.size);
    session.candidateCount = (int)build.candidateCells.size;
    session.candidates.resize(0);
    session.links.resize(0);
    build.candidateAt.resize((std::size_t)board.cells.rows() * board.cells.cols());
    std::fill_n(build.candidateAt.data, build.candidateAt.size, -1);
    build.tCells.resize(0);
    const Basic::Mark *marks = basic.marks[0];
    for (int candidate = 0; candidate < session.candidateCount; ++candidate) {
        const ObservedBoard::CellId cell = build.candidateCells.data[candidate];
        build.candidateAt.data[cell] = candidate;
        if (marks[cell] == Basic::Mark::T)
            build.tCells.push_back((BruteForceType::CandidateId)candidate);
    }

    // links 只列候选邻格；F 邻格提前写入 fixedMines，搜索表中的揭示值仍是 0~8。
    for (int candidate = 0; candidate < session.candidateCount; ++candidate) {
        const ObservedBoard::CellId cell = build.candidateCells.data[candidate];
        const std::uint32_t linksOffset = (std::uint32_t)session.links.size;
        std::uint8_t fixedMines = 0;
        const std::pair<int, int> position = board.pos(cell);
        forEachAdjacent(position.first, position.second, board.cells.rows(), board.cells.cols(), [&](int row, int col) {
            if (basic.marks[row][col] == Basic::Mark::F)
                ++fixedMines;
            const int linked = build.candidateAt.data[board.id(row, col)];
            if (linked >= 0)
                session.links.push_back((BruteForceType::CandidateId)linked);
        });
        session.candidates.push_back({cell, linksOffset, (std::uint8_t)(session.links.size - linksOffset), fixedMines});
    }

    build.assignmentOffsets.resize(build.instanceIds.size);
    build.assignmentCounts.resize(build.instanceIds.size);
    build.assignments.resize(0);
    for (std::size_t i = 0; i < build.instanceIds.size; ++i) {
        build.assignmentOffsets.data[i] = (int)build.assignments.size;
        build.assignmentCounts.data[i] = 0;
        const Structure::Instance &instance = instances.list.data[build.instanceIds.data[i]];
        const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
        const ShapeSolverType::Boxes boxes(instances.boxes.data.data + instance.boxes.offset, (int)instance.boxes.count);
        ShapeSolverDfs().forEachAssignment(shape, shapes.shapes, boxes, [&build, i](const vector<char> &assignment, long double) {
            for (int box = 0; box < (int)assignment.size; ++box)
                build.assignments.push_back(assignment.data[box]);
            ++build.assignmentCounts.data[i];
        });
    }
    session.possibilityCount = 0;
    build.placed.resize(0);
    // 先数方案，再分配二维表；两遍枚举顺序一致。
    enumerateInstances(session, build, instances, 0, 0, false);
    session.mineCandidateIds.resize(session.possibilityCount, session.minesPerConfig, 0);
    session.possibilityCount = 0;
    enumerateInstances(session, build, instances, 0, 0, true);
    session.configHashes.resize(session.possibilityCount);
    for (int config = 0; config < session.possibilityCount; ++config)
        session.configHashes.data[config] = splitmix64(config);
}

void BruteForce::runBackend(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result) {
    if (session.possibilityCount <= 1 || session.candidateCount > bitMask<8>::kBitCount)
        BruteForceNormal().solve(session, config, result);
    else
        BruteForceMultimask().solve(session, config, result);
}

void BruteForce::writeResult(const BruteForceType::Result &raw, int count, Result &result) {
    result.suggest.resize(0); result.candidates.resize(0);
    result.engine = SuggestResult::Engine::BruteForce; result.reason = SuggestResult::Reason::ScoreWins; result.nodes = raw.nodes;
    for (int i = 0; i < (int)raw.moves.size; ++i) {
        const BruteForceType::Result::Move &move = raw.moves.data[i];
        if (move.cell >= 0) result.candidates.push_back(SuggestResult::Candidate(move.cell, (long double)move.wins / count));
    }
    if (result.candidates.size == 0) result.reason = SuggestResult::Reason::Finished;
    result.recommend();
}

void BruteForce::solve(const ObservedBoard::Result &board, const Basic::Result &basic,
                       const vector<LogicComponent::Result::Component> &components, const LogicComponent::Pool &componentPool,
                       const Structure::Pool &shapes, int mines, const Config &config, Result &result) {
    Workspace &workspace = getWorkspace();
    Build &build = workspace.buildStorage;
    vector<BruteForceType::Session::Candidate> &candidates = workspace.candidates;
    vector<BruteForceType::CandidateId> &links = workspace.links;
    Grid<BruteForceType::CandidateId> &mineCandidateIds = workspace.mineCandidateIds;
    vector<std::uint64_t> &configHashes = workspace.configHashes;
    BruteForceType::Session session(candidates, links, mineCandidateIds, configHashes, mines);
    buildSession(session, build, board, basic, components, componentPool, shapes);
    BruteForceType::Result raw;
    runBackend(session, config, raw);
    writeResult(raw, session.possibilityCount, result);
}

} // 命名空间 mss
