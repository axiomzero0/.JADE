// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ProfileGuidedBlockReorder.hpp
//
// Profile-guided basic block reordering (Standard catalog §11.1).
//
// Place hot basic blocks (top 90% of execution frequency) contiguously in
// memory. Cold blocks (error paths, deopt stubs, rare branches) go to a
// separate cold section. Maximizes I-cache utilization.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class ProfileGuidedBlockReorderPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ProfileGuidedBlockReorder"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
