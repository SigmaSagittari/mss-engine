#pragma once

#include <bit>
#include <cstdint>

#include "core/assert.h"

namespace mss {

// 定宽位集合；下标必须在位宽内，未使用的高位由调用方保持为 0。
template <int WordCount> struct bitMask {
    static_assert(WordCount > 0, "bitMask 至少要有一个 word");
    static constexpr int kWordCount = WordCount;
    static constexpr int kBitCount = WordCount * 64;

    bitMask() {
        for (int i = 0; i < WordCount; ++i)
            words[i] = 0;
    }

    static bitMask bit(int index) {
        bitMask result;
        result.words[index / 64] = std::uint64_t{1} << (index % 64);
        return result;
    }

    static bitMask all(int bitCount) {
        assert_(bitCount >= 0 && bitCount <= kBitCount, "bitMask::all 的位数超出掩码位宽");
        bitMask result;
        for (int i = 0; i < WordCount && bitCount > 0; ++i) {
            result.words[i] = bitCount >= 64 ? ~std::uint64_t{} : (std::uint64_t{1} << bitCount) - 1;
            bitCount -= 64;
        }
        return result;
    }

    bool any() const {
        for (int i = 0; i < WordCount; ++i)
            if (words[i] != 0)
                return true;
        return false;
    }

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
            result += std::popcount(words[i]);
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
