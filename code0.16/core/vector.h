#pragma once

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace mss {

// 极简动态数组：只分配、扩容和记录长度，不构造也不析构元素。
// 危险：resize 增大的区域不初始化，读取前必须写入；元素类型必须可平凡复制。
template <typename T> struct Vector {
    static_assert(std::is_trivially_copyable_v<T>, "Vector 的元素类型必须可平凡复制");

    T *data;
    std::size_t size;
    std::size_t capacity;

    Vector() : data(nullptr), size(0), capacity(0) {}
    // 禁止复制：只持有裸指针，没有实现深拷贝。
    Vector(const Vector &) = delete;
    Vector &operator=(const Vector &) = delete;
    ~Vector() { ::operator delete(data); }

    // 容量不足时至少按两倍扩容并保留已有元素；缩小只改长度，不释放存储。
    void resize(std::size_t count) {
        if (count > capacity) reallocate(count > capacity * 2 ? count : capacity * 2);
        size = count;
    }

    void push_back(const T &value) {
        if (size < capacity) {
            data[size++] = value;
            return;
        }
        // 先按字节暂存：value 引用本容器元素时，扩容释放旧存储后仍然有效。
        alignas(T) unsigned char buffer[sizeof(T)];
        std::memcpy(buffer, &value, sizeof(T));
        reallocate(capacity == 0 ? 4 : capacity * 2);
        std::memcpy(data + size, buffer, sizeof(T));
        ++size;
    }

  private:
    void reallocate(std::size_t newCapacity) {
        T *next = (T *)::operator new(sizeof(T) * newCapacity);
        if (size > 0) std::memcpy(next, data, sizeof(T) * size);
        ::operator delete(data);
        data = next;
        capacity = newCapacity;
    }
};

} // 命名空间 mss
