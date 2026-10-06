#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/utility/rng.h"

namespace mss {

struct SplitMix64Hash {
    std::size_t operator()(std::uint64_t x) const noexcept {
        return splitmix64(x);
    }
};

// 开放寻址、线性探测；clear() 后让新一代表在 slots 的另一端工作。
// activeSlots_ 直接指向当前逻辑表，热路径不再判断方向；扩容时读当前 span、
// 写相反区间。slots_ 保持比当前表更大的物理空间，保证头尾可区分。
template <typename Key, typename Value, typename Hash = SplitMix64Hash> class FlatHashTable {
    struct Slot {
        Key key;
        Value value;
        unsigned char used;

        // 危险：批量预分配避免初始化实际键值；used 为 0 时不得读取 key/value，插入或 rehash 写完后才可查询。
        // vector 扩容和表复制只复制占用槽的键值，不能读取空槽中未写入的标量。
        Slot() : used(0) {}
        Slot(const Slot &other) : used(other.used) {
            if (used) {
                key = other.key;
                value = other.value;
            }
        }
        Slot &operator=(const Slot &other) {
            used = other.used;
            if (used) {
                key = other.key;
                value = other.value;
            }
            return *this;
        }
    };

  public:
    static constexpr double kMaxLoadFactor = 0.5;

    static_assert(std::is_invocable_r_v<std::size_t, const Hash &, const Key &>, "Hash must be callable as const Hash&(const Key&)");

    FlatHashTable() : size_(0) {}

    FlatHashTable(const FlatHashTable &other) : hash_(other.hash_), slots_(other.slots_), activeSlots_(), size_(other.size_) {
        bind(other.activeSlots_.size(), other.activeAtFront());
    }

    FlatHashTable(FlatHashTable &&other)
        : hash_(std::move(other.hash_)), slots_(std::move(other.slots_)), activeSlots_(other.activeSlots_), size_(other.size_) {
        other.activeSlots_ = {};
        other.size_ = 0;
    }

    FlatHashTable &operator=(const FlatHashTable &other) {
        if (this == &other)
            return *this;
        const std::size_t capacity = other.activeSlots_.size();
        const bool atFront = other.activeAtFront();
        hash_ = other.hash_;
        slots_ = other.slots_;
        size_ = other.size_;
        bind(capacity, atFront);
        return *this;
    }

    FlatHashTable &operator=(FlatHashTable &&other) {
        if (this == &other)
            return *this;
        hash_ = std::move(other.hash_);
        slots_ = std::move(other.slots_);
        activeSlots_ = other.activeSlots_;
        size_ = other.size_;
        other.activeSlots_ = {};
        other.size_ = 0;
        return *this;
    }

    std::size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }

    // 只重置逻辑表并保留物理槽位；下一次激活时使用另一端。
    void clear() {
        activeSlots_ = std::span<Slot>(activeSlots_.data(), 0);
        size_ = 0;
    }

    // 预留至少 expected 个元素对应的容量；可用另一端时原地重排。
    void reserve(std::size_t expected) {
        const std::size_t need = nextPowerOfTwo(expected > 0 ? expected * 2 : 1);
        if (need <= activeSlots_.size())
            return;
        if (activeSlots_.empty())
            activate(need);
        else
            rehash(need);
    }

    Value *find(const Key &key) {
        return valueAt(findIndex(key));
    }
    const Value *find(const Key &key) const {
        return valueAt(findIndex(key));
    }

    Value &operator[](const Key &key) {
        const std::size_t i = insertionIndex(key);
        if (insertKey(i, key))
            activeSlots_[i].value = Value{};
        return activeSlots_[i].value;
    }

    void emplace(const Key &key, const Value &value) {
        const std::size_t i = insertionIndex(key);
        if (insertKey(i, key))
            activeSlots_[i].value = value;
    }

  private:
    std::size_t capacity() const {
        return activeSlots_.size();
    }

    bool activeAtFront() const {
        return slots_.empty() || activeSlots_.data() == slots_.data();
    }

    void bind(std::size_t capacity, bool atFront) {
        if (capacity == 0) {
            activeSlots_ = atFront ? std::span<Slot>(slots_.data(), 0) : std::span<Slot>(slots_.data() + slots_.size(), 0);
            return;
        }
        const std::size_t base = atFront ? 0 : slots_.size() - capacity;
        activeSlots_ = std::span<Slot>(slots_.data() + base, capacity);
    }

    Value *valueAt(std::size_t i) {
        return i == capacity() ? nullptr : &activeSlots_[i].value;
    }

    const Value *valueAt(std::size_t i) const {
        return i == capacity() ? nullptr : &activeSlots_[i].value;
    }

    bool insertKey(std::size_t i, const Key &key) {
        Slot &slot = activeSlots_[i];
        if (slot.used)
            return false;
        slot.used = 1;
        slot.key = key;
        ++size_;
        return true;
    }

    std::size_t findIndex(const Key &key) const {
        if (size_ == 0)
            return capacity();
        const std::size_t mask = capacity() - 1;
        std::size_t i = hash_(key) & mask;
        for (;;) {
            const Slot &slot = activeSlots_[i];
            if (!slot.used)
                return capacity();
            if (slot.key == key)
                return i;
            i = (i + 1) & mask;
        }
    }

    std::size_t insertionIndex(const Key &key) {
        if (size_ + 1 > capacity() * kMaxLoadFactor)
            grow();
        const std::size_t mask = capacity() - 1;
        std::size_t i = hash_(key) & mask;
        for (;;) {
            const Slot &slot = activeSlots_[i];
            if (!slot.used || slot.key == key)
                return i;
            i = (i + 1) & mask;
        }
    }

    static std::size_t nextPowerOfTwo(std::size_t n) {
        std::size_t p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    void activate(std::size_t newCapacity) {
        const bool newAtFront = slots_.empty() ? true : !activeAtFront();
        if (newCapacity > slots_.size())
            slots_.resize(newCapacity);
        bind(newCapacity, newAtFront);
        for (Slot &slot : activeSlots_)
            slot.used = 0;
    }

    void grow() {
        if (activeSlots_.empty())
            activate(4);
        else
            rehash(activeSlots_.size() * 2);
    }

    void rehash(std::size_t newCapacity) {
        const std::span<Slot> oldSlots = activeSlots_;
        const std::size_t oldCapacity = oldSlots.size();
        const bool newAtFront = !activeAtFront();
        const std::size_t newMask = newCapacity - 1;

        if (oldCapacity + newCapacity <= slots_.size()) {
            const std::size_t newBase = newAtFront ? 0 : slots_.size() - newCapacity;
            std::span<Slot> next(slots_.data() + newBase, newCapacity);
            for (Slot &slot : next)
                slot.used = 0;
            for (const Slot &source : oldSlots) {
                if (!source.used)
                    continue;
                std::size_t j = hash_(source.key) & newMask;
                while (next[j].used)
                    j = (j + 1) & newMask;
                next[j].used = 1;
                next[j].key = source.key;
                next[j].value = source.value;
            }
            activeSlots_ = next;
        } else {
            std::vector<Slot> newSlots(newCapacity);
            const std::size_t newBase = newAtFront ? 0 : newSlots.size() - newCapacity;
            std::span<Slot> next(newSlots.data() + newBase, newCapacity);
            for (const Slot &source : oldSlots) {
                if (!source.used)
                    continue;
                std::size_t j = hash_(source.key) & newMask;
                while (next[j].used)
                    j = (j + 1) & newMask;
                next[j].used = 1;
                next[j].key = source.key;
                next[j].value = source.value;
            }
            slots_ = std::move(newSlots);
            bind(newCapacity, newAtFront);
        }
    }

    Hash hash_;
    std::vector<Slot> slots_;
    std::span<Slot> activeSlots_;
    std::size_t size_;
};

} // 命名空间 mss
