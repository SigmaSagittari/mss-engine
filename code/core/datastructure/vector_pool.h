#pragma once

#include <cstddef>

#include "core/datastructure/vector.h"

namespace mss {

// 池：vector<T> 负责存储，池按尾部切片分配；切片只记下标，池扩容后仍然有效。
// 只能在尾部增删切片，否则会破坏后续切片的存储区。
template <typename T> struct vectorPool {
    // 池内切片，必须配合同一个池读取。
    struct vector {
        std::size_t offset;
        std::size_t count;
    };

    // 在池尾开一个空切片。
    vector push_back() { return {data.size, 0}; }

    // 向尾部切片追加一个元素。
    void push_back(vector &part, T value) {
        data.push_back(value);
        ++part.count;
    }

    // 丢弃尾部切片及其后的内容。
    void pop_back(const vector &part) { data.size = part.offset; }

    mss::vector<T> data;
};

} // 命名空间 mss
