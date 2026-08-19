// SPDX-License-Identifier: MIT
// .JADE Benchmark Suite — harness/Stats.hpp
//
// Statistical utilities: median, MAD (median absolute deviation),
// outlier rejection, confidence intervals.
//
// Per the benchmark doctrine: 5 trials, report median + MAD,
// discard trials >3 MAD from median.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace bench {

struct Stats {
    double median_ns;
    double mad_ns;          // median absolute deviation
    double min_ns;
    double max_ns;
    int    trials;
    int    outliers_rejected;
    bool   significant;     // true if MAD/median < 0.05 (stable)
};

inline Stats compute_stats(std::vector<double> samples) {
    Stats s{};
    s.trials = static_cast<int>(samples.size());

    if (samples.empty()) {
        s.median_ns = 0;
        return s;
    }

    // Sort for median.
    std::sort(samples.begin(), samples.end());

    // Median.
    size_t n = samples.size();
    s.median_ns = (n % 2 == 0)
        ? (samples[n/2 - 1] + samples[n/2]) / 2.0
        : samples[n/2];

    // MAD: median of |xi - median|.
    std::vector<double> abs_devs;
    abs_devs.reserve(n);
    for (double v : samples) {
        abs_devs.push_back(std::abs(v - s.median_ns));
    }
    std::sort(abs_devs.begin(), abs_devs.end());
    s.mad_ns = (n % 2 == 0)
        ? (abs_devs[n/2 - 1] + abs_devs[n/2]) / 2.0
        : abs_devs[n/2];

    // Outlier rejection: discard samples > 3*MAD from median.
    double threshold = 3.0 * s.mad_ns;
    std::vector<double> filtered;
    for (double v : samples) {
        if (std::abs(v - s.median_ns) <= threshold) {
            filtered.push_back(v);
        }
    }
    s.outliers_rejected = static_cast<int>(samples.size() - filtered.size());

    // Recompute median from filtered set.
    if (!filtered.empty()) {
        std::sort(filtered.begin(), filtered.end());
        size_t fn = filtered.size();
        s.median_ns = (fn % 2 == 0)
            ? (filtered[fn/2 - 1] + filtered[fn/2]) / 2.0
            : filtered[fn/2];
        s.min_ns = filtered.front();
        s.max_ns = filtered.back();
    }

    // Stability check: MAD/median < 5% means stable.
    s.significant = s.median_ns > 0 && (s.mad_ns / s.median_ns) < 0.05;

    return s;
}

struct BenchResult {
    std::string name;
    std::string runtime;
    Stats       stats;
    int64_t     result;
    int64_t     expected;
    bool        correct;
};

inline std::string format_table(const std::vector<BenchResult>& results) {
    std::string out;
    out += "Benchmark              Runtime     Median ns   MAD ns   OK   Outliers\n";
    out += "--------               ------      ----------  -------  --   --------\n";
    for (const auto& r : results) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%-22s %-10s  %10.1f  %7.1f  %2s   %d/%d\n",
                      r.name.c_str(), r.runtime.c_str(),
                      r.stats.median_ns, r.stats.mad_ns,
                      r.correct ? "Y" : "N",
                      r.stats.outliers_rejected, r.stats.trials);
        out += buf;
    }
    return out;
}

inline std::string format_comparison(const std::vector<BenchResult>& results) {
    // Group by benchmark name, then show ratios.
    std::string out;
    out += "\n--- 3-Way Comparison ---\n";
    out += "Benchmark              JADE ns     LuaJIT ns   .NET ns    JADE/LuaJIT  JADE/.NET\n";
    out += "--------               ----------  ----------  ----------  -----------  ---------\n";

    // Find triplets.
    for (size_t i = 0; i < results.size(); i += 3) {
        if (i + 2 >= results.size()) break;
        double jade = results[i].stats.median_ns;
        double luajit = results[i+1].stats.median_ns;
        double dotnet = results[i+2].stats.median_ns;

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%-22s %10.1f  %10.1f  %10.1f  %10.2fx  %9.2fx\n",
                      results[i].name.c_str(), jade, luajit, dotnet,
                      jade / luajit, jade / dotnet);
        out += buf;
    }
    return out;
}

}  // namespace bench
