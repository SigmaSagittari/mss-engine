#pragma once

#include <cstdint>

#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/hash.h"
#include "core/datastructure/vector.h"

namespace mss {

// bruteforce 各文件共用的根类型；后端直接读写这些类型，不做翻译。
struct BruteForceType {
    using ConfigId = std::uint32_t; // 一张完整雷盘的编号，与算法 Config 参数无关。
    using CandidateId = std::uint32_t; // Session::candidates 下标，与棋盘 CellId 不同。

    struct Config {
        // true：返回每个 H/T 候选的第一步结果；false：只返回最佳一步。
        bool checkAllMoves;
        // 仅根节点并行；false 时串行。
        // 并行线程总数（含调用线程），必须为正；串行时忽略。
        int threadCount;

        void hash(U128Hasher &hasher) const {
            hasher.mix(checkAllMoves, threadCount);
        }
    };

    struct Result {
        struct Move {
            // -1 表示没有需要点击的候选格。
            ObservedBoard::CellId cell;
            // 该首步之后、最优后续能赢的完整布局数。
            int wins;

            Move(ObservedBoard::CellId cell, int wins) : cell(cell), wins(wins) {}
        };

        // 递归访问节点数；并行时为所有线程之和，只用于诊断。
        long long nodes;
        // 单步模式固定一条（无候选时 cell = -1，wins 为总胜线数）；
        // 全候选模式按候选顺序，每候选一条；没有候选时为空。
        vector<Move> moves;

        Result() : nodes(0) {}
    };

    // 一次 solve 的候选集合和完整布局表；数组借用 BruteForce 的临时存储，仅在本次 solve 内有效。
    struct Session {
        struct Candidate {
            ObservedBoard::CellId cell;
            // links 中该候选邻居区间的起点和数量。
            std::uint32_t linksOffset;
            std::uint8_t linksCount;
            // 不在候选集合里的 F 邻居数，预先并入揭示数字。
            std::uint8_t fixedMines;
        };

        int candidateCount;
        // 完整雷位方案数量；每个方案对应一个 ConfigId。
        int possibilityCount;
        vector<Candidate> &candidates;
        // 候选邻居的 CandidateId 连续存储，由 linksOffset/linksCount 定位。
        vector<CandidateId> &links;
        // 每行恰有这么多项。
        int minesPerConfig;
        // [ConfigId][雷序号] 的候选下标表。
        Grid<CandidateId> &mineCandidateIds;
        // 每个 ConfigId 的 splitmix64；所有后端共用，避免逐节点现算。
        vector<std::uint64_t> &configHashes;

        Session(vector<Candidate> &candidates, vector<CandidateId> &links, Grid<CandidateId> &mineCandidateIds, vector<std::uint64_t> &configHashes, int minesPerConfig) : candidateCount(0), possibilityCount(0), candidates(candidates), links(links), minesPerConfig(minesPerConfig), mineCandidateIds(mineCandidateIds), configHashes(configHashes) {}
    };
};

} // 命名空间 mss
