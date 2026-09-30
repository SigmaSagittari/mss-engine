#pragma once

#include <chrono>

namespace test {

struct TimeBox {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point deadline;

    explicit TimeBox(double seconds)
        : start(std::chrono::steady_clock::now()), deadline(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds))) {
    }

    // 判断计时期限是否已到。
    bool expired() const {
        return std::chrono::steady_clock::now() >= deadline;
    }

    // 返回计时盒启动至今经过的秒数。
    double elapsedSeconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
};

}
