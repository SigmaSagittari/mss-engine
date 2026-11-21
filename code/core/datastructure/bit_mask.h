#pragma once

#include <bit>
#include <cstdint>

#include "core/assert.h"

namespace mss {

struct CountBits {
    constexpr int operator()(std::uint64_t word) const {
#if defined(__POPCNT__) || defined(__aarch64__) || defined(__wasm__)
        return std::popcount(word);
#else
        // 通用 x86-64 构建没有 POPCNT，避免每个字都调用编译器的运行库函数。
        word -= (word >> 1) & UINT64_C(0x5555555555555555);
        word = (word & UINT64_C(0x3333333333333333)) + ((word >> 2) & UINT64_C(0x3333333333333333));
        word = (word + (word >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        return (word * UINT64_C(0x0101010101010101)) >> 56;
#endif
    }
};

// 定宽位集合；下标必须在位宽内，未使用的高位由调用方保持为 0。
template <int WordCount> struct bitMask {
    static_assert(WordCount > 0, "bitMask 至少要有一个 word");
    static constexpr int kWordCount = WordCount;
    static constexpr int kBitCount = WordCount * 64;

    bitMask() {
        for (int i = 0; i < WordCount; ++i)
            words[i] = 0;
    }

    bitMask(int bitCount) : bitMask() {
        assert_(bitCount >= 0 && bitCount <= kBitCount, "bitMask 的位数超出掩码位宽");
        for (int i = 0; i < WordCount && bitCount > 0; ++i) {
            words[i] = bitCount >= 64 ? ~std::uint64_t{} : (std::uint64_t{1} << bitCount) - 1;
            bitCount -= 64;
        }
    }

    bool any() const {
        for (int i = 0; i < WordCount; ++i)
            if (words[i] != 0)
                return true;
        return false;
    }

    bool operator==(const bitMask &) const = default;
    std::uint64_t word(int index) const { return words[index]; }

    // 调用方保证非空；空掩码触发断言。
    int firstSetBit() const {
        int index = 0;
        while (index + 1 < WordCount && words[index] == 0)
            ++index;
        assert_(words[index] != 0, "bitMask::firstSetBit: 调用了一个全空的 firstSetBit");
        return index * 64 + std::countr_zero(words[index]);
    }

    bool test(int index) const { return ((words[index / 64] >> (index % 64)) & 1) != 0; }
    void set(int index) { words[index / 64] |= std::uint64_t{1} << (index % 64); }
    void reset(int index) { words[index / 64] &= ~(std::uint64_t{1} << (index % 64)); }

    int popcount() const {
        int result = 0;
        for (int i = 0; i < WordCount; ++i)
            result += CountBits()(words[i]);
        return result;
    }

    bitMask &operator&=(const bitMask &other) {
        for (int i = 0; i < WordCount; ++i)
            words[i] &= other.words[i];
        return *this;
    }

    bitMask &operator|=(const bitMask &other) {
        for (int i = 0; i < WordCount; ++i)
            words[i] |= other.words[i];
        return *this;
    }

    bitMask operator&(const bitMask &other) const { bitMask result = *this; return result &= other; }
    bitMask operator|(const bitMask &other) const { bitMask result = *this; return result |= other; }

    bitMask operator~() const {
        bitMask result;
        for (int i = 0; i < WordCount; ++i)
            result.words[i] = ~words[i];
        return result;
    }

    template <typename Callback> void forEachSetBit(Callback &&callback) const {
        for (int i = 0; i < WordCount; ++i)
            for (std::uint64_t bits = words[i]; bits != 0; bits &= bits - 1)
                callback(i * 64 + std::countr_zero(bits));
    }

  private:
    std::uint64_t words[WordCount];
};

} // 命名空间 mss
