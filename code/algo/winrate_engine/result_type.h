#pragma once

#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/datastructure/vector.h"

namespace mss {

// 各算法的完整评分快照；不借用搜索树或工作区，推荐缓存另行查询。
struct SuggestResult {
    enum class Engine { None, Safe, LowRisk, JavaEval, Anytime, BruteForce };
    // 已结束、已有必安、强制死猜、Semi 本体，或普通评分选优；不记录候选是如何传入的。
    enum class Reason { Finished, Safe, Forced, Semi, ScoreWins };
    struct Candidate {
        ObservedBoard::CellId cell;
        // LowRisk 为安全率，BruteForce 为最终胜率，JavaE 为含推进奖励的评分。
        // Anytime 为最新完成层的普通分，不含推进奖励；推荐另看完成深度和选招分。
        long double value;
        Candidate(ObservedBoard::CellId cell, long double value) : cell(cell), value(value) {}
    };
    // 调用方按需接收明细；普通选招不分配这些记录。
    struct Details {
        // 一个候选显示某个数字后得到的子评分；发生概率另由 Observe 提供。
        struct Outcome {
            ObservedBoard::CellId cell;
            int digit;
            long double value; // 该数字下的后续评分，不乘这个数字的发生概率。
            Outcome(ObservedBoard::CellId cell, int digit, long double value) : cell(cell), digit(digit), value(value) {}
        };
        // 某个根候选完整算完一层后的历史分；depth=0 是刚建立根层的评分。
        struct Layer {
            ObservedBoard::CellId cell;
            int depth;
            long double value;
            Layer(ObservedBoard::CellId cell, int depth, long double value) : cell(cell), depth(depth), value(value) {}
        };
        vector<Outcome> outcomes; // 各根最新完成层的逐数字子评分，按 cell/digit 定位。
        vector<Layer> history; // 历次完成层记录，update 只追加，不覆盖过去层。
        void clear() { outcomes.resize(0); history.resize(0); }
    };

    vector<ObservedBoard::CellId> suggest; // 推荐格；必安时可有多个，普通评分只返回最佳一个。
    vector<Candidate> candidates; // 算法实际保留并给出评分的格子，不包含被剪掉的格子。
    Engine engine;
    Reason reason;
    long long nodes; // JavaE/Anytime 为全盘概率分析与 Observe 次数之和，暴力为递归节点数；独立 LTR 不计入。

    // 空结果没有推荐格；算法结果同时提供实际引擎和原因。
    SuggestResult() : SuggestResult(Engine::None, Reason::Finished) {}
    SuggestResult(Engine engine, Reason reason) : engine(engine), reason(reason), nodes(0) {}
    void swap(SuggestResult &other) {
        suggest.swap(other.suggest);
        candidates.swap(other.candidates);
        std::swap(engine, other.engine);
        std::swap(reason, other.reason);
        std::swap(nodes, other.nodes);
    }
    // 普通评分按分数选第一名；Anytime 自己按完成深度决定推荐。
    void recommend() {
        suggest.resize(0);
        if (candidates.size == 0) return;
        int best = 0;
        for (int i = 1; i < (int)candidates.size; ++i) if (candidates.data[i].value > candidates.data[best].value) best = i;
        suggest.push_back(candidates.data[best].cell);
    }
};

} // 命名空间 mss
