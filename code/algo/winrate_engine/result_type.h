#pragma once

#include "algo/probability_engine/observed_board/observed_board.h"
#include "core/datastructure/vector.h"

namespace mss {

// 各算法的完整评分快照；不借用搜索树或工作区，推荐缓存另行查询。
struct SuggestResult {
    enum class Engine { None, Safe, LowRisk, JavaEval, Anytime, BruteForce };
    enum class Reason { Finished, Safe, Forced, Semi, ScoreWins, Specified };
    struct Candidate {
        ObservedBoard::CellId cell;
        // 分数越高越优；LowRisk 为安全率，BruteForce 为最终获胜概率。
        long double value;
        Candidate(ObservedBoard::CellId cell, long double value) : cell(cell), value(value) {}
    };
    // 调用方按需接收明细；普通选招不分配这些记录。
    struct Details {
        struct Branch {
            ObservedBoard::CellId cell;
            int digit;
            long double value;
            Branch(ObservedBoard::CellId cell, int digit, long double value) : cell(cell), digit(digit), value(value) {}
        };
        struct Layer {
            ObservedBoard::CellId cell;
            int depth;
            long double value;
            Layer(ObservedBoard::CellId cell, int depth, long double value) : cell(cell), depth(depth), value(value) {}
        };
        vector<Branch> branches;
        vector<Layer> history;
        void clear() { branches.resize(0); history.resize(0); }
    };

    vector<ObservedBoard::CellId> suggest;
    vector<Candidate> candidates;
    Engine engine;
    Reason reason;
    long long nodes;

    explicit SuggestResult(Engine engine = Engine::None) : engine(engine), reason(engine == Engine::None ? Reason::Finished : Reason::ScoreWins), nodes(0) {}
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
