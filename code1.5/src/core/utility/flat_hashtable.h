#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/utility/rng.h"

namespace mss {

struct SplitMix64Hash {
    std::size_t operator()(std::uint64_t x) const noexcept { return splitmix64(x); }
};

// 开放寻址、线性探测；清空保留存储，下次在另一端建表，扩容时优先复用不重叠的区间。
template <typename Key, typename Value, typename Hash = SplitMix64Hash> class FlatHashTable {
    struct Slot {
        // 危险：空槽不初始化标量键值；只读 used，插入和 rehash 写完键值后才标记占用。
        // 复制与扩容仅复制占用槽的 key/value。
        Key key;
        Value value;
        unsigned char used;
    };

  public:
    static constexpr double kMaxLoadFactor = 0.5;

    FlatHashTable() : hash_(), slots_(nullptr), activeSlots_(nullptr), allocated_(0), capacity_(0), size_(0) {}
    FlatHashTable(const FlatHashTable &other) : FlatHashTable() { *this = other; }
    FlatHashTable(FlatHashTable &&other) : hash_(std::move(other.hash_)), slots_(std::exchange(other.slots_, nullptr)), activeSlots_(std::exchange(other.activeSlots_, nullptr)), allocated_(std::exchange(other.allocated_, 0)), capacity_(std::exchange(other.capacity_, 0)), size_(std::exchange(other.size_, 0)) {}
    ~FlatHashTable() { delete[] slots_; }

    FlatHashTable &operator=(const FlatHashTable &other) {
        if (this == &other) return *this;
        if (allocated_ < other.capacity_) {
            Slot *slots = new Slot[other.capacity_];
            delete[] slots_;
            slots_ = slots;
            allocated_ = other.capacity_;
        }
        hash_ = other.hash_;
        activeSlots_ = slots_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        for (std::size_t i = 0; i < capacity_; ++i) {
            activeSlots_[i].used = other.activeSlots_[i].used;
            if (activeSlots_[i].used) {
                activeSlots_[i].key = other.activeSlots_[i].key;
                activeSlots_[i].value = other.activeSlots_[i].value;
            }
        }
        return *this;
    }
    FlatHashTable &operator=(FlatHashTable &&other) {
        if (this == &other) return *this;
        hash_ = std::move(other.hash_);
        delete[] slots_;
        slots_ = std::exchange(other.slots_, nullptr);
        activeSlots_ = std::exchange(other.activeSlots_, nullptr);
        allocated_ = std::exchange(other.allocated_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
        size_ = std::exchange(other.size_, 0);
        return *this;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { capacity_ = size_ = 0; }

    void reserve(std::size_t expected) {
        std::size_t need = 1;
        while (need < expected * 2) need <<= 1;
        if (need > capacity_) rehash(need);
    }

    Value *find(const Key &key) {
        const std::size_t i = findIndex(key);
        return i == capacity_ ? nullptr : &activeSlots_[i].value;
    }
    const Value *find(const Key &key) const {
        const std::size_t i = findIndex(key);
        return i == capacity_ ? nullptr : &activeSlots_[i].value;
    }

    Value &operator[](const Key &key) {
        Slot &slot = insertionSlot(key);
        if (!slot.used) { slot.key = key; slot.value = Value{}; slot.used = 1; ++size_; }
        return slot.value;
    }
    void emplace(const Key &key, const Value &value) {
        Slot &slot = insertionSlot(key);
        if (!slot.used) { slot.key = key; slot.value = value; slot.used = 1; ++size_; }
    }

  private:
    std::size_t findIndex(const Key &key) const {
        if (size_ == 0) return capacity_;
        std::size_t i = hash_(key) & (capacity_ - 1);
        while (activeSlots_[i].used) {
            if (activeSlots_[i].key == key) return i;
            i = (i + 1) & (capacity_ - 1);
        }
        return capacity_;
    }

    Slot &insertionSlot(const Key &key) {
        if (size_ + 1 > capacity_ * kMaxLoadFactor) rehash(capacity_ ? capacity_ * 2 : 4);
        std::size_t i = hash_(key) & (capacity_ - 1);
        while (activeSlots_[i].used && !(activeSlots_[i].key == key)) i = (i + 1) & (capacity_ - 1);
        return activeSlots_[i];
    }

    void rehash(std::size_t newCapacity) {
        const bool allocate = capacity_ + newCapacity > allocated_;
        // 留够新旧两张表的空间，清空后重建到相同规模时继续复用双端区间。
        Slot *next = allocate ? new Slot[capacity_ + newCapacity] : (activeSlots_ == slots_ ? slots_ + allocated_ - newCapacity : slots_);
        for (std::size_t i = 0; i < newCapacity; ++i) next[i].used = 0;
        for (std::size_t i = 0; i < capacity_; ++i) {
            const Slot &source = activeSlots_[i];
            if (!source.used) continue;
            std::size_t j = hash_(source.key) & (newCapacity - 1);
            while (next[j].used) j = (j + 1) & (newCapacity - 1);
            next[j].key = source.key;
            next[j].value = source.value;
            next[j].used = 1;
        }
        if (allocate) {
            delete[] slots_;
            slots_ = next;
            allocated_ = capacity_ + newCapacity;
        }
        activeSlots_ = next;
        capacity_ = newCapacity;
    }

    Hash hash_;
    Slot *slots_;
    Slot *activeSlots_;
    std::size_t allocated_;
    std::size_t capacity_;
    std::size_t size_;
};

} // 命名空间 mss
