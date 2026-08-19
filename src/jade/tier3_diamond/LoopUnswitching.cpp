// SPDX-License-Identifier: MIT
// .JADE Compiler — tier3_diamond/LoopUnswitching.cpp
//
// Loop unswitching. Requires Loop regions; no-op on linear IR.

#include "jade/tier3_diamond/LoopUnswitching.hpp"

namespace jade::tier3 {

Result<void> LoopUnswitchingPass::run(Graph& /*g*/, PassContext& /*ctx*/) {
    // Requires Loop regions. No-op on the current IR shape.
    return {};
}

}  // namespace jade::tier3
