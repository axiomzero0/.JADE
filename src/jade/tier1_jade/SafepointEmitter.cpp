// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/SafepointEmitter.cpp
//
// asmjit-based safepoint polling emission.

#include "jade/tier1_jade/SafepointEmitter.hpp"
#include "asmjit/asmjit.h"

namespace jade::tier1 {

using namespace asmjit;

void SafepointEmitter::emit_poll(x86::Assembler& a,
                                  int state_ptr_reg_idx,
                                  Label& handler_label) {
    // Per the SafepointManager::ThreadState struct in
    // src/jade/runtime/Safepoint.hpp, the `poll_requested` field is the
    // first member (offset 0). We test its low bit.
    //
    // Emitted code:
    //   test byte [reg], 1
    //   jne handler_label
    const x86::Gp state_ptr64 = x86::gpq(state_ptr_reg_idx);
    a.test(x86::byte_ptr(state_ptr64), 1);
    a.jne(handler_label);
}

void SafepointEmitter::emit_handler(x86::Assembler& a,
                                     int state_ptr_reg_idx,
                                     Label& handler_label,
                                     Label& resume_label) {
    a.bind(handler_label);

    // Save caller-saved registers (RAX, RCX, RDX, RSI, RDI, R8-R11) on the stack.
    a.push(x86::rax);
    a.push(x86::rcx);
    a.push(x86::rdx);
    a.push(x86::rsi);
    a.push(x86::rdi);
    a.push(x86::r8);
    a.push(x86::r9);
    a.push(x86::r10);
    a.push(x86::r11);

    // Set at_safepoint = true. at_safepoint is at offset 1 of ThreadState.
    const x86::Gp state_ptr = x86::gpq(state_ptr_reg_idx);
    a.mov(x86::byte_ptr(state_ptr, 1), 1);

    // Spin: wait until poll_requested is cleared.
    Label spin_loop = a.new_label();
    a.bind(spin_loop);
    a.mov(x86::al, x86::byte_ptr(state_ptr, 0));
    a.test(x86::al, x86::al);
    a.jne(spin_loop);

    // Clear at_safepoint.
    a.mov(x86::byte_ptr(state_ptr, 1), 0);

    // Restore caller-saved registers (reverse order).
    a.pop(x86::r11);
    a.pop(x86::r10);
    a.pop(x86::r9);
    a.pop(x86::r8);
    a.pop(x86::rdi);
    a.pop(x86::rsi);
    a.pop(x86::rdx);
    a.pop(x86::rcx);
    a.pop(x86::rax);

    a.jmp(resume_label);
}

}  // namespace jade::tier1
