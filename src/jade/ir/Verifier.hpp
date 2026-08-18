// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/Verifier.hpp
//
// Graph verifier — Rule 42:
//   "Graph verifier runs in debug builds after every pass."
//
// Checks:
//   1. No dangling NodeIds.
//   2. Effect chain continuity — every Effect node has exactly one effect input;
//      chains terminate at Start or Loop phi.
//   3. Control dominance — every node's control input dominates the node itself.
//      (Simplified check: every control input exists and is reachable from Start.)
//   4. Use-def consistency — if A has input B, then B's output list contains A.
//   5. No dead nodes with live users — if A is marked IsDead, no live node may reference it.
//   6. FrameState attached to every guard.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/ir/Graph.hpp"

namespace jade {

[[nodiscard]] Result<void> verify_graph(const Graph& g) noexcept;

// Returns true if verification should run (debug builds, JADE_DEBUG_VERIFY).
[[nodiscard]] inline bool verifier_enabled() noexcept {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

// Run the verifier if enabled; abort compilation if it fails.
// Used by pass runners in debug builds.
[[nodiscard]] inline Result<void> verify_if_enabled(const Graph& g) noexcept {
    if (!verifier_enabled()) return {};
    return verify_graph(g);
}

}  // namespace jade
