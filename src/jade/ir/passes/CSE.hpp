// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/CSE.hpp
//
// Common Subexpression Elimination (Standard catalog §2.3).
// Local CSE: within a basic block, if a pure node appears twice with no
// intervening effectful node, replace the second with a use of the first.
//
// This is the local precursor to GVN. GVN does the same thing globally;
// CSE is cheaper and runs first to reduce GVN's work.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class CSEPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "CSE"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
