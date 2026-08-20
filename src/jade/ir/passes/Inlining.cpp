// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/Inlining.cpp
//
// Function inlining with cost model (Rule 47).
//
// Currently a no-op because inlining requires:
//   1. A call graph (which methods call which).
//   2. Callee bodies available for cloning.
//   3. A Graph::clone_subgraph() API.
//
// These require the metadata resolver to be wired. When that's available,
// this pass will:
//   1. Walk all Call/CallKnown nodes.
//   2. For each, check the cost model:
//      - callee_size < 35 nodes (budget).
//      - call site is hot (profile frequency > 0.5).
//      - not recursive (max 1 level deep).
//   3. Clone the callee's Graph into the caller's arena.
//   4. Replace the Call node with the callee's body (as a SoN subgraph).
//   5. Substitute arguments.
//   6. Rewire the return value.
//
// Per Rule 09: the pass correctly returns Ok without modifying the
// graph. The transformation is gated on a missing API.

#include "jade/ir/passes/Inlining.hpp"

namespace jade {

Result<void> InliningPass::run(Graph& /*g*/, PassContext& /*ctx*/) {
    // Cost model (Rule 47):
    //   callee_size < 35 nodes
    //   call_site_hotness > 0.5 (from profile)
    //   not recursive (depth <= 1)
    //
    // Without a call graph and callee bodies from the metadata resolver,
    // we cannot inline. The pass correctly returns Ok.
    return {};
}

}  // namespace jade
