#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "algo/winrate_engine/bruteforce/bruteforce_multimask.h"
#include "algo/winrate_engine/bruteforce/bruteforce_normal.h"
#include "algo/winrate_engine/bruteforce/bruteforce_type.h"
#include "algo/winrate_engine/result_type.h"

namespace mss {

// 精确枚举残局：把与观测一致的完整雷位布局逐个展开，按“最优后续还能赢的布局占比”给首步打分。
// 只适用于布局数可控的残局；调用方负责用候选阈值限制规模。
struct BruteForce {
    using Config = BruteForceType::Config;
    using Result = SuggestResult;

    struct Build;

    // board/basic/structure/shapes 必须来自同一刷新；shapes 是 structure 所属的池。
    // 输出评分为最优后续获胜布局数除以总布局数；没有候选格时推荐与候选为空。
    // checkAllMoves=false 只保留最佳首招，true 返回全部候选；Result 自行拥有输出数组。
    void solve(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Config &config, Result &result);

    // components 是当前池中的 InstanceId，彼此不重叠；offFrontierCells 与组件格子不重叠。
    // 只在 components 的 Box 格子和 offFrontierCells 里分配 mines 个雷，其余盘面不参与。
    void solveComponent(const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines, const Config &config, Result &result);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    void writeConfig(BruteForceType::Session &session, Build &build, bool writeConfigs);
    void enumerateTCells(BruteForceType::Session &session, Build &build, int start, int remaining, bool writeConfigs);
    void enumerateBoxCells(BruteForceType::Session &session, Build &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int assignmentStart, int box, int cellStart, int remaining, int componentMines, int usedMines, bool writeConfigs);
    void enumerateComponents(BruteForceType::Session &session, Build &build, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int component, int usedMines, bool writeConfigs);
    void writeResult(const BruteForceType::Result &raw, int count, Result &result);
    void populateSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Pool &shapes, span<const Structure::InstanceId> components, int mines);
    void buildSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes);
    void buildComponentSession(BruteForceType::Session &session, Build &build, const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines);
    void runBackend(const BruteForceType::Session &session, const BruteForceType::Config &config, BruteForceType::Result &result);
};

} // 命名空间 mss
