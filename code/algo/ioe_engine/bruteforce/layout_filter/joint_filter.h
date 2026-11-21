#pragma once

#include "algo/ioe_engine/bruteforce/layout_filter/constraints.h"

namespace mss {

struct IoeJointFilter {
    // matches 由调用方清空。成功时写入达标雷图并返回合法摆法数，未完成或关闭联合筛选时返回 0。
    // memoryBytes 限制联合筛选状态表的分配容量，扩容时包含新旧缓冲。
    // visited 累加联合筛选处理的中间状态，回退时保留已处理数量。
    std::size_t select(const IoeConstraints::Result &constraints, const IoeView &view, int spent, int minIoePercent,
                       std::uint64_t memoryBytes, vector<IoeLayout> &matches, std::uint64_t &visited, IoeObserver<std::uint64_t> progress);

  private:
    struct Weights;
    struct MineGraph;
    struct FrontierStep;
    struct DecisionDiagram;
    struct ThreeBV;
    struct Clicks;
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss
