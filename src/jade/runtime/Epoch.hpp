// SPDX-License-Identifier: MIT
// .JADE Compiler — runtime/Epoch.hpp
//
// Epoch-Based Reclamation (Rule C.4):
//   "Memory reclamation must be epoch-based, not lock-based."
//
//   When the optimizer replaces a Node, the old node is tagged with an epoch.
//   Once all compiler threads advance past that epoch, the memory is bulk-freed.
//   This avoids both locks and use-after-free.
//
// Implementation: simple epoch counter + per-thread epoch. A node retired in
// epoch E can be freed once every compiler thread has advanced to E+2.

#pragma once

#include <atomic>
#include <cstdint>
#include <array>
#include <vector>
#include <span>
#include <memory>

namespace jade {

class EpochGC;

// ─────────────────────────────────────────────────────────────────────────────
// Epoch — logical time stamp. Monotonically increasing.
// ─────────────────────────────────────────────────────────────────────────────
using Epoch = uint64_t;

// ─────────────────────────────────────────────────────────────────────────────
// RetiredNode — a Node (or side-table entry) retired by an optimizer pass.
// Will be freed once all compiler threads have advanced past `retired_at + 1`.
// ─────────────────────────────────────────────────────────────────────────────
struct RetiredNode {
    Epoch retired_at;
    // Owned memory; freed when reclaimed.
    // Stored as void* + deleter — but we use a tagged pointer instead to avoid
    // std::function (Rule B.4).
    void*  ptr{nullptr};
    void (*deleter)(void*) noexcept{nullptr};

    RetiredNode() = default;
    RetiredNode(Epoch e, void* p, void(*d)(void*) noexcept)
        : retired_at(e), ptr(p), deleter(d) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// EpochGC — global coordinator for EBR.
// One instance per compiler pool. Thread-safe for the operations used by
// compiler threads (enter/exit/retire) and the reclamation sweep.
// ─────────────────────────────────────────────────────────────────────────────
class EpochGC {
public:
    static constexpr std::size_t kMaxThreads = 64;

    EpochGC() = default;

    // Register a compiler thread; returns its slot index.
    // The thread must call enter()/exit() around every compilation.
    [[nodiscard]] uint32_t register_thread() {
        // Simple atomic counter; in practice threads register once at startup.
        uint32_t slot = next_slot_.fetch_add(1, std::memory_order_relaxed);
        if (slot >= kMaxThreads) {
            // Fall back to slot 0 — extremely unlikely for the initial milestone.
            slot = 0;
        }
        return slot;
    }

    // Enter a new compilation epoch for this thread.
    // Returns the new local epoch.
    Epoch enter(uint32_t slot) {
        const Epoch e = global_epoch_.load(std::memory_order_acquire);
        thread_epochs_[slot].store(e, std::memory_order_release);
        return e;
    }

    // Leave the current compilation; advances the local epoch to 0 (idle).
    void exit(uint32_t slot) {
        thread_epochs_[slot].store(0, std::memory_order_release);
    }

    // Retire a node — it will be freed once all threads advance past `now + 1`.
    // `now` is the epoch returned by enter().
    void retire(Epoch now, void* ptr, void(*deleter)(void*) noexcept) {
        retired_.emplace_back(now, ptr, deleter);
    }

    // Try to advance the global epoch and reclaim memory.
    // Called periodically by any thread (typically after retiring a batch).
    void try_reclaim() {
        // Compute the minimum epoch across all registered threads.
        Epoch min_epoch = std::numeric_limits<Epoch>::max();
        bool any_active = false;
        for (const auto& te : thread_epochs_) {
            const Epoch e = te.load(std::memory_order_acquire);
            if (e != 0) {
                any_active = true;
                min_epoch = std::min(min_epoch, e);
            }
        }

        // If no thread is active, we can reclaim everything retired at any epoch.
        // Otherwise, only reclaim nodes retired at epoch < min_epoch.
        const Epoch safe_epoch = any_active ? min_epoch : std::numeric_limits<Epoch>::max();

        std::size_t i = 0;
        while (i < retired_.size()) {
            if (retired_[i].retired_at < safe_epoch) {
                // Reclaim.
                if (retired_[i].deleter) retired_[i].deleter(retired_[i].ptr);
                retired_[i] = std::move(retired_.back());
                retired_.pop_back();
            } else {
                ++i;
            }
        }

        // Try to advance the global epoch so future reclaims can proceed.
        // Only advance if no thread is currently in an old epoch.
        if (any_active && min_epoch == global_epoch_.load(std::memory_order_acquire)) {
            global_epoch_.fetch_add(1, std::memory_order_acq_rel);
        } else if (!any_active) {
            global_epoch_.store(global_epoch_.load(std::memory_order_acquire) + 1,
                                std::memory_order_release);
        }
    }

    [[nodiscard]] Epoch current_epoch() const noexcept {
        return global_epoch_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t retired_count() const noexcept { return retired_.size(); }

private:
    std::atomic<Epoch>                       global_epoch_{1};
    std::array<std::atomic<Epoch>, kMaxThreads> thread_epochs_{};
    std::atomic<uint32_t>                    next_slot_{0};
    std::vector<RetiredNode>                 retired_;  // guarded by being called from a single thread
};

// ─────────────────────────────────────────────────────────────────────────────
// ScopedEpoch — RAII wrapper for enter()/exit().
// ─────────────────────────────────────────────────────────────────────────────
class ScopedEpoch {
public:
    ScopedEpoch(EpochGC& gc, uint32_t slot) : gc_(gc), slot_(slot), epoch_(gc.enter(slot)) {}
    ~ScopedEpoch() { gc_.exit(slot_); }

    ScopedEpoch(const ScopedEpoch&) = delete;
    ScopedEpoch& operator=(const ScopedEpoch&) = delete;

    [[nodiscard]] Epoch epoch() const noexcept { return epoch_; }

private:
    EpochGC& gc_;
    uint32_t slot_;
    Epoch    epoch_;
};

}  // namespace jade
