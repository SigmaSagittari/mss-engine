#pragma once

#include <cstddef>
#include <cstdint>

#include "core/utility/rng.h"

namespace mss {

// 128 位哈希值：两个独立的 64 位通道（lo/hi）。
// 同一批数据的碰撞概率约 2^-127，对 1e9 量级的数据也足够安全；项目将其
// 作为内容身份使用，不为哈希命中再做结构相等性核验。
struct U128 {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;

    // 比较两个 128 位身份值的全部位。
    bool operator==(const U128 &o) const {
        return lo == o.lo && hi == o.hi;
    }
    // 判断两个 128 位身份值是否不同。
    bool operator!=(const U128 &o) const {
        return !(*this == o);
    }

    // 分量相加：用于几何分组时把各数字格种子累加到邻格
    // 将另一个 128 位值按分量加入当前值。
    U128 &operator+=(const U128 &o) {
        lo += o.lo;
        hi += o.hi;
        return *this;
    }
};

// 设计目的：U128 同时承担内容身份和桶定位前的折叠输入；多个 pool 用它判定内容相同。
// 因此混合算法与键宽度属于缓存协议的一部分，必须保持稳定。

// u128 -> size_t 的桶折叠：只用于哈希表定位桶；键的完整相等性由探测时比较
// U128 全量保证，折叠碰撞只会增加探测长度，不改变缓存身份。
struct U128Hash {
    // 将 128 位身份折叠为哈希表桶下标输入。
    std::size_t operator()(const U128 &k) const noexcept {
        return k.lo ^ k.hi;
    }
};

// 流式 128 位混合哈希：两个不同种子的 splitmix 累加器并行推进。
// 对定宽值反复 mix()，最后 finalize() 得到 U128。
class U128Hasher {
  public:
    // 创建使用默认双通道种子的哈希器。
    U128Hasher() = default;

    // 创建使用指定种子初始化双通道的哈希器。
    explicit U128Hasher(std::uint64_t seed) : lo_(seed + kLoSeed), hi_(seed + kHiSeed) {
    }

    void mix(std::uint64_t v) {
        // 将一个 64 位字段混入两个独立的哈希通道。
        lo_ = splitmix64(lo_ + v);
        hi_ = splitmix64(hi_ + v + kMixOffset);
    }

    void mix(int v) {
        // 将有符号整数按项目约定转换后混入哈希器。
        mix((std::uint64_t)(v));
    }

    // 返回当前累计状态形成的 128 位身份值。
    U128 finalize() const {
        return {lo_, hi_};
    }

  private:
    static constexpr std::uint64_t kLoSeed = 0x9e3779b97f4a7c15ULL;
    static constexpr std::uint64_t kHiSeed = 0xd1b54a32d192ed03ULL;
    static constexpr std::uint64_t kMixOffset = 0x6d2b79f5a39c8b7dULL;

    std::uint64_t lo_ = kLoSeed;
    std::uint64_t hi_ = kHiSeed;
};

} // 命名空间 mss
