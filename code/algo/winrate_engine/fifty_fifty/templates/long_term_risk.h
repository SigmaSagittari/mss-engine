#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/probability/probability.h"
#include "algo/probability_engine/shape_solver/shape_solver_config.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/vector.h"
#include "game/position_state.h"

namespace mss {

struct TemplateLongTermRiskEngine;

struct TemplateLongTermRisk {
    struct Config {
        int eventLimit;
        explicit Config(int eventLimit) : eventLimit(eventLimit) {}
    };

    struct Description {
        long double bodyUnion;
        int noBodyRestrictionBins;
        Description(long double bodyUnion, int noBodyRestrictionBins) : bodyUnion(bodyUnion), noBodyRestrictionBins(noBodyRestrictionBins) {}
    };

    struct Pool {
        struct Point { int row, col; };
        // 危险：appendPattern 先写核心范围；extra 与 group 范围由 buildPatternBlockers 首次写入，模板编译完成前不读取这些字段。
        struct Pattern { int pointBegin, aCount, coreCount, wallCount, extraBegin, extraCount, groupBegin, groupEnd; };
        struct Group { int blockerBegin, blockerEnd; };
        struct Blocker { unsigned long long parent, extra, core; };
        struct Check { int row, col, skip, outputBegin, outputEnd; };
        struct BuildNode { Point point; int firstChild, nextSibling, outputHead; };
        struct BuildOutput { int pattern, next; };
        struct BuildChild { int wallBegin, wallCount; };

        struct Table {
            explicit Table(int prefix);

          private:
            friend struct TemplateLongTermRisk;
            friend struct TemplateLongTermRiskEngine;
            int prefix;
            vector<Point> points;
            vector<Pattern> patterns;
            vector<Group> groups;
            vector<Blocker> blockers;
            vector<Check> checks;
            vector<int> outputs;
        };

        Pool();
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;
        ~Pool();

      private:
        friend struct TemplateLongTermRisk;
        friend struct TemplateLongTermRiskEngine;
        vector<Table *> tables;
    };

    struct Result {
        struct Match { int pattern, row, col, eventBegin; };
        // 危险：为避免批量登记事件时清零未计算的条件和概率字段，基础切片及possible仅在prepared为true后读取，由prepareEvent首次写入；probability仅在probabilityValid为true后读取，由eventProbability首次写入；appendMatch只写事件身份和两个false标志。
        struct Event { int match, direction, kind, baseMineBegin, baseMineCount, baseSafeBegin, baseSafeCount, blockerBegin, blockerCount; long double probability; bool prepared, possible, probabilityValid; };
        struct BlockerSet { int cellBegin, cellCount; };
        struct Link { int match, slot; };
        struct CachedCount { int keyBegin, keyLength, next; long double value; };
        struct CellCache {
            // valid 与 descriptionValid 分别表示影响值与分解数据已经计算。
            // 危险：复用时只初始化两个有效标志，避免每次更新清零所有缓存值；influence 由 cacheGroup 或 influence 首次写入，分解字段由 cacheDescription 首次写入，仅在对应标志为 true 后读取。
            int noBodyRestrictionBegin, noBodyRestrictionBins;
            long double influence, bodyUnion;
            bool valid, descriptionValid;
        };

      private:
        friend struct TemplateLongTermRisk;
        friend struct TemplateLongTermRiskEngine;
        // 危险：以下是当前棋盘的实际输入信息；update 写入，后续查询只在 update 完成后读取。
        const Pool::Table *table;
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

    struct Workspace {
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        struct Term { int event, family; long double weight, mass; };
        struct Factor { int baseKey, blockerBegin, blockerCount; long double weight, bound; };
        struct DescriptionFactor { int baseKey, blockerBegin, blockerCount, bodyCount, restrictionCount; long double bound; };
        struct DescriptionNode { int valueBegin; };
        // index 0 专用于矛盾或零布局条件；其余项都是已计数的实际条件。
        struct Domain { int next; long double mass; };
        struct Tail { int factor, next; };
        struct Node { long double value; };
        struct BlockerWork { int cellBegin, cellCount; };

        // influence/describe 在首次读取前重建所需范围；这些缓冲只复用容量，不依赖上次查询内容。
        vector<ObservedBoard::CellId> mines, safes, originalCells, groupCells, eventBlockerCells;
        vector<BlockerWork> eventBlockers;
        vector<int> denseCells, factorBlockers;
        vector<Term> terms;
        vector<Factor> factors;
        vector<DescriptionFactor> descriptionFactors;
        vector<Domain> domains;
        vector<Tail> tails;
        vector<Node> nodes;
        vector<DescriptionNode> descriptionNodes;
        vector<long double> descriptionValues;
        vector<unsigned long long> mineWords, safeWords, domainMineWords, domainSafeWords, mergeMineWords, mergeSafeWords;
        FlatHashTable<unsigned long long, int> domainIndex;
        FlatHashTable<unsigned long long, int> tailIndex, nodeIndex, descriptionNodeIndex;
        vector<ObservedBoard::CellId> changedCells;
        Probability::Result forcedProbability;
        PositionState::Reveal reveal;
        vector<unsigned short> countSignature;
        vector<int> order, pendingMatches, pendingEvents;
        vector<unsigned long long> expressionWords, nextExpressionWords;
        vector<int> expressions, nextExpressions;
        vector<Pool::BuildNode> buildNodes;
        vector<Pool::BuildOutput> buildOutputs;
        vector<Pool::BuildChild> buildChildren;
        vector<Pool::Point> buildChildWalls;
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability);

      private:
        friend struct TemplateLongTermRiskEngine;
    };

    // 模板前缀范围为 1..maxPrefix()；事件上限必须为正数。
    static int maxPrefix();
    // 查询前须已更新至对应根盘面；更新会清空旧结果与缓存，Pool 中的模板表必须比 Result 活得久。
    static void update(Result &result, PositionState &state, int prefix, const Config &config, Pool &pool, Workspace &workspace);
    // 返回归一化到全盘布局数的前N方向事件联合风险补偿；同组成员共享 Result 中的缓存。
    static long double influence(ObservedBoard::CellId cell, PositionState &state, Result &result, const ShapeSolverConfig &shapeConfig, Workspace &workspace);
    // describe 使用条件 D=该格为雷，并按 influence 的互相必安组收集事件；返回值按 D 内布局数归一并缓存至下次 update。
    static Description describe(ObservedBoard::CellId cell, PositionState &state, Result &result, const ShapeSolverConfig &shapeConfig, Workspace &workspace);
    // 读取 P(无body且恰有k个保留限制方向事件 | D)；须先对该格调用 describe，k 范围为 [0, bins)。
    static long double noBodyByRestrictionCount(const Result &result, ObservedBoard::CellId cell, int k);
};

} // 命名空间 mss
