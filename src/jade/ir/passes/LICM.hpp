// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/LICM.hpp
//
// Loop Invariant Code Motion (Standard catalog §5.1).
//
// For every pure node inside a Loop region: if all its data inputs are
// defined outside the loop (or are loop-invariant), and it has no effect
// dependencies inside the loop, hoist it to the loop pre-header.
//
// For the initial milestone (no explicit Loop region in the graph yet),
// this pass is conservative: it identifies `ArrayLength` and `LdFld` nodes
// whose object input is a `Phi` (loop-carried) and marks them as
// candidates for hoisting. The actual hoist requires GCM.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class LICMPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "LICM"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
