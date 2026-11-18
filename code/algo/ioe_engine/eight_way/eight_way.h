#pragma once

#include <cstdint>

#include "core/datastructure/grid.h"

namespace mss {

struct EightWay {
    struct Filter {
        bool accepted;
        bool ranDom;
    };

    // 雷位为 0 或 1，盘面全盖且至少有一格安全格。target 按百分数传入，例如 160 表示 160%。
    // 无零格时直接淘汰。收盘时，opening 给出最大的零区。
    // 8way 未达标时，domExtraClicks 决定可多用几次点击进入 DOM；为零时跳过 DOM。
    // domExtraClicks 为正值时，盘面必须是 9×9（10 雷）、16×16（40 雷）或 16×30（99 雷）。
    Filter filterOpening(const Grid<char> &mines, double target, int domExtraClicks, vector<int> &opening);

  private:
    enum class Cell : std::uint8_t { Hidden, Open, Flag };
    // 禁用格仍会参与优先级更新。准备时最多受八个邻格加分，每轮最多再受八次插旗和一次直接揭开加分，仍低于入队下限 0。
    enum { disabledPriority = -10000 };

    struct NeighborOffsets;
    struct Prepared;
    struct Order;
    struct Queue;
    struct Workspace;

    Workspace &getWorkspace();
    int prepare(const Grid<char> &mines, const NeighborOffsets &offsets, Prepared &board);
    int simulate(const Prepared &board, const NeighborOffsets &offsets, const Order &order, int threeBV);
    void maximumOpening(const Prepared &board, vector<int> &opening);
    void open(int cell, int rank, const Prepared &board, const NeighborOffsets &offsets,
              vector<Cell> &cells, vector<int> &priority, Queue &queue, int &remainingThreeBV);
};

} // 命名空间 mss
