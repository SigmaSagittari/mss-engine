#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/probability/observe.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/utility/hash.h"
#include "core/workspace.h"

namespace mss {

struct GameControl {
    struct Game;

    struct MineBoard {
        MineBoard() = default;
        // 棋盘行列从 0 开始；Game 构造时会校验它与观测盘面的尺寸和已知状态相符。
        explicit MineBoard(RawGrid<char> board) : board_(std::move(board)) {}

        // rows、cols、mines 必须描述合法盘面；seed 决定固定的布雷排列。
        void generate(int rows, int cols, int mines, U128 seed);
        // 坐标必须在盘面内；雷格返回 9，其余返回相邻雷数。
        int number(int row, int col) const;

      private:
        friend struct Game;

        void makeSafe(int row, int col, int targetRow, int targetCol);
        bool matches(const ObservedBoard::Result &observedBoard) const;

        RawGrid<char> board_;
    };

    struct Position {
      public:
        struct Error {
            bool failed = false;
            std::string reason;
        };


        struct SuggestResult {
            enum class Engine { Safe, LowRisk, BruteForce, JavaEval };
            Engine engine = Engine::Safe;
            std::vector<ObservedBoard::CellId> cells;
            std::vector<ObservedBoard::CellId> candidates;
            BruteForce::Result bruteForce;
        };

        // 界面读取观测盘面；增量变更必须经 update，否则 Basic、Structure 与概率缓存会失效。
        ObservedBoard::Result observedBoard;
        Error error;

        // 返回与 observedBoard 同步的 Basic 标记；Position 更新后该引用仍指向同一对象。
        const Basic::Result &basic() const;
        // 返回当前前沿组件；更新后组件下标和引用内容可能变化。
        const Structure::Result &structure() const;

        Position(ObservedBoard::Result observedBoard, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo);
        // UI 已完成 Basic 校验时直接复用结果，不重复分析。
        Position(ObservedBoard::Result observedBoard, Basic::Result basic, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo);
        ShapeSolver::OrderAlgo orderAlgo() const;
        void setOrderAlgo(ShapeSolver::OrderAlgo orderAlgo);
        const Probability::Result &probability();
        // cell 必须是当前盘面的隐藏格；结果按点击后数字 0~8 和爆炸 9 给出概率。
        Observe::Result observe(ObservedBoard::CellId cell);
        // cell 必须是当前盘面有效 CellId，且当前概率结果有合法布局。
        long double mineProbability(ObservedBoard::CellId cell);
        // 返回当前形状池；ID 只在池未清空前有效。
        const Structure::Pool &shapePool() const;
        // 优先返回安全格；需要猜测时按策略和阈值选择引擎，附带该引擎的实际分析结果。
        SuggestResult suggest(const Config &config);

      private:
        friend struct Game;

        void initialize(Basic::Result basic);
        void update(ObservedBoard::Delta &updates);
        const std::vector<ObservedBoard::CellId> &safeMoves();

        std::vector<ObservedBoard::CellId> safeCells_;
        // 先复用 Basic 推导出的隐藏安全格，再检查组件级确定安全格，最后才运行全盘概率分析。
        std::vector<ObservedBoard::CellId> basicSafeCells_;
        // Basic::Delta 可对同一格记录多次状态转换；此表让安全格缓存每格只保留一项。
        std::vector<char> basicSafeSeen_;
        int remainingSafe_ = 0;
        Basic::Result basic_;
        Structure::Result structure_;
        Probability::Result probability_;
        Structure::Pool shapes_;
        ShapeSolver::DistributionPool distributions_;
        ShapeSolver::OrderAlgo orderAlgo_;
        Workspace &workspace_;
        bool probabilityDirty_ = false;
        Basic::Delta basicDelta_;
        Structure::Delta structureDelta_;
    };

    struct Game {
      private:
        std::unique_ptr<MineBoard> mineBoard_;

      public:
        Position position;

        // mineBoard 与 observedBoard 必须描述同一盘面；工作区必须比 Game 活得久且不得并发共享。
        Game(MineBoard mineBoard, ObservedBoard::Result observedBoard, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo = ShapeSolver::OrderAlgo::Auto);
        // 创建随机雷盘；observedBoard 非空时必须与生成的雷盘一致。
        Game(int rows, int cols, int mines, U128 seed, Workspace &workspace, const ObservedBoard::Result *observedBoard = nullptr, ShapeSolver::OrderAlgo orderAlgo = ShapeSolver::OrderAlgo::Auto);
        // 按相同尺寸和雷数重开一局，并清空本局的形状与分布缓存。
        void reset(U128 seed);
        // updates 中 CellId 必须唯一、当前目标格必须为 Hidden，且 next 必须是实际观测到的状态。
        void update(ObservedBoard::Delta &updates);
        // cell 必须在盘面内；若它是雷，safeCell 必须是盘面内的安全格并与它交换。
        void makeFirstMoveSafe(ObservedBoard::CellId cell, ObservedBoard::CellId safeCell);
        // 查询实际雷盘的数字；坐标必须在盘面内。
        int number(int row, int col) const;
        // 所有安全格已揭开时返回 true。
        bool won() const;
        // 持续打开安全格，直到当前局面没有可证明安全的格。
        void playUntilNoSafe();
    };
};

} // namespace mss
