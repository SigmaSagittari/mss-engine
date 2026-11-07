#pragma once

#include "algo/winrate_engine/selection/selection.h"
#include "core/datastructure/vector_pool.h"
#include "game/position_state.h"

namespace mss {

// 反复加深首招的后续搜索。analyze 建树并推进一次，update 用新预算接着推进。
// Pool 保存树和中断位置；Result 是查询句柄。
struct AnytimeSearch {
    struct Config {
        Selection::Config selection;
        // 续搜的全盘概率分析与 Observe 次数预算；每次各计 1，0 不续搜，根层初始化另算。
        // 一次局面到招法的评估、筛选及转存完整执行，可超支；开始下一局面前检查预算。
        long long nodeLimit;
        // 根层、第二步、更深层分别保留多少个候选；指定首招时根层不截断。
        int width[3];

        Config(Selection::Config selection, long long nodeLimit, int width0, int width1, int width2) : selection(selection), nodeLimit(nodeLimit) {
            width[0] = width0;
            width[1] = width1;
            width[2] = width2;
        }
    };

    struct Pool {
        // 树交替存放“点击哪个格”的候选节点和“显示某个数字后”的局面节点。
        // 候选节点用 cell，局面节点用 deltaIndex；child 分别指向数字结局或下一步候选。
        struct Node {
            // 最新完整后续分，不含推进奖励或根层补偿。
            // 危险：未展开局面为避免提前生成候选而延迟计算 value；必须原位构造，extendPosition 返回或 extendCandidate 结算本结局后才可读取。
            long double value;
            // JavaLite 首评推进乘数，用于续搜排序。
            long double progressMultiplier;
            // 同一 Pool 内的子节点切片；扩容后 offset/count 仍有效。
            vectorPool<Node>::vector child;
            union {
                ObservedBoard::CellId cell;
                int deltaIndex; // 对应 Pool::deltas 中的一条揭格增量。
            };
            // 未准备局面索引本层概率结果；招法为 -1，局面准备或终结后不再读取。
            int probabilityIndex;
            // depth 是当前点击步数；expandedDepth 是已完成评分的最深步数，根从 0 开始。
            int depth;
            int parent; // 父节点在同一 Pool 内的下标；根为 -1。
            int expandedDepth;
            // 后续已求完，无需继续加深。
            bool finished;
            // 根候选另有根补偿。
            bool root;
            // 分支位置是否已准备候选；expandedDepth 是该候选/位置已展开到的最大深度。
            bool prepared;
            Node(const Selection::Evaluation &evaluation, int depth, int parent) : value(evaluation.javaLite.value), progressMultiplier(evaluation.javaLite.progressMultiplier), child{}, cell(evaluation.cell), probabilityIndex(-1), depth(depth), parent(parent), expandedDepth(0), finished(evaluation.javaLite.deltas.count == 0), root(depth == 0), prepared(false) {}
            Node(int deltaIndex, int probabilityIndex, int depth, int parent) : progressMultiplier(1.0L), child{}, deltaIndex(deltaIndex), probabilityIndex(probabilityIndex), depth(depth), parent(parent), expandedDepth(0), finished(false), root(false), prepared(false) {}
        };

        vectorPool<Node> nodes;
        // 根候选占 nodes 的首个切片；补偿按 Node 下标保存，不能用 CellId 索引。
        vector<long double> rootCompensations;
        // 每个数字结局的概率及状态增量；其中切片借用 analyze 输入的 PositionState 记录池。
        vector<JavaLite::Delta> deltas;
        // 每层独立持有概率数组；节点只保存本层结果下标，释放不影响节点和分支权重。
        struct ProbabilityLayer;
        // 按局面深度索引，0 不使用；每层通过 new/delete 独立创建和释放。
        vector<ProbabilityLayer *> probabilities;
        int nextPosition; // 下次要推进的数字局面下标；-1 表示没有中断。
        JavaLite::Pool &pending; // 调用方持有的小池；只转存保留招法，须覆盖搜索池使用期。

        Pool(JavaLite::Pool &pending) : nextPosition(-1), pending(pending) {}
        ~Pool();
        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;
        void clear();
        void clearProbabilities();
        // 只在递归已经退出、state.probability 恢复根结果后调用。
        void releaseProbabilities();
        long double score(int index) const { const Node &candidate = nodes.data.data[index]; return candidate.value + (candidate.root ? rootCompensations.data[index] : 0.0L); }
    };

    struct Result {
        // 各根以 expandedDepth 发布最新完整评分，completedDepth 是所有根均已完成的层。
        vectorPool<Pool::Node>::vector roots;
        long long nodes; // 根层及续搜的全盘概率分析与 Observe 总次数；不含独立 LTR。
        SuggestResult::Reason reason;
        int completedDepth; // 所有根都已完成的最深层；部分根可能已经更深。
        bool finished; // 所有根都已求完，或者这是没有根的空句柄。

        Result() : roots{}, nodes(0), reason(SuggestResult::Reason::Finished), completedDepth(0), finished(true) {}
        // 先比较各根已发布深度，再比较该层评分；相同时保留原顺序。
        ObservedBoard::CellId recommended(const Pool &pool) const;
        // 复制当前完整分和推荐格；返回值自行拥有数组，不借用 Pool。
        SuggestResult snapshot(const Pool &pool) const;
    };

    // analyze/update 期间及续搜前须保持根盘面与 state 的池有效。
    // pool.pending 引用的概率池须绑定 state 的结构池和分布缓存。
    // cells=nullptr 要求入口已排除安全格，自动选根；否则按给定顺序保留全部根，内层仍自动选点。
    // 每次 update 从 config.nodeLimit 读取本次计算节点预算；analyze 先完成根层评分，再使用预算。
    // 续搜只允许调整预算；显式首招按输入顺序全部保留，内层照常剪枝和限制宽度。
    // details 由调用方持有；analyze 重置并记录第 0 层，update 追加完成层，同时发布该层子评分。
    Result analyze(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, const vector<ObservedBoard::CellId> *cells = nullptr, SuggestResult::Details *details = nullptr);
    void update(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, SuggestResult::Details *details = nullptr);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    void selectCandidates(vector<Selection::Evaluation> &evaluatedMoves, int limit);
    void selectRoot(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Selection::Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Selection::Result &selected, vector<Selection::Evaluation> &evaluatedMoves, JavaLite::Pool &pending, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details);
    long long prepareNode(PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Selection::Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Selection::Result &selected, vector<Selection::Evaluation> &evaluatedMoves, JavaLite::Pool &pending, int width);
    vectorPool<int>::vector orderMoves(const Pool &pool, vectorPool<Pool::Node>::vector children, int maxDepth, int resumeMove, vectorPool<int> &order);
    void appendDeltas(Pool &pool, int index, const JavaLite::Result &evaluated, const JavaLite::Pool &values);
    Result buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details);
    // 完成返回 -1；预算耗尽返回待推进局面的下标。
    int extendPosition(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long &budget, span<const int> resume);
    int extendCandidate(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, long long &budget, span<const int> resume);
};

} // 命名空间 mss
