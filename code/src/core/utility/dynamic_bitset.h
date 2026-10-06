#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mss {

class DynamicBitset {
  public:
    DynamicBitset() : size_(0) {}

    // resize 只改变可见位数；残局普通后端先 resize 再 setAll 建立 unopened，
    // 新增加的 word 为 0，不会自动继承“全候选开放”的语义。
    void resize(std::size_t size) {
        size_ = size;
        words_.resize((size + 63) / 64);
    }

    void setAll() {
        // 将当前可见范围内的所有位设为 1，并清除尾部 padding 位。
        for (std::uint64_t &word : words_)
            word = ~std::uint64_t{};
        if (size_ % 64 != 0)
            words_.back() &= tailMask();
    }

    // index 必须小于当前 size；本类没有边界检查，热路径调用依赖此契约。
    void reset(std::size_t index) {
        // 清除指定下标的位。
        words_[index / 64] &= ~(std::uint64_t{1} << (index % 64));
    }

    void set(std::size_t index) {
        // 设置指定下标的位。
        words_[index / 64] |= std::uint64_t{1} << (index % 64);
    }

    bool test(std::size_t index) const {
        // 查询指定下标的位是否为 1。
        return ((words_[index / 64] >> (index % 64)) & 1) != 0;
    }

    template <typename Callback> void for_each(Callback &&callback) const {
        // 按递增下标访问所有置位位；回调看到的是局部 word 提取出的下标，
        // 遍历期间不要通过同一个 bitset 改动位集合。
        for (int wordIndex = 0; wordIndex < (int)(words_.size()); ++wordIndex) {
            std::uint64_t word = words_[wordIndex];
            while (word != 0) {
                // word 是局部副本：提取和消去低位不改变 bitset 本身。
                const std::uint64_t lowbit = word & (~word + std::uint64_t{1});
                callback(wordIndex * 64 + std::countr_zero(lowbit));
                word ^= lowbit;
            }
        }
    }

  private:
    std::uint64_t tailMask() const {
        // 生成最后一个 word 中有效位对应的掩码。
        const std::size_t tail = size_ % 64;
        return tail == 0 ? ~std::uint64_t{} : (std::uint64_t{1} << tail) - 1;
    }

    std::vector<std::uint64_t> words_;
    std::size_t size_;
};

} // namespace mss
