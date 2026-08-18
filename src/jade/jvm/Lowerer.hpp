// SPDX-License-Identifier: MIT
// .JADE Compiler — jvm/Lowerer.hpp
//
// Lowers JVM bytecode into a Sea of Nodes IR Graph.
// Mirrors the CIL lowerer (`src/jade/cil/Lowerer.hpp`).

#pragma once

#include "jade/core/Result.hpp"
#include "jade/jvm/Opcode.hpp"
#include "jade/ir/Graph.hpp"

#include <vector>
#include <span>
#include <cstdint>

namespace jade::jvm {

class JvmLowerer {
public:
    JvmLowerer();

    [[nodiscard]] Result<Graph> lower(std::span<const uint8_t> jvm_bytes,
                                      uint16_t num_locals,
                                      uint16_t num_args);

private:
    Graph  g_;
    GraphBuilder b_;
    NodeId start_;
    NodeId current_effect_;
    NodeId current_ctrl_;
    std::vector<NodeId> eval_stack_;
    std::vector<NodeId> locals_;     // one NodeId per local slot (Phi-style)
    std::vector<NodeId> args_;       // one NodeId per arg slot

    void push(NodeId v);
    [[nodiscard]] NodeId pop();
    [[nodiscard]] NodeId peek(std::size_t depth = 0);
    [[nodiscard]] NodeId make_effectful(NodeKind kind, std::span<const NodeId> inputs);
};

}  // namespace jade::jvm
