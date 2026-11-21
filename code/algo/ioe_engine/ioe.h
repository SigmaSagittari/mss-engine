#pragma once

#include "algo/ioe_engine/bruteforce/layout_filter/layout_filter.h"
#include "algo/ioe_engine/bruteforce/strategy.h"
#include "game/position.h"

namespace mss {

struct Ioe {
    static constexpr int defaultMinIoePercent = 200;
    static constexpr std::uint64_t defaultJointMemory = 2560ULL * 1024 * 1024;
    static constexpr Position::Config defaultPositionConfig{{18, ShapeSolverConfig::Order::Auto, 10, nullptr}};

    struct FilterConfig {
        std::size_t maxLayouts;
        std::uint64_t jointMemory;
        int minIoePercent;
    };

    struct Config {
        FilterConfig filter;
        std::size_t maxQueries;
        bool moveOnlyIfProved;
    };

    enum class Status { Exact, TooManyLayouts, QueryLimit, InvalidBoard, ProofOnly };
    enum class Reason { None, Proved, Searched };

    struct Progress {
        enum class Phase { Enumeration, Filtering, Search };
        Phase phase;
        long double legal;
        IoeLayoutFilter::Progress filtered;
        std::size_t searchQueries;
    };

    struct FilterResult {
        Status status;
        long double legal;
        IoeLayoutFilter::Result filtered;
    };

    struct Result {
        Status status;
        Reason reason;
        IoeMove move;
        long double legal;
        std::size_t eligible;
        std::size_t rootDomCalls;
        std::uint64_t rootNodes;
        IoeStrategy::Result strategy;

        long double expectedScore() const { return strategy.scoreSum / legal; }
    };

    // checked 与 positions 来自同一初级盘面。flags 用行优先格号，点击数包含插旗和拔旗。
    Result analyze(Position::CheckedResult &checked, Position::Pool &positions, const IoeCells &flags, int spent,
                   const Config &config, IoeObserver<Progress> progress = {});
    FilterResult filterLayouts(const ObservedBoard::Result &board, const IoeCells &flags, int spent, const FilterConfig &config);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    Position::CheckedResult count(const ObservedBoard::Result &board);
};

} // 命名空间 mss
