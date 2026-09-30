#pragma once

#include <cstdint>

namespace mss {

// splitmix64：小巧快速的 64 位伪随机混合函数
// 将输入经过固定轮次的加法、乘法和异或移位，返回混合后的 64 位值。
inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct Random {
    Random(std::uint64_t lo, std::uint64_t hi) : lo_(lo), hi_(hi) {
    }

    std::uint64_t next() {
        lo_ = splitmix64(lo_ + 0x9e3779b97f4a7c15ULL);
        hi_ = splitmix64(hi_ + 0xd1b54a32d192ed03ULL);
        return lo_ ^ hi_;
    }

  private:
    std::uint64_t lo_;
    std::uint64_t hi_;
};

} // 命名空间 mss
