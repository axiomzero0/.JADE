// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ProfileGuidedBlockReorder.cpp
//
// Profile-guided block reordering. Requires basic block structure and
// profile data. No-op on the current linear IR.

#include "jade/ir/passes/ProfileGuidedBlockReorder.hpp"

namespace jade {

Result<void> ProfileGuidedBlockReorderPass::run(Graph& /*g*/, PassContext& /*ctx*/) {
    // Requires basic block structure and profile data. The current lowerer
    // produces linear graphs. When block structure and profiles are added,
    // this pass will reorder blocks by execution frequency.
    return {};
}

}  // namespace jade
