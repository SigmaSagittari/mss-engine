#pragma once

#include <cstdint>

#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct Basic {
    enum class Mark : std::uint8_t {
        H, // 与已翻开数字相邻的隐藏候选格。
        T, // 不与已翻开数字相邻的隐藏候选格。
        S, // 已知安全或已推导安全。
        F, // 已知为雷或已推导为雷。
    };

    struct Result {
        Grid<Mark> marks;
        // mineSum 统计 F；unknownSum 只统计 T，H 仍是候选但不计入。
        int unknownSum;
        int mineSum;

        Result() : unknownSum(0), mineSum(0) {}

      private:
        friend struct Basic;
        Grid<std::int8_t> mineAround;
        Grid<std::int8_t> hideAround;
    };

    struct Delta {
        // 单格标记变化；Basic 的 Delta 是 update 写出的日志，不是 ObservedBoard 那种变化本身。
        struct Change {
            ObservedBoard::CellId cell;
            Mark previous;
            Mark next;
        };

        // 按 update 实际改动顺序记录；同一格可多次出现，撤回时逐条逆序。
        vectorPool<Change>::vector changes;
    };

    // Delta 记录池；引用下层观测池，调用方保证观测池覆盖本池的使用期。
    struct Pool {
        ObservedBoard::Pool &board;
        vectorPool<Delta::Change> changes;

        Pool(ObservedBoard::Pool &board) : board(board) {}
        void clear();
    };

    // 临时队列；一次调用返回后可顺序复用，不可并发共享。
    struct Workspace {
        vector<ObservedBoard::CellId> pending;
        // 与 CellId 一一对应；数字处理完清零，调用结束时全为 0。
        vector<unsigned char> queued;
    };

    // state 必须是有效观测盘面；把标记写入 result（复用已有容量），不修改 state。
    static void analyze(Result &result, const ObservedBoard::Result &state, Workspace &workspace);

    // result 对应 updates 应用前的状态，board 已应用同一批 updates；在 pool 尾部写出标记变化日志。
    static Delta update(Result &result, const ObservedBoard::Result &board, const ObservedBoard::Delta &updates, Pool &pool, Workspace &workspace);

    // 正向按日志顺序同步标记与邻域计数；reverse 按逆序撤回，result 必须处于正向应用后的状态。
    static void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);

    // board/result 尺寸必须相同；任一标记、缓存、局部约束或全局雷数不一致时返回 false，不修改输入。
    static bool checkValid(const ObservedBoard::Result &board, const Result &result);
};

} // 命名空间 mss
