#pragma once

#include <array>
#include <memory>
#include <stop_token>
#include <vector>

#include "algo/winrate_engine/internal/move_evaluate.h"
#include "algo/winrate_engine/java_transplant/pseudo_helper.h"

namespace mss {

struct Config;

struct AnytimeSearch {
    struct Config {
        // 根层按 JavaEval、中层和其余层按短期评分保留的候选数，均为正数。
        std::array<int, 3> width;
        // 指定格分析保留全部根招法；自动分析执行搜索剪枝后，保留根宽度内的最高分候选。
        bool fullAnalysis;
        Config(std::array<int, 3> width, bool fullAnalysis) : width(width), fullAnalysis(fullAnalysis) {}
    };

    struct Candidate {
        struct Branch {
            int digit;
            long double value;
            int depth;
            Branch(int digit, long double value, int depth) : digit(digit), value(value), depth(depth) {}
        };
        winrate::CellId cellId;
        // 根层固定的 influence 与进度乘数；已完整解出的招法归一为 1。
        long double multiplier;
        // 连续保存各层完整评分；第零层使用根层评分，已完成深度为 value.size() - 1。
        std::vector<long double> value;
        // 各数字结局独立发布最新完整子评分；深度可先于本招的完整层评分推进。
        std::vector<Branch> branches;
        Candidate(winrate::CellId cellId, long double value, long double multiplier) : cellId(cellId), multiplier(multiplier), value{value} {}
    };

  private:
    struct State;

  public:
    struct Result {
        std::vector<Candidate> candidates;
        long long node;
        int completedDepth;
        bool finished;
        Result();
        ~Result();
        Result(Result &&other) noexcept;
        Result &operator=(Result &&other) noexcept;
        // 没有候选时返回 -1；其余先比较完成深度，再比较该层评分。
        winrate::CellId recommended() const;

      private:
        friend struct AnytimeSearch;
        std::unique_ptr<State> state;
    };

    struct Workspace {
        MoveEval::Workspace &moveEval;
        Pseudo::Workspace &pseudo;
        MoveEval::Selection::Workspace selection;
        Workspace(MoveEval::Workspace &moveEval, Pseudo::Workspace &pseudo) : moveEval(moveEval), pseudo(pseudo) {}
    };

    // 候选及观测已在同一有效根盘面上准备完成；只在 Search 内执行等价类和对换格剪枝。
    // 第零层无条件发布，预算只约束后续展开；每个 node 是一次强制安全或数字分支概率计算。
    // Result 拥有输入副本、搜索树和续算进度；借用的 shapes/distributions 池必须覆盖 Result 生命周期，不能清空或并发写入。
    // 配置在续算期间不变；工作区只借用临时容量，可在两次 update 之间串行用于其他分析。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, const MoveEval::Selection::Result &candidates, const mss::Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop);
    static void update(Result &result, const mss::Config &cfg, Workspace &workspace, long long nodeBudget, std::stop_token stop);
};

} // 命名空间 mss
