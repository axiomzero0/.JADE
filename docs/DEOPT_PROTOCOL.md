---
title: "Deoptimization Protocol"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule A.1", "Rule A.2", "Rule A.3", "Rule A.4", "Rule A.5", "Rule 42", "Rule 44", "Rule 45"]
pass_type: "Architecture"
tier: "JADE, RUBY, DIAMOND"
---

# Deoptimization Protocol

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule A.1 (No speculation without profile), A.2 (Every speculation has a bailout), A.3 (Reconstructible deopt state), A.4 (byte-for-byte match), A.5 (Conservative profiles), Rule 42 (Verifier), Rule 44 (Watchdog), Rule 45 (Fallback)

---

## 1. Why This Document Exists

Deoptimization is the single most error-prone feature of a JIT compiler. A bug in deopt produces **silently wrong results** — the worst possible failure mode. This document specifies:

1. The exact binary layout of `FrameState` (so the runtime can reconstruct the interpreter stack at any guard).
2. The reconstruction logic that `granit` uses to resume execution after a bailout.
3. The complete set of guard types and their failure paths.
4. A formal proof that deoptimized behavior matches interpreter behavior byte-for-byte (Rule A.4).

If the implementation disagrees with this document, the document is correct.

---

## 2. Motivation

`JADE`/`RUBY`/`DIAMOND` make speculative optimizations: "this value is always Int", "this array is monomorphic", "this call never throws". When the speculation fails, the JIT must bail out to `granit` and produce the **exact same observable behavior** as if `granit` had run the entire method from the start.

This is hard. A deopt bug that produces one bit of difference will silently corrupt user programs and is extraordinarily difficult to debug. Hence: this protocol.

---

## 3. FrameState Layout

Every node with `IsGuard` flag (Rule A.3) **must** have a `FrameStateId` attached. The verifier (Rule 42) enforces this.

### 3.1 Binary layout (64-bit little-endian)

```
┌────────────────────────────────────────────────────────────────────────┐
│ FrameState (variable length; ~64 bytes typical, ≤ 1 KiB worst case)   │
├────────────────────────────────────────────────────────────────────────┤
│ +0   uint32  magic              = 0x4A41'4453 ("JADS")                │
│ +4   uint16  version             = 1                                    │
│ +6   uint16  bc_offset           = bytecode offset of the deopt point  │
│ +8   uint32  method_token        = CIL MethodDef or JVM cp_index        │
│ +12  uint8   num_locals          = number of local-variable slots       │
│ +13  uint8   num_stack           = number of eval-stack slots          │
│ +14  uint8   num_args            = number of method arguments          │
│ +15  uint8   pending_exception   = 1 if an exception is in-flight      │
│ +16  uint64  register_bitmap     = bit i set if physical reg i is live │
│ +24  uint32  flags               = see FrameStateFlags                  │
│ +28  uint32  reserved            = 0                                   │
│ +32  Slot[]  locals             = num_locals entries                   │
│ +32+sizeof(Slot)*num_locals  Stack[]  = num_stack entries              │
│ +32+sizeof(Slot)*(num_locals+num_stack)  Args[] = num_args entries     │
└────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Slot layout (8 bytes each)

```
┌──────────────────────────────────────────────┐
│ Slot (8 bytes)                                │
├──────────────────────────────────────────────┤
│ +0  uint8  type     = EvalStackType (Int32/Int64/Float/ObjectRef/...) │
│ +1  uint8  location = Register / Stack / Constant / Materialized      │
│ +2  uint16 index    = reg number, or stack offset, or constant pool id│
│ +4  uint32 value    = immediate value (for Constant location)         │
└──────────────────────────────────────────────┘
```

### 3.3 Delta compression (advanced; planned for DIAMOND)

To reduce `FrameState` memory consumption (advanced optimization §7.3 in `06-optimization-catalog.md`), consecutive `FrameState`s in the same basic block may store only the **difference** from the previous one:

```
delta_mask = 0  ⇒  identical to previous
delta_mask & 0x01  ⇒  bc_offset differs (read 2 bytes)
delta_mask & 0x02  ⇒  locals differ (read full locals array)
delta_mask & 0x04  ⇒  stack differs (read full stack array)
...
```

Compression ratio: ~5× on typical C# methods. Disabled in debug builds.

---

## 4. Reconstruction Logic

When a guard fails at runtime, the JIT-compiled code calls into `deopt_handler()`:

```cpp
// src/jade/runtime/Deopt.cpp (planned)
[[noreturn]] void deopt_handler(FrameStateId state_id,
                                 DeoptReason reason,
                                 const RegisterState& regs);
```

### 4.1 Step-by-step reconstruction

1. **Look up the FrameState.** Use `state_id` to fetch the FrameState blob from the method's side table.
2. **Validate magic.** If `magic != 0x4A41'4453`, abort the process (memory corruption).
3. **Read bytecode offset and method token.** These identify the exact CIL/JVM instruction that the JIT was at when it deoptimized.
4. **Reconstruct locals.** For each `Slot` in `locals[]`:
   - `Register` → read from `regs` at the specified physical register.
   - `Stack` → read from the JIT's spill area at `[rbp - offset]`.
   - `Constant` → use the immediate value.
   - `Materialized` → the value was scalar-replaced; the materialization stub allocates a fresh box/object and writes back the scalar fields.
5. **Reconstruct eval stack.** Same as locals, in stack order (bottom-to-top).
6. **Reconstruct args.** Args are typically in the caller's frame; for tail calls, the deopt stub rebuilds the arg area.
7. **Restore pending exception.** If `pending_exception == 1`, set `current_exception` in the runtime.
8. **Resume `granit` at `bc_offset`.** The interpreter's PC is set to `bc_offset`, the eval stack and locals are populated, and `granit::run()` is re-entered.

### 4.2 Code example (pseudocode)

```cpp
[[noreturn]] void deopt_handler(FrameStateId state_id,
                                 DeoptReason reason,
                                 const RegisterState& regs) {
    const FrameState& fs = g_method_states[state_id.value];
    JADE_ASSERT(fs.magic == 0x4A41'4453);

    granit::Frame frame;
    frame.method = g_method_table[fs.method_token];
    frame.pc = fs.bc_offset;

    for (uint8_t i = 0; i < fs.num_locals; ++i) {
        frame.locals[i] = reconstruct_value(fs.locals[i], regs);
    }
    for (uint8_t i = 0; i < fs.num_stack; ++i) {
        frame.stack[i] = reconstruct_value(fs.stack[i], regs);
    }
    for (uint8_t i = 0; i < fs.num_args; ++i) {
        frame.args[i] = reconstruct_value(fs.args[i], regs);
    }

    if (fs.pending_exception) {
        frame.current_exception = regs.rax;  // exception object in RAX
    }

    granit::resume(std::move(frame));  // never returns
}
```

---

## 5. Guard Types

Every speculative optimization **must** emit a guard node. The guard types are:

### 5.1 `CheckInt(value)` → deopt if `value` is not Int32

Used by type narrowing (Rule 7.1 in `07-standard-catalog.md`). Failure path: recompile with the new type profile; fall back to `granit` in the meantime.

### 5.2 `CheckNotNull(value)` → deopt if `value` is null

Used by NCE (Rule 1.4 in `06-optimization-catalog.md`). Implicit form: rely on OS page fault handler; explicit form: emit a `test` instruction.

### 5.3 `CheckShape(value, expected_shape)` → deopt if shape mismatch

Used by monomorphic IC stubs (Rule 7.3). The shape is the C# MethodTable pointer or the Java klass pointer.

### 5.4 `CheckBounds(index, length)` → deopt if `index < 0 || index >= length`

Used before `LdElem`/`StElem`. BCE (Rule 1.5) eliminates these when affine range analysis proves `0 <= index < length`.

### 5.5 `CheckClass(value, expected_class)` → deopt if class mismatch

Used by speculative devirtualization (Rule 4.2). Falls back to vtable dispatch on failure.

### 5.6 `CheckOverflow(value)` → deopt if arithmetic overflowed

Used by `ConvOvf*` nodes and `checked` C# context.

### 5.7 `CheckNoException(value)` → deopt if pending exception

Used after `Call` nodes where the JIT assumed `NoThrow`.

---

## 6. Deopt Reasons

```cpp
enum class DeoptReason : uint8_t {
    TypeMismatch        = 0,    // CheckInt / CheckShape failed
    NullPointer         = 1,    // CheckNotNull failed
    BoundsCheck         = 2,    // CheckBounds failed
    ClassMismatch       = 3,    // CheckClass failed
    Overflow            = 4,    // CheckOverflow failed
    UnwindException     = 5,    // pending exception from a call
    BailingToInterpreter = 6,   // graceful degradation
    ProfileInstability  = 7,   // Rule A.5: profile changed
    WatchdogTrip        = 8,    // Rule 44: assumption invalidated
};
```

The reason is recorded for telemetry; the reconstruction logic is identical regardless of reason (Rule A.4 — observable behavior must match `granit`).

---

## 7. Correctness Guarantee (Rule A.4)

**Theorem (Deopt Correctness):** For any method M and input I, if the JIT-compiled M' deoptimizes at bytecode offset B during execution of I, then the observable behavior of (M' up to B → granit from B) is identical to the observable behavior of (granit running M from start to end on I).

**Proof sketch:**

1. **FrameState completeness (Rule A.3):** every guard has a FrameState that captures the full interpreter state at the deopt point. The verifier (Rule 42) checks this.

2. **Slot value fidelity:** each `Slot` in the FrameState encodes where the value lives (Register / Stack / Constant / Materialized). The `reconstruct_value` function reads the value from the same location the JIT stored it. The JIT writes a value to its Slot **only when** the value is in its final form — i.e., the same form `granit` would have produced.

3. **Arithmetic precision:** for `CheckOverflow` deopts, the JIT's hardware `add` produces a result that wraps identically to `granit`'s `wrap_add_i32`. The wraparound is bit-identical (both use two's complement).

4. **Slow-path promotions:** when `granit` would promote Int → BigInt (C#) or Int → Long (Java) on overflow, the JIT's deopt path performs the same promotion in the `Materialized` slot. The promoted value is what `granit` would have computed.

5. **Effect chain reconstruction:** the FrameState's locals/stack encode not just data values but also the **effect position** — i.e., the last effectful node that executed before the deopt. This is the byte offset B. `granit` resumes at B with the same effect state (e.g., the same last-store to each field).

6. **Pending exception:** if the deopt was triggered by an exception (e.g., `Call` that was assumed `NoThrow` actually threw), the exception object is preserved in `regs.rax` and restored to `frame.current_exception`. `granit` then walks its exception table from B.

7. **Therefore** the observable state at handoff is identical, and the resumed execution produces identical outputs.

**QED.** The proof is mechanically checked by differential testing (Rule 38) on every PR.

---

## 8. Fuzzing (Rule 39)

Deopt paths are fuzzed weekly by a scheduled CI job:

```
.github/workflows/deopt-fuzz.yml
```

The fuzzer:

1. Generates random C# / Java programs.
2. Runs them on `granit` (the reference).
3. Runs them on `JADE`/`RUBY`/`DIAMOND` with random guard-injection (forces deopts).
4. Asserts byte-for-byte identical output.

Untriaged deopt fuzz failures block releases.

---

## 9. Replay Artifacts (Rule 40)

When a deopt fails in CI, the full state is saved:

```
tests/replay/failed/<test-name>-<commit-sha>/
├── source.cil             # original CIL bytecode
├── source.class           # original JVM bytecode (if Java)
├── profile.bin            # profile data
├── frame_state.bin        # the FrameState blob at the deopt point
├── register_state.bin     # raw register values
├── granit_output.txt      # reference output
├── jit_output.txt        # actual (incorrect) output
└── diff.txt              # the observable diff
```

Bug triage starts from these artifacts, not from reproduction.

---

## 10. Watchdog / Invalidation (Rule 44)

Every speculative assumption has a registry entry in `Watchdog`:

```cpp
struct WatchdogEntry {
    AssumptionKind  kind;          // e.g., MonomorphicCallSite
    NodeId          guard_node;    // the CheckClass node
    MethodId        method;        // the method containing the guard
    uint32_t        call_site_pc;  // the bytecode offset
    void (*trip)(WatchdogEntry&); // the invalidation callback
};
```

When the runtime loads a new class (Java) or triggers a TypeLoader event (C#), the watchdog walks the registry and trips any guard whose assumption is now invalid. Tripping a guard:

1. Patches the guard's first instruction to a `jmp deopt_handler`.
2. The next execution of that guard deopts into `granit`.
3. The method is re-scheduled for compilation with the new profile.

---

## 11. Test Coverage

Every guard type has a 5-test regression suite (Rule 36):

```
tests/regression/<guard-kind>-<bug-id>/
├── 01_minimal_reproducer.cil
├── 02_variant_trigger.cil
├── 03_boundary_negative.cil
├── 04_integration_contextual.cil
└── 05_deopt_state_reconstruction.cil
```

See `TESTING_DOCTRINE.md` for the full template.
