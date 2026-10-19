#pragma once

#include <cstddef>
#include <cstdint>

#include "core/datastructure/hash.h"

namespace mss {

// 开放寻址、线性探测；容量为 2 的幂，负载不超过 1/2。
// 清空保留存储，下次在另一端建表，扩容时优先复用不重叠的区间。
template <typename Key, typename Value, typename Hash = SplitMix64Hash> class FlatHashTable {
    struct Slot {
        // 危险：空槽的 key/value 不初始化；只读 used，插入填完 key/value 后才标记占用。
        Key key;
        Value value;
        unsigned char used;
    };

  public:
    static constexpr double kMaxLoadFactor = 0.5;

    FlatHashTable() : hash_(), slots_(nullptr), activeSlots_(nullptr), allocated_(0), capacity_(0), size_(0) {}
    FlatHashTable(const FlatHashTable &other) : FlatHashTable() { *this = other; }
    FlatHashTable(FlatHashTable &&other) : hash_(other.hash_), slots_(other.slots_), activeSlots_(other.activeSlots_), allocated_(other.allocated_), capacity_(other.capacity_), size_(other.size_) {
        other.slots_ = nullptr;
        other.activeSlots_ = nullptr;
        other.allocated_ = 0;
        other.capacity_ = 0;
        other.size_ = 0;
    }
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
        hash_ = other.hash_;
        delete[] slots_;
        slots_ = other.slots_;
        activeSlots_ = other.activeSlots_;
        allocated_ = other.allocated_;
        capacity_ = other.capacity_;
        size_ = other.size_;
        other.slots_ = nullptr;
        other.activeSlots_ = nullptr;
        other.allocated_ = 0;
        other.capacity_ = 0;
        other.size_ = 0;
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
        const std::size_t index = findIndex(key);
        return index == capacity_ ? nullptr : &activeSlots_[index].value;
    }

    const Value *find(const Key &key) const {
        const std::size_t index = findIndex(key);
        return index == capacity_ ? nullptr : &activeSlots_[index].value;
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
        std::size_t index = hash_(key) & (capacity_ - 1);
        while (activeSlots_[index].used) {
            if (activeSlots_[index].key == key) return index;
            index = (index + 1) & (capacity_ - 1);
        }
        return capacity_;
    }

    Slot &insertionSlot(const Key &key) {
        if (size_ + 1 > capacity_ * kMaxLoadFactor) rehash(capacity_ ? capacity_ * 2 : 4);
        std::size_t index = hash_(key) & (capacity_ - 1);
        while (activeSlots_[index].used && !(activeSlots_[index].key == key)) index = (index + 1) & (capacity_ - 1);
        return activeSlots_[index];
    }

    void rehash(std::size_t newCapacity) {
        const bool allocate = capacity_ + newCapacity > allocated_;
        // 留够新旧两张表的空间，清空后重建到相同规模时继续复用双端区间。
        Slot *next = allocate ? new Slot[capacity_ + newCapacity] : (activeSlots_ == slots_ ? slots_ + allocated_ - newCapacity : slots_);
        for (std::size_t i = 0; i < newCapacity; ++i) next[i].used = 0;
        for (std::size_t i = 0; i < capacity_; ++i) {
            const Slot &source = activeSlots_[i];
            if (!source.used) continue;
            std::size_t index = hash_(source.key) & (newCapacity - 1);
            while (next[index].used) index = (index + 1) & (newCapacity - 1);
            next[index].key = source.key;
            next[index].value = source.value;
            next[index].used = 1;
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
