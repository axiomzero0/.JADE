// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/FrameLayout.hpp
//
// Stack frame layout for Tier 1 (JADE) compiled functions.
//
// Layout (relative to rbp after prologue):
//
//   High addresses
//   ┌─────────────────────────────────┐
//   │ return address (8 bytes)         │  [rbp + 8]
//   ├─────────────────────────────────┤
//   │ saved rbp (8 bytes)              │  [rbp]
//   ├─────────────────────────────────┤
//   │ spill slot N (8 bytes)           │  [rbp - 8]
//   │ spill slot N-1                   │  [rbp - 16]
//   │ ...                              │
//   │ spill slot 1                     │  [rbp - 8*N]
//   ├─────────────────────────────────┤
//   │ local 0 (8 bytes)                │  [rbp - 8*N - 8]
//   │ local 1                          │  [rbp - 8*N - 16]
//   │ ...                              │
//   │ local M-1                        │  [rbp - 8*N - 8*M]
//   ├─────────────────────────────────┤
//   │ (16-byte alignment padding)      │
//   └─────────────────────────────────┘
//   Low addresses  ← rsp after prologue
//
// Function arguments (SysV calling convention):
//   Integer/pointer args: RDI, RSI, RDX, RCX, R8, R9, then stack
//   Floating-point args: XMM0..XMM7, then stack
//
// For Tier 1 we support up to 6 integer args (passed in registers).
// Args 7+ are passed on the stack at [rbp + 16], [rbp + 24], etc.

#pragma once

#include "jade/core/NodeId.hpp"
#include <cstdint>
#include <vector>
#include <optional>

namespace jade::tier1 {

// SysV calling convention argument registers (in order).
enum class ArgReg : uint8_t {
    RDI = 0,   // arg 0
    RSI = 1,   // arg 1
    RDX = 2,   // arg 2
    RCX = 3,   // arg 3
    R8  = 4,   // arg 4
    R9  = 5,   // arg 5
};

// FrameLayout describes the stack frame for one compiled function.
struct FrameLayout {
    // Offsets (from rbp, negative = below saved rbp).
    std::vector<int32_t> spill_slot_offsets;     // indexed by spill slot index
    std::vector<int32_t> local_offsets;          // indexed by local slot index

    // Total frame size in bytes (the value passed to `sub rsp, N`).
    // Always 16-byte aligned.
    uint32_t frame_size = 0;

    // Number of integer-args passed in registers.
    uint8_t num_reg_args = 0;

    // Compute the stack offset for a spill slot (1-indexed; spill_slot=0
    // means "unassigned").
    [[nodiscard]] std::optional<int32_t> spill_offset(uint32_t spill_slot) const noexcept {
        if (spill_slot == 0 || spill_slot > spill_slot_offsets.size()) return std::nullopt;
        return spill_slot_offsets[spill_slot - 1];
    }

    // Compute the stack offset for a local variable (0-indexed).
    [[nodiscard]] std::optional<int32_t> local_offset(uint32_t local_index) const noexcept {
        if (local_index >= local_offsets.size()) return std::nullopt;
        return local_offsets[local_index];
    }
};

// Build a FrameLayout given a spill-slot count and a local count.
// `num_spill_slots` is the number of unique spill slots (each 8 bytes).
// `num_locals` is the number of local variable slots (each 8 bytes).
// `num_reg_args` is the number of args passed in registers (0..6).
[[nodiscard]] FrameLayout build_frame_layout(uint32_t num_spill_slots,
                                              uint32_t num_locals,
                                              uint8_t num_reg_args);

}  // namespace jade::tier1
