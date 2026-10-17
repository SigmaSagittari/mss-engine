#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "core/assert.h"

namespace mss {

// 定宽位集合；单字使用标量存储，其余宽度使用数组，集合操作共用同一份实现。
// 下标必须在位宽内；未使用的高位由调用方保持为 0，取反也遵守此约定。
template <std::size_t WordCount> struct bitMask {
    static_assert(WordCount > 0);
    static constexpr std::size_t kWordCount = WordCount;
    static constexpr int kBitCount = WordCount * 64;

    constexpr bitMask() : words_() {}

    static bitMask bit(int index) {
        bitMask result;
        result.word(index / 64) = std::uint64_t{1} << bitOffset(index);
        return result;
    }

    static bitMask all(int bitCount) {
        assert_(bitCount >= 0 && bitCount <= kBitCount, "bitMask::all 的位数超出掩码位宽");
        bitMask result;
        for (int i = 0; i < (int)WordCount && bitCount > 0; ++i) {
            result.word(i) = bitCount >= 64 ? ~std::uint64_t{} : (std::uint64_t{1} << bitCount) - 1;
            bitCount -= 64;
        }
        return result;
    }

    bool any() const {
        for (int i = 0; i < (int)WordCount; ++i)
            if (word(i)) return true;
        return false;
    }

    // 调用方保证非空；保留原有的空掩码报错。
    int firstSetBit() const {
        int index = 0;
        while (index + 1 < (int)WordCount && !word(index)) ++index;
        const std::uint64_t bits = word(index);
        assert_(bits != 0, "bitMask::firstSetBit: 调用了一个全空的 firstSetBit");
        return index * 64 + std::countr_zero(bits);
    }

    bool test(int index) const { return (word(index / 64) >> bitOffset(index)) & 1; }
    void set(int index) { word(index / 64) |= std::uint64_t{1} << bitOffset(index); }
    void reset(int index) { word(index / 64) &= ~(std::uint64_t{1} << bitOffset(index)); }

    int popcount() const {
        int result = 0;
        for (int i = 0; i < (int)WordCount; ++i) result += std::popcount(word(i));
        return result;
    }

    bitMask &operator&=(const bitMask &other) {
        for (int i = 0; i < (int)WordCount; ++i) word(i) &= other.word(i);
        return *this;
    }

    bitMask &operator|=(const bitMask &other) {
        for (int i = 0; i < (int)WordCount; ++i) word(i) |= other.word(i);
        return *this;
    }

    bitMask operator~() const {
        bitMask result;
        for (int i = 0; i < (int)WordCount; ++i) result.word(i) = ~word(i);
        return result;
    }

    template <typename Callback> void forEachSetBit(Callback &&callback) const {
        for (int i = 0; i < (int)WordCount; ++i)
            for (std::uint64_t bits = word(i); bits; bits &= bits - 1) callback(i * 64 + std::countr_zero(bits));
    }

  private:
    std::conditional_t<WordCount == 1, std::uint64_t, std::array<std::uint64_t, WordCount>> words_;

    // 单字保留直接移位，避免取余表达式改变编译器对位测试指令的选择。
    static int bitOffset(int index) { return WordCount == 1 ? index : index % 64; }

    std::uint64_t &word(int index) {
        if constexpr (WordCount == 1) return words_;
        else return words_[index];
    }
    std::uint64_t word(int index) const {
        if constexpr (WordCount == 1) return words_;
        else return words_[index];
    }
};

} // 命名空间 mss
