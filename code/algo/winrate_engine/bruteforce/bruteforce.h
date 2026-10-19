#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "algo/winrate_engine/bruteforce/bruteforce_multimask.h"
#include "algo/winrate_engine/bruteforce/bruteforce_normal.h"
#include "algo/winrate_engine/bruteforce/bruteforce_type.h"

namespace mss {

// 精确枚举残局：把与观测一致的完整雷位布局逐个展开，按“最优后续还能赢的布局数”给首步打分。
// 只适用于布局数可控的残局；调用方负责用候选阈值限制规模。
struct BruteForce {
    using Config = BruteForceType::Config;
    using Result = BruteForceType::Result;

    // 构建过程复用的临时数组；每次构建覆写有效范围，Session 不借用它们。
    struct Build {
        vector<int> candidateAt;
        vector<BruteForceType::CandidateId> tCells;
        vector<BruteForceType::CandidateId> placed;
        vector<int> assignmentOffsets;
        vector<int> assignmentCounts;
        vector<char> assignments;
    };

    // 引用成员：本 Workspace 必须严格短命于 shapeSolver，声明顺序由上层保证。
    // Session 由 solve 局部持有并显式传给后端，不经过 Workspace。
    struct Workspace {
        ShapeSolver::Workspace &shapeSolver;
        Build buildStorage;
        vector<BruteForceType::Session::Candidate> candidates;
        vector<BruteForceType::CandidateId> links;
        Grid<BruteForceType::CandidateId> mineCandidateIds;
        vector<std::uint64_t> configHashes;

        void swapSessionStorage(BruteForceType::Session &session);
        BruteForceNormal::Workspace normal;
        BruteForceMultimask::Workspace multimask;

        explicit Workspace(ShapeSolver::Workspace &shapeSolver) : shapeSolver(shapeSolver) {}
    };

    // board/basic/structure/shapes 必须来自同一刷新；shapes 是 structure 所属的池。
    // 无候选格时 checkAllMoves=false 返回一条 cell=-1、wins 为总胜线数的 Move。
    static void solve(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Config &config, Result &result, Workspace &workspace);

    // components 是当前池中的 InstanceId，彼此不重叠；offFrontierCells 与组件格子不重叠。
    // 只在 components 的 Box 格子和 offFrontierCells 里分配 mines 个雷，其余盘面不参与。
    static void solveComponent(const ObservedBoard::Result &board, const Basic::Result &basic, span<const Structure::InstanceId> components, span<const ObservedBoard::CellId> offFrontierCells, const Structure::Pool &shapes, int mines, const Config &config, Result &result, Workspace &workspace);
};

} // 命名空间 mss
