#include "algo/winrate_engine/bruteforce/bruteforce.h"

#include <algorithm>
#include <cstdio>
#include <functional>

#include "core/algorithm/neighbors.h"
#include "core/algorithm/rng.h"
#include "core/datastructure/bit_mask.h"

namespace mss {

// 构建过程复用的临时数组；每次构建覆写有效范围，Session 不借用它们。
struct BruteForce::Build {
    vector<ObservedBoard::CellId> candidateCells;
    vector<int> candidateAt; // 棋盘 CellId → 候选下标，-1 表示不在本次候选里。
    vector<BruteForceType::CandidateId> tCells; // 待分配雷数的非前沿格。
    vector<BruteForceType::CandidateId> placed; // 当前正在拼接的完整雷盘中的雷位。
    // 各前沿块的布局起点、数量，以及展开后的格子赋值。
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

using BruteForceSessionBuild = BruteForce::Build;

void BruteForce::writeConfig(BruteForceType::Session &session, BruteForceSessionBuild &build, bool writeConfigs) {
    if (writeConfigs)
        for (int i = 0; i < session.minesPerConfig; ++i)
            session.mineCandidateIds[session.possibilityCount][i] = build.placed.data[i];
    ++session.possibilityCount;
}

void BruteForce::enumerateTCells(BruteForceType::Session &session, BruteForceSessionBuild &build, int start, int remaining, bool writeConfigs) {
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


void BruteForce::enumerateBoxCells(BruteForceType::Session &session, BruteForceSessionBuild &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int assignmentStart, int box, int cellStart, int remaining, int componentMines, int usedMines, bool writeConfigs) {
    const Structure::Instance &instance = shapes.instances.list.data[components[component]];
    const int *boxOf = shapes.instances.boxOf.data.data + instance.boxOf.offset;
    const Structure::CellId *cells = shapes.instances.cells.data.data + instance.cells.offset;
    const int boxCount = (int)instance.boxOf.count - 1;
    if (remaining == 0) {
        if (box + 1 == boxCount) {
            enumerateComponents(session, build, shapes, components, component + 1, usedMines + componentMines, writeConfigs);
            return;
        }
        enumerateBoxCells(session, build, shapes, components, component, assignmentStart, box + 1, 0, build.assignments.data[assignmentStart + box + 1], componentMines, usedMines, writeConfigs);
        return;
    }
    const int first = boxOf[box];
    const int cellCount = boxOf[box + 1] - first;
    for (int i = cellStart; i <= cellCount - remaining; ++i) {
        build.placed.push_back((BruteForceType::CandidateId)build.candidateAt.data[cells[first + i]]);
        enumerateBoxCells(session, build, shapes, components, component, assignmentStart, box, i + 1, remaining - 1, componentMines, usedMines, writeConfigs);
        build.placed.size -= 1;
    }
}

void BruteForce::enumerateComponents(BruteForceType::Session &session, BruteForceSessionBuild &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int usedMines, bool writeConfigs) {
    if (component == components.size) {
        const int remaining = session.minesPerConfig - usedMines;
        if (remaining >= 0 && remaining <= (int)build.tCells.size)
            enumerateTCells(session, build, 0, remaining, writeConfigs);
        return;
    }
    const Structure::Instance &instance = shapes.instances.list.data[components[component]];
    const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
    const int boxCount = (int)shape.boxSizes.count;
    const int assignmentOffset = build.assignmentOffsets.data[component];
    for (int assignment = 0; assignment < build.assignmentCounts.data[component]; ++assignment) {
        const int assignmentStart = assignmentOffset + assignment * boxCount;
        int componentMines = 0;
        for (int box = 0; box < boxCount; ++box)
            componentMines += build.assignments.data[assignmentStart + box];
        if (usedMines + componentMines > session.minesPerConfig)
            continue;
        if (boxCount == 0)
            enumerateComponents(session, build, shapes, components, component + 1, usedMines, writeConfigs);
        else
            enumerateBoxCells(session, build, shapes, components, component, assignmentStart, 0, 0, build.assignments.data[assignmentStart], componentMines, usedMines, writeConfigs);
    }
}

void BruteForce::populateSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int mines) {
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

    build.assignmentOffsets.resize((int)components.size + 1);
    build.assignmentCounts.resize((int)components.size);
    std::fill(build.assignmentCounts.data, build.assignmentCounts.data + build.assignmentCounts.size, 0);
    build.assignments.resize(0);
    for (int component = 0; component < components.size; ++component) {
        build.assignmentOffsets.data[component] = (int)build.assignments.size;
        const Structure::Instance &instance = shapes.instances.list.data[components[component]];
        const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
        ShapeSolverDfs().forEachAssignment(shape, shapes, [&build, component](const vector<char> &assignment, long double) {
            for (int box = 0; box < (int)assignment.size; ++box)
                build.assignments.push_back(assignment.data[box]);
            ++build.assignmentCounts.data[component];
        });
    }
    build.assignmentOffsets.data[components.size] = (int)build.assignments.size;

    session.possibilityCount = 0;
    build.placed.resize(0);
    // 先数方案，再分配二维表；两遍枚举顺序一致。
    enumerateComponents(session, build, shapes, components, 0, 0, false);
    session.mineCandidateIds.resize(session.possibilityCount, mines, 0);
    session.possibilityCount = 0;
    enumerateComponents(session, build, shapes, components, 0, 0, true);
    session.configHashes.resize(session.possibilityCount);
    for (int config = 0; config < session.possibilityCount; ++config)
        session.configHashes.data[config] = splitmix64(config);
}

void BruteForce::buildSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes) {
    session.candidateCount = 0;
    session.possibilityCount = 0;
    session.candidates.resize(0);
    session.links.resize(0);
    build.candidateCells.resize(0);
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    build.candidateAt.resize(rows * cols);
    std::fill(build.candidateAt.data, build.candidateAt.data + build.candidateAt.size, -1);
    build.tCells.resize(0);
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            if (board.cells[row][col] != ObservedBoard::CellState::Hidden)
                continue;
            const Basic::Mark mark = basic.marks[row][col];
            if (mark != Basic::Mark::H && mark != Basic::Mark::T)
                continue;
            const ObservedBoard::CellId cell = board.id(row, col);
            build.candidateAt.data[cell] = session.candidateCount++;
            build.candidateCells.push_back(cell);
            if (mark == Basic::Mark::T)
                build.tCells.push_back((BruteForceType::CandidateId)build.candidateAt.data[cell]);
        }
    populateSession(session, build, board, basic, shapes, span<const Structure::InstanceId>(structure.components.data, (int)structure.components.size), board.totalMines - basic.mineSum);
}

void BruteForce::buildComponentSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines) {
    session.candidateCount = 0;
    session.possibilityCount = 0;
    session.candidates.resize(0);
    session.links.resize(0);
    build.candidateCells.resize(0);
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();
    build.candidateAt.resize(rows * cols);
    std::fill(build.candidateAt.data, build.candidateAt.data + build.candidateAt.size, -1);
    build.tCells.resize(0);
    for (int component = 0; component < components.size; ++component) {
        const Structure::Instance &instance = shapes.instances.list.data[components[component]];
        const Structure::CellId *cells = shapes.instances.cells.data.data + instance.cells.offset;
        for (int i = 0; i < (int)instance.cells.count; ++i) {
            build.candidateAt.data[cells[i]] = session.candidateCount++;
            build.candidateCells.push_back(cells[i]);
        }
    }
    for (int i = 0; i < offFrontierCells.size; ++i) {
        const ObservedBoard::CellId cell = offFrontierCells.data[i];
        build.candidateAt.data[cell] = session.candidateCount++;
        build.tCells.push_back((BruteForceType::CandidateId)build.candidateAt.data[cell]);
        build.candidateCells.push_back(cell);
    }
    populateSession(session, build, board, basic, shapes, components, mines);
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

void BruteForce::solve(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Config &config, Result &result) {
    Workspace &workspace = getWorkspace();
    BruteForceSessionBuild &build = workspace.buildStorage;
    vector<BruteForceType::Session::Candidate> &candidates = workspace.candidates;
    vector<BruteForceType::CandidateId> &links = workspace.links;
    Grid<BruteForceType::CandidateId> &mineCandidateIds = workspace.mineCandidateIds;
    vector<std::uint64_t> &configHashes = workspace.configHashes;
    BruteForceType::Session session(candidates, links, mineCandidateIds, configHashes, board.totalMines - basic.mineSum);
    buildSession(session, build, board, basic, structure, shapes);
    BruteForceType::Result raw;
    runBackend(session, config, raw);
    writeResult(raw, session.possibilityCount, result);
}

void BruteForce::solveComponent(const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines, const Config &config, Result &result) {
    Workspace &workspace = getWorkspace();
    BruteForceSessionBuild &build = workspace.buildStorage;
    vector<BruteForceType::Session::Candidate> &candidates = workspace.candidates;
    vector<BruteForceType::CandidateId> &links = workspace.links;
    Grid<BruteForceType::CandidateId> &mineCandidateIds = workspace.mineCandidateIds;
    vector<std::uint64_t> &configHashes = workspace.configHashes;
    BruteForceType::Session session(candidates, links, mineCandidateIds, configHashes, mines);
    buildComponentSession(session, build, board, basic, components, offFrontierCells, shapes, mines);
    BruteForceType::Result raw;
    runBackend(session, config, raw);
    writeResult(raw, session.possibilityCount, result);
}

} // 命名空间 mss
