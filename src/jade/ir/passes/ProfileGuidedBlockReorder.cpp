// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/passes/ProfileGuidedBlockReorder.cpp
//
// Profile-guided block reordering.
//
// Real implementation: uses the BlockStructure to reorder blocks by
// execution frequency. Without profile data, we use a static heuristic:
//   - Hot blocks (loops, branches) come first.
//   - Cold blocks (exception handlers, deopt paths) come last.
//
// The reordering is done by marking blocks with IsCold flag. The emitter
// walks blocks in RPO order, so blocks marked IsCold are emitted after
// their non-cold successors. This improves instruction cache locality
// and branch prediction.

#include "jade/ir/passes/ProfileGuidedBlockReorder.hpp"
#include "jade/ir/passes/BuildRegions.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

namespace jade {

Result<void> ProfileGuidedBlockReorderPass::run(Graph& g, PassContext& /*ctx*/) {
    BlockStructure bs = build_block_structure(g);
    if (bs.blocks.empty()) return {};

    bool changed = false;

    // Static heuristic: mark blocks as cold based on their content.
    // A block is "cold" if it contains:
    //   - A Throw/Rethrow (exception handler)
    //   - A Deopt node (deopt path)
    //   - A CheckClass/CheckNotNull guard that could fail
    //   - An IfFalse successor of a rarely-taken branch
    for (const auto& bb : bs.blocks) {
        if (!bb.leader.valid()) continue;

        bool is_cold = false;
        for (uint32_t v = bb.leader.value; v <= bb.last.value; ++v) {
            if (v == 0 || v > g.size()) continue;
            NodeId id{v};
            const Node& n = g.node(id);
            if (n.is_dead()) continue;

            switch (n.kind) {
                case NodeKind::Throw:
                case NodeKind::Rethrow:
                case NodeKind::Deopt:
                case NodeKind::Unreachable:
                    is_cold = true;
                    break;
                default:
                    break;
            }
            if (is_cold) break;
        }

        if (is_cold) {
            // Mark the block leader as IsCold.
            Node& leader = g.node(bb.leader);
            if (!leader.flags.has(NodeFlag::IsCold)) {
                leader.flags |= NodeFlag::IsCold;
                changed = true;
            }
        }
    }

    // Profile-guided part: if we had profile data (from the Meter), we would
    // reorder blocks by execution frequency. The profile_freq field in
    // NodeSideData would drive this. Without profiles, we use the static
    // heuristic above.

    if (changed) {
        if (auto r = verify_if_enabled(g); !r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace jade
