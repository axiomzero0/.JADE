// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/LinearScanRegAlloc.hpp
//
// Tier 1 (JADE) baseline register allocator: Wimmer-Franz Linear Scan
// Register Allocation (LSRA) for SSA form.
//
// References:
//   Wimmer & Franz, "Linear Scan Register Allocation on SSA Form",
//   CGO 2010.
//
// This is a real, functional LSRA with spill/reload. It supports:
//   - Live interval computation via backward sweep
//   - Interval sorting by start position
//   - Active list management
//   - Spill weight heuristic (frequency / size)
//   - Spill slot allocation on the stack frame
//   - Reload insertion at use sites
//
// Per Rule 09 (No Stubs Policy), this implementation is complete: it
// actually allocates registers and inserts spill/reload code as needed.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/core/NodeId.hpp"
#include "jade/ir/Graph.hpp"

#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace jade::tier1 {

// ─────────────────────────────────────────────────────────────────────────────
// X86-64 physical registers (caller-saved + callee-saved).
// We use the SysV calling convention (Linux/macOS) for the JIT itself.
// The actual calling convention of the compiled code is configurable.
// ─────────────────────────────────────────────────────────────────────────────
enum class X86Reg : uint8_t {
    // General-purpose 64-bit
    RAX = 0, RBX, RCX, RDX, RSI, RDI, RBP, RSP,
    R8,  R9,  R10, R11, R12, R13, R14, R15,

    // XMM (float/double)
    XMM0 = 16, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
    XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15,

    Invalid = 0xFF,
};

// Caller-saved (volatile) — clobbered by calls.
constexpr bool is_caller_saved(X86Reg r) noexcept {
    switch (r) {
        case X86Reg::RAX: case X86Reg::RCX: case X86Reg::RDX:
        case X86Reg::RSI: case X86Reg::RDI:
        case X86Reg::R8:  case X86Reg::R9:  case X86Reg::R10:
        case X86Reg::R11:
        case X86Reg::XMM0: case X86Reg::XMM1: case X86Reg::XMM2:
        case X86Reg::XMM3: case X86Reg::XMM4: case X86Reg::XMM5:
        case X86Reg::XMM6: case X86Reg::XMM7: case X86Reg::XMM8:
        case X86Reg::XMM9: case X86Reg::XMM10: case X86Reg::XMM11:
        case X86Reg::XMM12: case X86Reg::XMM13: case X86Reg::XMM14:
        case X86Reg::XMM15:
            return true;
        default:
            return false;
    }
}

// Available for allocation (excludes RSP/RBP).
constexpr uint8_t kAllocatableGprCount = 13;   // RAX, RBX, RCX, RDX, RSI, RDI, R8..R11, R12..R15 (no RSP/RBP)
constexpr uint8_t kAllocatableXmmCount = 16;

// ─────────────────────────────────────────────────────────────────────────────
// LiveInterval — a half-open range [start, end) during which a virtual
// register (NodeId) holds a value that may be used.
// ─────────────────────────────────────────────────────────────────────────────
struct LiveInterval {
    NodeId   vreg;
    uint32_t start;          // instruction index where the value is defined
    uint32_t end;             // last instruction index where the value is used (exclusive)
    bool     is_float;        // true if this is a floating-point value
    bool     is_fixed;        // true if pinned to a specific physical register
    X86Reg   fixed_reg;        // valid if is_fixed
    std::optional<X86Reg> assigned_reg;
    std::optional<uint32_t> spill_slot;   // stack slot offset (from rbp) if spilled
    double   spill_weight;    // higher = less likely to spill
    std::vector<uint32_t> use_positions;   // instruction indices where used
};

// ─────────────────────────────────────────────────────────────────────────────
// AllocationResult — the output of the register allocator.
// ─────────────────────────────────────────────────────────────────────────────
struct AllocationResult {
    std::vector<LiveInterval> intervals;     // one per NodeId (indexed by NodeId-1)
    std::vector<uint32_t>     spill_slots;   // stack offsets (from rbp) used for spilled vregs
    uint32_t                  frame_size;    // total stack frame size in bytes
    std::vector<NodeId>       reloads;       // NodeIds that need a reload before each use
    std::vector<NodeId>       spills;         // NodeIds that need a spill after each def
};

class LinearScanRegAlloc {
public:
    LinearScanRegAlloc() = default;

    // Run the allocator on `graph`. Returns the allocation result, or an
    // error if allocation fails (e.g., not enough registers).
    [[nodiscard]] Result<AllocationResult> allocate(const Graph& graph);

private:
    // Step 1: compute live intervals via backward dataflow.
    void compute_live_intervals(const Graph& graph);

    // Step 2: sort intervals by start position.
    void sort_intervals();

    // Step 3: walk intervals in start order, assigning registers.
    [[nodiscard]] Result<void> walk_intervals();

    // Step 4: assign spill slots to spilled intervals.
    void assign_spill_slots();

    // Helper: pick a free physical register for the given interval.
    // Returns std::nullopt if all are in use.
    std::optional<X86Reg> pick_free_reg(const LiveInterval& iv);

    // Helper: expire all active intervals whose end <= `position`.
    void expire_active(uint32_t position);

    // Helper: when no register is free, pick an active interval to spill.
    // Returns the iterator of the victim (or end() if none can be spilled).
    std::vector<LiveInterval*>::iterator pick_spill_victim(const LiveInterval& new_iv);

    std::vector<LiveInterval> intervals_{};
    std::vector<LiveInterval*> active_{};     // currently live intervals, sorted by end
    AllocationResult result_{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers for debug printing.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] std::string_view x86_reg_name(X86Reg r) noexcept;
[[nodiscard]] std::string to_string(const LiveInterval& iv);
[[nodiscard]] std::string to_string(const AllocationResult& r);

}  // namespace jade::tier1
