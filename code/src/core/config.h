#pragma once

#include <string>

namespace mss {

inline const std::string version = "0.1.5";

struct Config {
    struct Position {
        enum class Algo { Java, LowRisk };
        Algo algo = Algo::Java;
        int bruteForceThreshold = 5000;
    } position;

    struct BruteForce {
        // 三种后端结果语义一致；候选超过掩码宽度或只有一种布局时，调度层使用 Common 后端。
        enum class Solver {
            // 普通递归实现，支持任意候选数。
            Common,
            // 定宽位掩码实现，最多处理 bitMask<8> 可容纳的候选格。
            Bitwise,
            // 仅并行展开根节点候选；线程上限由 Config::bruteForce.maxThreads 控制，包含调用线程。
            BitwiseRootParallel,
        };
        // 仅为 true 时返回每个候选格的结果；false 时至多返回一条推荐步。
        bool checkAllMoves = false;
        // 单推荐步模式的最低胜局数，语义上必须至少为 1；全候选模式不做阈值剪枝。
        // 取 0 会让"最低胜局数"成为空洞约束：收尾判定 best >= need 恒真，从未搜到候选的
        // 哨兵值也会被当成真实胜局数参与解码。搜索返回的 0 专用于表示缓存未命中。
        int minWins = 1;
        // 后端选择不改变动作含义或胜局数。
        Solver solver = Solver::Bitwise;
        int maxThreads = 5;
    } bruteForce;

    struct LongTermRisk {
        // FINAL_THRESHOLD：影响度 > 方案数 × 此值才记账（tiles/enablers/possible）。
        long double influenceThreshold = 0.025L;
        // HOTSPOT_ENABLER_SAFETY：enabler 安全度低于此值才记热点。
        long double hotspotEnablerSafety = 0.9L;
        // 计数相等的相对容差（Java 的精确整数相等在 long double 下改相对比较）。
        long double eqEps = 1e-9L;
    } longTermRisk;

    struct JavaEvaluate {
        long double progressContribution = 0.001L;
        long double selectionThreshold1 = 0.10L;
        long double selectionThreshold2 = 0.20L;
        long double influenceScale = 0.9L;       // FIFTYFIFTY_INFLUENCE_SCALE
        long double equalityThreshold = 0.0001L; // EQUALITY_THRESHOLD
        int weight1 = 4;
        int weight2 = 1;
        int spaceThreshold = 8; // SpaceCounter 阈值
        long double eqEps = 1e-9L;
    } javaEvaluate;

    struct UI {
        bool includeProbabilities = false;
    } ui;
};

} // 命名空间 mss
