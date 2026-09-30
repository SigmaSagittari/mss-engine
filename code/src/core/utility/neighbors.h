#pragma once

namespace mss {

// 只调用真实盘面内的邻格，顺序为左上、上、右上、左、右、左下、下、右下。
template <typename Func> inline void forEachAdjacent(int row, int col, int rows, int cols, Func &&fn) {
    // rows/cols 必须是盘面尺寸，row/col 必须是盘面内的 0-based 坐标；此处不检查参数。
    bool up = row > 0;
    bool down = row < rows - 1;
    bool left = col > 0;
    bool right = col < cols - 1;

    if (up && left)
        fn(row - 1, col - 1);
    if (up)
        fn(row - 1, col);
    if (up && right)
        fn(row - 1, col + 1);
    if (left)
        fn(row, col - 1);
    if (right)
        fn(row, col + 1);
    if (down && left)
        fn(row + 1, col - 1);
    if (down)
        fn(row + 1, col);
    if (down && right)
        fn(row + 1, col + 1);
}

} // namespace mss
