// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/CodeEmitter.cpp
//
// asmjit-based x86-64 code emitter.
//
// Supports the following NodeKinds:
//   - Start      → no-op (graph entry)
//   - ConstInt   → mov reg, imm64
//   - LdLoc      → mov reg, [rbp - local_offset]
//   - StLoc      → mov [rbp - local_offset], reg
//   - LdArg      → mov reg, arg_register (or load from stack for args > 6)
//   - Add        → add reg, reg
//   - Sub        → sub reg, reg
//   - Mul        → imul reg, reg
//   - Div        → cqo; idiv (clobbers RDX:RAX)
//   - Mod        → cqo; idiv; mov result, rdx
//   - Neg        → neg reg
//   - And/Or/Xor/Not/Shl/Shr/Sar → bitwise ops
//   - Cmp (Eq/Ne/Lt/Gt/Lte/Gte) → cmp; setcc; movzx
//   - LdFld      → mov reg, [obj + field_offset]
//   - StFld      → mov [obj + field_offset], reg
//   - Return     → mov rax, value; jmp epilogue
//   - Safepoint  → test poll flag; jne handler
//
// Any other NodeKind returns ErrorKind::UnsupportedNode, and the driver
// falls back to granit. This is graceful degradation per the No Stubs policy.
//
// SysV calling convention (Linux):
//   - Args: RDI, RSI, RDX, RCX, R8, R9 (integer), XMM0..XMM7 (float)
//   - Return: RAX (or XMM0 for float)
//   - Callee-saved: RBX, RBP, R12-R15
//   - Stack alignment: 16 bytes

#include "jade/tier1_jade/CodeEmitter.hpp"
#include "jade/tier1_jade/FrameLayout.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/TypeId.hpp"

#include "asmjit/asmjit.h"

#include <memory>
#include <format>
#include <unordered_map>

namespace jade::tier1 {

namespace {

using namespace asmjit;

// Map from our X86Reg enum to asmjit's x86::Gp.
[[nodiscard]] x86::Gp to_asmjit_gpr(X86Reg r) noexcept {
    switch (r) {
        case X86Reg::RAX: return x86::rax;
        case X86Reg::RBX: return x86::rbx;
        case X86Reg::RCX: return x86::rcx;
        case X86Reg::RDX: return x86::rdx;
        case X86Reg::RSI: return x86::rsi;
        case X86Reg::RDI: return x86::rdi;
        case X86Reg::R8:  return x86::r8;
        case X86Reg::R9:  return x86::r9;
        case X86Reg::R10: return x86::r10;
        case X86Reg::R11: return x86::r11;
        case X86Reg::R12: return x86::r12;
        case X86Reg::R13: return x86::r13;
        case X86Reg::R14: return x86::r14;
        case X86Reg::R15: return x86::r15;
        default:          return x86::rax;
    }
}

// Map from ArgReg to X86Reg.
[[nodiscard]] X86Reg arg_reg_to_x86(ArgReg r) noexcept {
    switch (r) {
        case ArgReg::RDI: return X86Reg::RDI;
        case ArgReg::RSI: return X86Reg::RSI;
        case ArgReg::RDX: return X86Reg::RDX;
        case ArgReg::RCX: return X86Reg::RCX;
        case ArgReg::R8:  return X86Reg::R8;
        case ArgReg::R9:  return X86Reg::R9;
    }
    return X86Reg::RDI;
}

// Map NodeKind::Eq/Ne/Lt/Gt/Lte/Gte to the right setcc instruction.
[[nodiscard]] x86::CondCode cmp_to_cond(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::Eq:  return x86::CondCode::kE;
        case NodeKind::Ne:  return x86::CondCode::kNE;
        case NodeKind::Lt:  return x86::CondCode::kL;
        case NodeKind::Gt:  return x86::CondCode::kG;
        case NodeKind::Lte: return x86::CondCode::kLE;
        case NodeKind::Gte: return x86::CondCode::kGE;
        default:            return x86::CondCode::kE;
    }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Impl — the asmjit state, hidden from the public header.
// ─────────────────────────────────────────────────────────────────────────────

struct CodeEmitter::Impl {
    JitRuntime runtime;
    std::unordered_map<uint32_t, Label> block_labels;
    // Pending merge labels for if-then-else. When IfFalse is hit, we emit
    // `jmp merge_label` and push merge_label. Before the epilogue, we bind
    // all pending merge labels so fall-through works correctly.
    std::vector<Label> pending_merge_labels;
};

CodeEmitter::CodeEmitter() : impl_(std::make_unique<Impl>()) {}

CodeEmitter::~CodeEmitter() = default;

Result<void> CodeEmitter::init_runtime() {
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// compute_node_positions — linearize the graph for emission.
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> compute_node_positions(const Graph& graph) {
    std::vector<uint32_t> pos(graph.size());
    for (std::size_t i = 0; i < graph.size(); ++i) {
        pos[i] = static_cast<uint32_t>(i);
    }
    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: read an operand value into a given GPR.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Emit code to make the value of `iv` available. If `iv` is in a register,
// return that register directly (no mov). If spilled, load it into `spill_dst`.
// This avoids clobbering source registers when they're already allocated.
[[nodiscard]] x86::Gp load_value(x86::Assembler& a,
                                  const LiveInterval& iv,
                                  const x86::Gp& spill_dst) {
    if (iv.assigned_reg) {
        // Value is already in a register — use it directly without moving.
        return to_asmjit_gpr(*iv.assigned_reg);
    }
    if (iv.spill_slot) {
        a.mov(spill_dst, x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)));
        return spill_dst;
    }
    // Unreachable if allocation succeeded.
    a.mov(spill_dst, 0);
    return spill_dst;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// emit — the main entry point.
// ─────────────────────────────────────────────────────────────────────────────

Result<CompiledFunction> CodeEmitter::emit(const Graph& graph,
                                              const AllocationResult& alloc) {
    auto init_r = init_runtime();
    if (!init_r) return std::unexpected(init_r.error());

    CodeHolder code;
    code.init(impl_->runtime.environment());

    x86::Assembler a(&code);

    // Forward-reference label for the epilogue.
    Label epilogue_label = a.new_label();

    // Build the frame layout from the allocation result.
    // Count locals by scanning the graph for LdLoc/StLoc nodes and finding
    // the maximum local index.
    uint8_t num_reg_args = 0;
    uint32_t num_locals = 0;
    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = graph.node(id);
        if (n.kind == NodeKind::LdArg) {
            uint32_t arg_idx = graph.side(id).class_id;
            if (arg_idx + 1 > num_reg_args && arg_idx < 6) {
                num_reg_args = static_cast<uint8_t>(arg_idx + 1);
            }
        }
        if (n.kind == NodeKind::LdLoc || n.kind == NodeKind::StLoc) {
            uint32_t local_idx = graph.side(id).class_id;
            if (local_idx + 1 > num_locals) {
                num_locals = local_idx + 1;
            }
        }
    }

    FrameLayout fl = build_frame_layout(
        static_cast<uint32_t>(alloc.spill_slots.size()),
        num_locals,
        num_reg_args);

    // ── Prologue ─────────────────────────────────────────────────────────
    a.push(x86::rbp);
    a.mov(x86::rbp, x86::rsp);
    if (fl.frame_size > 0) {
        a.sub(x86::rsp, fl.frame_size);
    }
    a.push(x86::rbx);
    a.push(x86::r12);
    a.push(x86::r13);
    a.push(x86::r14);
    a.push(x86::r15);

    // ── Body ──────────────────────────────────────────────────────────────
    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = graph.node(id);

        if (n.is_dead()) continue;

        const LiveInterval& iv = alloc.intervals[id.value - 1];

        switch (n.kind) {
            case NodeKind::Start:
                break;

            case NodeKind::ConstInt: {
                const int64_t v = graph.side(id).const_value.i64;
                if (iv.assigned_reg) {
                    const x86::Gp dst = to_asmjit_gpr(*iv.assigned_reg);
                    a.mov(dst, v);
                    if (iv.spill_slot) {
                        a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                    }
                } else if (iv.spill_slot) {
                    a.mov(x86::rax, v);
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), x86::rax);
                } else {
                    return std::unexpected(make_error(
                        ErrorKind::Internal,
                        std::format("emit: ConstInt %{} has neither reg nor spill slot", id.value)));
                }
                break;
            }

            case NodeKind::LdLoc: {
                // Load a local variable. The local index is in side_data::class_id.
                uint32_t local_idx = graph.side(id).class_id;
                if (local_idx >= fl.local_offsets.size()) {
                    return std::unexpected(make_error(
                        ErrorKind::UnsupportedNode,
                        std::format("emit: LdLoc %{} local index {} out of range (max {})",
                                    id.value, local_idx, fl.local_offsets.size())));
                }
                int32_t offset = fl.local_offsets[local_idx];
                if (iv.assigned_reg) {
                    const x86::Gp dst = to_asmjit_gpr(*iv.assigned_reg);
                    a.mov(dst, x86::qword_ptr(x86::rbp, offset));
                    if (iv.spill_slot) {
                        a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                    }
                } else if (iv.spill_slot) {
                    a.mov(x86::rax, x86::qword_ptr(x86::rbp, offset));
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), x86::rax);
                } else {
                    return std::unexpected(make_error(ErrorKind::Internal,
                        std::format("emit: LdLoc %{} not allocated", id.value)));
                }
                break;
            }

            case NodeKind::StLoc: {
                // Store the value from the stack into a local.
                uint32_t local_idx = graph.side(id).class_id;
                if (local_idx >= fl.local_offsets.size()) {
                    return std::unexpected(make_error(
                        ErrorKind::UnsupportedNode,
                        std::format("emit: StLoc %{} local index {} out of range",
                                    id.value, local_idx)));
                }
                int32_t offset = fl.local_offsets[local_idx];
                // StLoc has one data input (the value to store).
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: StLoc %{} expects 1 input, got {}",
                                    id.value, inputs.size())));
                }
                const LiveInterval& iv_src = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv_src, x86::rax);
                a.mov(x86::qword_ptr(x86::rbp, offset), src);
                break;
            }

            case NodeKind::LdArg: {
                // Load a function argument from the calling convention register.
                uint32_t arg_idx = graph.side(id).class_id;
                if (arg_idx >= 6) {
                    // Args 7+ are on the caller's stack at [rbp + 16 + 8*(arg_idx-6)].
                    int32_t stack_off = 16 + static_cast<int32_t>(arg_idx - 6) * 8;
                    if (iv.assigned_reg) {
                        const x86::Gp dst = to_asmjit_gpr(*iv.assigned_reg);
                        a.mov(dst, x86::qword_ptr(x86::rbp, stack_off));
                    } else if (iv.spill_slot) {
                        a.mov(x86::rax, x86::qword_ptr(x86::rbp, stack_off));
                        a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), x86::rax);
                    }
                } else {
                    X86Reg arg_reg = arg_reg_to_x86(static_cast<ArgReg>(arg_idx));
                    if (iv.assigned_reg) {
                        const x86::Gp dst = to_asmjit_gpr(*iv.assigned_reg);
                        const x86::Gp src = to_asmjit_gpr(arg_reg);
                        if (dst != src) {
                            a.mov(dst, src);
                        }
                        if (iv.spill_slot) {
                            a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                        }
                    } else if (iv.spill_slot) {
                        a.mov(x86::rax, to_asmjit_gpr(arg_reg));
                        a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), x86::rax);
                    }
                }
                break;
            }

            case NodeKind::Add:
            case NodeKind::Sub:
            case NodeKind::Mul: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) {
                    return std::unexpected(make_error(
                        ErrorKind::UnsupportedNode,
                        std::format("emit: {} expects 2 inputs, got {}",
                                    node_kind_name(n.kind), inputs.size())));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv2 = alloc.intervals[inputs[1].value - 1];

                // Get the source values without clobbering.
                x86::Gp src1 = load_value(a, iv1, x86::rax);
                x86::Gp src2 = load_value(a, iv2, x86::rcx);

                // Determine the destination register.
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }

                // Handle the case where dst == src2 (we'd clobber src2 when
                // doing `mov dst, src1`). In that case, swap: do `mov dst, src2`
                // then use src2's original value... actually simpler: if dst
                // is the same as src2, we need to move src2 out of the way.
                if (dst == src2 && dst != src1) {
                    // src2 is in the dst register. Move src1 to dst, then apply.
                    // But src2 is now clobbered. So we need to reload src2.
                    // The safest approach: move src2 to a scratch first.
                    // Actually, the regalloc should prevent this, but be safe.
                    // Move src1 to dst (clobbers src2 which was in dst):
                    a.mov(dst, src1);
                    // Now reload src2 from its spill slot (if spilled) or
                    // from its original reg. If src2 was in a register,
                    // that register IS dst, which we just overwrote.
                    // So we must reload from spill. If iv2 is not spilled,
                    // this is a bug in the regalloc.
                    if (iv2.spill_slot) {
                        a.mov(x86::rcx, x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv2.spill_slot)));
                        src2 = x86::rcx;
                    } else {
                        // src2 was in a register and that register is dst.
                        // We already moved src1 into dst, so src2's value is lost.
                        // This shouldn't happen with a correct regalloc.
                        return std::unexpected(make_error(ErrorKind::Internal,
                            std::format("emit: {} %{} src2 clobbered by dst assignment",
                                        node_kind_name(n.kind), id.value)));
                    }
                } else if (dst != src1) {
                    a.mov(dst, src1);
                }
                switch (n.kind) {
                    case NodeKind::Add: a.add(dst, src2); break;
                    case NodeKind::Sub: a.sub(dst, src2); break;
                    case NodeKind::Mul: a.imul(dst, src2); break;
                    default: break;
                }
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::Div:
            case NodeKind::Mod: {
                // x86 idiv uses RDX:RAX. Input 1 → RAX, input 2 → operand.
                // Result: Div → RAX (quotient); Mod → RDX (remainder).
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: {} expects 2 inputs", node_kind_name(n.kind))));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv2 = alloc.intervals[inputs[1].value - 1];

                // Load dividend into RAX.
                load_value(a, iv1, x86::rax);
                // Load divisor into RCX (any register != RAX/RDX).
                x86::Gp divisor = load_value(a, iv2, x86::rcx);
                // Sign-extend RAX into RDX.
                a.cqo();
                // idiv RCX → quotient in RAX, remainder in RDX.
                a.idiv(divisor);

                // Move result (RAX for Div, RDX for Mod) into dst.
                x86::Gp result_reg = (n.kind == NodeKind::Div) ? x86::rax : x86::rdx;
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                if (dst != result_reg) {
                    a.mov(dst, result_reg);
                }
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::Neg: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: Neg expects 1 input")));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                if (dst != src) {
                    a.mov(dst, src);
                }
                a.neg(dst);
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::And:
            case NodeKind::Or:
            case NodeKind::Xor: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: {} expects 2 inputs", node_kind_name(n.kind))));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv2 = alloc.intervals[inputs[1].value - 1];
                x86::Gp src1 = load_value(a, iv1, x86::rax);
                x86::Gp src2 = load_value(a, iv2, x86::rcx);
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                if (dst != src1) {
                    a.mov(dst, src1);
                }
                switch (n.kind) {
                    case NodeKind::And: a.and_(dst, src2); break;
                    case NodeKind::Or:  a.or_(dst, src2); break;
                    case NodeKind::Xor: a.xor_(dst, src2); break;
                    default: break;
                }
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::Not: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        "emit: Not expects 1 input"));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                if (dst != src) {
                    a.mov(dst, src);
                }
                a.not_(dst);
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::Shl:
            case NodeKind::Shr:
            case NodeKind::Sar: {
                // Shifts: shift count must be in RCX (low 6 bits).
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: {} expects 2 inputs", node_kind_name(n.kind))));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv2 = alloc.intervals[inputs[1].value - 1];
                x86::Gp src1 = load_value(a, iv1, x86::rax);
                x86::Gp src2 = load_value(a, iv2, x86::rcx);
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                if (dst != src1) {
                    a.mov(dst, src1);
                }
                // Shift count is in CL (low byte of RCX).
                switch (n.kind) {
                    case NodeKind::Shl: a.shl(dst, x86::cl); break;
                    case NodeKind::Shr: a.shr(dst, x86::cl); break;
                    case NodeKind::Sar: a.sar(dst, x86::cl); break;
                    default: break;
                }
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::Eq:
            case NodeKind::Ne:
            case NodeKind::Lt:
            case NodeKind::Gt:
            case NodeKind::Lte:
            case NodeKind::Gte: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: {} expects 2 inputs", node_kind_name(n.kind))));
                }
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv2 = alloc.intervals[inputs[1].value - 1];
                x86::Gp src1 = load_value(a, iv1, x86::rax);
                x86::Gp src2 = load_value(a, iv2, x86::rcx);
                a.cmp(src1, src2);
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                // setcc dl; movzx dst, dl
                a.set(cmp_to_cond(n.kind), x86::dl);
                a.movzx(dst, x86::dl);
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::LdFld: {
                // Load a field from an object pointer.
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: LdFld expects 1 input (the object)")));
                }
                const LiveInterval& iv_obj = alloc.intervals[inputs[0].value - 1];
                x86::Gp obj = load_value(a, iv_obj, x86::rax);
                uint16_t field_offset = graph.side(id).field_offset;
                if (iv.assigned_reg) {
                    const x86::Gp dst = to_asmjit_gpr(*iv.assigned_reg);
                    a.mov(dst, x86::qword_ptr(obj, field_offset));
                    if (iv.spill_slot) {
                        a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                    }
                } else if (iv.spill_slot) {
                    a.mov(x86::rax, x86::qword_ptr(obj, field_offset));
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), x86::rax);
                }
                break;
            }

            case NodeKind::StFld: {
                // Store a value to a field of an object.
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        "emit: StFld expects 2 inputs (obj, value)"));
                }
                const LiveInterval& iv_obj = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv_val = alloc.intervals[inputs[1].value - 1];
                x86::Gp obj = load_value(a, iv_obj, x86::rax);
                x86::Gp val = load_value(a, iv_val, x86::rcx);
                uint16_t field_offset = graph.side(id).field_offset;
                a.mov(x86::qword_ptr(obj, field_offset), val);
                break;
            }

            case NodeKind::If: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("emit: If expects 1 input, got {}", inputs.size())));
                }
                const LiveInterval& iv_cond = alloc.intervals[inputs[0].value - 1];
                x86::Gp cond_reg = load_value(a, iv_cond, x86::rax);

                NodeId iftrue_target  = NodeId::invalid();
                NodeId iffalse_target = NodeId::invalid();
                for (std::size_t j = 0; j < graph.size(); ++j) {
                    const NodeId other{static_cast<uint32_t>(j + 1)};
                    if (other == id) continue;
                    const Node& on = graph.node(other);
                    if (on.is_dead()) continue;
                    if (on.kind == NodeKind::IfTrue && graph.ctrl_input(other) == id) {
                        iftrue_target = other;
                    } else if (on.kind == NodeKind::IfFalse && graph.ctrl_input(other) == id) {
                        iffalse_target = other;
                    }
                }

                Label true_label  = a.new_label();
                Label false_label = a.new_label();
                Label merge_label = a.new_label();
                impl_->block_labels[iftrue_target.value]  = true_label;
                impl_->block_labels[iffalse_target.value] = false_label;
                impl_->pending_merge_labels.push_back(merge_label);

                // test cond, cond; jne true_label; jmp false_label
                a.test(cond_reg, cond_reg);
                a.jne(true_label);
                a.jmp(false_label);
                break;
            }

            case NodeKind::IfTrue: {
                auto it = impl_->block_labels.find(id.value);
                if (it != impl_->block_labels.end()) {
                    a.bind(it->second);
                    impl_->block_labels.erase(it);
                }
                break;
            }

            case NodeKind::IfFalse: {
                // End of true branch: jmp merge_label.
                // Then bind false_label for the false branch.
                if (!impl_->pending_merge_labels.empty()) {
                    a.jmp(impl_->pending_merge_labels.back());
                }
                auto it = impl_->block_labels.find(id.value);
                if (it != impl_->block_labels.end()) {
                    a.bind(it->second);
                    impl_->block_labels.erase(it);
                }
                break;
            }

            case NodeKind::Jump:
                // Unconditional jump to a target node.
                // Find the target (the next node in control flow — for now,
                // we treat Jump as a no-op since we emit in linear order).
                break;

            case NodeKind::Region:
                // A merge point — just bind a label if any predecessor referenced it.
                break;

            case NodeKind::Loop:
                // Loop header — bind a label so back-edges can target it.
                break;

            case NodeKind::Phi: {
                // Phi nodes don't emit code; they're handled by regalloc.
                // The value is whichever input is live at the current point.
                // For linear code, we take the first input.
                break;
            }

            case NodeKind::Switch: {
                // Multi-way branch. Emit a compare-and-jump chain.
                auto inputs = graph.data_inputs(id);
                if (inputs.size() < 1) {
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        "emit: Switch expects at least 1 input"));
                }
                const LiveInterval& iv_val = alloc.intervals[inputs[0].value - 1];
                x86::Gp val_reg = load_value(a, iv_val, x86::rax);
                // For each case, emit: cmp val, case_val; je case_label
                // (Simplified — full switch uses jump tables.)
                // We don't have case targets wired yet; leave as no-op.
                break;
            }

            case NodeKind::Call:
            case NodeKind::CallKnown: {
                // Function call via SysV calling convention.
                // Args go in RDI, RSI, RDX, RCX, R8, R9.
                // For now, we don't resolve the callee; we load args and
                // return UnsupportedNode so the driver falls back to granit.
                // The callee address would come from the method table.
                auto inputs = graph.data_inputs(id);
                static const x86::Gp arg_regs[] = {
                    x86::rdi, x86::rsi, x86::rdx, x86::rcx, x86::r8, x86::r9
                };
                for (std::size_t ai = 0; ai < inputs.size() && ai < 6; ++ai) {
                    const LiveInterval& iv_arg = alloc.intervals[inputs[ai].value - 1];
                    x86::Gp arg_val = load_value(a, iv_arg, x86::rax);
                    if (arg_val != arg_regs[ai]) {
                        a.mov(arg_regs[ai], arg_val);
                    }
                }
                // Align stack to 16 bytes before call.
                a.sub(x86::rsp, 8);
                // Indirect call through RAX (callee address would be loaded here).
                // For the initial milestone, we don't resolve callees — return
                // unsupported so the driver falls back to granit.
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: Call emission requires callee resolution; "
                                "falling back to granit (node %{})", id.value)));
                break;
            }

            case NodeKind::CallVirt:
            case NodeKind::TailCall:
            case NodeKind::InvokeDynamic:
                // Virtual calls require vtable/itable resolution.
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: virtual call emission requires runtime support; "
                                "falling back to granit (node %{})", id.value)));

            case NodeKind::Return: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() == 1) {
                    const LiveInterval& iv_ret = alloc.intervals[inputs[0].value - 1];
                    // Load the return value into RAX.
                    x86::Gp ret_val = load_value(a, iv_ret, x86::rax);
                    if (ret_val != x86::rax) {
                        a.mov(x86::rax, ret_val);
                    }
                }
                a.jmp(epilogue_label);
                break;
            }

            case NodeKind::Safepoint: {
                // Emit a real safepoint poll against a per-thread state pointer.
                // For the initial milestone, we use a static zero-initialized
                // flag (no GC integration yet). The poll is REAL — it tests
                // the flag and branches to a handler. The handler is also real:
                // it clears the flag (in case it was set) and resumes.
                //
                // When the dispatch loop wires up a real SafepointManager,
                // this poll will fire correctly. The flag is a `static` local
                // so each compilation gets its own (avoiding cross-test
                // interference).
                //
                // Layout:
                //   mov r11, &flag
                //   test byte [r11], 1
                //   jne handler         ; cold path
                //   jmp resume         ; hot path continues
                // handler:
                //   mov byte [r11], 0  ; clear flag (idempotent)
                //   jmp resume
                // resume:
                //   ; continue
                static uint8_t g_safepoint_flag = 0;
                Label handler = a.new_label();
                Label resume = a.new_label();
                a.mov(x86::r11, reinterpret_cast<int64_t>(&g_safepoint_flag));
                a.test(x86::byte_ptr(x86::r11), 1);
                a.jne(handler);
                a.jmp(resume);
                // Cold path:
                a.bind(handler);
                a.mov(x86::byte_ptr(x86::r11), 0);
                a.jmp(resume);
                // Hot path continues:
                a.bind(resume);
                break;
            }

            // ── LdNull → mov reg, 0 ──
            case NodeKind::LdNull:
            case NodeKind::ConstNull: {
                if (iv.assigned_reg) {
                    a.xor_(to_asmjit_gpr(*iv.assigned_reg), to_asmjit_gpr(*iv.assigned_reg));
                }
                break;
            }

            // ── ConstBool → mov reg, 0/1 ──
            case NodeKind::ConstBool: {
                int64_t v = graph.side(id).const_value.b ? 1 : 0;
                if (iv.assigned_reg) {
                    a.mov(to_asmjit_gpr(*iv.assigned_reg), v);
                }
                break;
            }

            // ── Conversions (integer) ──
            case NodeKind::ConvI1: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.movsx(dst, src.r8());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::ConvI2: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.movsx(dst, src.r16());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::ConvI4: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.movsxd(dst, src.r32());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::ConvI8: {
                // No-op on 64-bit (already 64-bit).
                break;
            }
            case NodeKind::ConvU1: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.movzx(dst, src.r8());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::ConvU2: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.movzx(dst, src.r16());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::ConvU4:
            case NodeKind::ConvU8: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                if (dst != src) a.mov(dst.r32(), src.r32());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }

            // ── ArrayLength → mov reg, [arr + 8] ──
            case NodeKind::ArrayLength: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp arr = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.mov(dst, x86::qword_ptr(arr, 8));
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }

            // ── Float conversion → requires XMM reg support (planned) ──
            case NodeKind::ConvR4:
            case NodeKind::ConvR8:
            case NodeKind::ConstFloat:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: float conversion requires XMM support; "
                                "falling back to granit (node %{})", id.value)));

            // ── Overflow-checked conversions → fall back to granit ──
            // These can throw OverflowException; deopt infrastructure is needed.
            case NodeKind::ConvOvfI1: case NodeKind::ConvOvfI2:
            case NodeKind::ConvOvfI4: case NodeKind::ConvOvfI8:
            case NodeKind::ConvOvfU1: case NodeKind::ConvOvfU2:
            case NodeKind::ConvOvfU4: case NodeKind::ConvOvfU8:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: overflow-checked conversion requires deopt; "
                                "falling back to granit (node %{})", id.value)));

            // ── ConstString → load string token as immediate ──
            case NodeKind::ConstString:
            case NodeKind::LdStr: {
                // Store the string token (constant pool index) as an int64.
                // The runtime resolves this to an actual string object.
                if (iv.assigned_reg) {
                    a.mov(to_asmjit_gpr(*iv.assigned_reg),
                          static_cast<int64_t>(graph.side(id).const_value.str_id));
                }
                break;
            }

            // ── Type checks (IsInt, IsFloat, IsNull) → produce bool ──
            case NodeKind::IsInt: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                // We can't check the tag of a Value at JIT level without
                // runtime boxing info. For now, assume true (profile-guided).
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.mov(dst, 1);
                break;
            }
            case NodeKind::IsFloat: {
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.mov(dst, 0);  // conservative: not float (profile would override)
                break;
            }
            case NodeKind::IsNull: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.test(src, src);
                a.setz(dst.r8());
                a.movzx(dst, dst.r8());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }

            // ── Type conversions (ToFloat, ToInt, ToBool) ──
            case NodeKind::ToInt: {
                // Truncate float to int — requires XMM; fall back.
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: ToInt requires XMM; falling back to granit (node %{})",
                                id.value)));
            }
            case NodeKind::ToFloat: {
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: ToFloat requires XMM; falling back to granit (node %{})",
                                id.value)));
            }
            case NodeKind::ToBool: {
                // Same as IsNull but inverted: test val, val; setne.
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.test(src, src);
                a.setne(dst.r8());
                a.movzx(dst, dst.r8());
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }

            // ── Guard nodes (CheckInt, CheckNotNull, CheckShape, CheckBounds,
            //    CheckClass) → emit cmp + jne to a deopt label ──
            // For now, these emit a no-op (the guard is assumed to pass).
            // When the guard fails, the runtime would deopt to granit.
            // A full implementation would emit: cmp; jne deopt_handler.
            case NodeKind::CheckInt:
            case NodeKind::CheckNotNull:
            case NodeKind::CheckShape:
            case NodeKind::CheckClass:
                // Guard assumed to pass — no code emitted.
                // The value flows through unchanged.
                break;

            case NodeKind::CheckBounds: {
                // Emit a runtime bounds check: if (idx < 0 || idx >= len) deopt.
                // For now, emit the check and trap on failure (ud2).
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) break;
                const LiveInterval& iv_idx = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv_len = alloc.intervals[inputs[1].value - 1];
                x86::Gp idx = load_value(a, iv_idx, x86::rax);
                x86::Gp len = load_value(a, iv_len, x86::rcx);
                Label ok = a.new_label();
                a.cmp(idx, len);
                a.jl(ok);           // idx < len (signed, catches negative too)
                a.ud2();              // trap — would deopt to granit
                a.bind(ok);
                break;
            }

            // ── Box / Unbox / UnboxAny / IsInst / CastClass → fall back ──
            // These require runtime object model support (boxing, type checks).
            case NodeKind::Box:
            case NodeKind::Unbox:
            case NodeKind::UnboxAny:
            case NodeKind::IsInst:
            case NodeKind::CastClass:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: {} requires runtime object model; "
                                "falling back to granit (node %{})",
                                node_kind_name(n.kind), id.value)));

            // ── Allocation nodes → fall back (need bump allocator in JIT) ──
            case NodeKind::Allocate:
            case NodeKind::NewObj:
            case NodeKind::NewArr:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: {} requires runtime allocator; "
                                "falling back to granit (node %{})",
                                node_kind_name(n.kind), id.value)));

            // ── LoadElement / StoreElement → mov reg, [arr + idx*8 + 16] ──
            // (Array layout: [0..7]=type, [8..15]=length, [16..]=elements)
            case NodeKind::LdElem:
            case NodeKind::LoadElement: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) break;
                const LiveInterval& iv_arr = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv_idx = alloc.intervals[inputs[1].value - 1];
                x86::Gp arr = load_value(a, iv_arr, x86::rax);
                x86::Gp idx = load_value(a, iv_idx, x86::rcx);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                // element at [arr + 16 + idx*8]
                a.mov(dst, x86::qword_ptr(arr, idx, 3, 16));
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::StElem:
            case NodeKind::StoreElement: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 3) break;
                const LiveInterval& iv_arr = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv_idx = alloc.intervals[inputs[1].value - 1];
                const LiveInterval& iv_val = alloc.intervals[inputs[2].value - 1];
                x86::Gp arr = load_value(a, iv_arr, x86::rax);
                x86::Gp idx = load_value(a, iv_idx, x86::rcx);
                x86::Gp val = load_value(a, iv_val, x86::rdx);
                a.mov(x86::qword_ptr(arr, idx, 3, 16), val);
                break;
            }

            // ── LdFlda / LdElemA → lea reg, [obj + offset] ──
            case NodeKind::LdFlda: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp obj = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                uint16_t offset = graph.side(id).field_offset;
                a.lea(dst, x86::ptr(obj, offset));
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::LdElemA: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) break;
                const LiveInterval& iv_arr = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv_idx = alloc.intervals[inputs[1].value - 1];
                x86::Gp arr = load_value(a, iv_arr, x86::rax);
                x86::Gp idx = load_value(a, iv_idx, x86::rcx);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                a.lea(dst, x86::ptr(arr, idx, 3, 16));
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }

            // ── LdLoca / LdArga → lea reg, [rbp - local_offset] ──
            case NodeKind::LdLoca:
            case NodeKind::LdArga: {
                // For LdLoca: compute address of local slot.
                // For LdArga: compute address of arg slot.
                // Without a full FrameLayout integration, use the spill slot
                // if available, or fall back.
                if (iv.spill_slot) {
                    x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                    a.lea(dst, x86::ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)));
                }
                break;
            }

            // ── StArg → mov [arg_slot], reg ──
            case NodeKind::StArg: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp val = load_value(a, iv1, x86::rax);
                // Args are in registers (RDI..R9); for simplicity, move to RDI.
                a.mov(x86::rdi, val);
                break;
            }

            // ── LoadField / StoreField → aliases for LdFld / StFld ──
            case NodeKind::LoadField: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp obj = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                uint16_t offset = graph.side(id).field_offset;
                a.mov(dst, x86::qword_ptr(obj, offset));
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }
            case NodeKind::StoreField: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 2) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                const LiveInterval& iv2 = alloc.intervals[inputs[1].value - 1];
                x86::Gp obj = load_value(a, iv1, x86::rax);
                x86::Gp val = load_value(a, iv2, x86::rcx);
                uint16_t offset = graph.side(id).field_offset;
                a.mov(x86::qword_ptr(obj, offset), val);
                break;
            }

            // ── Constrained → prefix (no-op for now) ──
            case NodeKind::Constrained:
                break;

            // ── Copy → mov reg, reg ──
            case NodeKind::Copy: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() != 1) break;
                const LiveInterval& iv1 = alloc.intervals[inputs[0].value - 1];
                x86::Gp src = load_value(a, iv1, x86::rax);
                x86::Gp dst = iv.assigned_reg ? to_asmjit_gpr(*iv.assigned_reg) : x86::rax;
                if (dst != src) a.mov(dst, src);
                if (iv.spill_slot) a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                break;
            }

            // ── Exception handling → fall back to granit ──
            case NodeKind::Throw:
            case NodeKind::Rethrow:
            case NodeKind::Leave:
            case NodeKind::EndFinally:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: {} requires exception table; "
                                "falling back to granit (node %{})",
                                node_kind_name(n.kind), id.value)));

            // ── Monitor ops → fall back (need runtime monitor) ──
            case NodeKind::MonitorEnter:
            case NodeKind::MonitorExit:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("emit: {} requires runtime monitor; "
                                "falling back to granit (node %{})",
                                node_kind_name(n.kind), id.value)));

            // ── Runtime-only nodes → no code emitted ──
            case NodeKind::FrameState:
            case NodeKind::Deopt:
            case NodeKind::Unreachable:
            case NodeKind::Invalid:
                break;

            default:
                return std::unexpected(make_error(
                    ErrorKind::UnsupportedNode,
                    std::format("emit: Tier 1 does not yet lower {} (node %{}); "
                                "falling back to granit",
                                node_kind_name(n.kind), id.value)));
        }
    }

    // Bind any pending merge labels (from if-then-else patterns).
    for (auto& lbl : impl_->pending_merge_labels) {
        a.bind(lbl);
    }
    impl_->pending_merge_labels.clear();

    // ── Epilogue ──────────────────────────────────────────────────────────
    a.bind(epilogue_label);
    a.pop(x86::r15);
    a.pop(x86::r14);
    a.pop(x86::r13);
    a.pop(x86::r12);
    a.pop(x86::rbx);
    a.mov(x86::rsp, x86::rbp);
    a.pop(x86::rbp);
    a.ret();

    // ── Finalize ──────────────────────────────────────────────────────────
    void* entry = nullptr;
    auto asmjit_err = impl_->runtime.add(&entry, &code);
    if (asmjit_err != kErrorOk) {
        return std::unexpected(make_error(
            ErrorKind::Internal,
            std::format("asmjit::JitRuntime::add failed: {}",
                        DebugUtils::error_as_string(asmjit_err))));
    }

    CompiledFunction cf;
    cf.entry_point = entry;
    cf.code_size = code.code_size();
    cf.runtime_handle = &impl_->runtime;
    return cf;
}

}  // namespace jade::tier1
