// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ControlFlowSimplification.hpp
//
// Standard catalog §3: branch folding, empty block merging, jump threading.
//
//   If(ConstBool(true))  → unconditional Jump to IfTrue
//   If(ConstBool(false)) → unconditional Jump to IfFalse
//
// For the initial milestone, this pass focuses on constant-folded branches
// (after SCCP/ConstantFolding). Full control-flow simplification requires
// a basic-block structure that we don't yet have.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class ControlFlowSimplificationPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ControlFlowSimplification"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
