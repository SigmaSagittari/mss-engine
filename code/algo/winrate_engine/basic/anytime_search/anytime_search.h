#pragma once

#include "algo/winrate_engine/result_type.h"
#include "algo/winrate_engine/selection/selection.h"
#include "core/datastructure/grid.h"

namespace mss {

struct AnytimeSearch {
    struct Config {
        Selection::Config selection; // 子局面的候选筛选。
        long double progressContribution; // 非负。
        long long nodeLimit; // 本次续搜预算，0 只保留已完成的评分。根层初始化另计。
        // 首招数、第二步候选数、更深的候选数。首招数为 0 时保留全部传入候选，后两项为正。
        int width[3];

        void hash(U128Hasher &hasher) const {
            selection.hash(hasher);
            hasher.mix(progressContribution, (std::uint64_t) nodeLimit, width[0], width[1], width[2]);
        }
    };

    struct Pool {
        struct Branch;
        struct ProbabilityLayer;

        struct Move {
            ObservedBoard::CellId cell;
            int depth, parent; // parent 索引父局面，根候选为 -1。
            int expandedDepth; // 已发布的评分深度。
            bool finished;
            long double value, progressMultiplier; // 原始分与排序时使用的推进乘数。
            vectorPool<Branch>::vector children; // 数字结局在同一池内的切片。
        };

        struct Branch {
            Position::Delta delta; // 借用 analyze 所用 Position::Pool 的记录，搜索完成或清池后失效。
            long double probability, value;
            int digit, knownSafe, parent;
            int expandedDepth;
            int probabilityIndex; // 本深度概率结果的下标，已消费为 -1。
            bool finished;
            vectorPool<Move>::vector children;
        };

        vectorPool<Move> moves;
        vectorPool<Branch> branches;
        vector<long double> rootCompensations;
        vector<ProbabilityLayer *> probabilities;
        Position::Delta deltaStart; // 搜索开始前，四份 Delta 记录在 Position::Pool 中的起点。
        bool hasDeltas;
        int remainingSafe, nextBranch;

        Pool() : deltaStart({}, {}, {}), hasDeltas(false), remainingSafe(0), nextBranch(-1) {}
        ~Pool();
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;

        // 放弃搜索并使本池的 Result 失效。未完成时传入原盘的 Position::Pool，在改动原盘前调用。
        // 若原盘的 Position::Pool 仍在使用，销毁本池前也须先清理未完成的搜索。
        void clear(Position::Pool &positions);
        void clearProbabilities();
    };

    struct Result {
        vectorPool<Pool::Move>::vector roots; // 借用同一次搜索的 Pool，清池或重新 analyze 后失效。
        long long nodes; // 全盘概率分析与 Observe 次数；独立 LTR 不计入。
        SuggestResult::Reason reason;
        int completedDepth;
        bool finished;

        Result() : roots{}, nodes(0), reason(SuggestResult::Reason::Finished), completedDepth(0), finished(true) {}
        ObservedBoard::CellId recommended(const Pool &pool) const;
        SuggestResult snapshot(const Pool &pool) const;
    };

    // 候选、概率及 remainingSafe 对应同一盘面，Structure 已同步。概率切片来自 positions.probabilities。
    // longTermRisk 按盘面行列存放各首招的补偿。
    // width[0] 为 0 时按输入顺序保留全部首招。为正数时按含 LTR 的分数取前 width[0] 格。
    // 根局面、Position::Pool 的增量记录及概率池借用的分布和组合数池须覆盖续搜期。
    // 开始搜索时把已判定的必雷写入 position，返回后保留。写入后，查询当前盘面须重新向 Position 取概率。
    // 用本池搜索另一盘前，先用原盘的 Position::Pool 清理未完成的搜索。
    Result analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                   const Selection::Result &selected, const Grid<long double> &longTermRisk,
                   int remainingSafe, const Config &config, Pool &pool);
    Result analyze(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                   const Selection::Result &selected, const Grid<long double> &longTermRisk,
                   int remainingSafe, const Config &config, Pool &pool, SuggestResult::Details &details);

    // 每次推进使用 config.nodeLimit，续搜只调整预算。返回前恢复根盘面，搜完后回收本次搜索的 Delta。
    // 记录明细时，传入 analyze 初始化的同一份 details，只在首招完成一层后更新。
    void update(Result &result, Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool);
    void update(Result &result, Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool,
                SuggestResult::Details &details);

  private:
    struct Workspace;
    struct Outcome;
    struct Evaluation;

    Workspace &getWorkspace();
    Evaluation evaluate(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                        Probability::Pool &probabilities, const Config &config, const Selection::Candidate &candidate,
                        int remaining, long double floor, vectorPool<Outcome> &outcomes, long double compensation = 0.0L);
    vectorPool<Pool::Move>::vector appendMoves(Pool &pool, int parent, int depth, const vector<Evaluation> &evaluations,
                                               const vectorPool<Outcome> &outcomes, Probability::Pool &probabilities);
    Result buildRoot(Position::Result &position, Position::Pool &positions, Position::ProbabilityResult probability,
                     const Selection::Result &selected, const Grid<long double> &longTermRisk,
                     int remainingSafe, const Config &config, Pool &pool);
    // 为首招之后的局面选招。进入第二范围和兜底看等价、支配剪枝前的候选数，评分只取前 limit 格。
    // evaluations 与 outcomes 由调用方清空。实际执行的 Observe 和条件概率分析从 budget 扣除。
    void select(Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                Probability::Pool &probabilities, const Config &config, int depth, int remaining, long long &budget,
                vector<Evaluation> &evaluations, vectorPool<Outcome> &outcomes);
    void preparePosition(Position::Result &position, Position::Pool &positions, const Config &config,
                         Pool &pool, int index, long long &budget);
    vectorPool<int>::vector orderMoves(const Pool &pool, vectorPool<Pool::Move>::vector children, int depth, int resumeMove, vectorPool<int> &order);
    int extendPosition(Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool,
                       int index, int depth, long long &budget, span<const int> resume);
    int extendMove(Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool,
                   int index, int depth, long long &budget, span<const int> resume);
    void releaseSearchData(Position::Pool &positions, Pool &pool);
    void releaseProbabilities(vector<Pool::ProbabilityLayer *> &probabilities);
    long double rootScore(const vector<Pool::Move> &moves, const vector<long double> &compensations, int index);
    void record(const Pool &pool, int index, SuggestResult::Details &details);

    template <typename Record>
    void updateImpl(Result &result, Position::Result &position, Position::Pool &positions, const Config &config, Pool &pool, Record record);
};

} // 命名空间 mss
