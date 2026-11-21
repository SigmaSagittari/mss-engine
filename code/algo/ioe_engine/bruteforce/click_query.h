#pragma once

#include "algo/ioe_engine/bruteforce/click_plans.h"
#include "core/datastructure/flat_hashtable.h"

namespace mss {

struct IoeClickQuery {
  private:
    using Coverage = IoeClickPlans::Coverage;

  public:
    static constexpr int overBudget = IoeClickPlans::overBudget;
    static constexpr int limitReached = -2;

    struct Root {
        const IoeView &view;
        int minimum; // 这张雷图在根局面的最少剩余点击。
        int budget;
    };

    struct Request {
        IoeView view;
        int budget;
        int exact; // 为 -1 时从计划或 DOM 求值。已证出的剩余点击也消耗首次查询额度。
        std::size_t limit;
    };

    struct Estimate {
        // 对应 cache.canEstimate(id) 为 true 时，estimate 才会写入可读取的单击、双击集合。
        IoeCells opens() const { return moves.opens; }
        IoeCells chords() const { return moves.bestChords; }

      private:
        friend struct IoeClickQuery;
        // 危险：无可用计划时省去逐格写入，estimate 只写 coverage；有计划时由 estimate 首次填充 moves。
        IoeClickPlans::Estimate moves;
        Coverage coverage;
    };

    struct Bound {
        int clicks;
        bool exact; // 与本招的最低结算点击数合并后，可直接记为精确值。
    };

    struct Cache {
        Cache() : queried(0) {}

        void clear(std::size_t layouts);
        // 按雷图编号、已揭格和旗位统计首次查询，已有精确点击数的首次查询也计入。
        std::size_t queries() const { return queried; }
        bool canEstimate(int id) const { return plans[id].plans.count; }

      private:
        friend struct IoeClickQuery;

        struct Key {
            IoeCells revealed;
            IoeCells flags;
            int layout;
            bool operator==(const Key &) const = default;
        };

        struct Hash {
            std::size_t operator()(const Key &key) const;
        };

        struct CachedEstimate {
            IoeClickPlans::Estimate moves;
            int remaining;
        };

        vector<IoeClickPlans::Result> plans;
        IoeClickPlans::Pool planPool;
        FlatHashTable<Key, int, Hash> distances;
        FlatHashTable<Key, std::uint32_t, Hash> estimateIndex;
        vector<CachedEstimate> estimates;
        std::size_t queried;
    };

    int minimum(Cache &cache, int id, const IoeStrategyBoard &layout, const Request &request, const Root &root);
    // 无可用计划时，结果只供 bound 使用；读取 opens()/chords() 前须确认 cache.canEstimate(id)。
    void estimate(Cache &cache, int id, const IoeStrategyBoard &layout, const IoeView &view, int remaining, Estimate &result);
    Bound bound(const Estimate &estimate, const IoeMove &move, bool opensCenter, int remaining, int rootMinimum);
};

} // 命名空间 mss
