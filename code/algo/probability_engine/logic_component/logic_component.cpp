#include "algo/probability_engine/logic_component/logic_component.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "core/algorithm/neighbors.h"

namespace mss {

struct LogicComponent::Workspace {
    vector<std::uint8_t> visited;
    vector<ObservedBoard::CellId> pending;
};

__declspec(noinline) LogicComponent::Workspace &LogicComponent::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void LogicComponent::analyze(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic,
                             const Structure::Result &structure, const Structure::Pool::Instances &instances, Pool &pool) {
    using CellId = ObservedBoard::CellId;
    using Mark = Basic::Mark;

    Workspace &workspace = getWorkspace();
    vector<std::uint8_t> &visited = workspace.visited;
    vector<CellId> &pending = workspace.pending;
    const int rows = board.cells.rows();
    const int cols = board.cells.cols();

    result.components.resize(0);
    pool.instanceIds.data.resize(0);
    pool.tCells.data.resize(0);
    visited.resize((std::size_t) rows * cols);
    std::fill_n(visited.data, visited.size, 0);

    auto visit = [&](int row, int col, Result::Component &component) {
        const CellId cell = board.id(row, col);
        if (visited.data[cell])
            return;
        const Mark mark = basic.marks[row][col];
        if (mark == Mark::T) {
            visited.data[cell] = 1;
            pending.push_back(cell);
            pool.tCells.push_back(component.tCells, cell);
        } else if (mark == Mark::H) {
            // 同一实例可能跨过数字格。首次碰到其中一个 H 时，整实例入队并同时标记，实例只记一次。
            const int instanceIndex = structure.cellLoc.data[cell].instanceIndex;
            const Structure::InstanceId id = structure.instanceIds.data[instanceIndex];
            const Structure::Instance &instance = instances.list.data[id];
            pool.instanceIds.push_back(component.instanceIds, id);
            for (const vectorPool<CellId>::vector &box : instances.boxes.span(instance.boxes))
                for (CellId member : instances.cells.span(box)) {
                    visited.data[member] = 1;
                    pending.push_back(member);
                }
        }
    };

    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const Mark mark = basic.marks[row][col];
            if ((mark != Mark::H && mark != Mark::T) || visited.data[board.id(row, col)])
                continue;
            Result::Component component(pool.instanceIds.push_back(), pool.tCells.push_back());
            visit(row, col, component);
            for (std::size_t i = 0; i < pending.size; ++i) {
                const auto [r, c] = board.pos(pending.data[i]);
                forEachAdjacent(r, c, rows, cols, [&](int nr, int nc) { visit(nr, nc, component); });
            }
            pending.resize(0);
            result.components.push_back(component);
        }
}

} // 命名空间 mss
