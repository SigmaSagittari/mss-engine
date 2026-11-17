#pragma once

#include <cstdint>

#include "core/datastructure/span.h"
#include "core/datastructure/vector_pool.h"

namespace mss::ioe::ref_dom {

constexpr int side = 9;
constexpr int cells = side * side;
constexpr int mines = 10;

struct Mask {
    uint64_t low;
    uint64_t high;

    bool has(int cell) const {
        return cell < 64 ? (low >> cell) & 1 : (high >> (cell - 64)) & 1;
    }
    void set(int cell) {
        if (cell < 64) low |= uint64_t{1} << cell;
        else high |= uint64_t{1} << (cell - 64);
    }
    void clear(int cell) {
        if (cell < 64) low &= ~(uint64_t{1} << cell);
        else high &= ~(uint64_t{1} << (cell - 64));
    }
    bool operator==(const Mask&) const = default;
};

struct BaseDescription {
    enum class Kind { Zero, Single } kind;
    int representative;
};

// 每张摆法持有自己的区间和元素，供后续观察状态复用。
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

struct Model {
    uint8_t mines[cells];
    int8_t numbers[cells];
    int candidate_index[cells];
    vector<int> candidates;
    vector<int> mine_cells;
    vector<BaseDescription> base_descriptions;
    Scopes zeros;
    Scopes graph;
    Scopes zero_scopes;
    Scopes mine_scopes;
    Scopes base_scopes;

    int three_bv() const { return base_scopes.size(); }
};

struct Neighbors {
    struct Row {
        int count;
        int items[8];
    };

    Row rows[cells];
    span<const int> operator[](int cell) const { return {rows[cell].items, rows[cell].count}; }
};

struct RefDom {
    Neighbors makeNeighbors();
    void buildModel(Mask layout, const Neighbors &adjacent, Model &model);
    bool partialOpening(const Model &model, Mask shown, const Neighbors &adjacent);
    int remainingClicks(const Model &model, Mask shown, Mask playerFlags, const Neighbors &adjacent);

private:
    struct Builder;
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss::ioe::ref_dom
