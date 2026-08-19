// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/Inlining.hpp
//
// Function inlining with cost model (Rule 47).
//
//   - Inline only if callee size < budget (35 nodes default).
//   - Inline only if call site is hot (profile frequency > 0.5).
//   - Recursive calls: inline max 1 level.
//
// For the initial milestone, inlining is conservative: it only inlines
// calls where the callee is a single-node graph (trivial). Full inlining
// requires call-graph analysis and callee body cloning.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class InliningPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Inlining"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
