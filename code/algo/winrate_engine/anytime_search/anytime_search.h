#pragma once

#include <deque>

#include "algo/winrate_engine/java_eval/java_eval.h"
#include "core/datastructure/vector_pool.h"
#include "game/position_state.h"

namespace mss {

// 迭代加深搜索：Pool 拥有节点；分支增量借用同一 PositionState 的记录池。
struct AnytimeSearch {
    struct Config {
        JavaEval::Config javaEval;
        int width[3];
        bool fullAnalysis;

        Config(JavaEval::Config javaEval, int width0, int width1, int width2, bool fullAnalysis) : javaEval(javaEval), fullAnalysis(fullAnalysis) {
            width[0] = width0;
            width[1] = width1;
            width[2] = width2;
        }
    };

    struct Pool {
        struct Node {
            long double value;
            long double multiplier;
            // 同一 Pool 内的子节点切片；扩容后 offset/count 仍有效。
            vectorPool<Node>::vector child;
            // 候选和位置交替出现：候选使用 cell，位置使用 transitions 下标，两者不同时占存储。
            union {
                ObservedBoard::CellId cell;
                int transition;
            };
            int depth;
            int expandedDepth;
            bool finished;
            // 根候选保留根层结果顺序；续搜顺序由 Frame 冻结，不参与树内宽度。
            bool root;
            // 分支位置是否已准备候选；expandedDepth 是该候选/位置已展开到的最大深度。
            bool prepared;
            Node(int item, int depth, bool position) : value(0.0L), multiplier(1.0L), child{}, depth(depth), expandedDepth(0), finished(false), root(false), prepared(false) {
                if (position) transition = item;
                else cell = item;
            }
        };

        vectorPool<Node> nodes;
        // 只有位置节点占用分支存储，候选节点不再携带 Delta 和结局概率。
        vector<JavaEval::Branch> transitions;
        // 仅当前路径持有游标；中断后按原顺序续算，不把搜索状态放进 Workspace。
        struct Frame {
            vector<int> order;
            int next;
            int branch;
            long double value;
            bool settled;
            bool active;

            Frame() : next(0), branch(0), value(0.0L), settled(true), active(false) {}
        };
        std::deque<Frame> frames;
        // 同时最多准备一个新位置；预算只在一个候选结算后减少。
        Selection::Result selected;
        vector<JavaEval::Peer> peers;
        vector<JavaEval::Analysis> analyses;
        vectorPool<JavaEval::Branch> branches;
        int next;
        bool preparing;
        bool safe;

        Pool() : next(0), preparing(false), safe(false) {}
    };

    struct Result {
        // 各根以 expandedDepth 发布最新完整评分，completedDepth 是所有根均已完成的层。
        vectorPool<Pool::Node>::vector roots;
        long long node;
        int completedDepth;
        bool finished;

        Result() : roots{}, node(0), completedDepth(0), finished(true) {}
        // 先比较各根已发布深度，再比较该层评分；相同时保留原顺序。
        ObservedBoard::CellId recommended(const Pool &pool) const;
    };

    struct Workspace {
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : javaEval(basic, structure, probability) {}

        JavaEval::Workspace javaEval;
        // 根层等价剪枝的临时签名。
        vector<U128> identities;
    };

    // analyze/update 期间及续搜前不得改变根盘面或清空 state 的池；Workspace 可顺序复用。
    // cells=nullptr 自动选根；否则按给定 vector 的顺序保留全部根，内层仍自动选点；输入只在本次调用读取。
    static Result analyze(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, long long nodeBudget, const vector<ObservedBoard::CellId> *cells = nullptr);
    static void update(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, long long nodeBudget);

  private:
    // 一次 analyze/update 持有的中间结果；各节点显式覆写并消费，容量在本次遍历复用。
    struct Evaluation {
        // 扩展 deque 不移动祖先帧保存的概率结果。
        std::deque<Probability::Result> probabilities;
    };

    static void swapProbability(Probability::Result &left, Probability::Result &right);
    static void countSafe(const PositionState &state, int &remainingSafe, int &forcedSafes);
    static bool interchangeable(const PositionState &state, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    static void pruneRoot(vector<JavaEval::Analysis> &analyses, const PositionState &state, int width, Workspace &workspace);
    static void selectCandidates(vector<JavaEval::Analysis> &analyses, int limit);
    static long long selectRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, const vector<ObservedBoard::CellId> *cells);
    static bool prepareNode(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, int width, long long &budget, long long &nodeCount);
    static void orderMoves(Pool &pool, vectorPool<Pool::Node>::vector children, Pool::Frame &frame);
    static void appendBranches(Pool &pool, int index, const JavaEval::Analysis &analysis, const vectorPool<JavaEval::Branch> &branches);
    static Result buildRoot(PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, const vector<ObservedBoard::CellId> *cells);
    static long long extend(Result &result, PositionState &state, Pool &pool, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Evaluation &evaluation, long long nodeBudget, int maxDepth);
    static void extendPosition(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Evaluation &evaluation, long long &budget, long long &nodeCount, bool &complete);
    static void extendCandidate(PositionState &state, Pool &pool, int index, int maxDepth, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, Evaluation &evaluation, long long &budget, long long &nodeCount, bool &complete);
};

} // 命名空间 mss
