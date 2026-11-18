#include "algo/ioe_engine/ref_dom/frontier.h"

#include <numeric>

#include "algo/ioe_engine/ref_dom/dominance.h"
#include "core/assert.h"

namespace mss::ioe::ref_dom {

uint32_t ConnectivityPool::intern(SignatureView signature) {
    size_t hash = signature.size();
    for (uint64_t word : signature) hash = FrontierWord().combine(hash, word);
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

struct Frontier::FactorPlan {
    struct Factor {
        span<const int> scope;
        int slot;
        bool flag;
    };

    Factor factors[maxCells];
    int count;
    int slotCount;
};

struct Frontier::Workspace {
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
    FactorTable table[2];
    ConnectivityPool connectivity[2];
    mss::vector<Connection> transitions;
};

__declspec(noinline) Frontier::Workspace &Frontier::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

void Frontier::makeFactorPlan(const ModelGraph &model, FactorPlan &plan) {
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

int Frontier::solve(const ModelGraph &model) {
    Workspace &workspace = getWorkspace();
    FactorPlan &factors = workspace.factors.plan;
    makeFactorPlan(model, factors);
    Dominance dominance;
    const int q = (int)model.candidates.size;
    // 活跃因子来自当前线及相邻两线，每线至多 16 格。
    assert_(factors.slotCount <= 64, "DOM 活跃因子超过一个字");
    uint64_t (&flagMember)[maxCells] = workspace.factors.flagMember;
    uint64_t (&baseMember)[maxCells] = workspace.factors.baseMember;
    uint64_t (&activeAfter)[maxCells] = workspace.factors.activeAfter;
    uint64_t (&baseAfter)[maxCells] = workspace.factors.baseAfter;
    std::fill_n(flagMember, q, 0);
    std::fill_n(baseMember, q, 0);
    std::fill_n(activeAfter, q, 0);
    std::fill_n(baseAfter, q, 0);
    for (int f = 0; f < factors.count; ++f) {
        const FactorPlan::Factor &factor = factors.factors[f];
        const uint64_t bit = uint64_t{1} << factor.slot;
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

    FactorTable (&tables)[2] = workspace.table;
    ConnectivityPool (&pools)[2] = workspace.connectivity;
    mss::vector<Workspace::Connection> &transitions = workspace.transitions;
    tables[0].reset(16);
    tables[0].add({0, 0}, FrontierWord().combine(FrontierWord().combine(0, 0), 0), model.three_bv());
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
                const SignatureView previous = pool[old.connectivityId];
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
                    cached.branch[selected] = {id, closed, FrontierWord().combine(0, id)};
                }

                int (&penalty)[2] = cached.penalty;
                if (cached.branch[0].connectivityId == cached.branch[1].connectivityId) {
                    penalty[0] = penalty[1] = 0;
                } else {
                    penalty[0] = penalty[1] = noBound;
                    const SignatureView skip = nextPool[cached.branch[0].connectivityId];
                    const SignatureView take = nextPool[cached.branch[1].connectivityId];
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
            const int factorCost = FrontierWord().populationCount(newFlags) - FrontierWord().populationCount(newBases);
            const ConnectivityTransition (&branch)[2] = cached.branch;
            const int (&penalty)[2] = cached.penalty;
            // 只补偿下一步仍能改变的因子。同分时优先跳过，至少保留一种转移。
            const int delta = 1 + branch[1].closed - branch[0].closed + factorCost;
            int first = 0, last = 1;
            if (penalty[0] != noBound && delta >= penalty[0] &&
                delta >= penalty[0] + FrontierWord().populationCount(newFlags & activeAfter[i])) last = 0;
            else if (penalty[1] != noBound && -delta >= penalty[1] &&
                -delta >= penalty[1] + FrontierWord().populationCount(newBases & activeAfter[i])) first = 1;
            for (int selected = first; selected <= last; ++selected) {
                const uint64_t hits = (old.hits | (selected ? factorMember : 0)) & activeAfter[i];
                // 起始 3BV 已计过每个零区，之后由关闭分量的操作付出点击数。
                const int cost = item.cost + selected + branch[selected].closed
                    + (selected ? factorCost : 0) - openingCount[i];
                next.add({branch[selected].connectivityId, hits}, FrontierWord().combine(branch[selected].hashSeed, hits), cost);
            }
        }

        // 每处理两个候选做一次完整支配剪枝，末轮也做。
        if (i % 2 == 1 || i + 1 == q) dominance.prune(next, baseAfter[i], nextPool, signatureWords);
    }

    FactorTable &final = tables[q & 1];
    FactorTable::iterator found = final.begin();
    while (found != final.end() && (found->state.connectivityId != 0 || found->state.hits != 0)) ++found;
    assert_(found != final.end(), "DOM 前沿动态规划缺少终止状态");
    return found->cost;
}

} // 命名空间 mss::ioe::ref_dom
