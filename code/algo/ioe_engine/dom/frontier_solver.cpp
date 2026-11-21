#include "algo/ioe_engine/dom/frontier_solver.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <numeric>

#include "core/datastructure/bit_mask.h"
#include "core/datastructure/flat_hashtable.h"
#include "core/datastructure/vector.h"
#include "core/assert.h"

namespace mss::ioe_dom {

constexpr int signatureCapacity = (maxCells + 63) / 64;
struct DomWord {
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
class Signature {
public:
    Signature(const uint64_t *data, size_t count) : data_(data), size_(count) {}
    const uint64_t *begin() const { return data_; }
    const uint64_t *end() const { return data_ + size_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    uint64_t operator[](size_t index) const { return data_[index]; }
    bool operator==(Signature other) const {
        return size_ == other.size_ && std::equal(begin(), end(), other.begin());
    }

private:
    const uint64_t *data_;
    size_t size_;
};

// 每层去重连通签名。每个前沿状态只保存 32 位签名 ID。
class ConnectivityPool {
public:
    uint32_t intern(Signature signature);

    Signature operator[](uint32_t id) const {
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

uint32_t ConnectivityPool::intern(Signature signature) {
    size_t hash = signature.size();
    for (uint64_t word : signature) hash = DomWord().combine(hash, word);
    size_t slot = hash & (slots_.size - 1);
    while (slots_[slot] != empty_slot) {
        const Entry &entry = entries_[slots_[slot]];
        if (entry.hash == hash && entry.length == signature.size() &&
            std::equal(signature.begin(), signature.end(), words_.data + entry.offset))
            return slots_[slot];
        slot = (slot + 1) & (slots_.size - 1);
    }
    if (entries_.size + 1 > slots_.size / 2) {
        rehash(slots_.size * 2);
        slot = hash & (slots_.size - 1);
        while (slots_[slot] != empty_slot) slot = (slot + 1) & (slots_.size - 1);
    }
    const uint32_t id = (uint32_t)entries_.size;
    const size_t offset = words_.size;
    words_.resize(offset + signature.size());
    std::copy_n(signature.begin(), signature.size(), words_.data + offset);
    entries_.push_back({offset, signature.size(), hash});
    slots_[slot] = id;
    return id;
}

void ConnectivityPool::reset(size_t expected) {
    entries_.size = 0;
    entries_.resize(expected);
    entries_.size = 0;
    words_.resize(1);
    words_[0] = 0;
    // 保留空签名的 ID 0 和一个有效的空区间起点。
    entries_.push_back({0, 0, 0});
    size_t capacity = 16;
    while (capacity / 2 < expected) capacity *= 2;
    slots_.resize(capacity);
    std::fill_n(slots_.data, capacity, empty_slot);
    slots_[0] = 0;
}

void ConnectivityPool::rehash(size_t capacity) {
    slots_.size = 0;
    slots_.resize(capacity);
    std::fill_n(slots_.data, capacity, empty_slot);
    for (uint32_t id = 0; id < entries_.size; ++id) {
        size_t slot = entries_[id].hash & (capacity - 1);
        while (slots_[slot] != empty_slot) slot = (slot + 1) & (capacity - 1);
        slots_[slot] = id;
    }
}

void FactorTable::reset(size_t expected) {
    entries_.size = alive_.size = 0;
    entries_.resize(expected);
    alive_.resize(expected);
    entries_.size = alive_.size = live_ = 0;
    size_t capacity = 8;
    while (capacity - capacity / 4 < expected) capacity *= 2;
    slots_.resize(capacity);
    std::fill_n(slots_.data, capacity, emptySlot);
}

void FactorTable::add(State state, size_t hash, int cost) {
    const size_t mask = slots_.size - 1;
    size_t slot = hash & mask;
    while (slots_[slot] != emptySlot) {
        Entry &entry = entries_[slots_[slot]];
        if (entry.state == state) {
            entry.cost = std::min(entry.cost, cost);
            return;
        }
        slot = (slot + 1) & mask;
    }
    slots_[slot] = (uint32_t)entries_.size;
    entries_.push_back({state, cost});
    alive_.push_back(1);
    ++live_;
}

struct ConnectivityTransition {
    uint32_t connectivityId;
    int closed;
    size_t hashSeed;
};

struct FrontierSolver::FactorPlan {
    struct Factor {
        span<const int> scope;
        int slot;
        bool flag;
    };

    Factor factors[maxCells];
    int count;
    int slotCount;
};

struct FrontierSolver::Dominance {
    bool coarsens(Signature coarse, Signature fine, size_t signatureWords);
    bool exactQuotient(Signature coarse, Signature fine, size_t signatureWords);
    void prune(FactorTable &table, uint64_t baseMask, const ConnectivityPool &pool, size_t signatureWords);

private:
    template <size_t FixedWords> bool coarsensImpl(Signature coarse, Signature fine, size_t runtimeWords);
    template <size_t FixedWords> int mergePenaltyImpl(Signature a, Signature b, size_t runtimeWords, int limit);
    int mergePenalty(Signature a, Signature b, size_t signatureWords, int limit);
    bool penaltyAtMost(uint64_t candidate, uint64_t other, uint64_t baseMask, int allowance);
    bool matchWord(const uint64_t *eligible, int *match, int candidate, uint64_t &seen);
    bool matchWide(const bitMask<signatureCapacity> *eligible, int *match, int candidate, bitMask<signatureCapacity> &seen);
    uint64_t primaryKey(int cost, int baseHits);
};

struct FrontierSolver::Workspace {
    struct Connection {
        ConnectivityTransition branch[2];
        int penalty[2];
        bool ready;
    };
    struct Opening { uint64_t words[signatureCapacity]; int next; };

    // 危险：只复用本次扫掠需要的前缀。建表时初始化累加项，其余槽位在建立索引或生成状态时写入。
    struct {
        FactorPlan plan;
        int order[maxCells], slotLast[maxCells];
        uint64_t flagMember[maxCells], baseMember[maxCells], activeAfter[maxCells], baseAfter[maxCells];
        int futureCredits[maxCells];
    } factors;
    struct {
        int firstContact[maxCells], contactOrder[maxCells], slot[maxCells], slotLast[maxCells];
        uint64_t compactFuture[maxCells][signatureCapacity];
        Opening openings[maxCells];
        int firstOpening[maxCells], openingCount[maxCells];
    } contacts;
    struct {
        // 分量来自不同的已选中心或零区，已付首开至多另占一个。
        uint64_t withZeros[signatureCapacity * maxCells], outgoing[signatureCapacity * maxCells], canonical[signatureCapacity * maxCells];
        int offsets[maxCells];
    } signatures;
    FactorTable factorTables[2];
    ConnectivityPool connectivityPools[2];
    mss::vector<Connection> transitions;

    struct Pruning {
        struct CachedItem {
            FactorTable::Entry *item;
            uint64_t key;
            int quasiScore;
            uint8_t baseHits;
            uint8_t flagHits;
            bool exactDominated;
            bool crossDominated;

            bool keyBefore(const CachedItem &other) const {
                if (key != other.key) return key < other.key;
                // 主键相同时，命中因子总数较多的状态排在前面。
                return baseHits + flagHits > other.baseHits + other.flagHits;
            }
        };
        struct CachedList {
            CachedItem *data;
            size_t count;
            CachedItem *begin() { return data; }
            CachedItem *end() { return data + count; }
            void push_back(CachedItem item) { data[count++] = item; }
        };
        struct IdList {
            uint32_t *data;
            size_t count;
            uint32_t *begin() const { return data; }
            uint32_t *end() const { return data + count; }
            size_t size() const { return count; }
        };
        struct Bucket {
            IdList connectivityIds;
            CachedList entries;
            size_t stateCount;

            size_t pairOffset;
        };
        struct SurvivorList {
            CachedItem** data;
            size_t count;
            bool empty() const { return count == 0; }
            CachedItem** begin() const { return data; }
            CachedItem** end() const { return data + count; }
            CachedItem *front() const { return data[0]; }
            CachedItem *back() const { return data[count - 1]; }
            void push_back(CachedItem *item) { data[count++] = item; }
        };

        struct UnionSignature {
            uint64_t words[signatureCapacity];
            bool operator==(const UnionSignature &) const = default;
        };
        struct UnionHash {
            size_t operator()(const UnionSignature &reach) const {
                const DomWord word;
                size_t hash = word.combine(0, reach.words[0]);
                for (int i = 1; i < signatureCapacity; ++i)
                    if (reach.words[i]) hash = word.combine(hash, reach.words[i]);
                return hash;
            }
        };
        struct PendingPosting {
            int slice;
            uint32_t id;
        };

        // 危险：复用匹配和分量比较的缓冲。仅当前签名占用的前缀有效，各入口先写入再使用。
        struct {
            int fineMatch[maxCells];
            uint64_t eligibleWord[64];
            bitMask<signatureCapacity> eligibleWide[maxCells];
            uint64_t recovered[signatureCapacity * maxCells];
            int parent[maxCells];
        } components;
        struct {
            FlatHashTable<UnionSignature, size_t, UnionHash> bucketByUnion;
            mss::vector<size_t> stateCounts;
            mss::vector<Bucket> buckets;
            mss::vector<size_t> connectivityBucket;
            mss::vector<uint32_t> bucketIdStorage;
            mss::vector<CachedItem> cachedStorage;
        } partition;
        struct {
            mss::vector<int> pairIndex;
            mss::vector<uint32_t> lastId;
            vectorPool<uint32_t> postings;
            mss::vector<vectorPool<uint32_t>::vector> postingSlices;
            mss::vector<PendingPosting> pending;
        } positions;
        struct {
            mss::vector<int> minimumQuasi;
            mss::vector<int> maximumQuasi;
            mss::vector<int> minimumCost;
            mss::vector<size_t> componentCounts;
            mss::vector<size_t> populationOffsets;
            mss::vector<int> componentPopulations;
            mss::vector<int> minimumComponentPopulation;
            mss::vector<int> maximumComponentPopulation;
        } bounds;
        struct {
            mss::vector<CachedItem*> survivorStorage;
            mss::vector<SurvivorList> survivors;
            mss::vector<uint32_t> coarser;
            mss::vector<CachedItem*> liveTargets;
        } surviving;
    } pruning;
};

__declspec(noinline) FrontierSolver::Workspace &FrontierSolver::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void FrontierSolver::makeFactorPlan(const ClickGraph &model, FactorPlan &plan) {
    Workspace &workspace = getWorkspace();
    int (&order)[maxCells] = workspace.factors.order;
    int (&slotLast)[maxCells] = workspace.factors.slotLast;
    plan.count = plan.slotCount = 0;
    const auto add = [&](const Scopes &scopes, int start, bool flag) {
        for (int i = start; i < scopes.size(); ++i) {
            const span<const int> scope = scopes[i];
            if (!scope.size) continue;
            FactorPlan::Factor &factor = plan.factors[plan.count++];
            factor.scope = scope;
            factor.flag = flag;
        }
    };
    add(model.mine_scopes, 0, true);
    add(model.base_scopes, model.zero_scopes.size(), false);
    std::iota(order, order + plan.count, 0);
    std::sort(order, order + plan.count, [&](int a, int b) {
        const span<const int> x = plan.factors[a].scope, y = plan.factors[b].scope;
        if (x[0] != y[0]) return x[0] < y[0];
        if (x[x.size - 1] != y[y.size - 1]) return x[x.size - 1] < y[y.size - 1];
        return a < b;
    });
    for (int i = 0; i < plan.count; ++i) {
        FactorPlan::Factor &factor = plan.factors[order[i]];
        int slot = 0;
        while (slot < plan.slotCount && slotLast[slot] >= factor.scope[0]) ++slot;
        if (slot == plan.slotCount) ++plan.slotCount;
        slotLast[slot] = factor.scope[factor.scope.size - 1];
        factor.slot = slot;
    }
}

int FrontierSolver::compute(const ClickGraph &model, int allowance) {
    Workspace &workspace = getWorkspace();
    FactorPlan &factors = workspace.factors.plan;
    makeFactorPlan(model, factors);
    Dominance dominance;
    const int q = (int)model.candidates.size;
    const bool bounded = allowance < model.three_bv();
    // 活跃因子来自当前线及相邻两线，每线至多 16 格。
    assert_(factors.slotCount <= 64, "DOM 活跃因子超过一个字");
    uint64_t (&flagMember)[maxCells] = workspace.factors.flagMember;
    uint64_t (&baseMember)[maxCells] = workspace.factors.baseMember;
    uint64_t (&activeAfter)[maxCells] = workspace.factors.activeAfter;
    uint64_t (&baseAfter)[maxCells] = workspace.factors.baseAfter;
    int (&futureCredits)[maxCells] = workspace.factors.futureCredits;
    std::fill_n(flagMember, q, 0);
    std::fill_n(baseMember, q, 0);
    std::fill_n(activeAfter, q, 0);
    std::fill_n(baseAfter, q, 0);
    std::fill_n(futureCredits, q, 0);
    for (int f = 0; f < factors.count; ++f) {
        const FactorPlan::Factor &factor = factors.factors[f];
        const uint64_t bit = uint64_t{1} << factor.slot;
        if (!factor.flag) ++futureCredits[factor.scope[0]];
        for (int variable : factor.scope)
            (factor.flag ? flagMember[variable] : baseMember[variable]) |= bit;
        for (int i = factor.scope[0]; i < factor.scope[factor.scope.size - 1]; ++i) {
            activeAfter[i] |= bit;
            if (!factor.flag) baseAfter[i] |= bit;
        }
    }

    int (&firstContact)[maxCells] = workspace.contacts.firstContact;
    std::fill_n(firstContact, q, q);
    for (int i = 0; i < q; ++i) {
        for (int other : model.graph[i]) {
            if (other <= i) continue;
            firstContact[other] = std::min(firstContact[other], i);
        }
    }
    for (int z = 0; z < model.zero_scopes.size(); ++z) {
        const span<const int> scope = model.zero_scopes[z];
        if (!scope.size) continue;
        for (int candidate : scope) firstContact[candidate] = std::min(firstContact[candidate], scope[0]);
    }
    int (&contactOrder)[maxCells] = workspace.contacts.contactOrder;
    int (&connectivitySlot)[maxCells] = workspace.contacts.slot;
    int contactCount = 0;
    for (int i = 0; i < q; ++i) {
        if (firstContact[i] < q) contactOrder[contactCount++] = i;
        else connectivitySlot[i] = -1;
    }
    std::sort(contactOrder, contactOrder + contactCount, [&](int a, int b) {
        return firstContact[a] != firstContact[b] ? firstContact[a] < firstContact[b] : a < b;
    });
    int (&slotLast)[maxCells] = workspace.contacts.slotLast;
    int slotCount = 0;
    for (int at = 0; at < contactCount; ++at) {
        const int candidate = contactOrder[at];
        int slot = 0;
        while (slot < slotCount && slotLast[slot] >= firstContact[candidate]) ++slot;
        if (slot == slotCount) ++slotCount;
        slotLast[slot] = candidate;
        connectivitySlot[candidate] = slot;
    }
    const int signatureWords = std::max(1, (slotCount + 63) / 64);
    uint64_t (&compactFuture)[maxCells][signatureCapacity] = workspace.contacts.compactFuture;
    const auto addContact = [&](int from, int to) {
        const int slot = connectivitySlot[to];
        compactFuture[from][slot / 64] |= uint64_t{1} << (slot % 64);
    };
    for (int i = 0; i < q; ++i) {
        std::fill_n(compactFuture[i], signatureWords, 0);
        for (int other : model.graph[i])
            if (other > i) addContact(i, other);
    }

    Workspace::Opening (&openings)[maxCells] = workspace.contacts.openings;
    int (&firstOpening)[maxCells] = workspace.contacts.firstOpening;
    int (&openingCount)[maxCells] = workspace.contacts.openingCount;
    std::fill_n(firstOpening, q, -1);
    std::fill_n(openingCount, q, 0);
    for (int z = model.zero_scopes.size() - 1; z >= 0; --z) {
        const span<const int> scope = model.zero_scopes[z];
        if (!scope.size) continue;
        const int at = scope[0];
        Workspace::Opening &entry = openings[z];
        entry = {{}, firstOpening[at]};
        for (int candidate : scope) {
            const int slot = connectivitySlot[candidate];
            entry.words[slot / 64] |= uint64_t{1} << (slot % 64);
            for (int other : scope)
                if (other > candidate) addContact(candidate, other);
        }
        firstOpening[at] = z;
        ++openingCount[at];
    }
    for (int i = q - 1, later = 0; i >= 0; --i) {
        const int credit = futureCredits[i] + openingCount[i];
        futureCredits[i] = later;
        later += credit;
    }

    FactorTable (&tables)[2] = workspace.factorTables;
    ConnectivityPool (&pools)[2] = workspace.connectivityPools;
    mss::vector<Workspace::Connection> &transitions = workspace.transitions;
    tables[0].reset(16);
    tables[0].add({0, 0}, DomWord().combine(DomWord().combine(0, 0), 0), model.three_bv());
    pools[0].reset(0);

    uint64_t (&withZeros)[signatureCapacity * maxCells] = workspace.signatures.withZeros;
    uint64_t (&outgoing)[signatureCapacity * maxCells] = workspace.signatures.outgoing;
    uint64_t (&canonical)[signatureCapacity * maxCells] = workspace.signatures.canonical;
    int (&offsets)[maxCells] = workspace.signatures.offsets;
    constexpr int noBound = std::numeric_limits<int>::max();
    for (int i = 0; i < q; ++i) {
        FactorTable &table = tables[i & 1];
        FactorTable &next = tables[(i + 1) & 1];
        ConnectivityPool &pool = pools[i & 1];
        ConnectivityPool &nextPool = pools[(i + 1) & 1];
        next.reset(table.size() * 2 + 16);
        nextPool.reset(pool.size() * 2 + 16);
        transitions.size = 0;
        transitions.resize(pool.size());
        for (size_t id = 0; id < transitions.size; ++id) transitions[id].ready = false;

        const int slot = connectivitySlot[i];
        const uint64_t factorMember = flagMember[i] | baseMember[i];
        for (const FactorTable::Entry &item : table) {
            const FactorTable::State &old = item.state;
            Workspace::Connection &cached = transitions[old.connectivityId];
            if (!cached.ready) {
                const Signature previous = pool[old.connectivityId];
                int oldWords = (int)previous.size();
                const uint64_t *oldData = previous.begin();
                if (firstOpening[i] >= 0) {
                    std::copy_n(previous.begin(), oldWords, withZeros);
                    for (int z = firstOpening[i]; z >= 0; z = openings[z].next) {
                        std::copy_n(openings[z].words, signatureWords, withZeros + oldWords);
                        oldWords += signatureWords;
                    }
                    oldData = withZeros;
                }

                for (int selected = 0; selected <= 1; ++selected) {
                    int closed = 0, outWords = 0;
                    uint32_t id;
                    if (signatureWords == 1) {
                        const uint64_t current = slot >= 0 ? uint64_t{1} << slot : 0;
                        uint64_t merged = selected ? compactFuture[i][0] : 0;
                        for (int j = 0; j < oldWords; ++j) {
                            const uint64_t component = oldData[j];
                            const uint64_t remaining = component & ~current;
                            if (selected && (component & current)) merged |= remaining;
                            else if (remaining) outgoing[outWords++] = remaining;
                            else ++closed;
                        }
                        if (selected) {
                            if (merged) outgoing[outWords++] = merged;
                            else ++closed;
                        }
                        std::sort(outgoing, outgoing + outWords);
                        id = nextPool.intern({outgoing, (size_t)outWords});
                    } else {
                        const int word = slot >= 0 ? slot / 64 : 0;
                        const uint64_t bit = slot >= 0 ? uint64_t{1} << (slot % 64) : 0;
                        uint64_t merged[signatureCapacity]{};
                        if (selected) std::copy_n(compactFuture[i], signatureWords, merged);
                        int components = 0;
                        for (int j = 0; j < oldWords; j += signatureWords) {
                            const bool join = selected && (oldData[j + word] & bit);
                            bool remaining = false;
                            for (int w = 0; w < signatureWords; ++w) {
                                const uint64_t projected = oldData[j + w] & ~(w == word ? bit : 0);
                                if (join) merged[w] |= projected;
                                else outgoing[outWords + w] = projected;
                                remaining |= projected != 0;
                            }
                            if (join) continue;
                            if (remaining) {
                                offsets[components++] = outWords;
                                outWords += signatureWords;
                            } else ++closed;
                        }
                        if (selected) {
                            if (std::any_of(merged, merged + signatureWords, [](uint64_t w) { return w != 0; })) {
                                offsets[components++] = outWords;
                                std::copy_n(merged, signatureWords, outgoing + outWords);
                                outWords += signatureWords;
                            } else ++closed;
                        }
                        std::sort(offsets, offsets + components, [&](int a, int b) {
                            return std::lexicographical_compare(outgoing + a, outgoing + a + signatureWords,
                                                                outgoing + b, outgoing + b + signatureWords);
                        });
                        for (int component = 0; component < components; ++component)
                            std::copy_n(outgoing + offsets[component], signatureWords, canonical + signatureWords * component);
                        id = nextPool.intern({canonical, (size_t)outWords});
                    }
                    cached.branch[selected] = {id, closed, DomWord().combine(0, id)};
                }

                int (&penalty)[2] = cached.penalty;
                if (cached.branch[0].connectivityId == cached.branch[1].connectivityId) {
                    penalty[0] = penalty[1] = 0;
                } else {
                    penalty[0] = penalty[1] = noBound;
                    const Signature skip = nextPool[cached.branch[0].connectivityId];
                    const Signature take = nextPool[cached.branch[1].connectivityId];
                    bool sameUnion = true;
                    for (int word = 0; word < signatureWords; ++word) {
                        uint64_t skipUnion = 0, takeUnion = 0;
                        for (size_t j = word; j < skip.size(); j += signatureWords) skipUnion |= skip[j];
                        for (size_t j = word; j < take.size(); j += signatureWords) takeUnion |= take[j];
                        if (skipUnion != takeUnion) { sameUnion = false; break; }
                    }
                    if (sameUnion) {
                        if (dominance.coarsens(skip, take, signatureWords)) penalty[0] = 0;
                        else if (dominance.exactQuotient(take, skip, signatureWords))
                            penalty[0] = (int)((skip.size() - take.size()) / signatureWords);
                        if (dominance.coarsens(take, skip, signatureWords)) penalty[1] = 0;
                        else if (dominance.exactQuotient(skip, take, signatureWords))
                            penalty[1] = (int)((take.size() - skip.size()) / signatureWords);
                    }
                }
                cached.ready = true;
            }

            const uint64_t newFlags = flagMember[i] & ~old.hits;
            const uint64_t newBases = baseMember[i] & ~old.hits;
            const int factorCost = CountBits()(newFlags) - CountBits()(newBases);
            const ConnectivityTransition (&branch)[2] = cached.branch;
            const int (&penalty)[2] = cached.penalty;
            // 只补偿下一步仍能改变的因子。同分时优先跳过，至少保留一种转移。
            const int delta = 1 + branch[1].closed - branch[0].closed + factorCost;
            int first = 0, last = 1;
            if (penalty[0] != noBound && delta >= penalty[0] &&
                delta >= penalty[0] + CountBits()(newFlags & activeAfter[i])) last = 0;
            else if (penalty[1] != noBound && -delta >= penalty[1] &&
                -delta >= penalty[1] + CountBits()(newBases & activeAfter[i])) first = 1;
            for (int selected = first; selected <= last; ++selected) {
                const uint64_t hits = (old.hits | (selected ? factorMember : 0)) & activeAfter[i];
                // 起始 3BV 已计过每个零区，之后由关闭分量的操作付出点击数。
                const int cost = item.cost + selected + branch[selected].closed
                    + (selected ? factorCost : 0) - openingCount[i];
                // 未命中的数字和未开始的零区是后续仅有的减项。
                if (bounded && cost - futureCredits[i] - CountBits()(baseAfter[i] & ~hits) > allowance) continue;
                next.add({branch[selected].connectivityId, hits}, DomWord().combine(branch[selected].hashSeed, hits), cost);
            }
        }

        if (!next.size()) return -1;
        // 每处理两个候选做一次完整支配剪枝，末轮也做。
        if (i % 2 == 1 || i + 1 == q) dominance.prune(next, baseAfter[i], nextPool, signatureWords);
    }

    FactorTable &final = tables[q & 1];
    FactorTable::iterator found = final.begin();
    while (found != final.end() && (found->state.connectivityId != 0 || found->state.hits != 0)) ++found;
    assert_(found != final.end(), "DOM 前沿缺少终止状态");
    return found->cost <= allowance ? found->cost : -1;
}

// 比较未覆盖的编号格和已支付的旗，求两种因子状态的最大额外代价。
bool FrontierSolver::Dominance::penaltyAtMost(uint64_t candidate, uint64_t other,
                                      uint64_t baseMask, int allowance) {
    const uint64_t candidateGood = candidate ^ baseMask;
    const uint64_t otherGood = other ^ baseMask;
    return CountBits()(otherGood & ~candidateGood) <= allowance;
}

bool FrontierSolver::Dominance::matchWord(const uint64_t *eligible, int *match, int candidate, uint64_t &seen) {
    for (uint64_t choices = eligible[candidate] & ~seen; choices; choices &= choices - 1) {
        const int fine = std::countr_zero(choices);
        seen |= uint64_t{1} << fine;
        if (match[fine] < 0 || matchWord(eligible, match, match[fine], seen)) {
            match[fine] = candidate;
            return true;
        }
    }
    return false;
}

bool FrontierSolver::Dominance::matchWide(const bitMask<signatureCapacity> *eligible, int *match, int candidate, bitMask<signatureCapacity> &seen) {
    bitMask<signatureCapacity> choices = eligible[candidate];
    choices &= ~seen;
    while (choices.any()) {
        const int fine = choices.firstSetBit();
        seen.set(fine);
        if (match[fine] < 0 || matchWide(eligible, match, match[fine], seen)) {
            match[fine] = candidate;
            return true;
        }
        choices &= ~seen;
    }
    return false;
}

uint64_t FrontierSolver::Dominance::primaryKey(int cost, int baseHits) {
    // 将有符号代价偏移后打包，保持原字典序。
    return (uint64_t{(uint32_t)(cost + baseHits) ^ UINT32_C(0x80000000)} << 32) |
           ((uint32_t)cost ^ UINT32_C(0x80000000));
}

// 每个细分量的可达集合都须被某个粗分量包含。每个粗分量须匹配不同的细分量，避免多付一次起手点击。
template <size_t FixedWords>
bool FrontierSolver::Dominance::coarsensImpl(Signature coarse,
                                  Signature fine,
                                  size_t runtimeWords) {
    const size_t signatureWords = FixedWords ? FixedWords : runtimeWords;
    const size_t coarseCount = coarse.size() / signatureWords;
    const size_t fineCount = fine.size() / signatureWords;
    if (coarseCount > fineCount) return false;
    if (coarseCount == 0) return fineCount == 0;

    Workspace &workspace = FrontierSolver().getWorkspace();
    int (&fineMatch)[maxCells] = workspace.pruning.components.fineMatch;
    // 至多 64 个细分量时，用一个字保存每个粗分量的匹配候选。
    constexpr size_t kInlineComponents = 64;
    if (coarseCount <= kInlineComponents && fineCount <= kInlineComponents) {
        uint64_t (&eligible)[kInlineComponents] = workspace.pruning.components.eligibleWord;
        uint64_t represented = 0;
        for (size_t c = 0; c < coarseCount; ++c) {
            uint64_t mask = 0;
            for (size_t f = 0; f < fineCount; ++f) {
                bool subset = true;
                for (size_t word = 0; word < signatureWords; ++word) {
                    const uint64_t fineWord = fine[f * signatureWords + word];
                    const uint64_t coarseWord = coarse[c * signatureWords + word];
                    if ((fineWord | coarseWord) != coarseWord) {
                        subset = false;
                        break;
                    }
                }
                if (subset) mask |= uint64_t{1} << f;
            }
            eligible[c] = mask;
            represented |= mask;
        }
        const uint64_t allFine = fineCount == 64
            ? ~uint64_t{0} : ((uint64_t{1} << fineCount) - 1);
        if (represented != allFine) return false;

        std::fill_n(fineMatch, fineCount, -1);
        for (size_t c = 0; c < coarseCount; ++c) {
            uint64_t seen = 0;
            if (!matchWord(eligible, fineMatch, (int)c, seen)) return false;
        }
        return true;
    }

    // 每个粗分量用位掩码保存可匹配的细分量。
    bitMask<signatureCapacity> (&eligible)[maxCells] = workspace.pruning.components.eligibleWide;
    std::fill_n(eligible, coarseCount, bitMask<signatureCapacity>{});
    for (size_t f = 0; f < fineCount; ++f) {
        bool represented = false;
        for (size_t c = 0; c < coarseCount; ++c) {
            bool subset = true;
            for (size_t word = 0; word < signatureWords; ++word) {
                const uint64_t fineWord = fine[f * signatureWords + word];
                const uint64_t coarseWord = coarse[c * signatureWords + word];
                if ((fineWord | coarseWord) != coarseWord) {
                    subset = false;
                    break;
                }
            }
            if (subset) eligible[c].set(f);
            represented = represented || subset;
        }
        if (!represented) return false;
    }

    // 先给每个粗分量匹配一个不同的细分量。其余细分量只需归属任一粗分量。
    std::fill_n(fineMatch, fineCount, -1);
    for (size_t c = 0; c < coarseCount; ++c) {
        bitMask<signatureCapacity> seen;
        if (!matchWide(eligible, fineMatch, (int)c, seen)) return false;
    }
    return true;
}

// 单字签名走固定宽度版本，省去按字循环和偏移计算。
bool FrontierSolver::Dominance::coarsens(Signature coarse,
                                  Signature fine,
                                  size_t signatureWords) {
    if (signatureWords == 1) return coarsensImpl<1>(coarse, fine, 1);
    return coarsensImpl<0>(coarse, fine, signatureWords);
}

// 反向支配要求每个细分量有唯一归属，且并集恰好还原粗分量。仅满足包含关系时，可能额外合并将来的分量。
bool FrontierSolver::Dominance::exactQuotient(Signature coarse,
                                        Signature fine,
                                        size_t signatureWords) {
    const size_t coarseCount = coarse.size() / signatureWords;
    const size_t fineCount = fine.size() / signatureWords;
    if (coarseCount == 0 || coarseCount >= fineCount) return false;
    // 同一桶的总可达集合相同。
    if (coarseCount == 1) return true;
    Workspace &workspace = FrontierSolver().getWorkspace();
    uint64_t (&recovered)[signatureCapacity * maxCells] = workspace.pruning.components.recovered;
    std::fill_n(recovered, coarse.size(), 0);
    if (signatureWords == 1 && coarseCount <= 64) {
        for (uint64_t component : fine) {
            size_t parent = coarseCount;
            for (size_t c = 0; c < coarseCount; ++c) {
                if ((component & ~coarse[c]) != 0) continue;
                if (parent != coarseCount) return false;
                parent = c;
            }
            if (parent == coarseCount) return false;
            recovered[parent] |= component;
        }
        for (size_t c = 0; c < coarseCount; ++c) {
            if (recovered[c] != coarse[c]) return false;
        }
        return true;
    }
    for (size_t f = 0; f < fineCount; ++f) {
        size_t parent = coarseCount;
        for (size_t c = 0; c < coarseCount; ++c) {
            bool contained = true;
            for (size_t word = 0; word < signatureWords; ++word) {
                if ((fine[f * signatureWords + word] &
                     ~coarse[c * signatureWords + word]) != 0) {
                    contained = false;
                    break;
                }
            }
            if (!contained) continue;
            if (parent != coarseCount) return false;
            parent = c;
        }
        if (parent == coarseCount) return false;
        for (size_t word = 0; word < signatureWords; ++word) {
            recovered[parent * signatureWords + word] |=
                fine[f * signatureWords + word];
        }
    }
    return std::equal(recovered, recovered + coarse.size(), coarse.begin());
}

// 两侧总可达集合相同，沿重叠关系合并后得到共同的粗划分。A 比 B 最多多付 A 的分量数减共同分量数次起手点击。
template <size_t FixedWords>
int FrontierSolver::Dominance::mergePenaltyImpl(Signature a,
                                      Signature b,
                                      size_t runtimeWords, int maximumPenalty) {
    const size_t words = FixedWords ? FixedWords : runtimeWords;
    const int na = (int)(a.size() / words);
    const int nb = (int)(b.size() / words);
    if (na + nb > 128) return std::numeric_limits<int>::max();
    // 把同一 A 分量触及的 B 分量用并查集合起来。
    Workspace &workspace = FrontierSolver().getWorkspace();
    int (&parent)[maxCells] = workspace.pruning.components.parent;
    std::iota(parent, parent + nb, 0);
    auto root = [&](int v) {
        while (parent[v] != v) { parent[v] = parent[parent[v]]; v = parent[v]; }
        return v;
    };
    int groups = nb;
    // 总可达集合相同，非空 A 分量一定触及 B。每次合并都会把代价下界加一。
    if (na - groups > maximumPenalty) return std::numeric_limits<int>::max();
    for (int x = 0; x < na; ++x) {
        int anchor = -1;
        for (int y = 0; y < nb; ++y) {
            bool overlap = false;
            for (size_t w = 0; w < words; ++w) {
                if (a[x * words + w] & b[y * words + w]) { overlap = true; break; }
            }
            if (!overlap) continue;
            const int ry = root(y);
            if (anchor < 0) anchor = ry;
            else if (anchor != ry) {
                parent[ry] = anchor;
                --groups;
                if (na - groups > maximumPenalty)
                    return std::numeric_limits<int>::max();
                // B 已合并到一个分量时，剩下的非空 A 分量都触及它。
                if (groups == 1) return na - 1;
            }
        }
    }
    return na - groups;
}

int FrontierSolver::Dominance::mergePenalty(Signature a,
                                      Signature b,
                                      size_t signatureWords,
                                      int maximumPenalty) {
    if (signatureWords == 1)
        return mergePenaltyImpl<1>(a, b, 1, maximumPenalty);
    return mergePenaltyImpl<0>(a, b, signatureWords, maximumPenalty);
}

void FrontierSolver::Dominance::prune(FactorTable &table, uint64_t baseMask,
                              const ConnectivityPool &connectivityPool,
                              size_t signatureWords) {
    if (table.size() < 2) return;
    using CachedItem = Workspace::Pruning::CachedItem;
    using Bucket = Workspace::Pruning::Bucket;
    using SurvivorList = Workspace::Pruning::SurvivorList;
    Workspace &workspace = FrontierSolver().getWorkspace();
    constexpr size_t noPosting = std::numeric_limits<size_t>::max();
    const size_t connectivityCount = connectivityPool.size();
    mss::vector<size_t> &stateCounts = workspace.pruning.partition.stateCounts;
    stateCounts.size = 0;
    stateCounts.resize(connectivityCount);
    std::fill_n(stateCounts.data, connectivityCount, 0);
    for (FactorTable::iterator item = table.begin(); item != table.end(); ++item)
        ++stateCounts[item->state.connectivityId];

    // 以总可达集合分桶，同一桶的局部排序供三轮支配剪枝复用。
    FlatHashTable<Workspace::Pruning::UnionSignature, size_t, Workspace::Pruning::UnionHash> &lookup = workspace.pruning.partition.bucketByUnion;
    lookup.clear();
    lookup.reserve(connectivityCount);
    mss::vector<Bucket> &buckets = workspace.pruning.partition.buckets;
    buckets.size = 0;
    mss::vector<size_t> &connectivityBucket = workspace.pruning.partition.connectivityBucket;
    connectivityBucket.size = 0;
    connectivityBucket.resize(connectivityCount);
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        if (!stateCounts[id]) continue;
        const Signature signature = connectivityPool[id];
        Workspace::Pruning::UnionSignature reach{};
        for (size_t i = 0; i < signature.size(); i += signatureWords)
            for (size_t word = 0; word < signatureWords; ++word)
                reach.words[word] |= signature[i + word];
        size_t &index = lookup[reach];
        if (!index) {
            buckets.push_back({{}, {}, 0, noPosting});
            index = buckets.size;
        }
        const size_t bucket = index - 1;
        connectivityBucket[id] = bucket;
        ++buckets[bucket].connectivityIds.count;
        buckets[bucket].stateCount += stateCounts[id];
    }

    mss::vector<uint32_t> &bucketIdStorage = workspace.pruning.partition.bucketIdStorage;
    bucketIdStorage.size = 0;
    bucketIdStorage.resize(connectivityCount);
    size_t bucketIdOffset = 0;
    for (size_t bucketId = 0; bucketId < buckets.size; ++bucketId) {
        Bucket &bucket = buckets[bucketId];
        bucket.connectivityIds.data = bucketIdStorage.data + bucketIdOffset;
        bucketIdOffset += bucket.connectivityIds.count;
        bucket.connectivityIds.count = 0;
    }
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        if (stateCounts[id] == 0) continue;
        Workspace::Pruning::IdList &ids = buckets[connectivityBucket[id]].connectivityIds;
        ids.data[ids.count++] = id;
    }
    mss::vector<int> &pairIndex = workspace.pruning.positions.pairIndex;
    mss::vector<uint32_t> &lastId = workspace.pruning.positions.lastId;
    vectorPool<uint32_t> &postings = workspace.pruning.positions.postings;
    mss::vector<vectorPool<uint32_t>::vector> &postingSlices = workspace.pruning.positions.postingSlices;
    mss::vector<Workspace::Pruning::PendingPosting> &pending = workspace.pruning.positions.pending;
    pairIndex.size = postings.data.size = postingSlices.size = 0;
    if (signatureWords == 1) {
        constexpr int pairCount = 64 * 64;
        for (size_t bucketId = 0; bucketId < buckets.size; ++bucketId) {
            Bucket &bucket = buckets[bucketId];
            if (bucket.connectivityIds.size() < 64) continue;
            // 签名 ID 在桶之间也唯一，本轮剪枝只需清一次去重表。
            if (pairIndex.size == 0) {
                lastId.resize(pairCount);
                std::fill_n(lastId.data, pairCount, std::numeric_limits<uint32_t>::max());
            }
            bucket.pairOffset = pairIndex.size;
            pairIndex.resize(pairIndex.size + pairCount);
            int *index = pairIndex.data + bucket.pairOffset;
            std::fill_n(index, pairCount, -1);

            // 同一签名里的同一对位置只记一次。每对位置首次出现时登记区间，先用区间长度计数。
            const size_t firstSlice = postingSlices.size;
            pending.size = 0;
            for (uint32_t id : bucket.connectivityIds) {
                for (uint64_t component : connectivityPool[id]) {
                    for (uint64_t outer = component; outer; outer &= outer - 1) {
                        const int a = std::countr_zero(outer);
                        for (uint64_t inner = outer & (outer - 1); inner; inner &= inner - 1) {
                            const int pair = (a << 6) | std::countr_zero(inner);
                            if (lastId[pair] == id) continue;
                            lastId[pair] = id;
                            int &slot = index[pair];
                            if (slot < 0) {
                                slot = (int)postingSlices.size;
                                postingSlices.push_back({0, 0});
                            }
                            ++postingSlices[slot].count;
                            pending.push_back({slot, id});
                        }
                    }
                }
            }
            size_t end = postings.data.size;
            for (size_t slice = firstSlice; slice < postingSlices.size; ++slice) {
                vectorPool<uint32_t>::vector &part = postingSlices[slice];
                part.offset = end;
                end += part.count;
                part.count = 0;
            }
            postings.data.resize(end);
            for (size_t i = 0; i < pending.size; ++i) {
                const Workspace::Pruning::PendingPosting &item = pending[i];
                vectorPool<uint32_t>::vector &part = postingSlices[item.slice];
                postings.data[part.offset + part.count++] = item.id;
            }
        }
    }

    mss::vector<CachedItem> &cachedStorage = workspace.pruning.partition.cachedStorage;
    cachedStorage.size = 0;
    cachedStorage.resize(table.size());
    size_t cachedOffset = 0;
    for (size_t bucketId = 0; bucketId < buckets.size; ++bucketId) {
        Bucket &bucket = buckets[bucketId];
        bucket.entries.data = cachedStorage.data + cachedOffset;
        cachedOffset += bucket.stateCount;
    }
    for (FactorTable::iterator item = table.begin(); item != table.end(); ++item) {
        const size_t bucket = connectivityBucket[item->state.connectivityId];
        const int baseHits = CountBits()(item->state.hits & baseMask);
        const int flagHits = CountBits()(item->state.hits) - baseHits;
        const uint64_t key = primaryKey(item->cost, baseHits);
        const int quasiScore = item->cost + baseHits - flagHits;
        buckets[bucket].entries.push_back(
            CachedItem{&*item, key, quasiScore, (uint8_t)baseHits,
                       (uint8_t)flagHits, false, false});
    }
    for (size_t bucketId = 0; bucketId < buckets.size; ++bucketId) {
        Bucket &bucket = buckets[bucketId];
        std::sort(bucket.entries.begin(), bucket.entries.end(),
                  [](const CachedItem &a, const CachedItem &b) {
                      return a.keyBefore(b);
                  });
    }

    // 每个签名最多保留其入层状态数个条目。各签名使用同一缓冲中的独立区间。
    mss::vector<CachedItem*> &survivorStorage = workspace.pruning.surviving.survivorStorage;
    survivorStorage.size = 0;
    survivorStorage.resize(table.size());
    mss::vector<SurvivorList> &survivors = workspace.pruning.surviving.survivors;
    survivors.size = 0;
    survivors.resize(connectivityCount);
    mss::vector<int> &minimumQuasi = workspace.pruning.bounds.minimumQuasi;
    mss::vector<int> &maximumQuasi = workspace.pruning.bounds.maximumQuasi;
    mss::vector<int> &minimumCost = workspace.pruning.bounds.minimumCost;
    minimumQuasi.size = 0;
    minimumQuasi.resize(connectivityCount);
    maximumQuasi.size = 0;
    maximumQuasi.resize(connectivityCount);
    minimumCost.size = 0;
    minimumCost.resize(connectivityCount);
    size_t survivorOffset = 0;
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        survivors[id] = {survivorStorage.data + survivorOffset, 0};
        survivorOffset += stateCounts[id];
    }
    auto dominates = [&](const CachedItem *candidate, const CachedItem *target) {
        const int allowance = target->item->cost - candidate->item->cost;
        if (allowance < 0) return false;
        // 因子数量差给出额外代价的下界，先据此排除不可支配的比较。
        const int lowerBound =
            std::max(0, target->flagHits - candidate->flagHits) +
            std::max(0, candidate->baseHits - target->baseHits);
        if (lowerBound > allowance) return false;
        return penaltyAtMost(
            candidate->item->state.hits, target->item->state.hits,
            baseMask, allowance);
    };

    // 先比较连通签名相同的状态。保留条目沿用分桶排序的顺序。
    for (size_t bucketId = 0; bucketId < buckets.size; ++bucketId) {
        Bucket &bucket = buckets[bucketId];
        for (CachedItem &cached : bucket.entries) {
            const uint32_t connectivityId = cached.item->state.connectivityId;
            SurvivorList &kept = survivors[connectivityId];
            bool isDominated = false;
            for (CachedItem *prior : kept) {
                if (dominates(prior, &cached)) {
                    isDominated = true;
                    break;
                }
            }
            cached.exactDominated = isDominated;
            if (!isDominated) {
                if (kept.empty()) {
                    minimumQuasi[connectivityId] = maximumQuasi[connectivityId] = cached.quasiScore;
                    minimumCost[connectivityId] = cached.item->cost;
                } else {
                    minimumQuasi[connectivityId] = std::min(minimumQuasi[connectivityId], cached.quasiScore);
                    maximumQuasi[connectivityId] = std::max(maximumQuasi[connectivityId], cached.quasiScore);
                    minimumCost[connectivityId] = std::min(minimumCost[connectivityId], cached.item->cost);
                }
                kept.push_back(&cached);
            }
        }
    }

    mss::vector<size_t> &componentCounts = workspace.pruning.bounds.componentCounts;
    componentCounts.size = 0;
    componentCounts.resize(connectivityCount);
    // 按签名给分量的位数表分配连续区间。
    mss::vector<size_t> &populationOffsets = workspace.pruning.bounds.populationOffsets;
    populationOffsets.size = 0;
    populationOffsets.resize(connectivityCount + 1);
    populationOffsets[0] = 0;
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        componentCounts[id] = survivors[id].empty() ? 0 : connectivityPool[id].size() / signatureWords;
        populationOffsets[id + 1] = populationOffsets[id] + componentCounts[id];
    }
    mss::vector<int> &componentPopulations = workspace.pruning.bounds.componentPopulations;
    componentPopulations.size = 0;
    componentPopulations.resize(populationOffsets[connectivityCount]);
    mss::vector<int> &minimumComponentPopulation = workspace.pruning.bounds.minimumComponentPopulation;
    mss::vector<int> &maximumComponentPopulation = workspace.pruning.bounds.maximumComponentPopulation;
    minimumComponentPopulation.size = 0;
    minimumComponentPopulation.resize(connectivityCount);
    maximumComponentPopulation.size = 0;
    maximumComponentPopulation.resize(connectivityCount);
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        if (!survivors[id].empty()) {
            const Signature signature = connectivityPool[id];
            const size_t first = populationOffsets[id];
            size_t populationIndex = first;
            for (size_t offset = 0; offset < signature.size(); offset += signatureWords) {
                int population = 0;
                for (size_t word = 0; word < signatureWords; ++word) {
                    population += CountBits()(signature[offset + word]);
                }
                componentPopulations[populationIndex++] = population;
            }
            if (componentCounts[id] > 1)
                std::sort(componentPopulations.data + first, componentPopulations.data + populationIndex);
            minimumComponentPopulation[id] = first < populationIndex ? componentPopulations[first] : std::numeric_limits<int>::max();
            maximumComponentPopulation[id] = first < populationIndex ? componentPopulations[populationIndex - 1] : 0;
        }
    }

    // 再做粗签名对细签名的支配。扫描完再删除，仍可用已被支配的状态作为判断依据。
    mss::vector<uint32_t> &coarser = workspace.pruning.surviving.coarser;
    for (uint32_t fineId = 0; fineId < connectivityCount; ++fineId) {
        if (survivors[fineId].empty()) continue;
        coarser.size = 0;
        const Bucket &bucket = buckets[connectivityBucket[fineId]];
        const uint32_t *candidateBegin = bucket.connectivityIds.begin();
        size_t candidateCount = bucket.connectivityIds.size();
        if (bucket.pairOffset != noPosting) {
            for (uint64_t component : connectivityPool[fineId]) {
                for (uint64_t outer = component; outer; outer &= outer - 1) {
                    const int a = std::countr_zero(outer);
                    uint64_t inner = outer & (outer - 1);
                    while (inner) {
                        const int b = std::countr_zero(inner);
                        inner &= inner - 1;
                        const int index = pairIndex[bucket.pairOffset + ((a << 6) | b)];
                        if (index >= 0 && postingSlices[index].count < candidateCount) {
                            const vectorPool<uint32_t>::vector &part = postingSlices[index];
                            candidateBegin = postings.data.data + part.offset;
                            candidateCount = part.count;
                        }
                    }
                }
            }
        }
        for (size_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
            const uint32_t coarseId = candidateBegin[candidateIndex];
            if (coarseId == fineId || survivors[coarseId].empty()) continue;
            if (componentCounts[coarseId] > componentCounts[fineId]) continue;
            if (maximumComponentPopulation[fineId] >
                    maximumComponentPopulation[coarseId] ||
                minimumComponentPopulation[fineId] >
                    minimumComponentPopulation[coarseId]) {
                continue;
            }
            bool populationMatchingPossible = true;
            for (size_t component = 0; component < componentCounts[coarseId];
                 ++component) {
                if (componentPopulations[populationOffsets[fineId] + component] >
                    componentPopulations[populationOffsets[coarseId] + component]) {
                    populationMatchingPossible = false;
                    break;
                }
            }
            if (!populationMatchingPossible) continue;
            if (survivors[fineId].back()->keyBefore(*survivors[coarseId].front())) continue;
            if (minimumQuasi[coarseId] > maximumQuasi[fineId]) continue;

            bool relation;
            if (componentCounts[coarseId] == 1) {
                // 同一桶的唯一粗分量包含所有细分量。
                relation = true;
            } else {
                relation = coarsens(connectivityPool[coarseId],
                                                  connectivityPool[fineId],
                                                  signatureWords);
            }
            if (relation) {
                coarser.push_back(coarseId);
            }
        }
        if (!coarser.size) continue;
        uint32_t *bestCoarse = std::min_element(coarser.data, coarser.data + coarser.size,
            [&](uint32_t a, uint32_t b) {
                if (minimumQuasi[a] != minimumQuasi[b]) return minimumQuasi[a] < minimumQuasi[b];
                return minimumCost[a] < minimumCost[b];
            });
        std::iter_swap(coarser.data, bestCoarse);
        for (CachedItem *cached : survivors[fineId]) {
            bool isDominated = false;
            for (size_t i = 0; i < coarser.size; ++i) {
                const uint32_t coarseId = coarser[i];
                for (CachedItem *prior : survivors[coarseId]) {
                    if (cached->keyBefore(*prior)) break;
                    if (dominates(prior, cached)) {
                        isDominated = true;
                        break;
                    }
                }
                if (isDominated) break;
            }
            cached->crossDominated = isDominated;
        }
    }


    // 最后尝试允许补偿起手代价的反向支配。已被前面两轮支配的目标格跳过。
    mss::vector<CachedItem*> &liveTargets = workspace.pruning.surviving.liveTargets;
    for (uint32_t targetId = 0; targetId < connectivityCount; ++targetId) {
        if (survivors[targetId].empty()) continue;
        liveTargets.size = 0;
        liveTargets.resize(survivors[targetId].count);
        std::copy_n(survivors[targetId].data, liveTargets.size, liveTargets.data);
        int liveMaximumCost;
        int liveMaximumQuasi;
        auto refreshTargets = [&]() {
            liveMaximumCost = std::numeric_limits<int>::min();
            liveMaximumQuasi = std::numeric_limits<int>::min();
            size_t kept = 0;
            for (size_t i = 0; i < liveTargets.size; ++i) {
                CachedItem *target = liveTargets[i];
                if (target->crossDominated) continue;
                liveTargets[kept++] = target;
                liveMaximumCost = std::max(liveMaximumCost, target->item->cost);
                liveMaximumQuasi = std::max(liveMaximumQuasi, target->quasiScore);
            }
            liveTargets.resize(kept);
        };
        // 保留可作为判断依据的状态，按原顺序压紧仍需比较的目标状态。
        refreshTargets();
        if (!liveTargets.size) continue;
        const Bucket &bucket = buckets[connectivityBucket[targetId]];
        for (uint32_t candidateId : bucket.connectivityIds) {
            if (candidateId == targetId || survivors[candidateId].empty()) continue;
            // 正补偿要求用于支配的状态代价更低，避免循环删除。
            int extraSeeds = std::max(1,
                (int)(componentCounts[candidateId]) -
                (int)(componentCounts[targetId]));
            if (minimumCost[candidateId] + extraSeeds > liveMaximumCost ||
                minimumQuasi[candidateId] + extraSeeds > liveMaximumQuasi) {
                continue;
            }
            // 主键高半部分是代价加活跃 3BV 命中数。用 64 位比较，避免补偿的进位影响低半部分。
            const uint64_t minimumPrimary = survivors[candidateId].front()->key >> 32;
            const uint64_t maximumPrimary = liveTargets[liveTargets.size - 1]->key >> 32;
            if (minimumPrimary + (uint64_t)extraSeeds > maximumPrimary)
                continue;
            if (!exactQuotient(connectivityPool[targetId],
                                             connectivityPool[candidateId],
                                             signatureWords)) {
                // 超过剩余目标可负担的补偿上限时，提前停止合并。
                const int maximumPenalty = (int)(std::min<uint64_t>(
                    std::min(liveMaximumCost - minimumCost[candidateId],
                             liveMaximumQuasi - minimumQuasi[candidateId]),
                    maximumPrimary - minimumPrimary));
                extraSeeds = mergePenalty(
                    connectivityPool[candidateId], connectivityPool[targetId],
                    signatureWords, maximumPenalty);
                if (extraSeeds <= 0 ||
                    extraSeeds == std::numeric_limits<int>::max() ||
                    minimumCost[candidateId] + extraSeeds > liveMaximumCost ||
                    minimumQuasi[candidateId] + extraSeeds > liveMaximumQuasi) {
                    continue;
                }
                if (minimumPrimary + (uint64_t)extraSeeds > maximumPrimary)
                    continue;
            }
            const SurvivorList &candidates = survivors[candidateId];
            CachedItem** eligibleEnd = candidates.begin();
            bool removedTarget = false;
            for (size_t i = 0; i < liveTargets.size; ++i) {
                CachedItem *target = liveTargets[i];
                const uint64_t targetPrimary = target->key >> 32;
                // 按目标主键顺序推进可负担的比较前缀，避免在内层重复检查。
                while (eligibleEnd != candidates.end() &&
                       ((*eligibleEnd)->key >> 32) +
                           (uint64_t)extraSeeds <= targetPrimary) {
                    ++eligibleEnd;
                }
                for (CachedItem** candidateIt = candidates.begin(); candidateIt != eligibleEnd;
                     ++candidateIt) {
                    CachedItem *candidate = *candidateIt;
                    if (candidate->item->cost + extraSeeds >
                        target->item->cost) continue;
                    if (candidate->quasiScore + extraSeeds > target->quasiScore)
                        continue;
                    const int allowance = target->item->cost -
                        candidate->item->cost - extraSeeds;
                    const int lowerBound =
                        std::max(0, target->flagHits - candidate->flagHits) +
                        std::max(0, candidate->baseHits - target->baseHits);
                    if (lowerBound <= allowance && penaltyAtMost(
                            candidate->item->state.hits, target->item->state.hits,
                            baseMask, allowance)) {
                        target->crossDominated = true;
                        removedTarget = true;
                        break;
                    }
                }
            }
            if (removedTarget) {
                refreshTargets();
                if (!liveTargets.size) break;
            }
        }
    }

    for (size_t bucketId = 0; bucketId < buckets.size; ++bucketId) {
        Bucket &bucket = buckets[bucketId];
        for (CachedItem &cached : bucket.entries) {
            if (!cached.exactDominated && !cached.crossDominated) continue;
            table.erase(cached.item);
        }
    }
}

} // 命名空间 mss::ioe_dom
