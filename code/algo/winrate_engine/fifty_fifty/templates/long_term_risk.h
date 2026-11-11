#pragma once

#include <type_traits>

#include "algo/winrate_engine/fifty_fifty/templates/template_data.h"
#include "game/position.h"

namespace mss {

struct TemplateLongTermRisk {
    enum class DetailRequest { Score, Details };

    struct Score {
        long double compensation;
    };

    struct Details {
        long double compensation, body;
        vector<long double> restrictions; // 已知候选为雷时，无本体且恰好命中 k 个限制的概率。

        Details() : compensation(0.0L), body(0.0L) {}
    };

    template <DetailRequest request>
    using InfluenceResult = std::conditional_t<request == DetailRequest::Details, Details, Score>;

    struct Result {
      private:
        friend struct TemplateLongTermRisk;
        struct Match { int pattern; FiftyFiftyData::Point anchor; };
        struct Link { int match, slot; };
        struct Count { vectorPool<int>::vector key; int next; long double ways; };

        vector<Match> matches;
        vector<vectorPool<Link>::vector> byCell;
        vectorPool<Link> links;
        vector<int> boxes;
        vector<long double> mine, compensation;
        vector<Count> counts;
        vectorPool<int> keys;
        FlatHashTable<std::uint64_t, int> countIndex;
        int limit; // 由 update 写入，与本盘的补偿缓存共用。
    };

    // 下一次更新会清除旧匹配及缓存。limit 为参与联合计数的方向事件上限，须大于零。
    void update(Result &result, const Position::Result &position, const Position::ProbabilityResult &probability,
                const Probability::Pool &probabilities, const FiftyFiftyData::Pool &templates, int limit);

    // 使用 update 的匹配结果，按事件上限 1 查询整盘。正式上限不同则重新缓存逐格补偿。
    // 模板表和概率须与 update 对应。
    void coarse(Grid<long double> &output, Result &result, Position::Result &position, Position::Pool &positions,
                const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                const FiftyFiftyData::Pool &templates);

    // 返回可加到根层评分的概率。position、probability 和 templates 与 update 对应。
    // 明细请求重新计算事件和分布。临时假设会在返回前撤回。
    template <DetailRequest request = DetailRequest::Score>
    InfluenceResult<request> influence(ObservedBoard::CellId cell, Result &result, Position::Result &position, Position::Pool &positions,
                                       const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                       const FiftyFiftyData::Pool &templates);

  private:
    struct Workspace;
    struct Query;
    Workspace &getWorkspace();
};

} // 命名空间 mss
