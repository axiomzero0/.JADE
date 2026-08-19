// SPDX-License-Identifier: MIT
// .JADE Compiler — runtime/TierManager.hpp
//
// Tier escalation manager. Tracks per-method invocation counts and
// triggers compilation at the appropriate tier.
//
// Escalation policy (Definition of Done):
//   granit → JADE at N=100 invocations
//   JADE  → RUBY at M=1000 invocations
//   RUBY  → DIAMOND at K=10000 invocations + profile stability
//
// Per Rule C.1: mutator threads never block on compilation.
// If compiled code isn't ready, fall back to the lower tier.

#pragma once

#include <cstdint>
#include <functional>
#include <atomic>
#include <string>

namespace jade {

enum class Tier : uint8_t {
    Interpreter = 0,   // granit (T0)
    Baseline    = 1,   // JADE (T1)
    Optimizing  = 2,   // RUBY (T2)
    Peak        = 3,   // DIAMOND (T3)
};

// Thresholds for tier escalation.
struct TierThresholds {
    uint32_t interpreter_to_baseline{100};     // N
    uint32_t baseline_to_optimizing{1000};      // M
    uint32_t optimizing_to_peak{10000};        // K
};

// MethodHandle — one per compiled method.
struct MethodHandle {
    std::string name;
    std::atomic<uint32_t> invocation_count{0};
    Tier current_tier{Tier::Interpreter};

    // Entry points for each tier. nullptr = not compiled.
    void* interpreter_entry{nullptr};
    void* baseline_entry{nullptr};
    void* optimizing_entry{nullptr};
    void* peak_entry{nullptr};

    // Get the best available entry point.
    [[nodiscard]] void* entry() const {
        if (peak_entry) return peak_entry;
        if (optimizing_entry) return optimizing_entry;
        if (baseline_entry) return baseline_entry;
        return interpreter_entry;
    }

    // Get the current tier's entry point.
    [[nodiscard]] void* current_entry() const {
        switch (current_tier) {
            case Tier::Peak:        return peak_entry ? peak_entry : current_entry_fallback();
            case Tier::Optimizing:  return optimizing_entry ? optimizing_entry : current_entry_fallback();
            case Tier::Baseline:    return baseline_entry ? baseline_entry : current_entry_fallback();
            default:                return interpreter_entry;
        }
    }

private:
    [[nodiscard]] void* current_entry_fallback() const {
        // Fall back to the next-lower tier.
        if (optimizing_entry) return optimizing_entry;
        if (baseline_entry) return baseline_entry;
        return interpreter_entry;
    }
};

// TierManager — manages tier escalation for all methods.
class TierManager {
public:
    TierManager() = default;
    explicit TierManager(TierThresholds thresholds) : thresholds_(thresholds) {}

    // Called on every method invocation. Returns the tier that SHOULD
    // be active (the caller checks if the entry point is ready).
    [[nodiscard]] Tier on_invocation(MethodHandle& method) {
        uint32_t count = method.invocation_count.fetch_add(1, std::memory_order_relaxed) + 1;

        switch (method.current_tier) {
            case Tier::Interpreter:
                if (count >= thresholds_.interpreter_to_baseline)
                    return Tier::Baseline;
                break;
            case Tier::Baseline:
                if (count >= thresholds_.baseline_to_optimizing)
                    return Tier::Optimizing;
                break;
            case Tier::Optimizing:
                if (count >= thresholds_.optimizing_to_peak)
                    return Tier::Peak;
                break;
            default:
                break;
        }
        return method.current_tier;
    }

    // Mark a method as compiled at a given tier.
    void mark_compiled(MethodHandle& method, Tier tier, void* entry) {
        switch (tier) {
            case Tier::Baseline:   method.baseline_entry = entry; break;
            case Tier::Optimizing:  method.optimizing_entry = entry; break;
            case Tier::Peak:       method.peak_entry = entry; break;
            default: break;
        }
        method.current_tier = tier;
    }

    [[nodiscard]] const TierThresholds& thresholds() const noexcept { return thresholds_; }

private:
    TierThresholds thresholds_;
};

}  // namespace jade
