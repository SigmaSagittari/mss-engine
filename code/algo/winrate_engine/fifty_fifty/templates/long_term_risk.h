#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"
#include "game/position_state.h"

namespace mss {

struct TemplateLongTermRiskEngine;

struct TemplateLongTermRisk {
    struct Config {
        int eventLimit;
        Config(int eventLimit) : eventLimit(eventLimit) {}
    };

    struct Description {
        long double bodyUnion;
        int noBodyRestrictionBins;
        Description(long double bodyUnion, int noBodyRestrictionBins) : bodyUnion(bodyUnion), noBodyRestrictionBins(noBodyRestrictionBins) {}
    };

    struct Pool {
        struct Point;
        struct Pattern;
        struct Group;
        struct Blocker;
        struct Check;
        struct Table;

        Pool();
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;

      private:
        friend struct TemplateLongTermRisk;
        friend struct TemplateLongTermRiskEngine;
        vectorPool<Point> points;
        vectorPool<Pattern> patterns;
        vectorPool<Group> groups;
        vectorPool<Blocker> blockers;
        vectorPool<Check> checks;
        vectorPool<int> outputs;
        vector<Table> tables;
    };

    struct Result {
        struct Match;
        struct Event;
        struct BlockerSet;
        struct Link;
        struct CachedCount;
        struct CellCache;

        Result() = default;
        Result(const Result &other);
        Result &operator=(const Result &other);

      private:
        friend struct TemplateLongTermRisk;
        friend struct TemplateLongTermRiskEngine;
        // 危险：以下是当前棋盘的实际输入信息；update 写入，后续查询只在 update 完成后读取。
        const Pool *pool;
        int tableIndex;
        int rows, cols, eventLimit;
        vector<long double> probabilities;
        vector<unsigned char> hidden;
        vector<Match> matches;
        vector<Event> events;
        vector<ObservedBoard::CellId> eventCells, blockerCells;
        vector<BlockerSet> blockerSets;
        vector<int> offsets;
        vector<Link> links;
        vector<unsigned short> countKeys;
        vector<CachedCount> counts;
        vector<long double> noBodyRestrictions;
        FlatHashTable<unsigned long long, int> countIndex;
        vector<CellCache> cells;
    };

    // 模板前缀范围为 1..maxPrefix()；事件上限必须为正数。
    int maxPrefix();
    // 查询前须已更新至对应根盘面；更新会清空旧结果与缓存，Pool 中的模板表必须比 Result 活得久。
    void update(Result &result, PositionState &state, const Probability::Pool &probabilities, int prefix, const Config &config, Pool &pool);
    // 返回归一化到全盘布局数的前N方向事件联合风险补偿；同组成员共享 Result 中的缓存。
    long double influence(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, Result &result, const ShapeSolverConfig &shapeConfig);
    // describe 使用条件 D=该格为雷，并按 influence 的互相必安组收集事件；返回值按 D 内布局数归一并缓存至下次 update。
    Description describe(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, Result &result, const ShapeSolverConfig &shapeConfig);
    // 读取 P(无body且恰有k个保留限制方向事件 | D)；须先对该格调用 describe，k 范围为 [0, bins)。
    long double noBodyByRestrictionCount(const Result &result, ObservedBoard::CellId cell, int k);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    friend struct TemplateLongTermRiskEngine;
};

} // 命名空间 mss
