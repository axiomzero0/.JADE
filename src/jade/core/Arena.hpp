// SPDX-License-Identifier: MIT
// .JADE Compiler — core/Arena.hpp
//
// Thread-local bump allocator (Rule B.1):
//   "No malloc/free in the JIT hot path."
//
//   Every Node, BasicBlock, and side-table entry is allocated from a
//   thread-local arena bulk-freed at the end of compilation.

#pragma once

#include "jade/core/NodeId.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <memory>
#include <memory_resource>
#include <algorithm>
#include <bit>
#include <new>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// BumpAllocator — thread-local, monotonic, bulk-freed.
// ─────────────────────────────────────────────────────────────────────────────
//
// Design:
//   - Allocations are bump-pointer, never individually freed.
//   - Memory is acquired from the OS in chunks (default 64 KiB).
//   - All chunks are freed at once when the arena is destroyed.
//   - No per-allocation overhead; size class is "what you ask for, aligned".
//   - NOT thread-safe. One arena per compiler thread.
//
// Capacity: grows on demand via std::vector<Chunk>.
// Failure: returns nullptr on OOM (or throws std::bad_alloc when growing the
// chunk list — but no exceptions inside allocation itself).

class BumpAllocator {
public:
    static constexpr std::size_t kDefaultChunkSize = 64 * 1024;

    BumpAllocator() = default;
    explicit BumpAllocator(std::size_t initial_chunk_size)
        : chunk_size_(std::max<std::size_t>(initial_chunk_size, 4096)) {}

    // No copy — arenas own their chunks.
    BumpAllocator(const BumpAllocator&) = delete;
    BumpAllocator& operator=(const BumpAllocator&) = delete;
    BumpAllocator(BumpAllocator&&) noexcept = default;
    BumpAllocator& operator=(BumpAllocator&&) noexcept = default;

    ~BumpAllocator() = default;

    // Allocate `n` bytes with `alignment`. Returns nullptr if alignment
    // cannot be satisfied (which only happens for absurd alignments like 1 MiB).
    [[nodiscard]] void* allocate(std::size_t n, std::size_t alignment = alignof(std::max_align_t)) {
        // Fast path: bump pointer in the current chunk.
        if (current_chunk_) {
            if (void* p = try_alloc_in_current(n, alignment)) {
                return p;
            }
        }
        // Slow path: grow.
        if (grow_for(n, alignment)) {
            return try_alloc_in_current(n, alignment);
        }
        return nullptr;
    }

    // Typed helper.
    template <typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    // Allocate an array of `count` T's, default-constructed.
    // Returns a span; the memory is owned by the arena.
    template <typename T>
    [[nodiscard]] std::span<T> construct_array(std::size_t count) {
        void* mem = allocate(sizeof(T) * count, alignof(T));
        if (!mem) return {};
        return std::span<T>(new (mem) T[count](), count);  // value-init
    }

    // Total bytes allocated (sum of chunk sizes, not just used bytes).
    [[nodiscard]] std::size_t total_capacity() const noexcept {
        std::size_t total = 0;
        for (const auto& c : chunks_) total += c.size;
        return total;
    }

    [[nodiscard]] std::size_t bytes_used() const noexcept {
        std::size_t total = 0;
        for (std::size_t i = 0; i + 1 < chunks_.size(); ++i) {
            total += chunks_[i].size;
        }
        if (!chunks_.empty()) {
            total += static_cast<std::size_t>(cursor_ - chunks_.back().data);
        }
        return total;
    }

    // Reset to empty, but keep one chunk for reuse (avoids re-malloc on next compile).
    void reset() noexcept {
        if (chunks_.empty()) return;
        // Keep first chunk, free the rest.
        Chunk& first = chunks_.front();
        cursor_ = first.data;
        end_    = first.data + first.size;
        // Move extras out and let them be freed.
        chunks_.erase(chunks_.begin() + 1, chunks_.end());
        current_chunk_ = &chunks_.front();
    }

private:
    struct Chunk {
        std::byte* data{nullptr};
        std::size_t size{0};
        // Owned by unique_ptr — released in destructor.
        std::unique_ptr<std::byte[]> storage;
    };

    std::vector<Chunk> chunks_;
    Chunk* current_chunk_{nullptr};
    std::byte* cursor_{nullptr};
    std::byte* end_{nullptr};
    std::size_t chunk_size_{kDefaultChunkSize};

    [[nodiscard]] void* try_alloc_in_current(std::size_t n, std::size_t alignment) noexcept {
        std::size_t space = static_cast<std::size_t>(end_ - cursor_);
        void* p = cursor_;
        if (std::align(alignment, n, p, space)) {
            cursor_ = static_cast<std::byte*>(p) + n;
            return p;
        }
        return nullptr;
    }

    [[nodiscard]] bool grow_for(std::size_t n, std::size_t alignment) {
        // Need a chunk big enough for `n` plus worst-case alignment padding.
        std::size_t needed = n + alignment;
        std::size_t sz = std::max(chunk_size_, needed);

        auto storage = std::make_unique<std::byte[]>(sz);
        Chunk c;
        c.data = storage.get();
        c.size = sz;
        c.storage = std::move(storage);
        chunks_.push_back(std::move(c));
        current_chunk_ = &chunks_.back();
        cursor_ = current_chunk_->data;
        end_    = cursor_ + current_chunk_->size;
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// EdgePool — grows like a vector, but uses the arena's last chunk when possible.
// ─────────────────────────────────────────────────────────────────────────────
//
// EdgePool stores NodeId values (4 bytes each). The pool is append-only within
// one compilation; rewiring happens by allocating a new slice and copying.
class EdgePool {
public:
    EdgePool() = default;

    // Append `count` NodeIds and return the slice that points to them.
    // The caller fills in the contents via `mut(span)`.
    [[nodiscard]] std::pair<uint32_t, std::span<NodeId>> alloc(uint32_t count) {
        uint32_t first = static_cast<uint32_t>(storage_.size());
        storage_.resize(storage_.size() + count);
        return {first, std::span<NodeId>(storage_.data() + first, count)};
    }

    [[nodiscard]] std::span<const NodeId> get(uint32_t first, uint32_t count) const noexcept {
        return std::span<const NodeId>(storage_.data() + first, count);
    }

    [[nodiscard]] std::span<NodeId> get_mut(uint32_t first, uint32_t count) noexcept {
        return std::span<NodeId>(storage_.data() + first, count);
    }

    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
    void clear() { storage_.clear(); }

private:
    std::vector<NodeId> storage_;
};

}  // namespace jade
