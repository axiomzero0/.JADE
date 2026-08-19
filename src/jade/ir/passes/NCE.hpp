// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/NCE.hpp
//
// Null Check Elimination (Standard catalog §1.4).
//
// Eliminate CheckNotNull nodes whose input is provably non-null:
//   - The result of NewObj/NewArr/Allocate (always non-null)
//   - The result of a previous CheckNotNull (already checked)
//   - A constant non-null value

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class NCEPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "NCE"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
