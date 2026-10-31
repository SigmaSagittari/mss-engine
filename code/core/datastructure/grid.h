#pragma once

#include <cstddef>

#include "core/datastructure/vector.h"

namespace mss {

// 行优先连续存储的二维网格；坐标从 0 开始，不带填充行列。
template <typename T> class Grid {
  public:
    // 只读定宽视图：Cols 必须等于网格列数；借用网格存储，网格 resize 或销毁后失效。
    template <int Cols> class FixedView {
      public:
        FixedView(const Grid &grid) : data_(grid.cells_.data) {}
        const T *operator[](std::size_t row) const { return data_ + row * Cols; }

      private:
        const T *data_;
    };

    Grid() : rows_(0), cols_(0) {}

    // rows/cols 必须非负；既有多余内容丢弃，全部元素填入 value。
    Grid(int rows, int cols, const T &value) : rows_(0), cols_(0) { resize(rows, cols, value); }

    void resize(int rows, int cols, const T &value) {
        rows_ = rows;
        cols_ = cols;
        cells_.resize((std::size_t)rows * cols);
        fill(value);
    }

    // 不改变尺寸，只覆盖现有元素。
    void fill(const T &value) {
        for (std::size_t i = 0; i < cells_.size; ++i) cells_.data[i] = value;
    }

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    void swap(Grid &other) {
        cells_.swap(other.cells_);
        std::swap(rows_, other.rows_);
        std::swap(cols_, other.cols_);
    }

    // row 必须在 [0, rows()) 内；返回该行连续存储区，本类不检查列越界。
    T *operator[](int row) { return cells_.data + (std::size_t)row * cols_; }
    const T *operator[](int row) const { return cells_.data + (std::size_t)row * cols_; }

  private:
    int rows_;
    int cols_;
    vector<T> cells_;
};

} // 命名空间 mss
