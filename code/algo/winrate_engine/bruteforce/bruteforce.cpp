#include "algo/winrate_engine/bruteforce/bruteforce.h"

#include <algorithm>
#include <cstdio>
#include <functional>

#include "core/algorithm/neighbors.h"
#include "core/algorithm/rng.h"
#include "core/datastructure/bit_mask.h"

namespace mss {

using BruteForceSessionBuild = BruteForce::Build;

void BruteForce::Workspace::swapSessionStorage(BruteForceType::Session &session) {
    candidates.swap(session.candidates);
    links.swap(session.links);
    mineCandidateIds.swap(session.mineCandidateIds);
    configHashes.swap(session.configHashes);
}

static void writeConfig(BruteForceType::Session &session, BruteForceSessionBuild &build, bool writeConfigs) {
    if (writeConfigs)
        for (int i = 0; i < session.minesPerConfig; ++i)
            session.mineCandidateIds[session.possibilityCount][i] = build.placed.data[i];
    ++session.possibilityCount;
}

static void enumerateTCells(BruteForceType::Session &session, BruteForceSessionBuild &build, int start, int remaining, bool writeConfigs) {
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

static void enumerateComponents(BruteForceType::Session &session, BruteForceSessionBuild &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int usedMines, bool writeConfigs);

static void enumerateBoxCells(BruteForceType::Session &session, BruteForceSessionBuild &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int assignmentStart, int box, int cellStart, int remaining, int componentMines, int usedMines, bool writeConfigs) {
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

static void enumerateComponents(BruteForceType::Session &session, BruteForceSessionBuild &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int usedMines, bool writeConfigs) {
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

static void populateSession(BruteForceType::Session &session, BruteForceSessionBuild &build, ShapeSolver::Workspace &shapeSolver, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int mines) {
    // links 只列候选邻格；F 邻格提前写入 fixedMines，搜索表中的揭示值仍是 0~8。
    for (int candidate = 0; candidate < session.candidateCount; ++candidate) {
        BruteForceType::Session::Candidate &current = session.candidates.data[candidate];
        current.linksOffset = (std::uint32_t)session.links.size;
        const std::pair<int, int> position = board.pos(current.cell);
        forEachAdjacent(position.first, position.second, board.cells.rows(), board.cells.cols(), [&](int row, int col) {
            if (basic.marks[row][col] == Basic::Mark::F)
                ++current.fixedMines;
            const int linked = build.candidateAt.data[board.id(row, col)];
            if (linked >= 0)
                session.links.push_back((BruteForceType::CandidateId)linked);
        });
        current.linksCount = (std::uint8_t)(session.links.size - current.linksOffset);
    }

    build.assignmentOffsets.resize((int)components.size + 1);
    build.assignmentCounts.resize((int)components.size);
    std::fill(build.assignmentCounts.data, build.assignmentCounts.data + build.assignmentCounts.size, 0);
    build.assignments.resize(0);
    for (int component = 0; component < components.size; ++component) {
        build.assignmentOffsets.data[component] = (int)build.assignments.size;
        const Structure::Instance &instance = shapes.instances.list.data[components[component]];
        const Structure::Shape &shape = shapes.shapes.list.data[instance.shape];
        ShapeSolverDfs::forEachAssignment(shape, shapes, shapeSolver.dfs, [&build, component](const vector<char> &assignment, long double) {
            for (int box = 0; box < (int)assignment.size; ++box)
                build.assignments.push_back(assignment.data[box]);
            ++build.assignmentCounts.data[component];
        });
    }
    build.assignmentOffsets.data[components.size] = (int)build.assignments.size;

    session.minesPerConfig = mines;
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

static void buildSession(BruteForceType::Session &session, BruteForceSessionBuild &build, ShapeSolver::Workspace &shapeSolver, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes) {
    session.candidateCount = 0;
    session.possibilityCount = 0;
    session.candidates.resize(0);
    session.links.resize(0);
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
            session.candidates.push_back({cell, 0, 0, 0});
            if (mark == Basic::Mark::T)
                build.tCells.push_back((BruteForceType::CandidateId)build.candidateAt.data[cell]);
        }
    populateSession(session, build, shapeSolver, board, basic, shapes, span<const Structure::InstanceId>(structure.components.data, (int)structure.components.size), board.totalMines - basic.mineSum);
}

static void buildComponentSession(BruteForceType::Session &session, BruteForceSessionBuild &build, ShapeSolver::Workspace &shapeSolver, const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines) {
    session.candidateCount = 0;
    session.possibilityCount = 0;
    session.candidates.resize(0);
    session.links.resize(0);
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
            session.candidates.push_back({cells[i], 0, 0, 0});
        }
    }
    for (int i = 0; i < offFrontierCells.size; ++i) {
        const ObservedBoard::CellId cell = offFrontierCells.data[i];
        build.candidateAt.data[cell] = session.candidateCount++;
        build.tCells.push_back((BruteForceType::CandidateId)build.candidateAt.data[cell]);
        session.candidates.push_back({cell, 0, 0, 0});
    }
    populateSession(session, build, shapeSolver, board, basic, shapes, components, mines);
}

static void runBackend(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result, BruteForce::Workspace &workspace) {
    if (session.possibilityCount <= 1 || session.candidateCount > bitMask<8>::kBitCount)
        BruteForceNormal::solve(session, config, result, workspace.normal);
    else
        BruteForceMultimask::solve(session, config, result, workspace.multimask);
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

void BruteForce::solve(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Config &config, Result &result, Workspace &workspace) {
    BruteForceSessionBuild &build = workspace.buildStorage;
    BruteForceType::Session session;
    workspace.swapSessionStorage(session);
    buildSession(session, build, workspace.shapeSolver, board, basic, structure, shapes);
    BruteForceType::Result raw;
    runBackend(session, config, raw, workspace);
    writeResult(raw, session.possibilityCount, result);
    workspace.swapSessionStorage(session);
}

void BruteForce::solveComponent(const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines, const Config &config, Result &result, Workspace &workspace) {
    BruteForceSessionBuild &build = workspace.buildStorage;
    BruteForceType::Session session;
    workspace.swapSessionStorage(session);
    buildComponentSession(session, build, workspace.shapeSolver, board, basic, components, offFrontierCells, shapes, mines);
    BruteForceType::Result raw;
    runBackend(session, config, raw, workspace);
    writeResult(raw, session.possibilityCount, result);
    workspace.swapSessionStorage(session);
}

} // 命名空间 mss
