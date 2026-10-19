#include "algo/probability_engine/observed_board/observed_board.h"

namespace mss {

void ObservedBoard::apply(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    const Delta::Change *changes = pool.changes.data.data + delta.changes.offset;
    for (int i = 0; i < (int)delta.changes.count; ++i) {
        const Delta::Change &change = changes[reverse ? (int)delta.changes.count - 1 - i : i];
        const std::pair<int, int> position = result.pos(change.cell);
        result.cells[position.first][position.second] = reverse ? change.previous : change.next;
    }
}

} // 命名空间 mss
