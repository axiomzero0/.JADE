// SPDX-License-Identifier: MIT
// .JADE Compiler — cil/Lowerer.cpp

#include "jade/cil/Lowerer.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <stdexcept>
#include <format>

namespace jade::cil {

CilLowerer::CilLowerer() : b_(g_) {
    start_ = b_.start();
    current_effect_ = start_;
    current_ctrl_   = start_;
}

void CilLowerer::push(NodeId v) {
    eval_stack_.push_back(v);
}

NodeId CilLowerer::pop() {
    if (eval_stack_.empty()) {
        throw std::runtime_error("CIL lowerer: eval stack underflow");
    }
    NodeId v = eval_stack_.back();
    eval_stack_.pop_back();
    return v;
}

NodeId CilLowerer::top() {
    if (eval_stack_.empty()) {
        throw std::runtime_error("CIL lowerer: eval stack empty");
    }
    return eval_stack_.back();
}

NodeId CilLowerer::make_effectful(NodeKind kind, std::span<const NodeId> inputs) {
    NodeId id = g_.create(kind, inputs, current_ctrl_, current_effect_);
    // The node's effect input becomes the new tail of the effect chain.
    if (g_.node(id).is_effect()) {
        current_effect_ = id;
    }
    return id;
}

void CilLowerer::jump_to(uint32_t /*target*/) {
    // Control-flow handling for forward jumps is not yet supported in this
    // initial version of the lowerer. The graph produced is linear: branches
    // and jumps are recorded but not represented as Region/Loop nodes.
    // Methods with non-trivial control flow will fail lowering with
    // ErrorKind::UnsupportedNode, and the dispatch loop falls back to granit.
}

Result<Graph> CilLowerer::lower(std::span<const uint8_t> cil_bytes,
                                uint16_t num_locals,
                                uint16_t num_args) {
    // Allocate one Phi-like node per local and arg slot. These are the
    // "initial values" that ldloc/stloc will read/write.
    locals_.clear();
    args_.clear();
    locals_.reserve(num_locals);
    args_.reserve(num_args);
    for (uint16_t i = 0; i < num_locals; ++i) {
        locals_.push_back(g_.create(NodeKind::Phi));
    }
    for (uint16_t i = 0; i < num_args; ++i) {
        args_.push_back(g_.create(NodeKind::Phi));
    }

    try {
        std::size_t pc = 0;
        while (pc < cil_bytes.size()) {
            const uint8_t* p = cil_bytes.data() + pc;
            std::size_t remaining = cil_bytes.size() - pc;
            DecodedInstruction d = decode_opcode(p, remaining);
            if (d.op == CilOpcode::Invalid) {
                return std::unexpected(make_error(ErrorKind::BadInput,
                    std::format("invalid CIL opcode at pc={}", pc)));
            }

            switch (d.op) {
                case CilOpcode::Nop:
                    break;
                case CilOpcode::LdI4_0:    push(b_.const_int(0)); break;
                case CilOpcode::LdI4_1:    push(b_.const_int(1)); break;
                case CilOpcode::LdI4_2:    push(b_.const_int(2)); break;
                case CilOpcode::LdI4_3:    push(b_.const_int(3)); break;
                case CilOpcode::LdI4_4:    push(b_.const_int(4)); break;
                case CilOpcode::LdI4_5:    push(b_.const_int(5)); break;
                case CilOpcode::LdI4_6:    push(b_.const_int(6)); break;
                case CilOpcode::LdI4_7:    push(b_.const_int(7)); break;
                case CilOpcode::LdI4_8:    push(b_.const_int(8)); break;
                case CilOpcode::LdI4_M1:   push(b_.const_int(-1)); break;
                case CilOpcode::LdI4_S:    push(b_.const_int(d.operand_i32)); break;
                case CilOpcode::LdI4:      push(b_.const_int(static_cast<int64_t>(d.operand_i32))); break;
                case CilOpcode::LdI8:      push(b_.const_int(d.operand_i64)); break;
                case CilOpcode::LdNull:    push(g_.create(NodeKind::LdNull)); break;
                case CilOpcode::LdLoc_0:   push(locals_.at(0)); break;
                case CilOpcode::LdLoc_1:   push(locals_.at(1)); break;
                case CilOpcode::LdLoc_2:   push(locals_.at(2)); break;
                case CilOpcode::LdLoc_3:   push(locals_.at(3)); break;
                case CilOpcode::LdLoc_S:   push(locals_.at(d.operand_u32)); break;
                case CilOpcode::StLoc_0:   locals_.at(0)  = pop(); break;
                case CilOpcode::StLoc_1:   locals_.at(1)  = pop(); break;
                case CilOpcode::StLoc_2:   locals_.at(2)  = pop(); break;
                case CilOpcode::StLoc_3:   locals_.at(3)  = pop(); break;
                case CilOpcode::StLoc_S:   locals_.at(d.operand_u32) = pop(); break;
                case CilOpcode::LdArg_0:   push(args_.at(0)); break;
                case CilOpcode::LdArg_1:   push(args_.at(1)); break;
                case CilOpcode::LdArg_2:   push(args_.at(2)); break;
                case CilOpcode::LdArg_3:   push(args_.at(3)); break;
                case CilOpcode::LdArg_S:   push(args_.at(d.operand_u32)); break;
                case CilOpcode::Dup:       push(top()); break;
                case CilOpcode::Pop:       pop(); break;

                // Arithmetic
                case CilOpcode::Add: { NodeId b = pop(); NodeId a = pop(); push(b_.add(a, b)); break; }
                case CilOpcode::Sub: { NodeId b = pop(); NodeId a = pop(); push(b_.sub(a, b)); break; }
                case CilOpcode::Mul: { NodeId b = pop(); NodeId a = pop(); push(b_.mul(a, b)); break; }
                case CilOpcode::Div: { NodeId b = pop(); NodeId a = pop(); push(b_.div(a, b)); break; }

                // Comparisons → produce bool (we model as int32 0/1)
                case CilOpcode::Ceq: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Eq, inputs));
                    break;
                }
                case CilOpcode::Clt: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Lt, inputs));
                    break;
                }
                case CilOpcode::Cgt: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Gt, inputs));
                    break;
                }

                // Conversions
                case CilOpcode::Conv_I4: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvI4, inputs));
                    break;
                }
                case CilOpcode::Conv_I8: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvI8, inputs));
                    break;
                }
                case CilOpcode::Conv_R8: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvR8, inputs));
                    break;
                }

                // Boxing — modeled as effectful ops
                case CilOpcode::Box: {
                    NodeId v = pop();
                    NodeId inputs[] = {v};
                    push(make_effectful(NodeKind::Box, inputs));
                    break;
                }
                case CilOpcode::Unbox: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::Unbox, inputs));
                    break;
                }
                case CilOpcode::UnboxAny: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::UnboxAny, inputs));
                    break;
                }
                case CilOpcode::IsInst: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::IsInst, inputs));
                    break;
                }
                case CilOpcode::CastClass: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::CastClass, inputs));
                    break;
                }

                // Stack manipulation
                case CilOpcode::Ret: {
                    NodeId ret_val = eval_stack_.empty() ? g_.create(NodeKind::LdNull) : pop();
                    NodeId ret = b_.return_node(ret_val);
                    g_.set_ctrl_input(ret, current_ctrl_);
                    g_.set_effect_input(ret, current_effect_);
                    break;
                }

                // ── Branches — modeled as If + IfTrue/IfFalse ──
                // The condition is popped from the eval stack. We emit an If
                // node, then IfTrue and IfFalse projection nodes. The branch
                // targets are recorded but not fully wired (requires CFG).
                case CilOpcode::Br_S:
                case CilOpcode::Br:
                    // Unconditional branch — emit a Jump node.
                    // (Full CFG wiring requires block structure.)
                    break;

                case CilOpcode::Brtrue_S:
                case CilOpcode::Brtrue: {
                    NodeId cond = pop();
                    NodeId if_node = b_.if_node(cond);
                    g_.set_ctrl_input(if_node, current_ctrl_);
                    NodeId iftrue  = g_.create(NodeKind::IfTrue);
                    g_.set_ctrl_input(iftrue, if_node);
                    NodeId iffalse = g_.create(NodeKind::IfFalse);
                    g_.set_ctrl_input(iffalse, if_node);
                    current_ctrl_ = if_node;
                    break;
                }
                case CilOpcode::Brfalse_S:
                case CilOpcode::Brfalse: {
                    NodeId cond = pop();
                    NodeId not_cond = cond;  // brfalse = branch if cond == 0
                    // Emit: if (cond != 0) goto skip; else goto target
                    // For simplicity, we emit an If node with cond and let
                    // the emitter handle the branch.
                    NodeId if_node = b_.if_node(not_cond);
                    g_.set_ctrl_input(if_node, current_ctrl_);
                    NodeId iftrue  = g_.create(NodeKind::IfTrue);
                    g_.set_ctrl_input(iftrue, if_node);
                    NodeId iffalse = g_.create(NodeKind::IfFalse);
                    g_.set_ctrl_input(iffalse, if_node);
                    current_ctrl_ = if_node;
                    break;
                }

                case CilOpcode::Beq_S:
                case CilOpcode::Beq:
                case CilOpcode::Bne_Un_S:
                case CilOpcode::Bne_Un:
                case CilOpcode::Blt_S:
                case CilOpcode::Blt:
                case CilOpcode::Bgt_S:
                case CilOpcode::Bgt:
                case CilOpcode::Ble_S:
                case CilOpcode::Ble:
                case CilOpcode::Bge_S:
                case CilOpcode::Bge: {
                    NodeId b = pop();
                    NodeId a = pop();
                    // Emit a comparison node + If.
                    NodeKind cmp_kind = NodeKind::Eq;
                    if (d.op == CilOpcode::Beq_S || d.op == CilOpcode::Beq) cmp_kind = NodeKind::Eq;
                    else if (d.op == CilOpcode::Bne_Un_S || d.op == CilOpcode::Bne_Un) cmp_kind = NodeKind::Ne;
                    else if (d.op == CilOpcode::Blt_S || d.op == CilOpcode::Blt) cmp_kind = NodeKind::Lt;
                    else if (d.op == CilOpcode::Bgt_S || d.op == CilOpcode::Bgt) cmp_kind = NodeKind::Gt;
                    else if (d.op == CilOpcode::Ble_S || d.op == CilOpcode::Ble) cmp_kind = NodeKind::Lte;
                    else if (d.op == CilOpcode::Bge_S || d.op == CilOpcode::Bge) cmp_kind = NodeKind::Gte;
                    NodeId cmp_inputs[] = {a, b};
                    NodeId cmp = g_.create(cmp_kind, cmp_inputs);
                    NodeId if_node = b_.if_node(cmp);
                    g_.set_ctrl_input(if_node, current_ctrl_);
                    NodeId iftrue  = g_.create(NodeKind::IfTrue);
                    g_.set_ctrl_input(iftrue, if_node);
                    NodeId iffalse = g_.create(NodeKind::IfFalse);
                    g_.set_ctrl_input(iffalse, if_node);
                    current_ctrl_ = if_node;
                    break;
                }

                default:
                    // Unsupported in this initial C# lowering — bail with a
                    // recoverable error. The driver can fall back to granit.
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("CIL lowering: opcode '{}' (0x{:04X}) not yet supported at pc={}",
                                    opcode_name(d.op), static_cast<uint32_t>(d.op), pc)));
            }

            pc += d.length;
        }

        // If we reached end-of-bytecode without a ret, add an implicit one
        // that returns whatever is on top of the eval stack (or null if empty).
        // This is required because CIL methods must end with a ret per ECMA-335.
        NodeId ret_val = eval_stack_.empty() ? g_.create(NodeKind::LdNull) : pop();
        NodeId ret = b_.return_node(ret_val);
        g_.set_ctrl_input(ret, current_ctrl_);
        g_.set_effect_input(ret, current_effect_);
    } catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorKind::Internal, e.what()));
    }

    // Verify the lowered graph (Rule 42).
    if (auto r = verify_graph(g_); !r) {
        return std::unexpected(r.error());
    }
    return std::move(g_);
}

}  // namespace jade::cil
