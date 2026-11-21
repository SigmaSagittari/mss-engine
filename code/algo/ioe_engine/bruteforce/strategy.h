#pragma once

#include "algo/ioe_engine/bruteforce/click_query.h"
#include "core/datastructure/vector.h"

namespace mss {

struct IoeStrategy {
    struct Result {
        struct Ratio {
            int numerator;
            int denominator;
            vectorPool<int>::vector won; // 在 winsByExtraClicks 中从 0 次额外点击起计数。
            int discarded;
            int unsearched;
        };

        long double scoreSum;
        std::size_t completed;
        std::size_t won;
        std::size_t queries;
        bool exhausted;
        bool reachedUpperBound;
        vector<Ratio> distribution;
        vectorPool<int> winsByExtraClicks;

        Result() : scoreSum(0), completed(0), won(0), queries(0), exhausted(false), reachedUpperBound(false) {}
    };

    IoeMove solve(const vector<IoeLayout> &layouts, const IoeView &root, int spent, const IoeMove &fixed, int minIoePercent,
                  std::size_t queryLimit, Result &result, IoeObserver<std::size_t> progress = {});

  private:
    struct Candidate;
    struct Win;
    struct Answer;
    struct Cached;
    struct Key;
    struct Hash;
    struct Rewards;
    struct Prepared;
    struct Root;
    struct Expansion;
    struct Node;
    struct Workspace;
    Workspace &getWorkspace();
    long double score(const IoeLayout &layout, int total, const Rewards &rewards);
    vectorPool<Win>::vector store(vectorPool<Win> &traces, const vector<Win> &wins);
    bool provePair(const vector<IoeStrategyBoard> &boards, IoeClickQuery::Cache &distances,
                   const IoeView &start, int startSpent, Candidate a, Candidate b);
    Answer search(const vector<IoeLayout> &layouts, const Prepared &prepared,
                  IoeClickQuery::Cache &distances, FlatHashTable<Key, Cached, Hash> &exact,
                  vectorPool<Win> &traces, const Root &root, Expansion &expansion, const Node &node);
    void summarize(const vector<IoeLayout> &layouts, span<const Candidate> initial, span<const Win> wins, Result &result);
};

} // 命名空间 mss
