#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/probability/observe.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"

namespace mss {

// GameControl 只提供对局层共享的配置和雷盘；Position/Game 由各自头文件定义。
namespace GameControl {

struct Position;
struct Game;

// 调用方持有、跨 Position/Game 串行复用的建议缓存。
struct SuggestPool {
    FlatHashTable<U128, ObservedBoard::CellId, U128Hash> cache;
};

// 目前只有 ShapeSolver 参数；策略配置不进这里，随 SuggestPolicy 由调用方提供。
struct Config {
    ShapeSolver::Config shapeSolver;
};

struct MineBoard {
    MineBoard();
    // 实际雷盘中 1 表示雷，0 表示非雷。
    MineBoard(Grid<char> board);
    // 首步坐标必须在盘面内，且 0 <= mines < rows*cols；seed 固定布雷，首步格保证无雷。
    MineBoard(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed);

    void generate(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed);
    // 雷格返回 9，非雷格返回相邻雷数。
    int number(int row, int col) const;

  private:
    Grid<char> board_;
};

} // 命名空间 GameControl

} // 命名空间 mss
