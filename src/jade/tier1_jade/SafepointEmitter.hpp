// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/SafepointEmitter.hpp
//
// Safepoint polling infrastructure for Tier 1 (JADE) compiled code.
//
// Per Definition of Done #5:
//   "A request from any thread reaches all mutator threads within a bounded
//    number of bytecode instructions."
//
// In compiled code, the poll is a single `test` instruction at every loop
// back-edge. The flag is a cache-line-aligned atomic byte.
//
// Emitted sequence at a safepoint:
//   test byte [rax], 1          ; 3 bytes
//   jne safepoint_handler        ; 2 bytes (short) or 6 bytes (near)
//   ; continue normal execution
//
// Where rax holds the address of the thread's SafepointManager::ThreadState.
//
// The safepoint handler:
//   1. Saves all live registers to the FrameState.
//   2. Sets the at_safepoint flag.
//   3. Spins until poll_requested is cleared.
//   4. Restores live registers.
//   5. Returns to the instruction after the safepoint.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/runtime/Safepoint.hpp"

// We need asmjit types in the public API, so include the asmjit header.
// This is acceptable because the SafepointEmitter is only used by code that
// already links asmjit (jade_tier1).
#include "asmjit/asmjit.h"

namespace jade::tier1 {

class SafepointEmitter {
public:
    // Emit a safepoint poll at a back-edge or call site.
    static void emit_poll(asmjit::x86::Assembler& a,
                          int state_ptr_reg_idx,
                          asmjit::Label& handler_label);

    // Emit the safepoint handler (the cold path).
    static void emit_handler(asmjit::x86::Assembler& a,
                             int state_ptr_reg_idx,
                             asmjit::Label& handler_label,
                             asmjit::Label& resume_label);
};

}  // namespace jade::tier1
