#pragma once

#include <cstddef>
#include <cstdint>

#include "core/utility/rng.h"

namespace mss {

// 双通道内容身份；完整相等性比较两个分量，桶定位只折叠它们。
struct U128 {
    std::uint64_t lo;
    std::uint64_t hi;

    constexpr U128() : lo(0), hi(0) {}
    constexpr U128(std::uint64_t lo, std::uint64_t hi) : lo(lo), hi(hi) {}
    bool operator==(const U128 &o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const U128 &o) const { return !(*this == o); }
    U128 &operator+=(const U128 &o) { lo += o.lo; hi += o.hi; return *this; }
};

struct U128Hash {
    std::size_t operator()(const U128 &k) const noexcept { return k.lo ^ k.hi; }
};

// 混合算法与种子保持稳定，两个 splitmix 通道分别累加。
class U128Hasher {
  public:
    U128Hasher() : lo_(kLoSeed), hi_(kHiSeed) {}
    explicit U128Hasher(std::uint64_t seed) : lo_(seed + kLoSeed), hi_(seed + kHiSeed) {}
    void mix(std::uint64_t v) { lo_ = splitmix64(lo_ + v); hi_ = splitmix64(hi_ + v + kMixOffset); }
    void mix(int v) { mix((std::uint64_t)v); }
    U128 finalize() const { return {lo_, hi_}; }

  private:
    static constexpr std::uint64_t kLoSeed = 0x9e3779b97f4a7c15ULL;
    static constexpr std::uint64_t kHiSeed = 0xd1b54a32d192ed03ULL;
    static constexpr std::uint64_t kMixOffset = 0x6d2b79f5a39c8b7dULL;
    std::uint64_t lo_;
    std::uint64_t hi_;
};

} // 命名空间 mss
