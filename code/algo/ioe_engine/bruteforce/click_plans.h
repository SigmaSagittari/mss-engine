#pragma once

#include "algo/ioe_engine/bruteforce/strategy_type.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct IoeClickPlans {
    static constexpr int overBudget = -1;
    enum class Coverage { Untried, Unavailable, RootOptimum, Complete };

  private:
    struct Plan {
        IoeCells chords;
        IoeCells flags;
        IoeCells independent;
        vectorPool<IoeCells>::vector components;
    };

  public:
    struct Result {
        Coverage coverage;
        // 切片来自 Pool::plans，清空或回退对应段后失效。
        vectorPool<Plan>::vector plans;
    };

    struct Pool {
        vectorPool<Plan> plans;
        vectorPool<IoeCells> components;

        void clear();
    };

    struct Estimate {
        IoeCells opens;
        IoeCells bestChords;
        std::uint8_t chords[IoeBoard::cells]; // 255 表示所存方案均未使用此双击。
    };

    Result build(const IoeStrategyBoard &layout, const IoeView &root, int optimum, int budget, Pool &pool);
    // 返回所存方案中的最少剩余点击；没有适用方案时返回 overBudget。
    int minimum(const IoeStrategyBoard &layout, const IoeView &view, const Result &plans, const Pool &pool, int budget);
    void estimate(const IoeStrategyBoard &layout, const IoeView &view, const Result &plans, const Pool &pool,
                  int optimum, Estimate &result);

  private:
    struct Cost {
        int clicks;
        IoeCells opens, chords;
    };
    struct Workspace;
    Workspace &getWorkspace();
    Cost measure(const Plan &plan, span<const IoeCells> components, const IoeCells &hidden, const IoeCells &active,
                 const IoeCells &revealed, const IoeCells &missing, const IoeCells *adjacent);
    IoeCells activeChords(const IoeCells &hidden, const IoeCells *adjacent);
};

} // 命名空间 mss
