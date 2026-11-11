#include "algo/probability_engine/observed_board/observed_board.h"

namespace mss {

void ObservedBoard::Pool::clear() {
    changes.data.resize(0);
}

void ObservedBoard::applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse) {
    // Gridhack - 利用 [0] 的越界访问来不必计算 position，直接在内存里写。
    if (reverse) {
        for (const Delta::Change &change : pool.changes.reverseSpan(delta.changes))
            result.cells[0][change.cell] = change.previous;
    } else
        for (const Delta::Change &change : pool.changes.span(delta.changes))
            result.cells[0][change.cell] = change.next;
}

} // 命名空间 mss
