#pragma once

#include <cstdint>

#include "core/datastructure/grid.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct EffOpening {
    struct Geometry {
        struct Neighbors {
            std::uint16_t cells[8];
            std::uint8_t count;
        };

        vector<Neighbors> adjacent;
        vector<std::uint16_t> rank, byRank;
    };

    struct Config {
        double target;
        int extraClicks;
    };

    struct Result {
        bool accepted;
        bool checkedMinimum;
    };

    // 连续生成同尺寸盘面时，只需准备一次邻接和扫描次序。
    void prepare(int rows, int cols, Geometry &geometry);
    // 雷图每格 0 或 1，geometry 对应雷图尺寸。target 为百分数。
    // extraClicks 为 0 时只走启发式；接受时给出最大零区的格号。
    Result select(const Grid<char> &mines, const Geometry &geometry, const Config &config, vector<int> &opening);

  private:
    struct Cell;
    struct Workspace;
    Workspace &getWorkspace();
    int build(const Grid<char> &mines, const Geometry &geometry, vector<Cell> &cells, vectorPool<int> &zeros,
              vectorPool<int> &boundary, vector<vectorPool<int>::vector> &regions, vectorPool<int>::vector &largest);
    int highest(const std::uint8_t *values, int size);
    int firstAtLeast(const std::uint8_t *data, int size, int score);
    template <bool first> void change(int index, int delta, const std::uint16_t *rank,
                                      vector<std::uint8_t> &priority, vector<std::uint8_t> &upper);
    template <bool first> void reveal(int index, const Geometry &geometry, const vector<Cell> &cells, const vectorPool<int> &boundary,
                                      const vector<vectorPool<int>::vector> &regions, const std::uint16_t *rank,
                                      vector<std::uint8_t> &priority, vector<std::uint8_t> &upper, vector<char> &revealed, int &remaining);
    template <bool first> int simulate(const Geometry &geometry, const vector<Cell> &cells, const vectorPool<int> &boundary,
                                       const vector<vectorPool<int>::vector> &regions, int threeBV, int direction,
                                       vector<std::uint8_t> &priority, vector<std::uint8_t> &upper, vector<char> &revealed);
};

} // 命名空间 mss
