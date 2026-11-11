#pragma once

#include <bit>
#include <cstddef>
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
        S, // 已知安全。
        F, // 已知为雷。
    };

    struct Result {
        Grid<Mark> marks;
        int unknownSum; // T 格的数量。
        int mineSum; // F 格的数量。

        Result() : unknownSum(0), mineSum(0) {}

        // 返回编号最小的 T 格，没有 T 格时返回 -1。
        ObservedBoard::CellId firstT() const;
        // 按格号升序回调 T 格。
        template <typename Callback> void tCells(Callback &&callback) const { forEachCell(tWords, callback); }
        // 按格号升序回调尚未揭开的必安格。
        template <typename Callback> void safeCells(Callback &&callback) const { forEachCell(safeWords, callback); }

      private:
        friend struct Basic;
        Grid<std::int8_t> mineAround;
        Grid<std::int8_t> hideAround;
        vector<std::uint64_t> tWords;
        vector<std::uint64_t> safeWords;

        template <typename Callback> void forEachCell(const vector<std::uint64_t> &words, Callback &callback) const {
            for (int word = 0; word < (int)words.size; ++word)
                for (std::uint64_t bits = words[word]; bits != 0; bits &= bits - 1)
                    callback(word * 64 + std::countr_zero(bits));
        }
    };

    struct Delta {
        struct Change {
            ObservedBoard::CellId cell;
            Mark previous;
            Mark next;
            // 未揭必安集合的归属；S 格揭开为数字时，标记相同但归属改变。
            bool previousSafe;
            bool nextSafe;
        };

        // 变更记录存在 Basic::Pool::changes 中。清池或截掉这段记录后，切片失效。
        vectorPool<Change>::vector changes;
    };

    struct Pool {
        ObservedBoard::Pool &board;
        vectorPool<Delta::Change> changes;

        Pool(ObservedBoard::Pool &board) : board(board) {}
        void clear();
    };

    void analyze(Result &result, const ObservedBoard::Result &board);
    // 调用前先把 updates 应用到 board。result 仍对应变化前的盘面。
    Delta update(Result &result, const ObservedBoard::Result &board, const ObservedBoard::Delta &updates, Pool &pool);
    void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);
    bool checkValid(const ObservedBoard::Result &board, const Result &result);

  private:
    struct Workspace;
    // 取当前线程复用的传播工作区。
    Workspace &getWorkspace();
    // 扩展入队标记，只清零新增槽位。
    void growQueued(vector<std::uint8_t> &queued, std::size_t cellCount);
    // 判断格子是否仍待判定（H 或 T）。
    bool isCandidate(Mark mark);
    // 只将尚未入队的数字格加入传播队列。
    void enqueue(const ObservedBoard::Result &board, int row, int col, vector<ObservedBoard::CellId> &pending, vector<std::uint8_t> &queued);
    // 改标记及未揭必安状态，同步位图和计数，对计数变化的邻格调用 affected。
    template <typename Callback> Delta::Change changeMark(Result &result, int row, int col, Mark mark, bool safe, Callback &&affected);
    // 处理队列中的数字，反复推导必安和必雷，直到队列清空。
    template <typename SetMark> void propagate(const ObservedBoard::Result &board, const Result &result, vector<ObservedBoard::CellId> &pending, vector<std::uint8_t> &queued, SetMark &&setMark);
};

} // 命名空间 mss
