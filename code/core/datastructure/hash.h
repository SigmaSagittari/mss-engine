#pragma once

#include <cstddef>
#include <cstdint>

#include "core/algorithm/rng.h"

namespace mss {

// 双通道内容身份；完整比较两个分量，桶定位只折叠它们。
struct U128 {
    std::uint64_t lo;
    std::uint64_t hi;

    U128() : lo(0), hi(0) {}
    U128(std::uint64_t lo, std::uint64_t hi) : lo(lo), hi(hi) {}
    bool operator==(const U128 &other) const { return lo == other.lo && hi == other.hi; }
    bool operator!=(const U128 &other) const { return !(*this == other); }
    U128 &operator+=(const U128 &other) { lo += other.lo; hi += other.hi; return *this; }
};

struct U128Hash {
    std::size_t operator()(const U128 &key) const noexcept { return key.lo ^ key.hi; }
};

struct SplitMix64Hash {
    std::size_t operator()(std::uint64_t key) const noexcept { return splitmix64(key); }
};

// 增量内容摘要：两次 mix 后 finalize 得到 U128；同输入同种子结果稳定。
class U128Hasher {
  public:
    U128Hasher() : lo_(kLoSeed), hi_(kHiSeed) {}
    U128Hasher(std::uint64_t seed) : lo_(seed + kLoSeed), hi_(seed + kHiSeed) {}

    void mix(std::uint64_t value) { lo_ = splitmix64(lo_ + value); hi_ = splitmix64(hi_ + value + kMixOffset); }
    void mix(int value) { mix((std::uint64_t)value); }
    U128 finalize() const { return {lo_, hi_}; }

  private:
    static const std::uint64_t kLoSeed = 0x9e3779b97f4a7c15ULL;
    static const std::uint64_t kHiSeed = 0xd1b54a32d192ed03ULL;
    static const std::uint64_t kMixOffset = 0x6d2b79f5a39c8b7dULL;
    std::uint64_t lo_;
    std::uint64_t hi_;
};

} // 命名空间 mss
