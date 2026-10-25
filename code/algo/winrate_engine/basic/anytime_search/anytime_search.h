#pragma once

#include <deque>

#include "algo/winrate_engine/basic/java_eval/java_eval.h"
#include "core/datastructure/vector_pool.h"
#include "game/position_state.h"

namespace mss {

// 反复加深首招的后续搜索。analyze 建树并推进一次，update 用新预算接着推进。
// Pool 保存整棵树和暂停位置；Result 是这棵树的查询句柄；Workspace 只复用临时内存。
struct AnytimeSearch {
    struct Config {
        JavaEval::Config javaEval;
        // 续搜的全盘概率分析与 Observe 次数预算；每次各计 1，0 不续搜，根层初始化另算。
        // 候选观察批次和单格数字结局各自完整计算，可超支，下一阶段开始前检查预算。
        long long nodeLimit;
        // 根层、第二步、更深层分别保留多少个候选；指定首招时根层不截断。
        int width[3];

        Config(JavaEval::Config javaEval, long long nodeLimit, int width0, int width1, int width2) : javaEval(javaEval), nodeLimit(nodeLimit) {
            width[0] = width0;
            width[1] = width1;
            width[2] = width2;
        }
    };

    struct Pool {
        // 树交替存放“点击哪个格”的候选节点和“显示某个数字后”的局面节点。
        // 候选节点用 cell，局面节点用 deltaIndex；child 分别指向数字结局或下一步候选。
        struct Node {
            // 最新完整后续分，不含推进奖励；根候选另包含旧 LTR 修正。
            long double value;
            // 推进奖励只参与选招，不进入 value 回传。
            long double progressMultiplier;
            // 同一 Pool 内的子节点切片；扩容后 offset/count 仍有效。
            vectorPool<Node>::vector child;
            union {
                ObservedBoard::CellId cell;
                int deltaIndex; // 对应 Pool::deltas 中的一条揭格增量。
            };
            // depth 是当前点击步数；expandedDepth 是已完成评分的最深步数，根从 0 开始。
            int depth;
            int expandedDepth;
            // 后续已求完，无需继续加深。
            bool finished;
            // 根候选保留根层结果顺序；续搜顺序由 Frame 冻结，不参与树内宽度。
            bool root;
            // 分支位置是否已准备候选；expandedDepth 是该候选/位置已展开到的最大深度。
            bool prepared;
            Node(int item, int depth, bool isPosition) : value(0.0L), progressMultiplier(1.0L), child{}, depth(depth), expandedDepth(0), finished(false), root(false), prepared(false) {
                if (isPosition) deltaIndex = item;
                else cell = item;
            }
            long double score() const { return value * progressMultiplier; }
        };

        vectorPool<Node> nodes;
        // 根候选是 nodes 的首个切片；旧 LTR 乘数按根下标保存，内层节点不占存储。
        vector<long double> rootRiskMultipliers;
        // 每个数字结局的概率及状态增量；其中切片借用 analyze 输入的 PositionState 记录池。
        vector<JavaEval::Delta> deltas;
        // 每个搜索深度一份续算游标；预算耗尽后保留顺序、下一结局及未完成的概率合计。
        struct Frame {
            vector<int> order; // 这一层候选的 Node 下标，按选招分排序后冻结。
            int next; // order 中下一招的位置。
            int nextOutcome; // 当前招法的 child 中下一个数字结局的位置。
            long double value; // 当前候选已完成的数字结局之概率加权和。
            bool settled; // 已完成的结局是否都已经求到终局。
            bool active; // 当前深度是否有待续算的候选顺序。

            Frame() : next(0), nextOutcome(0), value(0.0L), settled(true), active(false) {}
        };
        std::deque<Frame> frames;
        // 同时最多准备一个新位置；安全假设、数字结局与 Observe 按实际调用次数扣节点。
        Selection::Result selected;
        vector<JavaEval::Result> evaluatedMoves;
        vectorPool<JavaEval::Delta> pendingDeltas; // 当前新局面的评分暂存，建好子节点后转存到 deltas。
        int next; // selected 中下一个尚未准备、评分的候选下标。
        bool preparing; // 当前新局面的候选评分尚未完成，update 需要从 next 接着算。

        Pool() : next(0), preparing(false) {}
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

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : javaEval(basic, structure, probability) {}

        JavaEval::Workspace javaEval;
    };

    // analyze/update 期间及续搜前不得改变根盘面或清空 state 的池；Workspace 可顺序复用。
    // cells=nullptr 要求入口已排除安全格，自动选根；否则按给定顺序保留全部根，内层仍自动选点。
    // 每次 update 从 config.nodeLimit 读取本次计算节点预算；analyze 先完成根层评分，再使用预算。
    // 续搜只允许调整预算；显式首招按输入顺序全部保留，内层照常剪枝和限制宽度。
    // details 由调用方持有；analyze 重置并记录第 0 层，update 追加完成层，同时发布该层子评分。
    static Result analyze(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells = nullptr, SuggestResult::Details *details = nullptr);
    static void update(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, SuggestResult::Details *details = nullptr);

  private:
    // 一次 analyze/update 持有的中间结果；各节点显式覆写并消费，容量在本次遍历复用。
    struct Evaluation {
        // 扩展 deque 不移动祖先帧保存的概率结果。
        std::deque<Probability::Result> probabilities;
    };

    static void swapProbability(Probability::Result &left, Probability::Result &right);
    // remainingSafe 含所有未揭开的安全格；forcedSafes 只数盘面上显式标记的 ForcedSafe。
    static void countSafe(const PositionState &state, int &remainingSafe, int &forcedSafes);
    static void selectCandidates(vector<JavaEval::Result> &evaluatedMoves, int limit);
    static void selectRoot(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details);
    static bool prepareNode(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, int width, long long &budget, long long &nodeCount);
    static void orderMoves(Pool &pool, vectorPool<Pool::Node>::vector children, Pool::Frame &frame);
    static void appendDeltas(Pool &pool, int index, const JavaEval::Result &evaluated, const vectorPool<JavaEval::Delta> &deltas);
    static Result buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, const vector<ObservedBoard::CellId> *cells, SuggestResult::Details *details);
    static long long extend(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, Evaluation &evaluation, long long nodeBudget, int maxDepth, SuggestResult::Details *details);
    static void extendPosition(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, Evaluation &evaluation, long long &budget, long long &nodeCount, bool &complete);
    static void extendCandidate(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, const FiftyFifty::Config &fiftyConfig, FiftyFifty::Pool &fiftyPool, Workspace &workspace, Evaluation &evaluation, long long &budget, long long &nodeCount, bool &complete);
};

} // 命名空间 mss
