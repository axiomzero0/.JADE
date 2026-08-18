// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/DeadCodeElimination.hpp
//
// Optimization 2.1:
//   "Remove any Pure node with zero uses. Iterate until fixpoint. Must respect
//    effect chains: never remove a node with IsEffect flag even if it has no
//    data uses."

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class DeadCodeEliminationPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "DCE"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
