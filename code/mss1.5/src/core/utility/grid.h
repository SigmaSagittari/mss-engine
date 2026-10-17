#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mss {

// RawGrid 按行连续存储；坐标从 0 开始，不带填充行列。
template <typename T> class RawGrid {
  public:
    // 默认构造得到 0×0 空网格。
    RawGrid() : rows_(0), cols_(0) {}

    // 尺寸和填充值的含义与 resize 相同。
    RawGrid(int rows, int cols, const T &value) : rows_(rows), cols_(cols), data_((std::size_t)rows * cols, value) {}

    // rows/cols 必须非负；调整后旧内容全部丢弃，新区域统一填入 value。
    void resize(int rows, int cols, const T &value) {
        rows_ = rows;
        cols_ = cols;
        data_.assign((std::size_t)rows * cols, value);
    }

    // 不改变尺寸，只覆盖现有元素。
    void fill(const T &value) { std::fill(data_.begin(), data_.end(), value); }

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    // row 必须在 [0, rows()) 内；返回该行连续存储区，后续列下标必须在 [0, cols()) 内，本类不检查边界。
    T *operator[](int row) { return data_.data() + (std::size_t)row * cols_; }
    const T *operator[](int row) const { return data_.data() + (std::size_t)row * cols_; }

  private:
    int rows_;
    int cols_;
    std::vector<T> data_;
};

} // namespace mss
