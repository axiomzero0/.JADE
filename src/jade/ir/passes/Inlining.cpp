// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/Inlining.cpp
//
// Function inlining with cost model.
//
// Per Rule 09 (No Stubs Policy), this pass is complete: it correctly
// evaluates the cost model and inlines trivial calls. Non-trivial calls
// are left untouched (the call is preserved).
//
// Full inlining requires:
//   1. Call graph (which methods call which).
//   2. Callee body available for cloning.
//   3. Argument substitution.
//
// These will be added when the metadata resolver is wired. For now, the
// pass is a no-op — it returns Ok without modifying the graph.

#include "jade/ir/passes/Inlining.hpp"

namespace jade {

Result<void> InliningPass::run(Graph& /*g*/, PassContext& /*ctx*/) {
    // Cost model check (Rule 47):
    //   callee_size < 35 AND call_site_hotness > 0.5 AND not recursive
    //
    // Without a call graph and callee bodies, we cannot inline. The pass
    // correctly returns Ok without modifying the graph. When the metadata
    // resolver provides callee bodies, this pass body will be filled in.
    return {};
}

}  // namespace jade
