#pragma once

// 使用契约：
// 人工 Position 在构造时完成局部检查与全盘概率分析；调用方构造后只需读取一次 error()，成功后即可使用分析接口。
// mineProbability、observe、suggest 要求局面存在合法全盘布局；Game 的雷盘与观测必须一致。
// 坐标、CellId、状态转换、对象生命周期和 Workspace 的串行使用由调用方保证，具体前置条件见各接口。
// 内部流程信任这些契约，不层层检查、传播查询失败或重复 assert；违反前置条件属于接口约定的未定义行为。
// Error 只报告人工输入的约束矛盾，不能当作内部流程每一步都要检查的状态；Game 的结构变化合并后按需更新，概率结果按需计算并缓存。

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/probability/observe.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver_types.h"
#include "algo/probability_engine/structure/structure.h"
#include "algo/winrate_engine/types.h"
#include "core/config.h"
#include "core/utility/grid.h"
#include "core/utility/hash.h"

namespace mss {

struct Workspace;

struct GameControl {
    struct Game;
    struct MineBoard {
        // 实际雷盘中，1 表示雷，0 表示非雷。
        explicit MineBoard(RawGrid<char> board) : board_(std::move(board)) {}
        // 按给定尺寸、雷数和种子布雷；首步坐标保证无雷，前置条件与 generate 相同。
        MineBoard(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed);

        // 首步坐标必须在盘面内，且 0 <= mines < rows*cols；seed 固定布雷，首步格保证无雷。
        void generate(int rows, int cols, int mines, int firstRow, int firstCol, U128 seed);
        // 雷格返回 9，非雷格返回相邻雷数。
        int number(int row, int col) const;

      private:
        RawGrid<char> board_;
    };

    // Position 只分析观测信息，不持有真实雷位或胜负状态。人工输入的约束矛盾通过 Error 报告。
    // ForcedMine/ForcedSafe 是外部已知约束，不是玩家旗标；引擎推导的安全和雷保留在 Basic::Mark 中，不改写观测盘面。
    struct Position {
        using SuggestAlgo = winrate::SuggestAlgo;

        struct Error {
            bool failed;
            std::string reason;

            Error() : failed(false) {}
        };

        // suggest() 的返回值由调用者持有；suggest 中的格子列表可直接交给 Game::play，Position 不缓存建议。
        struct SuggestResult {
            std::vector<ObservedBoard::CellId> suggest;
            // true 表示格子全部确定安全；false 表示只有一个待猜格。空列表表示当前没有建议。
            bool safeResult;

            SuggestResult() : safeResult(true) {}
            SuggestResult(std::vector<ObservedBoard::CellId> suggest, bool safeResult) : suggest(std::move(suggest)), safeResult(safeResult) {}
        };

        // 尺寸、总雷数和 CellState 编码须有效；构造完成局部检查和指定算法的全盘概率分析，约束矛盾由 error() 报告。
        // 工作区须比 Position 活得久，查询和更新均不得并发或重入共享它。
        Position(ObservedBoard::Result observedBoard, Workspace &workspace, const ShapeSolver::OrderAlgo &algo = ShapeSolver::OrderAlgo::Auto);
        const ObservedBoard::Result &observedBoard() const;
        const Error &error() const;
        // 人工 Position 的构造检查必须成功；返回的引用在 Position 下一次更新或销毁前有效。
        const Basic::Result &basic() const;
        // const 查询会合并待处理变化，返回与当前 Board/Basic 一致的结构及其池。
        const Structure::Result &structure() const;
        const Structure::Pool &shapePool() const;
        // 局面必须合法；人工 Position 复用构造时的概率结果，Game 按需计算，更新后复用结果容量重新分析。
        // const 查询会使用工作区并刷新概率缓存。
        const Probability::Result &probability(const ShapeSolver::OrderAlgo &algo) const;
        // 借用本 Position 的完整分析输入；临时分支须在返回前恢复，不得并发使用或跨 Position 更新、销毁持有。
        winrate::Input input(const ShapeSolver::OrderAlgo &algo);
        // 局面必须合法，cell 必须属于当前盘面；已知 S/F 直接返回 0/1，其余格按需查询全盘概率。
        long double mineProbability(ObservedBoard::CellId cell, const ShapeSolver::OrderAlgo &algo) const;
        // 局面必须合法，cell 必须是 Hidden 或 ForcedSafe；返回揭开后的数字与爆炸概率。
        Observe::Result observe(ObservedBoard::CellId cell, const ShapeSolver::OrderAlgo &algo) const;
        // 局面必须合法；优先返回一批确定安全格，否则按 suggestAlgo 返回一个待猜格；没有可点击格时返回空列表。
        // LowRisk 按雷概率选择，JavaEval 使用 cfg 中的长期风险、伪 50/50 与评估配置；工作区借自 Position。
        // JavaBrute 在候选布局数 < cfg.bruteForce.candidateThreshold 时直接使用暴力，否则使用 JavaEval。
        SuggestResult suggest(const ShapeSolver::OrderAlgo &algo, SuggestAlgo suggestAlgo, const Config &cfg);

      private:
        friend struct Game;

        struct PositionResult {
            Basic::Result basic;
            mutable Structure::Result structure;
            // 首次全盘分析才创建；已有结果在更新后保留容量，dirty 时不能读取。
            mutable std::optional<Probability::Result> probability;
            // 观测盘面或约束结果变化后置为 true；读取概率前重新分析。
            mutable bool probabilityDirty;

            explicit PositionResult(Basic::Result basic) : basic(std::move(basic)), probabilityDirty(true) {}
        };

        // Basic 保存最近一轮的撤回记录，Structure 保存最近一次合并更新的撤回记录。
        struct PositionDelta {
            Basic::Delta basic;
            mutable Structure::Delta structure;
        };

        struct PositionPool {
            // Structure::Result 的组件 ID 由 shapes 拥有，池必须活得比 Result 久。
            mutable Structure::Pool shapes;
            // 按 Shape 复用求解分布，池内 ID 在 clear 前有效。
            mutable ShapeSolver::DistributionPool distributions;
        };

        ObservedBoard::Result observedBoard_;
        Error error_;
        PositionResult result_;
        PositionDelta delta_;
        PositionPool pool_;
        // 自上次结构同步以来揭开的格子；Game 每格只揭开一次，无需去重，空列表即表示结构已同步。
        mutable std::vector<ObservedBoard::CellId> structureLazyUpdate_;

        // Game 提供合法观测；构造时建立基础和结构结果，概率按需计算。
        Position(Workspace &workspace, ObservedBoard::Result observedBoard);
        void initialize();
        void reset(ObservedBoard::Result observedBoard);
        void update(ObservedBoard::Delta &updates);
        void updateStructure() const;
        const std::vector<ObservedBoard::CellId> &safeMoves(const ShapeSolver::OrderAlgo &algo);

        std::vector<ObservedBoard::CellId> safeCells_;
        std::vector<ObservedBoard::CellId> basicSafeCells_;
        std::vector<char> basicSafeSeen_;
        // 借用调用方工作区复用临时空间；调用期间不得并发共享。
        Workspace &workspace_;
    };
    // 随机局只持有数字/Hidden；固定雷盘可保留 ForcedMine，观测须与雷盘一致。模拟调用方将 ForcedSafe 转为 Hidden 后立即安全地 play，Game 本身不持有 ForcedSafe。
    struct Game {
        enum class State { Running, Won, Lost };

      private:
        // 首局首次点击才构造；重开后保留容量，旧雷位在当前局布雷前无效。
        std::optional<MineBoard> mineBoard_;
        // 有值表示当前局尚未布雷，首次实际点击后消耗；固定雷盘不需要此种子。
        std::optional<U128> pendingSeed_;
        // 统计仍为 Hidden 的安全格。
        int remainingSafe_;
        State state_;
        // 每次批量 play 复用同一份观测变化，保留 changes 的容量。
        ObservedBoard::Delta playDelta_;
        Position position_;

        static int countRemainingSafe(const ObservedBoard::Result &observedBoard);

      public:
        using SuggestResult = Position::SuggestResult;
        using SuggestAlgo = Position::SuggestAlgo;

        // 观测只含数字、Hidden、ForcedMine，且与 mineBoard 一致；工作区须比 Game 活得久，且不得并发共享。
        Game(MineBoard mineBoard, ObservedBoard::Result observedBoard, Workspace &workspace);
        // rows、cols 须为正且 0 <= mines < rows*cols；只创建空观测盘面并保存种子，首次实际点击时布雷并保证该格无雷。
        Game(int rows, int cols, int mines, U128 seed, Workspace &workspace);
        // 按原尺寸和雷数重开空盘，保存新种子，等待首次点击布雷，并清空本局池内 ID。
        void reset(U128 seed);
        // 只读访问当前 Position；引用由 Game 持有，生命周期不超过 Game。
        const Position &position() const;
        // 游戏必须处于 Running；返回的格子列表可交给 play。
        SuggestResult suggest(const ShapeSolver::OrderAlgo &algo, SuggestAlgo suggestAlgo, const Config &cfg);
        // CellId 必须属于当前盘面且互不重复；只揭开 Hidden，其余格跳过。本局必须处于 Running；随机局按首个实际揭开格布雷，遇雷后停止，整批只更新一次 Position。
        void play(const std::vector<ObservedBoard::CellId> &cells);
        // 查询实际雷盘的数字；当前局必须已布雷，坐标必须在盘面内。
        int number(int row, int col) const;
        // 返回 Running、Won 或 Lost。
        State state() const;
        // 连续执行确定安全的建议；需要猜测、无可走格或游戏结束时返回。
        void playUntilNoSafe(const ShapeSolver::OrderAlgo &algo);
    };
};

} // namespace mss
