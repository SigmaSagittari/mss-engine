#pragma once

namespace mss {

// 按左上、上、右上、左、右、左下、下、右下访问盘面内的真实邻格。
template <typename Callback> inline void forEachAdjacent(int row, int col, int rows, int cols, Callback &&callback) {
    for (int deltaRow = -1; deltaRow <= 1; ++deltaRow)
        for (int deltaCol = -1; deltaCol <= 1; ++deltaCol) {
            if (deltaRow == 0 && deltaCol == 0) continue;
            const int neighborRow = row + deltaRow;
            const int neighborCol = col + deltaCol;
            if (neighborRow < 0 || neighborRow >= rows || neighborCol < 0 || neighborCol >= cols) continue;
            callback(neighborRow, neighborCol);
        }
}

} // 命名空间 mss
