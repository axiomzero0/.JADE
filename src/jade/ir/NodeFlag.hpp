// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/NodeFlag.hpp
//
// Bitmask of orthogonal boolean properties on a Node (Rule 51).
// Wrapped via Flags<NodeFlag>.

#pragma once

#include "jade/core/Flags.hpp"
#include <array>
#include <string_view>

namespace jade {

enum class NodeFlag : uint16_t {
    None         = 0,
    Pure         = 1u << 0,   // no side effects; can be moved freely
    Effect       = 1u << 1,   // participates in effect chain
    Control      = 1u << 2,   // participates in control flow
    Commutative  = 1u << 3,   // inputs can be reordered before hashing
    Associative  = 1u << 4,   // chain can be flattened/reassociated
    NoThrow      = 1u << 5,   // call cannot throw
    IsGuard      = 1u << 6,   // requires FrameState for deopt reconstruction
    HasState     = 1u << 7,   // FrameState attached
    IsConst      = 1u << 8,   // node produces a constant value
    IsDead       = 1u << 9,   // marked dead by DCE; no live user may reference it
    IsScheduled  = 1u << 10,  // already placed into a block by GCM
    HasSideExit  = 1u << 11,  // can deopt (any guard)
    IsCold       = 1u << 12,  // unreachable in hot path (cold path / deopt stub)
    IsLoop       = 1u << 13,  // is a Loop header node
    HasTypeNarrowing = 1u << 14,  // has been type-narrowed by CheckXxx
};

using NodeFlags = Flags<NodeFlag>;

// Symbolic names for printing (Rule 51 — mandatory).
// Indexed by single-bit position.
[[nodiscard]] std::string to_string(NodeFlags flags);
[[nodiscard]] std::string_view flag_bit_name(NodeFlag bit);

}  // namespace jade
