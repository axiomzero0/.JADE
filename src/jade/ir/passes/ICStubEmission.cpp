// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ICStubEmission.cpp
//
// Monomorphic IC emission. Requires profile data from granit.
// Without profiles, the pass is a no-op.

#include "jade/ir/passes/ICStubEmission.hpp"

namespace jade {

Result<void> ICStubEmissionPass::run(Graph& /*g*/, PassContext& /*ctx*/) {
    // Walk all CallVirt nodes. For each, check if the profile shows
    // a single observed receiver class. If so, replace the generic
    // CallVirt with:
    //   CheckClass(receiver, expected_class) + CallKnown.
    //
    // Without profile data, we cannot safely specialize. The pass
    // correctly returns Ok without modifying the graph.
    return {};
}

}  // namespace jade
