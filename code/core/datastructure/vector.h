#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace mss {

// 极简动态数组：只分配、扩容和记录长度，不构造也不析构元素。
// 危险：resize 增大的区域不初始化，读取前必须写入；元素类型必须可平凡复制。
// 元素可平凡复制在使用处断言，允许 T 内含 vectorPool<T>::vector 的自引用切片。
template <typename T> struct vector {
    T *data;
    std::size_t size;
    std::size_t capacity;

    vector() : data(nullptr), size(0), capacity(0) {}
    // 深拷贝 other 的内容；容量够用时不重新分配。
    vector(const vector &other) : data(nullptr), size(0), capacity(0) { *this = other; }
    vector &operator=(const vector &other) {
        if (this == &other) return *this;
        resize(other.size);
        if (other.size > 0) std::memcpy(data, other.data, sizeof(T) * other.size);
        return *this;
    }
    ~vector() { ::operator delete(data); }

    T &operator[](std::size_t index) { return data[index]; }
    const T &operator[](std::size_t index) const { return data[index]; }
    std::uint64_t allocatedBytes() const { return std::uint64_t{capacity} * sizeof(T); }

    std::uint64_t release() {
        const std::uint64_t bytes = allocatedBytes();
        ::operator delete(data);
        data = nullptr;
        size = capacity = 0;
        return bytes;
    }

    // 交换存储所有权，不分配或复制元素。
    void swap(vector &other) {
        std::swap(data, other.data);
        std::swap(size, other.size);
        std::swap(capacity, other.capacity);
    }

    // 容量不足时至少按两倍扩容并保留已有元素；缩小只改长度，不释放存储。
    void resize(std::size_t count) {
        static_assert(std::is_trivially_copyable_v<T>, "vector 的元素类型必须可平凡复制");
        if (count > capacity) reallocate(resizeCapacity(count));
        size = count;
    }

    // 额度须包含扩容期间同时存在的旧、新缓冲；成功后只保留新缓冲的开销。
    bool resize(std::size_t count, std::uint64_t &memoryLeft) {
        static_assert(std::is_trivially_copyable_v<T>, "vector 的元素类型必须可平凡复制");
        if (count > capacity && !reallocate(resizeCapacity(count), memoryLeft)) return false;
        size = count;
        return true;
    }

    // 按值传参：调用方传入本容器元素时，扩容释放旧存储前已完成拷贝。
    void push_back(T value) {
        static_assert(std::is_trivially_copyable_v<T>, "vector 的元素类型必须可平凡复制");
        if (size == capacity) reallocate(appendCapacity());
        data[size++] = value;
    }

    bool push_back(T value, std::uint64_t &memoryLeft) {
        static_assert(std::is_trivially_copyable_v<T>, "vector 的元素类型必须可平凡复制");
        if (size == capacity && !reallocate(appendCapacity(), memoryLeft)) return false;
        data[size++] = value;
        return true;
    }

  private:
    std::size_t resizeCapacity(std::size_t count) const { return count > capacity * 2 ? count : capacity * 2; }
    std::size_t appendCapacity() const { return capacity == 0 ? 4 : capacity * 2; }

    bool reallocate(std::size_t newCapacity, std::uint64_t &memoryLeft) {
        const std::uint64_t bytes = std::uint64_t{newCapacity} * sizeof(T);
        if (bytes > memoryLeft) return false;
        memoryLeft -= bytes - allocatedBytes();
        reallocate(newCapacity);
        return true;
    }

    void reallocate(std::size_t newCapacity) {
        T *next = (T *)::operator new(sizeof(T) * newCapacity);
        if (size > 0) std::memcpy(next, data, sizeof(T) * size);
        ::operator delete(data);
        data = next;
        capacity = newCapacity;
    }
};

} // 命名空间 mss
