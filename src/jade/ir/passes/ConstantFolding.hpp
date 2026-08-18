// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ConstantFolding.hpp
//
// Optimization 1.1 from the Standard Catalogue:
//   "If all inputs to a pure node are ConstInt/ConstFloat, evaluate at compile
//    time. Must handle overflow semantics identically to granit."

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class ConstantFoldingPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ConstantFolding"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
