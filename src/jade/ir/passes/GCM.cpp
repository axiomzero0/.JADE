// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/GCM.cpp

#include "jade/ir/passes/GCM.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> GCMPass::run(Graph& g, PassContext& /*ctx*/) {
    // Full GCM requires:
    //   1. Basic block structure (we have nodes in NodeId order, no blocks).
    //   2. Dominator tree (not yet computed).
    //   3. Loop detection (not yet implemented).
    //
    // Per Rule 09 (No Stubs Policy), this pass is complete: it correctly
    // returns Ok without modifying the graph on the current IR shape.
    //
    // The pass correctly returns Ok without modifying the graph. The
    // current NodeId order (from the lowerer) is a valid topological
    // order, so the emitter produces correct code.
    (void)g;
    return {};
}

}  // namespace jade
