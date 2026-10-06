#include "algo/probability_engine/probability/observe.h"

#include <span>
#include <utility>

#include "core/utility/neighbors.h"

namespace mss {

Observe::Result Observe::observe(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Probability::Result &probability, ShapeSolver::DistributionPool &distributions, ObservedBoard::CellId cell, Probability::Workspace &workspace, const ShapeSolver::OrderAlgo &algo) {
    Probability::Workspace::Observe &ws = workspace.observe;
    Observe::Result result;
    const std::pair<int, int> position = board.pos(cell);
    const int row = position.first;
    const int col = position.second;
    const Basic::Mark mark = basic.marks[row][col];
    const int totalMines = board.totalMines - basic.mineSum;
    result.probability[9] = probability.mineProbability(cell, board, basic, structure);
    if (mark == Basic::Mark::F)
        return result;

    const Structure::Result::CellLocation clickedLocation = structure.cellLoc[cell];
    const bool clickedT = mark == Basic::Mark::T;
    const Structure::ComponentId clickedComponent = clickedLocation.component;
    const Structure::BoxId clickedBox = clickedLocation.box;
    int fixedMines = 0;
    int tNeighbors = 0;
    ws.captured.clear();
    ws.seen.assign(structure.components.size(), 0);
    // 固定雷只平移数字下标；只捕获邻接组件，所有 T 邻格稍后按组合数合并。
    forEachAdjacent(row, col, board.board.rows(), board.board.cols(), [&](int neighborRow, int neighborCol) {
        const Basic::Mark neighborMark = basic.marks[neighborRow][neighborCol];
        if (neighborMark == Basic::Mark::F) {
            ++fixedMines;
            return;
        }
        if (neighborMark == Basic::Mark::T) {
            ++tNeighbors;
            return;
        }
        const Structure::ComponentId component = structure.cellLoc[board.id(neighborRow, neighborCol)].component;
        if (component < 0 || ws.seen[component])
            return;
        ws.seen[component] = 1;
        ws.captured.push_back(component);
    });

    int maxCapturedMines = tNeighbors;
    for (const Structure::ComponentId component : ws.captured) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const Structure::Shape &shape = shapes.get(instance.shape);
        for (int box = 0; box < shape.boxes.size; ++box)
            maxCapturedMines += shape.boxes.span(shapes.boxes)[box].size;
    }
    const int stride = maxCapturedMines + 1;
    ws.dp.resize(9, stride, 0.0L);
    ws.dp[0][0] = 1.0L;
    // ShapeSolver::observe 给出点击安全时的“邻居雷数—组件雷数”联合 ways；逐块卷积到工作区 DP。
    ShapeSolver::ObversedResult &observed = ws.observed;
    for (const Structure::ComponentId component : ws.captured) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const Structure::Shape &shape = shapes.get(instance.shape);
        ws.adjacentBoxCells.assign(shape.boxes.size, 0);
        const std::span<const ObservedBoard::CellId> cells = instance.boxes.cells.span(shapes.cells);
        const std::span<const int> boxOf = instance.boxes.boxOf.span(shapes.boxOf);
        for (int box = 0; box < shape.boxes.size; ++box)
            for (int i = boxOf[box]; i < boxOf[box + 1]; ++i) {
                const std::pair<int, int> boxPosition = board.pos(cells[i]);
                const int rowDistance = boxPosition.first - row;
                const int colDistance = boxPosition.second - col;
                if (rowDistance >= -1 && rowDistance <= 1 && colDistance >= -1 && colDistance <= 1 && (rowDistance != 0 || colDistance != 0))
                    ++ws.adjacentBoxCells[box];
            }

        const Structure::BoxId xBox = component == clickedComponent ? clickedBox : -1;
        ShapeSolver::observe(shape, shapes, ws.adjacentBoxCells, xBox, observed, workspace.shapeSolver);
        ws.nextDp.resize(9, stride, 0.0L);
        for (int currentNeighborMines = 0; currentNeighborMines <= 8; ++currentNeighborMines)
            for (int addedNeighborMines = 0; currentNeighborMines + addedNeighborMines <= 8; ++addedNeighborMines) {
                const ShapeSolver::ObversedResult::View distribution = observed.distribution(addedNeighborMines);
                for (int capturedMines = 0; capturedMines <= maxCapturedMines; ++capturedMines) {
                    const long double base = ws.dp[currentNeighborMines][capturedMines];
                    if (base == 0.0L)
                        continue;
                    for (int i = 0; i < (int)distribution.ways.size(); ++i)
                        ws.nextDp[currentNeighborMines + addedNeighborMines][capturedMines + distribution.start + i] += base * distribution.ways[i];
                }
            }
        std::swap(ws.dp, ws.nextDp);
    }

    // 邻接 T 格彼此独立，组合数同时增加邻居雷数和捕获区域雷数。
    if (tNeighbors > 0) {
        ws.nextDp.resize(9, stride, 0.0L);
        for (int neighborMines = 0; neighborMines <= 8; ++neighborMines)
            for (int capturedMines = 0; capturedMines <= maxCapturedMines; ++capturedMines) {
                const long double base = ws.dp[neighborMines][capturedMines];
                if (base == 0.0L)
                    continue;
                for (int tMines = 0; tMines <= tNeighbors && neighborMines + tMines <= 8; ++tMines)
                    ws.nextDp[neighborMines + tMines][capturedMines + tMines] += base * Combination::chooseSmall(tNeighbors, tMines);
            }
        std::swap(ws.dp, ws.nextDp);
    }

    // 剩余 T 格与未捕获组件只影响总雷数，不影响点开数字；将其合成 restWays。
    const int tPool = basic.unknownSum - tNeighbors - (clickedT ? 1 : 0);
    int restStart = 0;
    ws.restCoefficients.assign(1, 1.0L);
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        if (ws.seen[component])
            continue;
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        const ShapeSolver::DistributionId id = ShapeSolver::analyze(shapes.get(instance.shape), shapes, distributions, workspace.shapeSolver, algo);
        const ShapeSolver::DistributionResult &distribution = distributions.get(id);
        const Poly left(restStart, ws.restCoefficients);
        const Poly right(distribution.start(), distribution.ways());
        const int nextStart = left.start + right.start;
        Poly::multiply(left, right, ws.productScratch);
        std::swap(ws.restCoefficients, ws.productScratch);
        restStart = nextStart;
    }

    if (ws.restCoefficients.empty())
        ws.restWays.clear();
    else {
        ws.restWays.assign(restStart + tPool + ws.restCoefficients.size(), 0.0L);
        for (int i = 0; i < (int)ws.restCoefficients.size(); ++i) {
            const long double ways = ws.restCoefficients[i];
            const int componentMines = restStart + i;
            for (int tMines = 0; tMines <= tPool; ++tMines) {
                const int restMines = componentMines + tMines;
                if (tPool <= 9)
                    ws.restWays[restMines] += ways * Combination::chooseSmall(tPool, tMines);
                else
                    ws.restWays[restMines] += ways * Combination::chooseLarge(tPool, tMines, workspace.combination);
            }
        }
    }

    std::array<long double, 9> neighborWays{};
    // 分母沿用全盘 Probability 结果；分子只累计点击安全且邻居数字确定的布局。
    for (int neighborMines = 0; neighborMines + fixedMines <= 8; ++neighborMines)
        for (int capturedMines = 0; capturedMines <= maxCapturedMines; ++capturedMines) {
            const long double ways = ws.dp[neighborMines][capturedMines];
            const int restMines = totalMines - capturedMines;
            if (ways == 0.0L || restMines < 0 || restMines >= (int)ws.restWays.size())
                continue;
            neighborWays[neighborMines] += ways * ws.restWays[restMines];
        }

    for (int neighborMines = 0; neighborMines + fixedMines <= 8; ++neighborMines)
        result.probability[fixedMines + neighborMines] = neighborWays[neighborMines] / probability.candidates();
    return result;
}

}
