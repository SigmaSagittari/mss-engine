#pragma once

namespace mss {

// 程序版本是固定信息，与可调配置分开。
inline const char version[] = "0.16";

// 配置聚合的空壳；各引擎的 Config 落地前不定义字段。
struct Config {};

} // 命名空间 mss
