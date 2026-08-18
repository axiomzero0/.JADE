// SPDX-License-Identifier: MIT
// .JADE Compiler — core/NodeId.hpp
//
// Stable, lightweight identifiers for IR nodes (Rule 3 of SoN Design):
//   "Use stable NodeIds, not raw pointers, for long-lived data."
//
// Pointers are invalidated when the arena grows. NodeId is a 32-bit opaque
// integer that remains valid until the Graph (arena) is freed.

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>   // std::hash
#include <compare>      // spaceship operator (C++20+)
#include <string>        // for to_string declaration
#include <format>        // for std::format

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// NodeId — opaque, stable identifier for a Node in a Graph.
// ─────────────────────────────────────────────────────────────────────────────
struct NodeId {
    uint32_t value{0};

    NodeId() = default;
    explicit constexpr NodeId(uint32_t v) noexcept : value(v) {}

    // Sentinel: invalid ID, used for "no node" placeholders.
    static constexpr NodeId invalid() noexcept { return NodeId{0}; }
    static constexpr NodeId start() noexcept { return NodeId{1}; }

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const NodeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NodeId&) const noexcept = default;

    // Allow `switch(id.value)` and similar — explicit conversion.
    [[nodiscard]] constexpr explicit operator uint32_t() const noexcept { return value; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid(); }

    // For humans / debug printing.
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value; }
};

// Pretty-printer for debug logging (NOT used in hot inner loops).
[[nodiscard]] inline auto to_string(NodeId id) -> std::string {
    return std::format("%{}", id.value);
}

// ─────────────────────────────────────────────────────────────────────────────
// NodeIdHash — for std::unordered_map<NodeId, ...>
// ─────────────────────────────────────────────────────────────────────────────
struct NodeIdHash {
    std::size_t operator()(NodeId id) const noexcept {
        return std::hash<uint32_t>{}(id.value);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Other stable IDs used by side tables.
// ─────────────────────────────────────────────────────────────────────────────
struct FrameStateId {
    uint32_t value{0};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const FrameStateId&) const noexcept = default;
    static constexpr FrameStateId invalid() noexcept { return FrameStateId{0}; }
};

struct ShapeId {
    uint32_t value{0};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const ShapeId&) const noexcept = default;
    static constexpr ShapeId invalid() noexcept { return ShapeId{0}; }
};

struct StringId {
    uint32_t value{0};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const StringId&) const noexcept = default;
    static constexpr StringId invalid() noexcept { return StringId{0}; }
};

// EdgeSlice — index range into the global edge pool.
// Used for data inputs, control input, effect input, and output list.
struct EdgeSlice {
    uint32_t first_edge{0};   // index into the EdgePool
    uint32_t count{0};

    [[nodiscard]] constexpr bool empty() const noexcept { return count == 0; }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return count; }
};

}  // namespace jade

// std::hash specialization — required for std::unordered_map<NodeId, ...>
template <>
struct std::hash<jade::NodeId> {
    std::size_t operator()(const jade::NodeId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};
