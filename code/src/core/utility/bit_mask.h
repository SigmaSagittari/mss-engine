#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "core/assert.h"

namespace mss {

// 定宽位集合：用 WordCount 个 64 位字表示最多 WordCount * 64 个下标。
// 残局多掩码后端用它把每个完整雷位方案压成候选格位图（最多 512 个候选格）。
// 高位未使用部分必须始终保持为 0，这样 ~mask 才能安全表示“其余候选”。
// 注意：这份“高位为 0”的契约由调用方维护——bit()/all() 只保证按给出的
// 下标/位数构造，set()/reset() 也不做上界检查；一旦越界就会污染高位，
// 并让 ~mask 派生的补集凭空多出下标。
template <std::size_t WordCount> struct bitMask {
    static_assert(WordCount > 0);
    static constexpr std::size_t kWordCount = WordCount;
    static constexpr int kBitCount = WordCount * 64;

    std::array<std::uint64_t, WordCount> words{};

    // 创建只包含一个指定位的掩码。
    static bitMask bit(int index) {
        // index 必须落在该 bitMask 的可表示范围内；此函数刻意不做边界检查。
        bitMask result;
        result.words[index / 64] = std::uint64_t{1} << (index % 64);
        return result;
    }

    // 创建低 bitCount 位为 1 的掩码。bitCount 超出位宽属于破坏调用契约：
    // 越界的位无法表示，静默丢掉会让调用方以为候选格都在掩码里，随后按掩码
    // 派生的下标少算一格。这里宁可炸掉，也不返回一个"看起来正常"的残缺掩码。
    static bitMask all(int bitCount) {
        assert_(bitCount >= 0 && bitCount <= kBitCount, "bitMask::all 的位数超出掩码位宽");
        bitMask result;
        int remaining = bitCount;
        for (std::uint64_t &word : result.words) {
            if (remaining >= 64) {
                word = ~std::uint64_t{};
                remaining -= 64;
            } else if (remaining > 0) {
                word = (std::uint64_t{1} << remaining) - 1;
                break;
            } else {
                break;
            }
        }
        return result;
    }

    // 判断掩码中是否至少有一个置位。
    bool any() const {
        for (std::uint64_t word : words)
            if (word != 0)
                return true;
        return false;
    }

    // 返回最低置位的下标；调用方保证掩码非空。
    int firstSetBit() const {
        int index = 0;
        for (std::uint64_t word : words) {
            if (word != 0)
                return index * 64 + std::countr_zero(word);
            ++index;
        }
        assert_(false, "bitMask::firstSetBit: mask is empty");
#if defined(_MSC_VER)
        __assume(0);
#else
        __builtin_unreachable();
#endif
    }

    // 查询指定下标是否置位。
    bool test(int index) const {
        return (words[index / 64] >> (index % 64)) & 1;
    }

    // 将指定下标设置为 1。
    void set(int index) {
        words[index / 64] |= std::uint64_t{1} << (index % 64);
    }

    // 将指定下标清为 0。
    void reset(int index) {
        words[index / 64] &= ~(std::uint64_t{1} << (index % 64));
    }

    // 统计置位数量。
    int popcount() const {
        int result = 0;
        for (std::uint64_t word : words)
            result += std::popcount(word);
        return result;
    }

    // 保留两个集合的交集。
    bitMask &operator&=(const bitMask &other) {
        for (int i = 0; i < (int)(WordCount); ++i)
            words[i] &= other.words[i];
        return *this;
    }

    // 将另一个集合并入当前集合。
    bitMask &operator|=(const bitMask &other) {
        for (int i = 0; i < (int)(WordCount); ++i)
            words[i] |= other.words[i];
        return *this;
    }

    // 按 word 取反，得到补集；高位是否合法由调用方的高位不变式保证。
    bitMask operator~() const {
        bitMask result;
        for (int i = 0; i < (int)(WordCount); ++i)
            result.words[i] = ~words[i];
        return result;
    }

    template <typename Callback>
    // 按下标递增顺序访问所有置位。
    void forEachSetBit(Callback &&callback) const {
        int index = 0;
        for (std::uint64_t word : words) {
            while (word != 0) {
                callback(index * 64 + std::countr_zero(word));
                word &= word - 1;
            }
            ++index;
        }
    }
};

// 单 word 特化：用标量替代 std::array<uint64_t, 1>，覆盖最常见的一档宽度，
// 避免数组间接寻址；语义与通用版逐项一致。
template <> struct bitMask<1> {
    static constexpr std::size_t kWordCount = 1;
    static constexpr int kBitCount = 64;

    std::uint64_t word = 0;

    // 创建只包含一个指定位的单 word 掩码。
    static bitMask bit(int index) {
        // 设计目的：此特化直接使用单个 64 位 word；index 的范围由调用方契约保证。
        bitMask result;
        result.word = std::uint64_t{1} << index;
        return result;
    }

    // 创建低 bitCount 位为 1 的单 word 掩码。与通用版一样，超界直接炸：
    // bitCount > 64 会让 (1 << bitCount) 变成移位未定义行为，比截断更糟。
    static bitMask all(int bitCount) {
        assert_(bitCount >= 0 && bitCount <= kBitCount, "bitMask::all 的位数超出掩码位宽");
        bitMask result;
        if (bitCount == 64)
            result.word = ~std::uint64_t{};
        else if (bitCount != 0)
            result.word = (std::uint64_t{1} << bitCount) - 1;
        return result;
    }

    // 判断单 word 掩码中是否至少有一个置位。
    bool any() const {
        return word != 0;
    }

    // 返回最低置位的下标；调用方保证掩码非空。
    int firstSetBit() const {
        if (word != 0)
            return std::countr_zero(word);
        assert_(false, "bitMask::firstSetBit: mask is empty");
#if defined(_MSC_VER)
        __assume(0);
#else
        __builtin_unreachable();
#endif
    }

    // 查询单 word 中指定下标是否置位。
    bool test(int index) const {
        return (word >> index) & 1;
    }

    // 将单 word 中指定下标置为 1。
    void set(int index) {
        word |= std::uint64_t{1} << index;
    }

    // 将单 word 中指定下标清为 0。
    void reset(int index) {
        word &= ~(std::uint64_t{1} << index);
    }

    // 统计单 word 掩码中的置位数量。
    int popcount() const {
        return std::popcount(word);
    }

    // 保留两个单 word 集合的交集。
    bitMask &operator&=(const bitMask &other) {
        word &= other.word;
        return *this;
    }

    // 将另一个单 word 集合并入当前集合。
    bitMask &operator|=(const bitMask &other) {
        word |= other.word;
        return *this;
    }

    // 返回单 word 集合的补集。
    bitMask operator~() const {
        bitMask result;
        result.word = ~word;
        return result;
    }

    template <typename Callback>
    // 按下标递增顺序访问单 word 中所有置位。
    void forEachSetBit(Callback &&callback) const {
        std::uint64_t bits = word;
        while (bits != 0) {
            callback(std::countr_zero(bits));
            bits &= bits - 1;
        }
    }
};

} // namespace mss
