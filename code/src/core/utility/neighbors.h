#pragma once

namespace mss {

// 输入为盘面内坐标和实际尺寸；按左上、上、右上、左、右、左下、下、右下访问真实邻格。
template <typename Func> inline void forEachAdjacent(int row, int col, int rows, int cols, Func &&fn) {
    const bool up = row > 0, down = row < rows - 1, left = col > 0, right = col < cols - 1;
    if (up && left) fn(row - 1, col - 1);
    if (up) fn(row - 1, col);
    if (up && right) fn(row - 1, col + 1);
    if (left) fn(row, col - 1);
    if (right) fn(row, col + 1);
    if (down && left) fn(row + 1, col - 1);
    if (down) fn(row + 1, col);
    if (down && right) fn(row + 1, col + 1);
}

} // 命名空间 mss
