// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/TailCallElimination.hpp
//
// Tail Call Elimination (Standard catalog §6.2).
//
// If a Call node is immediately followed by a Return with no intervening
// effectful operations, reuse the current frame. Emit a Jump instead of
// Call + Ret.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class TailCallEliminationPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "TCE"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
