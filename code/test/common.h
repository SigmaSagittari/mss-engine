#pragma once

#include "core/utility/rng.h"
#include "core/config.h"
#include "core/workspace.h"
#include "game/game.h"

namespace test {

// 本组用例按模块明确选择引擎参数。
inline const mss::Config engineConfig{
    mss::BruteForce::Config{
        /* 分析所有候选格 */ false,
        /* 求解器 */ mss::BruteForce::Config::Solver::Bitwise,
        /* 暴力切换方案数阈值 */ 1000,
        /* 线程上限 */ 5,
    },
    mss::LongTermRisk::Config{
        /* 影响阈值 */ 0.025L,
        /* 热点触发安全率 */ 0.9L,
        /* 等价判断容差 */ 1e-9L,
    },
    mss::Pseudo::Config(/* 等价判断容差 */ 1e-9L),
    mss::MoveEval::Config{
        /* 进度奖励系数 */ 0.001L,
        /* 影响缩放系数 */ 0.9L,
    },
    mss::JavaEval::Config{
        /* 第一候选安全率阈值 */ 0.10L,
        /* 第二候选安全率阈值 */ 0.20L,
        /* 连通区域大小阈值 */ 8,
    },
    mss::AnytimeSearch::Config{
        /* 根层、中层、深层候选宽度 */ {6, 3, 1},
        /* 完整分析指定根招法 */ false,
    },
    mss::MonteCarlo::Config{
        /* 采样温度 */ 0.05L,
        /* 先验权重 */ 1.0L,
    },
};

}
