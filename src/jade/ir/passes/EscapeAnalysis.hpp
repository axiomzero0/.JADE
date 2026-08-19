// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/EscapeAnalysis.hpp
//
// Basic (binary) Escape Analysis (Standard catalog §1.2, basic form).
//
// An allocation "escapes" if a reference to it is:
//   - Returned from the method
//   - Stored into a field of another object
//   - Passed as an argument to a call
//   - Used by a Throw / StoreElement
//
// If an Allocate/NewObj/NewArr node is proven non-escaping, mark it for
// elimination by SRA (Scalar Replacement of Aggregates).
//
// PEA (Partial Escape Analysis) in DIAMOND is the more advanced form that
// delays allocation to the exact escape path. This basic pass is binary:
// either escapes or doesn't.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class EscapeAnalysisPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "EscapeAnalysis"; }
    [[nodiscard]] bool is_analysis() const noexcept override { return true; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
