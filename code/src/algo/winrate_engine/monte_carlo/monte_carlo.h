#pragma once

#include <stop_token>
#include <vector>

#include "algo/winrate_engine/java_transplant/java_evaluate.h"
#include "algo/winrate_engine/types.h"
#include "core/utility/rng.h"

namespace mss {

struct Config;
struct Workspace;

struct MonteCarlo {
    struct Config {
        long double temperature;
        long double priorWeight;
        Config(long double temperature, long double priorWeight) : temperature(temperature), priorWeight(priorWeight) {}
    };

    struct Candidate {
        winrate::CellId cellId;
        long long samples;
        long long wins;

      private:
        friend struct MonteCarlo;
        // 评分、先验及后验只用于内部调度；实测胜率在 samples > 0 时为 wins/samples。
        long double weight;
        long double prior;
        long double mean;
        long double variance;
        Candidate(winrate::CellId cellId, long double weight, long double prior, const Config &cfg);
    };

    struct Result {
        // true 表示整局胜率采样，此时 candidates 为空；否则逐首招累积 candidates 中的胜负统计。
        bool winrate;
        std::vector<Candidate> candidates;
        // 返回全部完整样本及胜局数；比较首招时，各项实测胜率须读取对应 Candidate。
        long long samples() const;
        long long wins() const;
        // 返回推荐格；整局胜率采样没有首招推荐，返回 -1。
        winrate::CellId recommended() const;

      private:
        friend struct MonteCarlo;
        winrate::SuggestAlgo strategy;
        std::vector<winrate::CellId> safeCells;
        Random random;
        // 两个计数只累积整局胜率样本；首招模式的统计仅保存在 candidates 中。
        long long winrateSamples;
        long long winrateWins;
        // -1 仅用于没有首招的整局模式；其余是 Java 初始推荐在 candidates 中的下标。
        int initialRecommended;
        bool fullScores;
        Result(winrate::SuggestAlgo strategy, std::vector<winrate::CellId> safeCells, bool winrate, std::vector<Candidate> candidates, int initialRecommended, bool fullScores);
        int best() const;
    };

    struct Workspace {
        // 引用的工作区在使用期间有效；risk 是 javaEval 构造时关联的工作区，均不可被并发使用。
        Workspace(Probability::Workspace &probability, LongTermRisk::Workspace &risk, JavaEval::Workspace &javaEval, mss::Workspace &rollout);

      private:
        friend struct MonteCarlo;
        Probability::Workspace &probability;
        LongTermRisk::Workspace &risk;
        JavaEval::Workspace &javaEval;
        // 根评分结束后才运行 Game，串行复用主工作区；没有嵌套持有主 Workspace。
        mss::Workspace &rollout;
        ShapeSolver::AssignmentPool assignments;
        struct Chance {
            long long samples;
            long double target;
            long double logarithm;
            Chance(long long samples, long double target, long double logarithm) : samples(samples), target(target), logarithm(logarithm) {}
        };
        std::vector<Chance> chances;
        // 按候选下标定位缓存；-1 表示尚未计算，首次计算后指向由真实值构造的 Chance。
        std::vector<int> chanceOf;
        // 同一根盘面和策略下，首招位置及其数字唯一确定下一条建议；索引 -1 表示尚未计算。
        std::vector<int> nextMovesOf;
        std::vector<std::vector<winrate::CellId>> nextMoves;
        std::vector<winrate::CellId> firstMove;
    };

    // root 的对象及池来自同一合法非空盘面且候选数 > 0；临时评分在返回前恢复 root。
    // targets 为空时沿用 Java 自动候选，先采推荐首招，再补齐评分；否则只评估指定目标且关闭裁剪。
    // 指定目标互不重复，均为 Hidden 非确定雷格；有必安格或已获胜时直接采样整局，忽略 targets。
    // temperature > 0，priorWeight >= 0；strategy 使用与 Game 相同的枚举，控制模拟的后续落子。
    // 根评分与合法雷盘抽样使用下层引擎，完整对局由 Game::suggest / Game::play 执行。
    static Result analyze(const winrate::Input &root, winrate::SuggestAlgo strategy, const mss::Config &cfg, Workspace &workspace, winrate::Cells targets = {});
    // root、cfg 和工作区与 analyze 匹配，根观测及池身份须保持不变、池不得清空；根对象不由 Result 持有。
    // 工作区一次服务一份 Result，不可并发或重入；直接累积一个完整样本，取消返回 false，半局不计数、不消耗雷盘序号。
    // 首招投入按归档的竞争机会与预计方差下降调度；各首招的第 k 个完整样本共用雷盘种子。
    static bool update(Result &result, const winrate::Input &root, const mss::Config &cfg, Workspace &workspace, std::stop_token stop = {});

  private:
    enum class Outcome { Won, Lost, Cancelled };
    static std::vector<winrate::CellId> collectSafe(const winrate::View &root);
    static int remainingSafe(const winrate::Board &board);
    static JavaEval::Result evaluate(const winrate::Input &root, const mss::Config &cfg, Workspace &workspace, bool prune);
    static void estimate(Candidate &candidate, const Config &cfg);
    static void expand(Result &result, const winrate::Input &root, const mss::Config &cfg, Workspace &workspace);
    static long double logChance(long double won, long double lost, long double target);
    static int select(Result &result, const Config &cfg, Workspace &workspace);
    static Outcome simulate(const Result &result, const winrate::View &root, long long sequence, winrate::CellId first, const mss::Config &cfg, Workspace &workspace, std::stop_token stop);
};

} // 命名空间 mss
