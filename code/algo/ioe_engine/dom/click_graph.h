#pragma once

#include "core/datastructure/span.h"
#include "core/datastructure/vector_pool.h"

namespace mss::ioe_dom {

constexpr int maxCells = 16 * 30;

// operator[] 借用 values 中的区间，扩容或清空后失效。
struct Scopes {
    vectorPool<int> values;
    vector<vectorPool<int>::vector> parts;

    int size() const { return (int)parts.size; }
    span<const int> operator[](int index) const {
        const vectorPool<int>::vector& part = parts[index];
        return { part.count ? values.data.data + part.offset : nullptr, (int)part.count };
    }
    void clear();
    void append(span<const int> items);
};

struct ClickGraph {
    vector<int> candidates;
    Scopes graph;
    Scopes zero_scopes;
    Scopes mine_scopes;
    Scopes base_scopes;

    int three_bv() const { return base_scopes.size(); }
};

} // 命名空间 mss::ioe_dom
