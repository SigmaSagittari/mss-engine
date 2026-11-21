#include "algo/ioe_engine/ioe_type.h"

#include "core/algorithm/neighbors.h"

namespace mss {

__declspec(noinline) const IoeCells *IoeBoard::neighbors() {
    struct Table {
        IoeCells masks[cells];

        Table() {
            for (int cell = 0; cell < cells; ++cell)
                forEachAdjacent(cell / side, cell % side, side, side, [&](int row, int col) { masks[cell].set(row * side + col); });
        }
    };
    static const Table table;
    return table.masks;
}

} // 命名空间 mss
