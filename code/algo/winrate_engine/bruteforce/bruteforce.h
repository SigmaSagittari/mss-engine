#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/logic_component/logic_component.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/structure/structure.h"
#include "algo/winrate_engine/bruteforce/bruteforce_type.h"
#include "algo/winrate_engine/result_type.h"
#include "core/datastructure/vector.h"

namespace mss {

// 精确枚举残局：把与观测一致的完整雷位布局逐个展开，按“最优后续还能赢的布局占比”给首步打分。
// 只适用于布局数可控的残局；调用方负责用候选阈值限制规模。
struct BruteForce {
    using Config = BruteForceType::Config;
    using Result = SuggestResult;

    // 块来自同一盘面且互不重叠，切片来自 componentPool，实例 ID 指向 shapes.instances.list。
    // mines 是这些块的剩余总雷数，要求至少有一个合法布局。评分是最优后续能赢的布局占比。
    // checkAllMoves 为真时返回全部候选，否则只保留最佳首招。
    void solve(const ObservedBoard::Result &board, const Basic::Result &basic,
               const vector<LogicComponent::Result::Component> &components, const LogicComponent::Pool &componentPool,
               const Structure::Pool &shapes, int mines, const Config &config, Result &result);

  private:
    struct Build;
    struct Workspace;
    Workspace &getWorkspace();
    void writeConfig(BruteForceType::Session &session, Build &build, bool writeConfigs);
    void enumerateTCells(BruteForceType::Session &session, Build &build, int start, int remaining, bool writeConfigs);
    void enumerateBoxCells(BruteForceType::Session &session, Build &build, const Structure::Pool::Instances &instances, int instanceIndex, int assignmentStart, int box, int cellStart, int remaining, int instanceMines, int usedMines, bool writeConfigs);
    void enumerateInstances(BruteForceType::Session &session, Build &build, const Structure::Pool::Instances &instances, int instanceIndex, int usedMines, bool writeConfigs);
    void writeResult(const BruteForceType::Result &raw, int count, Result &result);
    void buildSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic,
                      const vector<LogicComponent::Result::Component> &components, const LogicComponent::Pool &componentPool,
                      const Structure::Pool &shapes);
    void runBackend(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result);
};

} // 命名空间 mss
