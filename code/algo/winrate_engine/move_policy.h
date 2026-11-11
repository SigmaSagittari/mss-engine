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
};

} // 命名空间 Suggest

struct MovePolicy {
    enum class Algorithm { LowRisk, JavaEval, Anytime };

    struct Config {
        Algorithm algorithm;
        const Selection::Config *selection; // JavaEval 和 Anytime 借用的根层候选配置，LowRisk 传 nullptr。
        const FiftyFifty::Config *fiftyFifty; // JavaEval 和 Anytime 使用，LowRisk 可为空。
        // 借用所选算法的配置，未选算法的指针可为空。
        const JavaEval::Config *javaEval;
        const AnytimeSearch::Config *anytime;

        struct Brute {
            int limit; // 布局数小于此值时用暴力求解，0 表示关闭。
            const BruteForce::Config *config; // 借用配置，limit 为 0 时可为空。

            void hash(U128Hasher &hasher) const {
                hasher.mix(limit);
                if (limit != 0) config->hash(hasher);
            }
        } brute;

        void hash(U128Hasher &hasher) const;
    };

    // 局面须有解。有必安时直接返回，否则按需向 Position 取概率。
    // knownRemainingSafe 可借用该盘面已知的未揭安全格数。为空时按盘面计算。
    // 指定格互异且尚未揭开。需要猜测时，由 Anytime 的 width[0] 决定保留全部指定格还是按分数取前几格。
    // search 为空时，返回前清除本次 Anytime 的搜索树和写入 positions 的 Delta 数据。
    // search 非空时保留续搜所需的数据。放弃时，在改动原盘前调用 suggestions.anytime.clear(positions)。
    SuggestResult suggest(Position::Result &position, Position::Pool &positions, const Config &config,
                          Suggest::Pool &suggestions, const int *knownRemainingSafe = nullptr,
                          span<const ObservedBoard::CellId> requested = {}, AnytimeSearch::Result *search = nullptr);

  private:
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss
