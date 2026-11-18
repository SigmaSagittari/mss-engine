#pragma once

#include "algo/ioe_engine/ref_dom/ref_dom.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

#include "core/datastructure/vector.h"

namespace mss::ioe::ref_dom {

constexpr int signatureCapacity = (maxCells + 63) / 64;
struct FrontierWord {
    int populationCount(uint64_t bits) const {
#if defined(__POPCNT__) || defined(__aarch64__)
        return std::popcount(bits);
#else
        bits -= (bits >> 1) & UINT64_C(0x5555555555555555);
        bits = (bits & UINT64_C(0x3333333333333333)) +
               ((bits >> 2) & UINT64_C(0x3333333333333333));
        bits = (bits + (bits >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        return (int)((bits * UINT64_C(0x0101010101010101)) >> 56);
#endif
    }

    size_t combine(size_t seed, uint64_t value) const {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }
};

// 签名视图借用连通池内存，扩容或清空后失效。
class SignatureView {
public:
    SignatureView(const uint64_t *data, size_t count) : data_(data), size_(count) {}
    const uint64_t *begin() const { return data_; }
    const uint64_t *end() const { return data_ + size_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    uint64_t operator[](size_t index) const { return data_[index]; }
    bool operator==(SignatureView other) const {
        return size_ == other.size_ && std::equal(begin(), end(), other.begin());
    }

private:
    const uint64_t *data_;
    size_t size_;
};

// 每层去重连通签名。每个前沿状态只保存 32 位签名 ID。
class ConnectivityPool {
public:
    uint32_t intern(SignatureView signature);

    SignatureView operator[](uint32_t id) const {
        const Entry &entry = entries_[id];
        return {words_.data + entry.offset, entry.length};
    }
    size_t size() const { return entries_.size; }
    void reset(size_t expected);

private:
    struct Entry {
        size_t offset;
        size_t length;
        size_t hash;
    };
    static constexpr uint32_t empty_slot = std::numeric_limits<uint32_t>::max();
    mss::vector<uint32_t> slots_;
    mss::vector<Entry> entries_;
    mss::vector<uint64_t> words_;

    void rehash(size_t capacity);
};

class FactorTable {
public:
    struct State {
        uint32_t connectivityId;
        uint64_t hits;
        bool operator==(const State &other) const = default;
    };

    struct Entry { State state; int cost; };

    class iterator {
    public:
        iterator(const FactorTable *owner, size_t index) : owner_(owner), index_(index) { skipDead(); }
        Entry &operator*() const { return owner_->entries_.data[index_]; }
        Entry *operator->() const { return owner_->entries_.data + index_; }
        iterator &operator++() {
            ++index_;
            skipDead();
            return *this;
        }
        bool operator==(const iterator &other) const { return index_ == other.index_; }

    private:
        void skipDead() {
            while (index_ < owner_->entries_.size && !owner_->alive_[index_]) ++index_;
        }
        const FactorTable *owner_;
        size_t index_;
    };

    FactorTable() : live_(0) {}
    size_t size() const { return live_; }
    iterator begin() { return {this, 0}; }
    iterator end() { return {this, entries_.size}; }

    // 每个入层状态最多生成两个后继，按调用方提供的上限一次建表。
    void reset(size_t expected);

    void add(State state, size_t hash, int cost);

    // 支配剪枝只在本层停止插入后调用，下一层重建哈希索引。
    void erase(Entry *entry) {
        alive_[entry - entries_.data] = 0;
        --live_;
    }

private:
    static constexpr uint32_t emptySlot = std::numeric_limits<uint32_t>::max();
    mss::vector<Entry> entries_;
    mss::vector<uint8_t> alive_;
    mss::vector<uint32_t> slots_;
    size_t live_;
};

struct Frontier {
    int solve(const ModelGraph &model);

private:
    struct Workspace;
    struct FactorPlan;
    Workspace &getWorkspace();
    void makeFactorPlan(const ModelGraph &model, FactorPlan &plan);
};

} // 命名空间 mss::ioe::ref_dom
