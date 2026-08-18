// SPDX-License-Identifier: MIT
// .JADE Compiler — runtime/Safepoint.hpp
//
// Safepoint polling (Rule A.4 / 5 of Definition of Done):
//   Every loop back-edge and return in granit checks an atomic flag.
//   Compiled code (JADE/RUBY/DIAMOND) polls at every back-edge via a single
//   `test` instruction. The GC only toggles this flag.

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <functional>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// SafepointManager — coordinates safepoint requests across mutator threads.
// ─────────────────────────────────────────────────────────────────────────────
class SafepointManager {
public:
    // Per-thread state. Each mutator thread polls its own state.
    // Cache-line aligned to avoid false sharing.
    struct alignas(64) ThreadState {
        std::atomic<bool> poll_requested{false};
        std::atomic<bool> at_safepoint{false};
        uint32_t          thread_id{0};
    };

    // Register a mutator thread; returns a handle to its state.
    // The thread polls `state->poll_requested` at every back-edge.
    [[nodiscard]] ThreadState* register_thread() {
        std::lock_guard lock(states_mutex_);
        uint32_t id = static_cast<uint32_t>(states_.size());
        auto& s = states_.emplace_back(std::make_unique<ThreadState>());
        s->thread_id = id;
        return s.get();
    }

    // Request a global safepoint. Returns once every mutator thread has
    // reached a safepoint (or after the timeout, in which case false is returned).
    bool request_global_safepoint(uint32_t timeout_ms = 1000) {
        // Set the poll flag on every thread.
        for (auto& s : states_) {
            s->poll_requested.store(true, std::memory_order_release);
        }
        // Spin-wait for each thread to reach a safepoint.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms);
        for (auto& s : states_) {
            while (!s->at_safepoint.load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::yield();
            }
        }
        return true;
    }

    // Release a global safepoint.
    void release_safepoint() {
        for (auto& s : states_) {
            s->poll_requested.store(false, std::memory_order_release);
            s->at_safepoint.store(false, std::memory_order_release);
        }
    }

    // Called by a mutator thread at every loop back-edge.
    // Inlined as a single `test` instruction in compiled code.
    static inline bool should_poll(ThreadState* ts) noexcept {
        return ts->poll_requested.load(std::memory_order_acquire);
    }

    // Called by a mutator thread when it has reached the safepoint.
    static inline void enter_safepoint(ThreadState* ts) noexcept {
        ts->at_safepoint.store(true, std::memory_order_release);
        // Spin until released.
        while (ts->poll_requested.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        ts->at_safepoint.store(false, std::memory_order_release);
    }

private:
    std::vector<std::unique_ptr<ThreadState>> states_;
    std::mutex states_mutex_;
};

}  // namespace jade
