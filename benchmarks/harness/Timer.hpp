// SPDX-License-Identifier: MIT
// .JADE Benchmark Suite — harness/Timer.hpp
//
// High-precision timer using std::chrono + rdtsc fallback.
// Measures wall-clock nanoseconds.

#pragma once

#include <chrono>
#include <cstdint>

namespace bench {

class Timer {
public:
    void start() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        end_ = std::chrono::high_resolution_clock::now();
    }

    [[nodiscard]] double nanoseconds() const {
        return std::chrono::duration<double, std::nano>(end_ - start_).count();
    }

    [[nodiscard]] double microseconds() const {
        return nanoseconds() / 1000.0;
    }

    [[nodiscard]] double milliseconds() const {
        return nanoseconds() / 1e6;
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
    std::chrono::high_resolution_clock::time_point end_;
};

// Run a function `trials` times, returning per-trial nanoseconds.
template<typename Func>
std::vector<double> measure(Func fn, int trials, int warmup = 3) {
    // Warmup.
    for (int i = 0; i < warmup; ++i) {
        volatile auto sink = fn();
        (void)sink;
    }

    std::vector<double> results;
    results.reserve(trials);
    for (int t = 0; t < trials; ++t) {
        Timer timer;
        timer.start();
        volatile auto sink = fn();
        timer.stop();
        (void)sink;
        results.push_back(timer.nanoseconds());
    }
    return results;
}

// Run a loop-based function N times, measuring total time, return ns/iter.
template<typename Func>
std::vector<double> measure_loop(Func fn, int64_t n, int trials, int warmup = 2) {
    // Warmup.
    for (int i = 0; i < warmup; ++i) {
        volatile auto sink = fn(n);
        (void)sink;
    }

    std::vector<double> results;
    results.reserve(trials);
    for (int t = 0; t < trials; ++t) {
        Timer timer;
        timer.start();
        volatile auto sink = fn(n);
        timer.stop();
        (void)sink;
        results.push_back(timer.nanoseconds() / static_cast<double>(n));
    }
    return results;
}

}  // namespace bench
