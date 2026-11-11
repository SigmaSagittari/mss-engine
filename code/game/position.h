#pragma once

#include <cstdint>

#include "algo/probability_engine/logic_component/logic_component.h"
#include "algo/probability_engine/probability/observe.h"
#include "algo/probability_engine/probability/probability.h"
#include "core/datastructure/vector.h"

namespace mss {

struct Position {
    struct Config {
        ShapeSolver::Config shapeSolver;

        void hash(U128Hasher &hasher) const;
    };

    struct LogicComponentResult {
        LogicComponent::Result result;
        LogicComponent::Pool pool;

        LogicComponentResult() : version(0), valid(false) {}

      private:
        friend struct Position;
        std::uint64_t version;
        bool valid;
    };

    struct Result {
        ObservedBoard::Result board;
        Basic::Result basic;
        Structure::Result structure; // 实例 ID 来自 Pool::structure。update 后在查询同步前仍是旧划分。

        Result(const ObservedBoard::Result &board, const Config &config)
            : board(board), config_(config), pendingBoard{}, pendingBasic{}, probabilityVersion(0) {}
        const Config &config() const { return config_; }

      private:
        friend struct Position;
        Config config_; // 该盘面及其所有假设局面共用的概率求解配置。
        LogicComponentResult logicComponents;
        ObservedBoard::Delta pendingBoard;
        Basic::Delta pendingBasic;
        std::uint64_t probabilityVersion;
    };

    struct CheckedResult {
        Result result;
        const char *error; // nullptr 表示 result 可用，否则指向静态报错文本。
    };

    struct ProbabilityResult {
        Probability::BoxResult boxes; // 外层切片来自 Probability::Pool::instanceBoxes，内层来自 boxProbabilities。
        long double totalWays; // 满足全盘约束的布局数；为零时 boxes 为空。
    };

    // 各层增量的切片来自 Pool 中的同名字段，退池或清空后失效。
    struct Delta {
        ObservedBoard::Delta board;
        Basic::Delta basic;
        Structure::Delta structure;

        Delta(const ObservedBoard::Delta &board, const Basic::Delta &basic, const Structure::Delta &structure)
            : board(board), basic(basic), structure(structure), logicComponentVersion(0) {}

      private:
        friend struct Position;
        std::uint64_t logicComponentVersion; // 变更前的分块版本，0 表示当时没有可用结果。
    };

    struct Pool {
        ObservedBoard::Pool board;
        Basic::Pool basic;
        Structure::Pool structure;
        ShapeSolver::DistributionPool distributions;
        Probability::Pool probabilities;

        Pool(Combination::Pool &combinations)
            : basic(board), distributions(structure), probabilities(distributions, combinations),
              logicComponentVersion(0), probabilityVersion(0), cachedProbability{} {}
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;

        void clear();
        // 从 delta 的四个起点截断池尾。截掉的增量记录须已用完。
        void popDelta(const Delta &delta);

      private:
        friend struct Position;
        std::uint64_t logicComponentVersion; // 同一个池里的分块分析使用不同版本号。
        std::uint64_t probabilityVersion; // 对应 cachedProbability 的版本，Result 用零表示待重算。
        ProbabilityResult cachedProbability;
    };

    // 从可信盘面构建 Basic 和 Structure。
    Result analyze(const ObservedBoard::Result &board, Pool &pool, const Config &config);
    // 从人工盘面构建局面并校验全盘有解。probabilityPool.distributions 绑定 pool.structure。
    CheckedResult analyzeChecked(const ObservedBoard::Result &board, Pool &pool, const Config &config, Probability::Pool &probabilityPool);

    // 更新盘面和 Basic，保留池尾增量供下次查询时合并更新 Structure。
    // 同一局面的待处理增量须连续追加到同一个池。
    // 同批格号互异，previous 是当前观测值。支持 Hidden 变为数字、ForcedMine 或 ForcedSafe，以及 ForcedSafe 揭成数字。
    void update(Result &result, const vector<ObservedBoard::Delta::Change> &changes, Pool &pool);
    // 合并应用待处理的变更，供直接读取 Structure 的调用者使用。
    void updateStructure(Result &result, Pool &pool);
    // 用于搜索：先同步根局面，再应用本批变更并记录可重放、可撤回的三层增量。
    Delta updateWithDelta(Result &result, const vector<ObservedBoard::Delta::Change> &changes, Pool &pool);
    // 正向要求 result 对应变更前的局面，逆向要求 result 对应变更后的局面；两者均保留增量记录。
    void applyDelta(Result &result, const Delta &delta, const Pool &pool, bool reverse = false);

    // 同一盘面复用概率。局面改动后下次调用重算，返回的切片借用 pool.probabilities。
    ProbabilityResult analyzeProbability(Result &result, Pool &pool);
    // 先同步 Structure，再向指定的概率池追加本次结果。probabilities.distributions 绑定 pool.structure。
    ProbabilityResult analyzeProbability(Result &result, Pool &pool, Probability::Pool &probabilities);

    // 计算点开 cell 后出现 0～8 或踩雷的概率。cell 是隐藏格或未揭必安格。
    // position 的 Structure 已同步。probability 对应当前盘面，切片来自 probabilities。
    void observe(const Result &position, const ProbabilityResult &probability, const Probability::Pool &probabilities,
                 ObservedBoard::CellId cell, Observe::Result &result);

    // 按需分析当前盘面。返回结果和切片借用 result，重新分析后失效，实例 ID 来自 pool.structure。
    const LogicComponentResult &analyzeLogicComponents(Result &result, Pool &pool);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    ObservedBoard::Delta updateBoard(ObservedBoard::Result &board, const vector<ObservedBoard::Delta::Change> &changes, ObservedBoard::Pool &pool);
    void analyzeDistributions(const Result &result, ShapeSolver::DistributionPool &pool, vector<ShapeSolver::DistributionId> &distributionIds);
};

} // 命名空间 mss
