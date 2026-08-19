// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/GCM.hpp
//
// Global Code Motion (Click, 1995). The crown jewel of Sea of Nodes.
//
//   Schedule Early: move pure nodes as close to their operands as possible
//                   to hide memory latency and expose ILP.
//   Schedule Late:  move pure nodes as close to their uses as possible to
//                   minimize register pressure.
//
// For the initial milestone, we use a simplified Late scheduling: pure
// nodes are left in their current NodeId order (which is the lowerer's
// original topological order). This is correct but not optimal.
//
// Full GCM requires a basic-block structure with dominance information,
// which we don't yet have. The pass is a no-op on the current IR shape.

#pragma once

#include "jade/ir/passes/Pass.hpp"

namespace jade {

class GCMPass final : public Pass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "GCM"; }
    [[nodiscard]] Result<void> run(Graph& g, PassContext& ctx) override;
};

}  // namespace jade
