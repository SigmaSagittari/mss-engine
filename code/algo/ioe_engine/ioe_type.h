#pragma once

#include <cstdint>
#include <type_traits>

#include "core/datastructure/bit_mask.h"

namespace mss {

using IoeCells = bitMask<2>;

struct IoeBoard {
    static constexpr int side = 9;
    static constexpr int cells = side * side;
    static constexpr int mines = 10;

    const IoeCells *neighbors();
};

struct IoeView {
    IoeCells revealed;
    IoeCells flags;
};

struct IoeMove {
    enum class Kind : std::uint8_t { None, Open, Unflag, Chord };

    Kind kind;
    int cell; // 空招时无格号。
    IoeCells addFlags;
    IoeCells removeFlags;

    IoeMove() : kind(Kind::None), cell(-1) {}
    IoeMove(Kind kind, int cell, IoeCells addFlags = {}, IoeCells removeFlags = {})
        : kind(kind), cell(cell), addFlags(addFlags), removeFlags(removeFlags) {}
    bool operator==(const IoeMove &) const = default;
};

struct IoeLayout {
    IoeCells mines;
    std::uint8_t threeBV;
    std::uint8_t remaining;
};

// 同步调用借用回调函数；函数对象须活到本次调用返回。
template <typename T> struct IoeObserver {
    IoeObserver() : callback(nullptr), context(nullptr) {}
    template <typename F> requires (!std::is_same_v<F, IoeObserver>)
    IoeObserver(F &function) : callback([](void *self, const T &value) { (*(F *)self)(value); }), context(&function) {}
    IoeObserver(const IoeObserver &) = default;
    void operator()(const T &value) const { if (callback) callback(context, value); }

  private:
    void (*callback)(void *, const T &);
    void *context;
};

} // 命名空间 mss
