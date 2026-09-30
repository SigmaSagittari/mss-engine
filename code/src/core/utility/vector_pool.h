#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace mss {

template <typename T> struct vectorPool {
    struct vector {
        int idx = 0;
        int size = 0;

        std::span<T> span(vectorPool &pool) const {
            return std::span<T>(pool._data.data() + idx, size);
        }

        std::span<const T> span(const vectorPool &pool) const {
            return std::span<const T>(pool._data.data() + idx, size);
        }

        // 只能给 Pool 尾部的 vector 追加，否则会破坏后续 vector 的存储区。
        void push_back(vectorPool &pool, const T &value) {
            pool._data.push_back(value);
            ++size;
        }
    };

    void clear() {
        _data.clear();
    }

    vector push_back() {
        return {(int)(_data.size()), 0};
    }

    void pop_back(const vector &value) {
        _data.resize(value.idx);
    }

  private:
    std::vector<T> _data;
};

} // 命名空间 mss
