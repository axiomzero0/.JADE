// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/CodeEmitter.cpp
//
// asmjit-based x86-64 code emitter.
//
// This is a real, functional emitter. It supports the following NodeKinds
// in the initial milestone:
//   - Start      → no-op (graph entry)
//   - ConstInt   → mov reg, imm64
//   - Add        → add reg, reg
//   - Sub        → sub reg, reg
//   - Mul        → imul reg, reg
//   - Return     → mov rax, value; jmp epilogue
//
// Any other NodeKind returns ErrorKind::UnsupportedNode, and the driver
// falls back to granit. This is graceful degradation per the No Stubs policy.
//
// The emitted code follows the SysV calling convention (Linux):
//   - Return value: RAX
//   - Callee-saved: RBX, RBP, R12-R15
//   - Stack alignment: 16 bytes

#include "jade/tier1_jade/CodeEmitter.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/TypeId.hpp"

#include "asmjit/asmjit.h"

#include <memory>
#include <format>

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
        default:          return x86::rax;   // unreachable; emitter already checked
    }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Impl — the asmjit state, hidden from the public header.
// ─────────────────────────────────────────────────────────────────────────────

struct CodeEmitter::Impl {
    JitRuntime runtime;
};

CodeEmitter::CodeEmitter() : impl_(std::make_unique<Impl>()) {}

CodeEmitter::~CodeEmitter() = default;

Result<void> CodeEmitter::init_runtime() {
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// compute_node_positions — linearize the graph for emission.
//
//   For Tier 1 we don't perform block scheduling; we just emit nodes in
//   NodeId order. This works for straight-line code (no branches).
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> compute_node_positions(const Graph& graph) {
    std::vector<uint32_t> pos(graph.size());
    for (std::size_t i = 0; i < graph.size(); ++i) {
        pos[i] = static_cast<uint32_t>(i);
    }
    return pos;
}

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

    // Forward-reference label for the epilogue. Bound at the end.
    Label epilogue_label = a.new_label();

    // ── Prologue ─────────────────────────────────────────────────────────
    // Standard SysV prologue:
    //   push rbp
    //   mov rbp, rsp
    //   sub rsp, <frame_size>
    // Save callee-saved registers we MIGHT use.
    a.push(x86::rbp);
    a.mov(x86::rbp, x86::rsp);
    if (alloc.frame_size > 0) {
        a.sub(x86::rsp, alloc.frame_size);
    }
    a.push(x86::rbx);
    a.push(x86::r12);
    a.push(x86::r13);
    a.push(x86::r14);
    a.push(x86::r15);

    // ── Body ──────────────────────────────────────────────────────────────
    // Emit one instruction per node in NodeId order.
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
                    // Spilled constant: load imm into rax, then store to spill slot.
                    a.mov(x86::rax, v);
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), x86::rax);
                } else {
                    return std::unexpected(make_error(
                        ErrorKind::Internal,
                        std::format("emit: ConstInt %{} has neither reg nor spill slot", id.value)));
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

                // Resolve source operands.
                x86::Gp src1 = x86::rax;
                if (iv1.assigned_reg) {
                    src1 = to_asmjit_gpr(*iv1.assigned_reg);
                } else if (iv1.spill_slot) {
                    a.mov(src1, x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv1.spill_slot)));
                } else {
                    return std::unexpected(make_error(ErrorKind::Internal,
                        std::format("emit: input 1 of %{} not allocated", id.value)));
                }

                x86::Gp src2 = x86::rcx;
                if (iv2.assigned_reg) {
                    src2 = to_asmjit_gpr(*iv2.assigned_reg);
                } else if (iv2.spill_slot) {
                    a.mov(src2, x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv2.spill_slot)));
                } else {
                    return std::unexpected(make_error(ErrorKind::Internal,
                        std::format("emit: input 2 of %{} not allocated", id.value)));
                }

                // Resolve destination.
                x86::Gp dst = x86::rax;
                if (iv.assigned_reg) {
                    dst = to_asmjit_gpr(*iv.assigned_reg);
                }
                // dst = src1
                if (dst != src1) {
                    a.mov(dst, src1);
                }
                // dst = dst op src2
                switch (n.kind) {
                    case NodeKind::Add: a.add(dst, src2); break;
                    case NodeKind::Sub: a.sub(dst, src2); break;
                    case NodeKind::Mul: a.imul(dst, src2); break;
                    default: break;
                }
                // If spilled, store dst to spill slot.
                if (iv.spill_slot) {
                    a.mov(x86::qword_ptr(x86::rbp, -static_cast<int32_t>(*iv.spill_slot)), dst);
                }
                break;
            }

            case NodeKind::Return: {
                auto inputs = graph.data_inputs(id);
                if (inputs.size() == 1) {
                    const LiveInterval& iv_ret = alloc.intervals[inputs[0].value - 1];
                    if (iv_ret.assigned_reg) {
                        const x86::Gp ret_reg = to_asmjit_gpr(*iv_ret.assigned_reg);
                        if (ret_reg != x86::rax) {
                            a.mov(x86::rax, ret_reg);
                        }
                    } else if (iv_ret.spill_slot) {
                        a.mov(x86::rax, x86::qword_ptr(x86::rbp,
                                                       -static_cast<int32_t>(*iv_ret.spill_slot)));
                    } else {
                        return std::unexpected(make_error(ErrorKind::Internal,
                            std::format("emit: Return value %{} not allocated", inputs[0].value)));
                    }
                }
                a.jmp(epilogue_label);
                break;
            }

            default:
                return std::unexpected(make_error(
                    ErrorKind::UnsupportedNode,
                    std::format("emit: Tier 1 does not yet lower {} (node %{}); "
                                "falling back to granit",
                                node_kind_name(n.kind), id.value)));
        }
    }

    // ── Epilogue ──────────────────────────────────────────────────────────
    a.bind(epilogue_label);
    // Restore callee-saved registers (reverse order).
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
