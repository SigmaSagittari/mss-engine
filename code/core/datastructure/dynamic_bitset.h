#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

#include "core/datastructure/vector.h"

namespace mss {

// 运行期位集合；resize 只改可见位数，新增加的 word 清零，不继承旧内容。
class DynamicBitset {
  public:
    DynamicBitset() : size_(0) {}

    void swap(DynamicBitset &other) {
        words_.swap(other.words_);
        std::swap(size_, other.size_);
    }

    void resize(std::size_t size) {
        const std::size_t words = (size + 63) / 64;
        const std::size_t previous = words_.size;
        words_.resize(words);
        for (std::size_t i = previous; i < words; ++i) words_.data[i] = 0;
        size_ = size;
    }

    // 把可见范围内的位全部置 1，并清除尾部 padding 位。
    void setAll() {
        for (std::size_t i = 0; i < words_.size; ++i) words_.data[i] = ~std::uint64_t{};
        if (size_ % 64 != 0) words_.data[words_.size - 1] &= tailMask();
    }

    // index 必须小于当前 size；热路径依赖此契约，不做边界检查。
    void reset(std::size_t index) { words_.data[index / 64] &= ~(std::uint64_t{1} << (index % 64)); }
    void set(std::size_t index) { words_.data[index / 64] |= std::uint64_t{1} << (index % 64); }
    bool test(std::size_t index) const { return ((words_.data[index / 64] >> (index % 64)) & 1) != 0; }

    // 按递增下标访问所有置位位；遍历期间不要改动同一个 bitset。
    template <typename Callback> void forEachSetBit(Callback &&callback) const {
        for (int wordIndex = 0; wordIndex < (int)words_.size; ++wordIndex) {
            std::uint64_t word = words_.data[wordIndex];
            while (word != 0) {
                const std::uint64_t lowbit = word & (~word + std::uint64_t{1});
                callback(wordIndex * 64 + std::countr_zero(lowbit));
                word ^= lowbit;
            }
        }
    }

  private:
    std::uint64_t tailMask() const {
        const std::size_t tail = size_ % 64;
        return tail == 0 ? ~std::uint64_t{} : (std::uint64_t{1} << tail) - 1;
    }

    vector<std::uint64_t> words_;
    std::size_t size_;
};

} // 命名空间 mss
