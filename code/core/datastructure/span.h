#pragma once

namespace mss {

// 非拥有视图：指向一段连续元素，不负责生命周期。
template <typename T> struct span {
    T *data;
    int size;

    span() : data(nullptr), size(0) {}
    span(T *data, int size) : data(data), size(size) {}

    // index 必须在 [0, size) 内。
    T &operator[](int index) const { return data[index]; }
    // offset 和 count 必须落在当前区间内。
    span<T> subspan(int offset, int count) const { return {data + offset, count}; }

    T *begin() const { return data; }
    T *end() const { return data + size; }
};

} // 命名空间 mss
