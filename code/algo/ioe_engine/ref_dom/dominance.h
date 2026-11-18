#pragma once

#include "algo/ioe_engine/ref_dom/frontier.h"
#include "core/datastructure/bit_mask.h"

namespace mss::ioe::ref_dom {

struct Dominance {
    bool coarsens(SignatureView coarse, SignatureView fine, size_t signatureWords);
    bool exactQuotient(SignatureView coarse, SignatureView fine, size_t signatureWords);
    void prune(FactorTable &table, uint64_t baseMask, const ConnectivityPool &pool, size_t signatureWords);

private:
    struct Workspace;
    Workspace &getWorkspace();
    template <size_t FixedWords> bool coarsensImpl(SignatureView coarse, SignatureView fine, size_t runtimeWords);
    template <size_t FixedWords> int mergePenaltyImpl(SignatureView a, SignatureView b, size_t runtimeWords, int limit);
    int mergePenalty(SignatureView a, SignatureView b, size_t signatureWords, int limit);
    bool penaltyAtMost(uint64_t candidate, uint64_t other, uint64_t baseMask, int allowance);
    bool matchWord(const uint64_t *eligible, int *match, int candidate, uint64_t &seen);
    bool matchWide(const bitMask<signatureCapacity> *eligible, int *match, int candidate, bitMask<signatureCapacity> &seen);
    uint64_t primaryKey(int cost, int baseHits);
};

} // 命名空间 mss::ioe::ref_dom
