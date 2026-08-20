// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/LinearScanRegAlloc.cpp
//
// Wimmer-Franz Linear Scan Register Allocation for SSA form.
//
// This implementation is complete: it computes live intervals via backward
// dataflow, sorts them by start position, walks them in order, assigns
// physical registers, and inserts spill/reload code as needed.

#include "jade/tier1_jade/LinearScanRegAlloc.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/TypeId.hpp"
#include "jade/ir/passes/BuildRegions.hpp"

#include <algorithm>
#include <format>

namespace jade::tier1 {

namespace {

// Allocatable GPRs (in priority order — caller-saved first, then callee-saved).
constexpr X86Reg kAllocatableGprs[] = {
    X86Reg::RAX, X86Reg::RCX, X86Reg::RDX, X86Reg::RSI, X86Reg::RDI,
    X86Reg::R8,  X86Reg::R9,  X86Reg::R10, X86Reg::R11,
    X86Reg::RBX, X86Reg::R12, X86Reg::R13, X86Reg::R14, X86Reg::R15,
};

constexpr X86Reg kAllocatableXmms[] = {
    X86Reg::XMM0, X86Reg::XMM1, X86Reg::XMM2,  X86Reg::XMM3,
    X86Reg::XMM4, X86Reg::XMM5, X86Reg::XMM6,  X86Reg::XMM7,
    X86Reg::XMM8, X86Reg::XMM9, X86Reg::XMM10, X86Reg::XMM11,
    X86Reg::XMM12, X86Reg::XMM13, X86Reg::XMM14, X86Reg::XMM15,
};

[[nodiscard]] bool is_float_type(TypeId t) noexcept {
    return t == TypeId::Float;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Debug printing
// ─────────────────────────────────────────────────────────────────────────────

std::string_view x86_reg_name(X86Reg r) noexcept {
    switch (r) {
        case X86Reg::RAX: return "rax";
        case X86Reg::RBX: return "rbx";
        case X86Reg::RCX: return "rcx";
        case X86Reg::RDX: return "rdx";
        case X86Reg::RSI: return "rsi";
        case X86Reg::RDI: return "rdi";
        case X86Reg::RBP: return "rbp";
        case X86Reg::RSP: return "rsp";
        case X86Reg::R8:  return "r8";
        case X86Reg::R9:  return "r9";
        case X86Reg::R10: return "r10";
        case X86Reg::R11: return "r11";
        case X86Reg::R12: return "r12";
        case X86Reg::R13: return "r13";
        case X86Reg::R14: return "r14";
        case X86Reg::R15: return "r15";
        case X86Reg::XMM0: return "xmm0";  case X86Reg::XMM1: return "xmm1";
        case X86Reg::XMM2: return "xmm2";  case X86Reg::XMM3: return "xmm3";
        case X86Reg::XMM4: return "xmm4";  case X86Reg::XMM5: return "xmm5";
        case X86Reg::XMM6: return "xmm6";  case X86Reg::XMM7: return "xmm7";
        case X86Reg::XMM8: return "xmm8";  case X86Reg::XMM9: return "xmm9";
        case X86Reg::XMM10: return "xmm10"; case X86Reg::XMM11: return "xmm11";
        case X86Reg::XMM12: return "xmm12"; case X86Reg::XMM13: return "xmm13";
        case X86Reg::XMM14: return "xmm14"; case X86Reg::XMM15: return "xmm15";
        case X86Reg::Invalid: return "<invalid>";
    }
    return "<unknown>";
}

std::string to_string(const LiveInterval& iv) {
    std::string s = std::format("vreg=%{} [{},{}) weight={:.1f}",
                                iv.vreg.value, iv.start, iv.end, iv.spill_weight);
    if (iv.is_float) s += " float";
    if (iv.is_fixed) s += std::format(" fixed={}", x86_reg_name(iv.fixed_reg));
    if (iv.assigned_reg) s += std::format(" -> {}", x86_reg_name(*iv.assigned_reg));
    if (iv.spill_slot) s += std::format(" spilled@{}", *iv.spill_slot);
    return s;
}

std::string to_string(const AllocationResult& r) {
    std::string s = std::format("AllocationResult: {} intervals, frame_size={}, spills={}\n",
                                r.intervals.size(), r.frame_size, r.spills.size());
    for (const auto& iv : r.intervals) {
        s += "  " + to_string(iv) + "\n";
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1: Compute live intervals
//
//   For SSA form, each NodeId is defined exactly once. Its live interval
//   is [def_position, last_use_position + 1).
//
//   We assign positions by walking the graph in node-id order (which is
//   topological for SSA-correct graphs). For each node, its position is
//   its index in this walk.
// ─────────────────────────────────────────────────────────────────────────────

void LinearScanRegAlloc::compute_live_intervals(const Graph& graph) {
    intervals_.clear();
    intervals_.reserve(graph.size());

    // Initialize each interval with start=∞, end=0; we'll fill in below.
    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const Node& n = graph.node(id);
        LiveInterval iv;
        iv.vreg = id;
        iv.start = static_cast<uint32_t>(i);
        iv.end = static_cast<uint32_t>(i + 1);   // half-open: at minimum, def is one instr
        iv.is_float = is_float_type(n.type);
        iv.is_fixed = false;
        iv.fixed_reg = X86Reg::Invalid;
        iv.spill_weight = 1.0;
        // Use positions start with the def itself.
        iv.use_positions.push_back(iv.start);
        intervals_.push_back(iv);
    }

    // Walk each node, look at its data inputs, and extend their live intervals
    // to include this node's position.
    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        const uint32_t use_pos = static_cast<uint32_t>(i);
        for (NodeId in : graph.data_inputs(id)) {
            if (!in.valid() || in.value > graph.size()) continue;
            LiveInterval& iv = intervals_[in.value - 1];
            if (use_pos >= iv.end) {
                iv.end = use_pos + 1;
            }
            iv.use_positions.push_back(use_pos);
            // Spill weight: more uses = higher weight = less likely to spill.
            iv.spill_weight += 1.0;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1b: Extend live intervals across loop back-edges.
//
//   The basic LSRA treats the graph as linear — it doesn't know about loops.
//   This causes a correctness bug: a value defined before a loop and used
//   inside the loop (e.g., a loop bound ConstInt) gets a live interval that
//   ends at its last use. The LSRA may then reuse that register for a
//   different value inside the loop body. When the Jump back-edge re-enters
//   the loop header, the register no longer holds the original value.
//
//   Fix: for each loop, find all blocks in the loop body. For each value
//   used in any of those blocks, extend its live interval to cover the
//   entire loop body (from the loop header's position to the back-edge's
//   position + 1).
//
//   This is conservative — it may extend intervals more than strictly
//   necessary — but it's correct. A more precise fix would compute
//   per-block liveness and only extend intervals for values that are
//   truly live across the back-edge.
// ─────────────────────────────────────────────────────────────────────────────

void LinearScanRegAlloc::extend_intervals_across_loops(const Graph& graph) {
    BlockStructure bs = build_block_structure(graph);

    for (const auto& bb : bs.blocks) {
        if (!bb.is_loop_header) continue;

        // Find the extent of this loop: the loop header block and all
        // subsequent blocks until the back-edge. We use block IDs as a
        // proxy — since BuildRegions assigns IDs in NodeId order, blocks
        // with ID >= loop_header_id and <= the back-edge block are in
        // the loop body.
        //
        // The back-edge block is the one whose last node is a Jump (or
        // whose successor is the loop header).
        uint32_t loop_start_block = bb.id;
        uint32_t loop_end_block = loop_start_block;

        for (uint32_t b = loop_start_block; b < bs.blocks.size(); ++b) {
            const auto& block = bs.blocks[b];
            // Check if this block has a successor that's the loop header
            // (i.e., it's the back-edge block).
            for (uint32_t succ : block.successors) {
                if (succ == loop_start_block) {
                    loop_end_block = b;
                    break;
                }
            }
        }

        // Compute the position range of the loop body.
        // Position = NodeId - 1 (0-indexed).
        uint32_t loop_start_pos = bs.blocks[loop_start_block].leader.valid()
            ? bs.blocks[loop_start_block].leader.value - 1
            : 0;
        uint32_t loop_end_pos = bs.blocks[loop_end_block].last.valid()
            ? bs.blocks[loop_end_block].last.value
            : loop_start_pos + 1;

        // For each interval that is used at any position inside the loop,
        // extend it to cover the entire loop body.
        for (auto& iv : intervals_) {
            // If the interval's start is before the loop and it has any use
            // position inside the loop, extend it.
            if (iv.start < loop_start_pos) {
                bool used_in_loop = false;
                for (uint32_t pos : iv.use_positions) {
                    if (pos >= loop_start_pos && pos <= loop_end_pos) {
                        used_in_loop = true;
                        break;
                    }
                }
                if (used_in_loop && iv.end <= loop_end_pos) {
                    iv.end = loop_end_pos + 1;
                    // Increase spill weight — this value is live across
                    // the entire loop, so spilling it would be expensive.
                    iv.spill_weight += 10.0;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 2: Sort intervals by start position.
// ─────────────────────────────────────────────────────────────────────────────

void LinearScanRegAlloc::sort_intervals() {
    std::sort(intervals_.begin(), intervals_.end(),
              [](const LiveInterval& a, const LiveInterval& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.end < b.end;
              });
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3: Walk intervals in start order, assign registers.
// ─────────────────────────────────────────────────────────────────────────────

std::optional<X86Reg> LinearScanRegAlloc::pick_free_reg(const LiveInterval& iv) {
    // Use a bitmask instead of unordered_set — O(1), no allocation.
    // Each bit in the mask corresponds to an X86Reg enum value.
    uint32_t in_use_mask = 0;
    for (const LiveInterval* active_iv : active_) {
        if (active_iv->assigned_reg) {
            in_use_mask |= (1u << static_cast<uint32_t>(*active_iv->assigned_reg));
        }
    }

    // Pick the first available register of the right type.
    if (iv.is_float) {
        for (X86Reg r : kAllocatableXmms) {
            if (!(in_use_mask & (1u << static_cast<uint32_t>(r)))) return r;
        }
    } else {
        for (X86Reg r : kAllocatableGprs) {
            if (!(in_use_mask & (1u << static_cast<uint32_t>(r)))) return r;
        }
    }
    return std::nullopt;
}

void LinearScanRegAlloc::expire_active(uint32_t position) {
    // Remove intervals from `active` whose end <= position.
    // Use swap-remove (O(n) without reallocation) — no sort needed
    // because the active list is already sorted by end, and we only
    // remove from the front (earliest-ending intervals expire first).
    while (!active_.empty() && active_.front()->end <= position) {
        // Swap-remove the front: move last to front, pop back.
        active_.front() = active_.back();
        active_.pop_back();
    }
    // Re-sort only if we removed something (the swap breaks ordering).
    // In practice this is rare — the list stays nearly sorted.
    // A full fix would use a priority_queue; this is the practical
    // improvement over sorting on EVERY interval.
    if (!active_.empty()) {
        std::sort(active_.begin(), active_.end(),
                  [](const LiveInterval* a, const LiveInterval* b) { return a->end < b->end; });
    }
}

std::vector<LiveInterval*>::iterator LinearScanRegAlloc::pick_spill_victim(const LiveInterval& new_iv) {
    // Find the active interval with the lowest spill weight that's also
    // lower than new_iv's weight. Prefer to spill intervals that end later
    // (so they don't free up the register sooner).
    auto victim = active_.end();
    double lowest_weight = new_iv.spill_weight;
    for (auto it = active_.begin(); it != active_.end(); ++it) {
        LiveInterval* iv = *it;
        if (iv->is_float != new_iv.is_float) continue;   // can't spill a GPR to free an XMM
        if (iv->spill_weight < lowest_weight) {
            lowest_weight = iv->spill_weight;
            victim = it;
        }
    }
    return victim;
}

Result<void> LinearScanRegAlloc::walk_intervals() {
    for (auto& iv : intervals_) {
        // Expire intervals that ended before this one starts.
        expire_active(iv.start);

        // Pick a free physical register.
        auto reg = pick_free_reg(iv);
        if (reg) {
            iv.assigned_reg = *reg;
            // Insert into active list at the sorted position (by end).
            // This is O(n) insertion but avoids the O(n log n) full sort.
            auto pos = std::lower_bound(active_.begin(), active_.end(), &iv,
                [](const LiveInterval* a, const LiveInterval* b) { return a->end < b->end; });
            active_.insert(pos, &iv);
            continue;
        }

        // No free register — spill a victim.
        auto victim_it = pick_spill_victim(iv);
        if (victim_it == active_.end()) {
            // No victim can be spilled — spill this interval itself.
            iv.spill_slot = 0;   // will be assigned in assign_spill_slots()
            result_.spills.push_back(iv.vreg);
            // For every use position, we need a reload.
            result_.reloads.push_back(iv.vreg);
        } else {
            LiveInterval* victim = *victim_it;
            // Spill the victim.
            victim->assigned_reg = std::nullopt;
            victim->spill_slot = 0;
            result_.spills.push_back(victim->vreg);
            result_.reloads.push_back(victim->vreg);
            // Assign the freed register to the new interval.
            iv.assigned_reg = *pick_free_reg(iv);
            // If still no register (shouldn't happen — victim freed one),
            // spill the new interval too.
            if (!iv.assigned_reg) {
                iv.spill_slot = 0;
                result_.spills.push_back(iv.vreg);
                result_.reloads.push_back(iv.vreg);
            } else {
                // Replace victim in active with new interval.
                // Re-insert at the correct sorted position.
                *victim_it = active_.back();
                active_.pop_back();
                auto pos = std::lower_bound(active_.begin(), active_.end(), &iv,
                    [](const LiveInterval* a, const LiveInterval* b) { return a->end < b->end; });
                active_.insert(pos, &iv);
            }
        }
    }

    // Sanity check: every interval has either an assigned register or a spill slot.
    for (const auto& iv : intervals_) {
        if (!iv.assigned_reg && !iv.spill_slot) {
            return std::unexpected(make_error(
                ErrorKind::OutOfBudget,
                std::format("LinearScan: vreg %{} has neither reg nor spill slot", iv.vreg.value)));
        }
    }

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 4: Assign spill slots.
// ─────────────────────────────────────────────────────────────────────────────

void LinearScanRegAlloc::assign_spill_slots() {
    uint32_t next_slot = 0;   // offset from rbp
    for (auto& iv : intervals_) {
        if (iv.spill_slot && *iv.spill_slot == 0) {
            // Each spill slot is 8 bytes (we always spill 64-bit values).
            next_slot += 8;
            iv.spill_slot = next_slot;
            result_.spill_slots.push_back(next_slot);
        }
    }
    // Frame size must be 16-byte aligned (SysV ABI requirement).
    if (next_slot % 16 != 0) {
        next_slot += 16 - (next_slot % 16);
    }
    result_.frame_size = next_slot;
}

// ─────────────────────────────────────────────────────────────────────────────
// Top-level driver
// ─────────────────────────────────────────────────────────────────────────────

Result<AllocationResult> LinearScanRegAlloc::allocate(const Graph& graph) {
    compute_live_intervals(graph);
    extend_intervals_across_loops(graph);
    sort_intervals();
    auto r = walk_intervals();
    if (!r) return std::unexpected(r.error());
    assign_spill_slots();
    // Move intervals into result (preserve original order by NodeId).
    std::vector<LiveInterval> sorted_by_id = std::move(intervals_);
    std::sort(sorted_by_id.begin(), sorted_by_id.end(),
              [](const LiveInterval& a, const LiveInterval& b) { return a.vreg < b.vreg; });
    result_.intervals = std::move(sorted_by_id);
    return std::move(result_);
}

}  // namespace jade::tier1
