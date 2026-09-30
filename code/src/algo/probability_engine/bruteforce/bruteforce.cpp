#include "algo/probability_engine/bruteforce/bruteforce.h"

#include <algorithm>

#include "algo/probability_engine/shape_solver/dfs_solver.h"
#include "core/utility/neighbors.h"

namespace mss {

BruteForce::Workspace::Workspace(ShapeSolver::Workspace &shapeSolver) : shapeSolver(shapeSolver) {}

BruteForce::Workspace::Normal::Layer &BruteForce::Workspace::Normal::layer(int depth) {
    if ((int)layers.size() <= depth)
        layers.emplace_back();
    return layers[depth];
}

void BruteForce::Workspace::Normal::reset() {
    for (Layer &layer : layers) {
        layer.deaths.clear();
        layer.safeCells.clear();
        layer.order.clear();
        for (std::vector<ConfigId> &group : layer.groups)
            group.clear();
        layer.groupList.clear();
        layer.safeHashes.clear();
        layer.safeGroupIds.clear();
        layer.safeGroupSizes.clear();
        layer.safeGroupOffsets.clear();
        layer.safeGroupedConfigs.clear();
        layer.safeGroupList.clear();
    }
    safeGroupTable.clear();
    cache.clear();
    nodes = 0;
}

BruteForce::Workspace::MultiMask::Layer &BruteForce::Workspace::MultiMask::Scratch::layer(int depth) {
    if ((int)layers.size() <= depth)
        layers.emplace_back();
    return layers[depth];
}

void BruteForce::Workspace::MultiMask::Scratch::reset() {
    for (Layer &layer : layers) {
        layer.deaths.clear();
        layer.order.clear();
        for (std::vector<ConfigId> &group : layer.groups)
            group.clear();
        layer.groupList.clear();
        layer.safeGroupIds.clear();
        layer.safeGroupSizes.clear();
        layer.safeGroupOffsets.clear();
        layer.safeGroupedConfigs.clear();
        layer.safeGroupList.clear();
    }
    safeGroupTable.clear();
    smallGroupKeys.clear();
    smallGroupConfigKeys.clear();
}

U128 BruteForce::hashConfigs(const std::vector<ConfigId> &configs, int start, int count) {
    // 递归分组会改变 ConfigId 顺序，缓存必须把它们视为无序集合。
    // 同一组方案会因揭示分组而改变排列；缓存键只依赖方案集合，不依赖排列。
    U128 hash{(std::uint64_t)count, (std::uint64_t)count};
    for (int i = start; i < start + count; ++i) {
        const std::uint64_t value = splitmix64(configs[i]);
        hash.lo += value;
        hash.hi ^= value;
    }
    hash.lo = splitmix64(hash.lo);
    return hash;
}

void BruteForce::saveFail(const U128 &key, int upper, int count, FlatHashTable<U128, int, U128Hash> &table) {
    // 负值编码“最多可能赢 upper 局”；等于方案总数的上界没有剪枝价值，不写入缓存。
    if (upper <= 0 || upper >= count)
        return;
    int *old = table.find(key);
    if (old == nullptr) {
        table[key] = -upper;
        return;
    }
    if (*old < 0)
        *old = (std::max)(*old, -upper);
}



void BruteForce::buildCommonSession(CommonSession &session, const ObservedBoard::Result &board, const Basic::Result &basic,
                                    const Structure::Result &structure, const Structure::Pool &shapes, Workspace &workspace) {
    // 全盘模式只把隐藏 H/T 格作为候选；Basic 已标出的 F/S 和已观测数字由邻接计数处理。
    session.candidateCount = 0;
    session.possibilityCount = 0;
    session.candidates.clear();
    session.links.clear();
    Workspace::Build &build = workspace.build;
    const int rows = board.board.rows();
    const int cols = board.board.cols();
    build.candidateAt.assign(rows * cols, -1);
    build.tCells.clear();
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            if (board.board[row][col] != ObservedBoard::CellState::Hidden)
                continue;
            const Basic::Mark mark = basic.marks[row][col];
            if (mark != Basic::Mark::H && mark != Basic::Mark::T)
                continue;
            const ObservedBoard::CellId cell = board.id(row, col);
            build.candidateAt[cell] = session.candidateCount++;
            session.candidates.push_back({cell, 0, 0, 0});
            if (mark == Basic::Mark::T)
                build.tCells.push_back(build.candidateAt[cell]);
        }
    populateCommonSession(session, board, basic, shapes, structure.components, workspace, board.totalMines - basic.mineSum);
}

void BruteForce::buildComponentCommonSession(CommonSession &session, const ObservedBoard::Result &board,
                                             const Basic::Result &basic,
                                             const std::vector<Structure::InstanceId> &components,
                                             const std::vector<ObservedBoard::CellId> &offFrontierCells,
                                             const Structure::Pool &shapes, int mines, Workspace &workspace) {
    // 组件 H 格参与形状约束，局外格只参与固定总雷数的组合枚举。
    session.candidateCount = 0;
    session.possibilityCount = 0;
    session.candidates.clear();
    session.links.clear();
    Workspace::Build &build = workspace.build;
    build.candidateAt.assign(board.board.rows() * board.board.cols(), -1);
    build.tCells.clear();
    for (Structure::InstanceId component : components) {
        const Structure::Instance &instance = shapes.getInstance(component);
        for (ObservedBoard::CellId cell : instance.boxes.cells.span(shapes.cells)) {
            build.candidateAt[cell] = session.candidateCount++;
            session.candidates.push_back({cell, 0, 0, 0});
        }
    }
    for (ObservedBoard::CellId cell : offFrontierCells) {
        build.candidateAt[cell] = session.candidateCount++;
        build.tCells.push_back(build.candidateAt[cell]);
        session.candidates.push_back({cell, 0, 0, 0});
    }
    populateCommonSession(session, board, basic, shapes, components, workspace, mines);
}

void BruteForce::populateCommonSession(CommonSession &session, const ObservedBoard::Result &board, const Basic::Result &basic,
                                       const Structure::Pool &shapes,
                                       const std::vector<Structure::InstanceId> &components, Workspace &workspace, int mines) {
    Workspace::Build &build = workspace.build;
    // links 只列候选邻格；确定为雷的 F 邻格提前写入 fixedMines，搜索表中的揭示值因此仍为 0~8。
    for (int candidate = 0; candidate < session.candidateCount; ++candidate) {
        CommonSession::Candidate &current = session.candidates[candidate];
        current.linksOffset = (std::uint32_t)session.links.size();
        const std::pair<int, int> position = board.pos(current.cell);
        forEachAdjacent(position.first, position.second, board.board.rows(), board.board.cols(), [&](int row, int col) {
            if (basic.marks[row][col] == Basic::Mark::F)
                ++current.fixedMines;
            const int linked = build.candidateAt[board.id(row, col)];
            if (linked >= 0)
                session.links.push_back(linked);
        });
        current.linksCount = (std::uint8_t)(session.links.size() - current.linksOffset);
    }

    build.assignmentOffsets.assign(components.size() + 1, 0);
    build.assignmentCounts.assign(components.size(), 0);
    build.assignments.clear();
    // DFS 只给出每个 Box 的雷数；后续还要从 Box 的真实格子中枚举组合，才能得到每种具体布局。
    for (int component = 0; component < (int)components.size(); ++component) {
        build.assignmentOffsets[component] = build.assignments.size();
        const Structure::Instance &instance = shapes.getInstance(components[component]);
        const Structure::Shape &shape = shapes.getShape(instance.shape);
        const int boxCount = shape.boxes.size;
        ShapeSolver::DfsSolver::forEachAssignment(shape, shapes, workspace.shapeSolver,
            [&](const std::vector<char> &assignment, long double) {
                for (int box = 0; box < boxCount; ++box)
                    build.assignments.push_back(assignment[box]);
                ++build.assignmentCounts[component];
            });
    }
    build.assignmentOffsets[components.size()] = build.assignments.size();

    session.minesPerConfig = mines;
    session.possibilityCount = 0;
    build.placed.clear();
    // 先数方案再分配二维表，避免逐个布局扩容；第二遍把同一批具体雷位写入连续行。
    enumerateComponents(session, shapes, components, workspace, 0, 0, false);
    session.mineCandidateIds.resize(session.possibilityCount, mines, 0);
    session.possibilityCount = 0;
    enumerateComponents(session, shapes, components, workspace, 0, 0, true);
}

void BruteForce::enumerateComponents(CommonSession &session, const Structure::Pool &shapes,
                                     const std::vector<Structure::InstanceId> &components, Workspace &workspace,
                                     int component, int usedMines, bool writeConfigs) {
    Workspace::Build &build = workspace.build;
    const int componentCount = components.size();
    // 对每个组件逐个尝试 DFS 赋值，再展开 Box 内具体格子；只保留不超过固定雷数的部分方案。
    if (component == componentCount) {
        const int remaining = session.minesPerConfig - usedMines;
        if (remaining >= 0 && remaining <= (int)build.tCells.size())
            enumerateTCells(session, workspace, 0, remaining, writeConfigs);
        return;
    }
    const Structure::Instance &instance = shapes.getInstance(components[component]);
    const Structure::Shape &shape = shapes.getShape(instance.shape);
    const int boxCount = shape.boxes.size;
    const int assignmentOffset = build.assignmentOffsets[component];
    for (int assignment = 0; assignment < build.assignmentCounts[component]; ++assignment) {
        const int assignmentStart = assignmentOffset + assignment * boxCount;
        int componentMines = 0;
        for (int box = 0; box < boxCount; ++box)
            componentMines += build.assignments[assignmentStart + box];
        if (usedMines + componentMines > session.minesPerConfig)
            continue;
        if (boxCount == 0)
            enumerateComponents(session, shapes, components, workspace, component + 1, usedMines, writeConfigs);
        else
            enumerateBoxCells(session, shapes, components, workspace, component, assignmentStart, 0, 0,
                              build.assignments[assignmentStart], componentMines, usedMines, writeConfigs);
    }
}

void BruteForce::enumerateBoxCells(CommonSession &session, const Structure::Pool &shapes,
                                   const std::vector<Structure::InstanceId> &components, Workspace &workspace,
                                   int component, int assignmentStart, int box, int cellStart, int remaining,
                                   int componentMines, int usedMines, bool writeConfigs) {
    // assignment 给出当前 Box 应放几颗雷；这里逐个选择该 Box 内的实际格子，保留布局枚举而非组合权重。
    Workspace::Build &build = workspace.build;
    const Structure::Instance &instance = shapes.getInstance(components[component]);
    const int boxCount = instance.boxes.count();
    const std::span<const int> boxOffsets = instance.boxes.boxOf.span(shapes.boxOf);
    if (remaining == 0) {
        if (box + 1 == boxCount) {
            enumerateComponents(session, shapes, components, workspace, component + 1, usedMines + componentMines, writeConfigs);
            return;
        }
        enumerateBoxCells(session, shapes, components, workspace, component, assignmentStart, box + 1, 0,
                          build.assignments[assignmentStart + box + 1], componentMines, usedMines, writeConfigs);
        return;
    }
    const int first = boxOffsets[box];
    const int cellCount = boxOffsets[box + 1] - first;
    const std::span<const ObservedBoard::CellId> cells = instance.boxes.cells.span(shapes.cells);
    for (int i = cellStart; i <= cellCount - remaining; ++i) {
        build.placed.push_back((CandidateId)build.candidateAt[cells[first + i]]);
        enumerateBoxCells(session, shapes, components, workspace, component, assignmentStart, box, i + 1, remaining - 1,
                          componentMines, usedMines, writeConfigs);
        build.placed.pop_back();
    }
}

void BruteForce::enumerateTCells(CommonSession &session, Workspace &workspace, int start, int remaining, bool writeConfigs) {
    Workspace::Build &build = workspace.build;
    // 局外格按递增下标组合选择，避免同一组雷位因排列不同而重复生成。
    if (remaining == 0) {
        writeConfig(session, workspace, writeConfigs);
        return;
    }
    for (int i = start; i <= (int)build.tCells.size() - remaining; ++i) {
        build.placed.push_back(build.tCells[i]);
        enumerateTCells(session, workspace, i + 1, remaining - 1, writeConfigs);
        build.placed.pop_back();
    }
}

void BruteForce::writeConfig(CommonSession &session, Workspace &workspace, bool writeConfigs) {
    // 计数遍只增加方案数；填表遍把 placed 的雷位写入当前方案行，二者走同一枚举顺序。
    if (writeConfigs) {
        for (int i = 0; i < session.minesPerConfig; ++i)
            session.mineCandidateIds[session.possibilityCount][i] = workspace.build.placed[i];
    }
    ++session.possibilityCount;
}

BruteForce::Result BruteForce::solve(const ObservedBoard::Result &board, const Basic::Result &basic,
                                     const Structure::Result &structure, const Structure::Pool &shapes,
                                     const Config &options, Workspace &workspace) {
    buildCommonSession(workspace.commonSession, board, basic, structure, shapes, workspace);
    return solveCommonSession(workspace.commonSession, options, workspace);
}

BruteForce::Result BruteForce::solveComponent(const ObservedBoard::Result &board, const Basic::Result &basic,
                                              const std::vector<Structure::InstanceId> &components,
                                              const std::vector<ObservedBoard::CellId> &offFrontierCells,
                                              const Structure::Pool &shapes, int mines, const Config &options,
                                              Workspace &workspace) {
    buildComponentCommonSession(workspace.commonSession, board, basic, components, offFrontierCells, shapes, mines, workspace);
    return solveCommonSession(workspace.commonSession, options, workspace);
}

} // 命名空间 mss
