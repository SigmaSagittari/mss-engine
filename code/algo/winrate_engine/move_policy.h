#pragma once

#include "algo/winrate_engine/basic/anytime_search/anytime_search.h"
#include "algo/winrate_engine/basic/java_eval/java_eval.h"
#include "algo/winrate_engine/bruteforce/bruteforce.h"
#include "algo/winrate_engine/fifty_fifty/fifty_fifty.h"
#include "core/datastructure/flat_hashtable.h"

namespace mss {

namespace Suggest {

// 调用方可跨局串行复用选招池。
// 改用另一盘面前，须用原盘的 Position::Pool 清理未完成的 Anytime 搜索。
struct Pool {
    struct Entry {
        ObservedBoard::CellId cell;
        SuggestResult::Engine engine;
        SuggestResult::Reason reason;
    };

    Combination::Pool combinations;
    FiftyFifty::Pool fiftyFifty;
    AnytimeSearch::Pool anytime;
    FlatHashTable<U128, Entry, U128Hash> cache;

    // 清除搜索、模板和建议缓存。若有未完成的搜索，positions 须是该搜索使用的局面池。保留组合数表。
    void clear(Position::Pool &positions);
};

} // 命名空间 Suggest

struct MovePolicy {
    enum class Algorithm { LowRisk, JavaEval, Anytime };

    struct Config {
        Algorithm algorithm;

        struct Guess {
            const Selection::Filter &selection;
            const Selection::Exchange *exchange; // Anytime 自动选首招时借用。JavaEval 传 nullptr。
            bool longTermRisk; // 是否计算粗 LTR 和首招的正式 LTR 补偿。
            FiftyFifty::Config fiftyFifty;
            // 借用所选算法的配置，未选算法的指针可为空。
            const JavaEval::Config *javaEval;
            const AnytimeSearch::Config *anytime;
        };
        const Guess *guess; // 借用本次选招的配置。LowRisk 传 nullptr。

        struct Brute {
            int limit; // 布局数小于此值时用暴力求解。
            BruteForce::Config config;
        };
        const Brute *brute; // 借用暴力接管配置。关闭时传 nullptr。

        void hash(U128Hasher &hasher) const;
    };

    // 局面须有解。有必安时直接返回，否则按需向 Position 取概率。
    // 对局已维护未揭安全格数。返回前释放本次搜索的临时结果。
    SuggestResult suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                          Suggest::Pool &suggestions, int remainingSafe);

    // 指定格互异且尚未揭开。需要猜测时，由 Anytime 的 width[0] 决定保留全部指定格还是按分数取前几格。
    // 同时输出评分明细并保留 Anytime 的续搜结果。放弃时，在改动原盘前调用 suggestions.anytime.clear(positions)。
    SuggestResult suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                          Suggest::Pool &suggestions, const vector<ObservedBoard::CellId> &requested,
                          AnytimeSearch::Result &search, SuggestResult::Details &details);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    template <bool Details>
    SuggestResult suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                          Suggest::Pool &suggestions, int remainingSafe, const vector<ObservedBoard::CellId> &requested,
                          AnytimeSearch::Result &search, SuggestResult::Details &details);
};

} // 命名空间 mss
