#include "algo/ioe_engine/ref_dom/dominance.h"

#include <numeric>

#include "core/datastructure/flat_hashtable.h"

namespace mss::ioe::ref_dom {

struct Dominance::Workspace {
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

    struct UnionHash {
        size_t operator()(Mask reach) const {
            const size_t low = FrontierWord().combine(0, reach.low);
            return reach.high ? FrontierWord().combine(low, reach.high) : low;
        }
    };
    struct PendingPosting {
        int slice;
        uint32_t id;
    };

    // 危险：复用匹配和分量比较的缓冲。仅当前签名占用的前缀有效，各入口先写入再使用。
    struct {
        int fineMatch[cells];
        uint64_t eligibleWord[64];
        Mask eligibleWide[cells];
        uint64_t recovered[2 * cells];
        int parent[cells];
    } components;
    struct {
        FlatHashTable<Mask, size_t, UnionHash> bucketByUnion;
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
};

__declspec(noinline) Dominance::Workspace &Dominance::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

// 比较未覆盖的编号格和已支付的旗，求两种因子状态的最大额外代价。
bool Dominance::penaltyAtMost(uint64_t candidate, uint64_t other,
                                      uint64_t baseMask, int allowance) {
    const uint64_t candidateGood = candidate ^ baseMask;
    const uint64_t otherGood = other ^ baseMask;
    return FrontierWord().populationCount(otherGood & ~candidateGood) <= allowance;
}

bool Dominance::matchWord(const uint64_t *eligible, int *match, int candidate, uint64_t &seen) {
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

bool Dominance::matchWide(const Mask *eligible, int *match, int candidate, Mask &seen) {
    for (int word = 0; word < 2; ++word) {
        const uint64_t allowed = word ? eligible[candidate].high : eligible[candidate].low;
        uint64_t &visited = word ? seen.high : seen.low;
        for (uint64_t choices = allowed & ~visited; choices; choices &= ~visited) {
            const int bit = std::countr_zero(choices);
            const int fine = 64 * word + bit;
            visited |= uint64_t{1} << bit;
            if (match[fine] < 0 || matchWide(eligible, match, match[fine], seen)) {
                match[fine] = candidate;
                return true;
            }
        }
    }
    return false;
}

uint64_t Dominance::primaryKey(int cost, int baseHits) {
    // 将有符号代价偏移后打包，保持原字典序。
    return (uint64_t{(uint32_t)(cost + baseHits) ^ UINT32_C(0x80000000)} << 32) |
           ((uint32_t)cost ^ UINT32_C(0x80000000));
}

// 每个细分量的可达集合都须被某个粗分量包含。每个粗分量须匹配不同的细分量，避免多付一次起手点击。
template <size_t FixedWords>
bool Dominance::coarsensImpl(SignatureView coarse,
                                  SignatureView fine,
                                  size_t runtimeWords) {
    const size_t signatureWords = FixedWords ? FixedWords : runtimeWords;
    const size_t coarseCount = coarse.size() / signatureWords;
    const size_t fineCount = fine.size() / signatureWords;
    if (coarseCount > fineCount) return false;
    if (coarseCount == 0) return fineCount == 0;

    Workspace &workspace = getWorkspace();
    int (&fineMatch)[cells] = workspace.components.fineMatch;
    // 至多 64 个细分量时，用一个字保存每个粗分量的匹配候选。
    constexpr size_t kInlineComponents = 64;
    if (coarseCount <= kInlineComponents && fineCount <= kInlineComponents) {
        uint64_t (&eligible)[kInlineComponents] = workspace.components.eligibleWord;
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

    // 每个粗分量用双字掩码保存可匹配的细分量。
    Mask (&eligible)[cells] = workspace.components.eligibleWide;
    std::fill_n(eligible, coarseCount, Mask{0, 0});
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
        Mask seen{};
        if (!matchWide(eligible, fineMatch, (int)c, seen)) return false;
    }
    return true;
}

// 单字签名走固定宽度版本，省去按字循环和偏移计算。
bool Dominance::coarsens(SignatureView coarse,
                                  SignatureView fine,
                                  size_t signatureWords) {
    if (signatureWords == 1) return coarsensImpl<1>(coarse, fine, 1);
    return coarsensImpl<0>(coarse, fine, signatureWords);
}

// 反向支配要求每个细分量有唯一归属，且并集恰好还原粗分量。仅满足包含关系时，可能额外合并将来的分量。
bool Dominance::exactQuotient(SignatureView coarse,
                                        SignatureView fine,
                                        size_t signatureWords) {
    const size_t coarseCount = coarse.size() / signatureWords;
    const size_t fineCount = fine.size() / signatureWords;
    if (coarseCount == 0 || coarseCount >= fineCount) return false;
    // 同一桶的总可达集合相同。
    if (coarseCount == 1) return true;
    Workspace &workspace = getWorkspace();
    uint64_t (&recovered)[2 * cells] = workspace.components.recovered;
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
int Dominance::mergePenaltyImpl(SignatureView a,
                                      SignatureView b,
                                      size_t runtimeWords, int maximumPenalty) {
    const size_t words = FixedWords ? FixedWords : runtimeWords;
    const int na = (int)(a.size() / words);
    const int nb = (int)(b.size() / words);
    if (na + nb > 128) return std::numeric_limits<int>::max();
    // 把同一 A 分量触及的 B 分量用并查集合起来。
    Workspace &workspace = getWorkspace();
    int (&parent)[cells] = workspace.components.parent;
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

int Dominance::mergePenalty(SignatureView a,
                                      SignatureView b,
                                      size_t signatureWords,
                                      int maximumPenalty) {
    if (signatureWords == 1)
        return mergePenaltyImpl<1>(a, b, 1, maximumPenalty);
    return mergePenaltyImpl<0>(a, b, signatureWords, maximumPenalty);
}

void Dominance::prune(FactorTable &table, uint64_t baseMask,
                              const ConnectivityPool &connectivityPool,
                              size_t signatureWords) {
    if (table.size() < 2) return;
    using CachedItem = Workspace::CachedItem;
    using Bucket = Workspace::Bucket;
    using SurvivorList = Workspace::SurvivorList;
    Workspace &workspace = getWorkspace();
    constexpr size_t noPosting = std::numeric_limits<size_t>::max();
    const size_t connectivityCount = connectivityPool.size();
    mss::vector<size_t> &stateCounts = workspace.partition.stateCounts;
    stateCounts.size = 0;
    stateCounts.resize(connectivityCount);
    std::fill_n(stateCounts.data, connectivityCount, 0);
    for (FactorTable::iterator item = table.begin(); item != table.end(); ++item)
        ++stateCounts[item->state.connectivityId];

    // 以总可达集合分桶，同一桶的局部排序供三轮支配剪枝复用。
    FlatHashTable<Mask, size_t, Workspace::UnionHash> &lookup = workspace.partition.bucketByUnion;
    lookup.clear();
    lookup.reserve(connectivityCount);
    mss::vector<Bucket> &buckets = workspace.partition.buckets;
    buckets.size = 0;
    mss::vector<size_t> &connectivityBucket = workspace.partition.connectivityBucket;
    connectivityBucket.size = 0;
    connectivityBucket.resize(connectivityCount);
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        if (!stateCounts[id]) continue;
        const SignatureView signature = connectivityPool[id];
        Mask reach{};
        for (size_t i = 0; i < signature.size(); i += signatureWords) {
            reach.low |= signature[i];
            if (signatureWords == 2) reach.high |= signature[i + 1];
        }
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

    mss::vector<uint32_t> &bucketIdStorage = workspace.partition.bucketIdStorage;
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
        Workspace::IdList &ids = buckets[connectivityBucket[id]].connectivityIds;
        ids.data[ids.count++] = id;
    }
    mss::vector<int> &pairIndex = workspace.positions.pairIndex;
    mss::vector<uint32_t> &lastId = workspace.positions.lastId;
    vectorPool<uint32_t> &postings = workspace.positions.postings;
    mss::vector<vectorPool<uint32_t>::vector> &postingSlices = workspace.positions.postingSlices;
    mss::vector<Workspace::PendingPosting> &pending = workspace.positions.pending;
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
                const Workspace::PendingPosting &item = pending[i];
                vectorPool<uint32_t>::vector &part = postingSlices[item.slice];
                postings.data[part.offset + part.count++] = item.id;
            }
        }
    }

    mss::vector<CachedItem> &cachedStorage = workspace.partition.cachedStorage;
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
        const FrontierWord word;
        const int baseHits = word.populationCount(item->state.hits & baseMask);
        const int flagHits = word.populationCount(item->state.hits) - baseHits;
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
    mss::vector<CachedItem*> &survivorStorage = workspace.surviving.survivorStorage;
    survivorStorage.size = 0;
    survivorStorage.resize(table.size());
    mss::vector<SurvivorList> &survivors = workspace.surviving.survivors;
    survivors.size = 0;
    survivors.resize(connectivityCount);
    mss::vector<int> &minimumQuasi = workspace.bounds.minimumQuasi;
    mss::vector<int> &maximumQuasi = workspace.bounds.maximumQuasi;
    mss::vector<int> &minimumCost = workspace.bounds.minimumCost;
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

    mss::vector<size_t> &componentCounts = workspace.bounds.componentCounts;
    componentCounts.size = 0;
    componentCounts.resize(connectivityCount);
    // 按签名给分量的位数表分配连续区间。
    mss::vector<size_t> &populationOffsets = workspace.bounds.populationOffsets;
    populationOffsets.size = 0;
    populationOffsets.resize(connectivityCount + 1);
    populationOffsets[0] = 0;
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        componentCounts[id] = survivors[id].empty() ? 0 : connectivityPool[id].size() / signatureWords;
        populationOffsets[id + 1] = populationOffsets[id] + componentCounts[id];
    }
    mss::vector<int> &componentPopulations = workspace.bounds.componentPopulations;
    componentPopulations.size = 0;
    componentPopulations.resize(populationOffsets[connectivityCount]);
    mss::vector<int> &minimumComponentPopulation = workspace.bounds.minimumComponentPopulation;
    mss::vector<int> &maximumComponentPopulation = workspace.bounds.maximumComponentPopulation;
    minimumComponentPopulation.size = 0;
    minimumComponentPopulation.resize(connectivityCount);
    maximumComponentPopulation.size = 0;
    maximumComponentPopulation.resize(connectivityCount);
    for (uint32_t id = 0; id < connectivityCount; ++id) {
        if (!survivors[id].empty()) {
            const SignatureView signature = connectivityPool[id];
            const size_t first = populationOffsets[id];
            size_t populationIndex = first;
            for (size_t offset = 0; offset < signature.size(); offset += signatureWords) {
                int population = 0;
                for (size_t word = 0; word < signatureWords; ++word) {
                    population += FrontierWord().populationCount(signature[offset + word]);
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
    mss::vector<uint32_t> &coarser = workspace.surviving.coarser;
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
    mss::vector<CachedItem*> &liveTargets = workspace.surviving.liveTargets;
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

} // 命名空间 mss::ioe::ref_dom
