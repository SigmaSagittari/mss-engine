#pragma once

#include <type_traits>

#include "game/position.h"

namespace mss {

struct LongTermRisk {
    enum class DetailRequest { Score, Details };

    struct Config {
        long double influenceThreshold;

        void hash(U128Hasher &hasher) const { hasher.mix(influenceThreshold); }
    };

    struct Result {
        Grid<long double> influences; // 全盘扫描得到的补偿布局数。

      private:
        friend struct LongTermRisk;
        vector<long double> enablers;
        Grid<long double> tallies; // 各模板以左上格索引，-1 表示尚未计数。
    };

    struct SemiResult {
        enum class Kind { None, Forced, Semi };
        Kind kind;
        vector<ObservedBoard::CellId> cells;

        SemiResult() : kind(Kind::None) {}
    };

    struct Score {
        long double compensationWays;
    };

    struct Details {
        long double compensationWays;
        long double body, restriction; // 已知该格为雷时的单模板本体及限制命中概率。
    };

    // 盘面、概率和池须匹配，且全盘有解。传入 semi 时，找到首组死猜即返回。
    void update(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                Probability::Pool &probabilities, const Config &config,
                SemiResult *semi = nullptr);
    // 返回补偿布局数，result 来自同一盘面的完整扫描。
    Score influence(ObservedBoard::CellId cell, Position::Result &position, Position::Pool &positions,
                    const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                    Result &result);
    // cell 的雷率大于零。重新扫描当前盘面，再计算补偿和明细。
    Details describe(ObservedBoard::CellId cell, Position::Result &position, Position::Pool &positions,
                     const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                     const Config &config);

  private:
    template <DetailRequest request>
    using InfluenceResult = std::conditional_t<request == DetailRequest::Details, Details, Score>;

    template <typename Restriction>
    void scan(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
              Probability::Pool &probabilities, const Config &config,
              SemiResult *semi, Restriction restriction);
    template <DetailRequest request>
    InfluenceResult<request> evaluate(ObservedBoard::CellId cell, Position::Result &position, Position::Pool &positions,
                                       const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                       Result &result);

    struct Workspace;
    struct Pattern;
    struct Match;
    Workspace &getWorkspace();
    long double mineTally(ObservedBoard::CellId cell, const Position::Result &position, const Position::ProbabilityResult &probability, const Probability::Pool &pool);
    bool match(Match &found, const Pattern &pattern, int row, int col, int maxMines, const Position::Result &position,
               const Position::ProbabilityResult &probability, const Probability::Pool &pool);
    bool checkChangedConstraints(const Position::Result &position, const Basic::Delta &delta, const Basic::Pool &pool);
    long double count(const Match &found, int tileCount, Position::Result &position, Position::Pool &positions,
                      Probability::Pool &probabilities);
};

} // 命名空间 mss
