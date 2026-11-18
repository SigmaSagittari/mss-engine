#pragma once

#include <cstddef>
#include <functional>

#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/datastructure/vector.h"

namespace mss {

struct IoeBruteForce {
    enum class Status { Exact = 0, CandidateLimit = 1, DomLimit = 2, InvalidBoard = 3 };
    enum class Action { None = 0, Open = 1, Unflag = 2, Chord = 3 };
    enum class Phase { Enumeration = 0, Root = 1, Search = 2 };

    struct Progress {
        Phase phase;
        std::size_t candidates;
        std::size_t enumerated;
        std::size_t rootAnalyzed;
        std::size_t rootEligible;
        std::size_t rootDomCalls;
        std::size_t searchDomCalls;
        std::size_t domLimit;
    };

    struct RootResult {
        Status status;
        long double candidates;
        std::size_t domCalls;
        std::size_t eligible;
    };

    struct Result {
        struct IoeCount {
            int numerator;
            int denominator;
            std::size_t won;
            std::size_t discarded;
        };

        Status status;
        long double candidates;
        long double expectedScore;
        std::size_t rootDomCalls;
        std::size_t searchDomCalls;
        std::size_t rootEligible;
        std::size_t wins;
        Action action;
        ObservedBoard::CellId cell; // action 为 None 时，-1 表示没有推荐格。
        int addedFlags[8];
        int removedFlags[8];
        int addedCount;
        int removedCount;
        vector<IoeCount> ioeDistribution;

        Result(const RootResult &root)
            : status(root.status), candidates(root.candidates), expectedScore(0), rootDomCalls(root.domCalls),
              searchDomCalls(0), rootEligible(root.eligible), wins(0), action(Action::None), cell(-1),
              addedFlags{}, removedFlags{}, addedCount(0), removedCount(0) {}
    };

    // 两个入口均要求 9×9、10 雷，玩家旗与盘面同尺寸，累计点击数非负。
    // 玩家旗是实际操作状态，与 board 中的必雷和必安分开。
    RootResult analyzeRoot(const ObservedBoard::Result &board, const Grid<bool> &playerFlags, int clicks, std::size_t candidateLimit);

    // 进度回调在当前求解线程执行，参数只在回调期间有效。
    Result solve(const ObservedBoard::Result &board, const Grid<bool> &playerFlags, int clicks,
                 std::size_t candidateLimit, std::size_t domLimit,
                 const std::function<void(const Progress &)> &publish = {});
};

} // 命名空间 mss
