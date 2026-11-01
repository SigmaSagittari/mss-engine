#pragma once

#include <cstdint>

#include "algo/winrate_engine/fifty_fifty/old_java/semi.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct TemplateSemi {
    // 一个模板前缀编译出的只读匹配表，包含旋转、镜像和边界/雷墙判断。
    // 相同前缀由 Pool 共享；构表缓冲由 Workspace 复用，不依赖具体盘面。
    struct Pool {
        enum class Kind : unsigned char;
        struct Point;
        struct Test;
        struct Check;
        struct Prefix;
        struct Table;
        vectorPool<Check> checks;
        vectorPool<Prefix> prefixes;
        vectorPool<Point> runs;
        vectorPool<int> starts;
        vector<Table> tables;
        Pool();
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;
    };

    int templateCount();
    void detect(Semi::Result &result, const PositionState &state, const Probability::Pool &probabilities, int prefix, Pool &pool);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    struct Facts;
    static constexpr long double halfTolerance = 1e-9L;
    int matchPrefix(const Pool::Table &table, const Pool &pool, int node, const Facts &facts, int root);
};

} // 命名空间 mss
