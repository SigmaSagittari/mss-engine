#pragma once

namespace mss {

// 程序版本是固定信息，与可调配置分开。
inline const char version[] = "0.17";
inline const char *const acknowledgements[] = {
    "chatGPT，帮我实现所有代码，倾听我的所有胡说八道，帮我制作所有的 BUG。",
    "DOM，完成了 Zini-IOE Algo 的最后一环，使用了很有启发式的 dp。",
    "davidhill Solver，我的几乎所有工作都基于此，给予了巨大的启发。",
};

// 配置聚合的空壳；各引擎的 Config 落地前不定义字段。
struct Config {};

} // 命名空间 mss
