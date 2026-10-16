#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "algo/probability_engine/observed_board/observed_board.h"

namespace mss {

struct Basic {
    enum class Mark : std::uint8_t {
        H, // 与已翻开数字相邻的隐藏候选格。
        T, // 不与已翻开数字相邻的隐藏候选格。
        S, // 已知安全或已推导安全的格子（如已经打开的数字）。
        F, // 已知为雷或已推导为雷的格子。
    };

    struct Result {
        RawGrid<Mark> marks;
        // mineSum 统计 F；unknownSum 只统计 T。H 仍是候选格，但受邻接数字约束，不计入 unknownSum。
        int unknownSum;
        int mineSum;

        Result() : unknownSum(0), mineSum(0) {}

      private:
        friend struct Basic;
        RawGrid<std::int8_t> mineAround;
        RawGrid<std::int8_t> hideAround;
    };

    struct Delta {
        struct Change {
            ObservedBoard::CellId cell;
            // 保留前后标记；重放按记录顺序，撤回按逆序。
            Mark previous;
            Mark next;
        };

        // 按 update 实际改动顺序记录；同一 CellId 可因 T→H→S/F 多次出现，不得去重，撤回时逐条逆序恢复。
        vectorPool<Change>::vector changes;
        explicit Delta(vectorPool<Change>::vector changes) : changes(changes) {}
    };

    // 按追加顺序保存各 Delta 的记录；池扩容后按切片重新取视图，clear 或退尾使对应切片失效。
    struct Pool {
        vectorPool<Delta::Change> changes;
    };

    // 临时队列；一次调用返回后可顺序复用，不可供并发调用共享。
    struct Workspace {
        std::vector<ObservedBoard::CellId> pending;
        // update 对尚未处理完的数字去重；处理结束后可因新标记变化再次入队，调用结束时全部归零。
        std::vector<unsigned char> queued;
    };

    // state 必须是有效观测盘面；返回同尺寸标记并将单格数字约束传播至队列耗尽，不修改 state；全盘雷数不参与推导。
    static Result analyze(const ObservedBoard::Result &state, Workspace &workspace);
    // 前置条件：result 与 board 尺寸相同；result 对应 updates 应用前的状态，board 已应用同一批 updates。
    // 在 pool 尾部按标记变更顺序记录前后值并返回切片；撤回必须在匹配状态上按 LIFO 进行。
    static Delta update(Result &result, const ObservedBoard::Result &board, const ObservedBoard::Delta &updates, const ObservedBoard::Pool &boardPool, Pool &pool, Workspace &workspace);
    // 正向时 result 对应更新前状态；reverse 时对应更新后状态且必须后进先出。切片只在调用期间借用，同步重放标记和邻域计数，不重新推导。
    static void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);
    // 供界面层调用；board/result 尺寸必须相同。发现任一标记、缓存、局部约束或全局雷数不一致时返回 false，不修改输入。
    static bool checkValid(const ObservedBoard::Result &board, const Result &result);

  private:
    // 数字状态底层值连续为 0~8；numberValue 只接收数字状态，isCandidate 只接受 H/T。
    static bool isNumber(ObservedBoard::CellState state);
    static int numberValue(ObservedBoard::CellState state);
    static bool isCandidate(Mark mark);
};

} // namespace mss
