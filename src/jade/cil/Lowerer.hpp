// SPDX-License-Identifier: MIT
// .JADE Compiler — cil/Lowerer.hpp
//
// Lowers a stream of CIL instructions into a Sea of Nodes IR Graph.
//   - Maintains a virtual evaluation stack.
//   - Tracks the effect chain.
//   - Records the bytecode offset on every node for deopt reconstruction.
//
//   This is the entry point that turns C# bytecode (CIL) into the IR that
//   RUBY/DIAMOND optimize. JADE (Tier 1) skips SoN and uses a flat SSA graph
//   for speed; RUBY/DIAMOND lower to SoN.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/cil/Opcode.hpp"
#include "jade/ir/Graph.hpp"

#include <vector>
#include <span>
#include <cstdint>

namespace jade::cil {

// ─────────────────────────────────────────────────────────────────────────────
// CilLowerer — turns a CIL byte stream into a SoN Graph.
//
//   The lowerer is intentionally simple: it does not perform type checking,
//   metadata resolution, or method binding. It models the evaluation stack
//   symbolically — each stack slot holds a NodeId. Locals and args are
//   modeled as Phi-style pseudo-nodes (one per local slot).
//
//   The graph produced is suitable for the existing passes (ConstantFolding,
//   GVN, DCE) to optimize. Higher-tier passes (RUBY/DIAMOND) will perform
//   additional lowering (effect-chain canonicalization, type narrowing).
// ─────────────────────────────────────────────────────────────────────────────
class CilLowerer {
public:
    CilLowerer();

    // Run the lowerer on a CIL byte buffer. Returns the resulting Graph.
    [[nodiscard]] Result<Graph> lower(std::span<const uint8_t> cil_bytes,
                                      uint16_t num_locals,
                                      uint16_t num_args);

private:
    Graph  g_;
    GraphBuilder b_;
    NodeId start_;
    NodeId current_effect_;
    NodeId current_ctrl_;
    std::vector<NodeId> eval_stack_;
    std::vector<NodeId> locals_;     // one NodeId per local slot (Phi-like)
    std::vector<NodeId> args_;       // one NodeId per arg slot (Phi-like)

    void push(NodeId v);
    [[nodiscard]] NodeId pop();
    [[nodiscard]] NodeId top();
    [[nodiscard]] NodeId make_effectful(NodeKind kind, std::span<const NodeId> inputs);
    void jump_to(uint32_t target);   // simplified: only forward jumps tracked
};

}  // namespace jade::cil
