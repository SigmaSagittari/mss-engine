#pragma once

#include "algo/ioe_engine/bruteforce/strategy_type.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct IoeMoves {
    struct Group {
        IoeCells revealed;
        vectorPool<int>::vector layouts; // 借用 Result::layouts，清空或截断该段后失效。
    };

    struct Result {
        vector<Group> groups;
        vectorPool<int> layouts;
        IoeCells flags;
    };

    int clicks(const IoeMove &move, const IoeView &view);
    bool play(const IoeMove &move, const IoeView &view, const IoeStrategyBoard &board, IoeCells &revealed);
    // 同一观测下，按走完后揭开的格子及其数字分组；每组成功的雷图编号保持 layouts 的输入顺序。
    void group(const IoeMove &move, const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, Result &result);
    void moves(const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, bool root, vector<IoeMove> &moves);

  private:
    struct Targets {
        IoeCells flags;
        IoeCells cells;
    };

    Targets targets(const IoeMove &move, const IoeView &view);
    void removeDominated(const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, vector<IoeMove> &moves);
    void postponeChords(const IoeView &view, const vector<IoeStrategyBoard> &boards, span<const int> layouts, vector<IoeMove> &moves);
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss
