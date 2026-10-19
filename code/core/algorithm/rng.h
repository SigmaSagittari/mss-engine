#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mss {

// splitmix64：固定轮次的加法、乘法与异或移位混合，输入相同则输出相同。
inline std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

// 双通道随机流；同一种子产生同一序列，不依赖标准库分布实现。
struct Random {
    Random(std::uint64_t lo, std::uint64_t hi) : lo_(lo), hi_(hi) {}

    std::uint64_t next() {
        lo_ = splitmix64(lo_ + 0x9e3779b97f4a7c15ULL);
        hi_ = splitmix64(hi_ + 0xd1b54a32d192ed03ULL);
        return lo_ ^ hi_;
    }

    // 返回 [0, 1) 内的随机数，保留 long double 可精确表示的有效位。
    long double nextUnit() {
        const std::uint64_t lo = next();
        const std::uint64_t hi = next();
        const int digits = (std::min)(std::numeric_limits<long double>::digits, 64);
        return std::ldexp((long double)((lo ^ hi) >> (64 - digits)), -digits);
    }

  private:
    std::uint64_t lo_;
    std::uint64_t hi_;
};

} // 命名空间 mss
