#pragma once

#include "algo/ioe_engine/ioe_type.h"
#include "algo/probability_engine/basic/basic.h"
#include "core/datastructure/vector.h"

namespace mss {

struct IoeLayoutFilter {
    struct Progress {
        std::size_t analyzed;
        std::size_t eligible;
        std::size_t domCalls;
        std::uint64_t nodes;
    };

    struct Result {
        vector<IoeLayout> layouts;
        std::size_t domCalls;
        std::uint64_t nodes;

        Result() : domCalls(0), nodes(0) {}
    };

    // basic 由 board 推得，且盘面已有至少一种合法摆法。
    void select(const ObservedBoard::Result &board, const Basic::Result &basic, const IoeView &view, int spent, int minIoePercent,
                std::uint64_t memoryBytes, Result &result, IoeObserver<Progress> progress = {});

  private:
    int threeBV(const IoeCells &mines);
};

} // 命名空间 mss
