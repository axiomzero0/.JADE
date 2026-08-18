---
title: ".JADE Architecture"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule B.1", "Rule B.2", "Rule B.3", "Rule B.4", "Rule C.1", "Rule C.2", "Rule C.3", "Rule C.4", "Rule 42", "Rule 51"]
pass_type: "Architecture"
tier: "All"
---

# .JADE Architecture

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule B.1 (Arena), B.2 (No exceptions), B.3 (No RTTI), B.4 (No shared_ptr in hot path), C.1–C.4 (Threading & Memory), Rule 42 (Verifier), Rule 51 (Bitmasks)

---

## 1. Why This Document Exists

`ARCHITECTURE.md` is the single source of truth for the .JADE compiler's structural design — the 4-tier pipeline, the memory model, the threading model, and the Sea-of-Nodes IR. Every contributor **must** read this document before modifying any code under `src/jade/`.

If the implementation disagrees with this document, the document is correct and the code is wrong (file an issue, fix the code).

---

## 2. High-Level Overview: The 4-Tier Pipeline

`.JADE` is a tiered, profile-driven JIT compiler. It targets **both C# (CIL bytecode) and Java (JVM bytecode)** with a unified Sea-of-Nodes IR. The same IR, the same passes, and the same backend serve both source languages.

```
              ┌──────────────┐
   CIL ─────► │              │
   (.dll)     │   granit    │  Tier 0 — Interpreter
              │  (T0, base)  │  Zero compilation latency, profile collection
   JVM ─────► │              │
   (.class)   └──────┬───────┘
                     │ after N invocations
                     ▼
              ┌──────────────┐
              │    JADE      │  Tier 1 — Baseline SSA JIT
              │   (T1)       │  Eliminate dispatch overhead in milliseconds
              └──────┬───────┘
                     │ after M invocations
                     ▼
              ┌──────────────┐
              │    RUBY      │  Tier 2 — Sea of Nodes Optimizing JIT
              │   (T2)       │  GVN, EA, LICM, BCE, GCM, OSR
              └──────┬───────┘
                     │ after K invocations + PGO stability
                     ▼
              ┌──────────────┐
              │   DIAMOND    │  Tier 3 — Peak AOT/JIT Hybrid Optimizer
              │   (T3)       │  PEA, SRA, SLP, vectorization, devirtualization
              └──────────────┘
```

### Tier roles

| Tier | Name | Role |
| :-- | :-- | :-- |
| T0 | `granit` | CIL/JVM interpreter. Reads PE/JAR files, parses metadata, executes bytecodes on a typed evaluation stack. Collects type feedback. Polls safepoints at back-edges. |
| T1 | `JADE` | Baseline SSA JIT. Lowers bytecode to flat SSA. Fast Linear-Scan register allocation. Monomorphic IC stubs for `callvirt` (C#) and `invokevirtual`/`invokeinterface` (Java). |
| T2 | `RUBY` | Sea of Nodes optimizing JIT. Full SoN IR with explicit effect chains. GVN, escape analysis, LICM, BCE, GCM, OSR. |
| T3 | `DIAMOND` | Peak AOT/JIT hybrid. PEA (box/unbox in C#, autoboxing in Java), SLP auto-vectorization, speculative devirtualization via CHA. WPD in AOT mode. |

### Escalation policy (Rule A.5)

- `granit → JADE` at N invocations (default 100).
- `JADE → RUBY` at M invocations (default 1000).
- `RUBY → DIAMOND` at K invocations (default 10000) + profile stability check (Rule 46).

A mutator thread that needs compiled code that isn't ready **must not block** (Rule C.1). It falls back to the next-lower tier.

---

## 3. Memory Model

### 3.1 Arena Allocation (Rule B.1)

Every `Node`, `BasicBlock`, and side-table entry is allocated from a thread-local `BumpAllocator`. `malloc`/`free` are **forbidden** in JIT hot paths (Rule B.1). There is a debug-mode hook (`ArenaLeakDetector`) that aborts the process if `malloc` is called from a compiler thread.

```cpp
// src/jade/core/Arena.hpp
class BumpAllocator {
public:
    [[nodiscard]] void* allocate(std::size_t n, std::size_t alignment);
    template <typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args);
    void reset() noexcept;   // bulk-free; keeps one chunk for reuse
};
```

- Allocations are bump-pointer; never individually freed.
- Chunks (default 64 KiB) grow on demand.
- All chunks are freed at once when the arena is destroyed or `reset()` is called.

### 3.2 Epoch-Based Reclamation (EBR, Rule C.4)

Cross-thread reclamation of retired IR nodes uses EBR, not locks. The `EpochGC` class coordinates:

```cpp
// src/jade/runtime/Epoch.hpp
class EpochGC {
public:
    uint32_t register_thread();
    Epoch     enter(uint32_t slot);                // mark thread as active in current epoch
    void      exit(uint32_t slot);                  // mark thread as idle
    void      retire(Epoch e, void* ptr, void(*deleter)(void*) noexcept);
    void      try_reclaim();                         // free retired nodes from past epochs
};
```

A node retired in epoch E can be freed once **every** compiler thread has advanced past E+1. The reclaim sweep runs at the end of every compilation.

### 3.3 GC Interaction

Mutator threads allocate from per-thread bump arenas (Rule C.2). Global GC synchronization happens **only at safepoints**. The SafepointManager toggles a per-thread atomic flag; mutator threads poll it at:

- every CIL/JVM back-edge,
- every method return,
- every OSR entry point.

The polling instruction in compiled code is a single `test byte [safepoint_flag], 1` followed by a `je` — 4 bytes total.

---

## 4. Threading Model

### 4.1 Work-Stealing Scheduler (enkiTS)

The compiler pool uses `enkiTS` (planned; see `third_party/enkiTS/`). Each compiler thread owns:

- one `Graph` (the IR being compiled),
- one `BumpAllocator` (for IR mutation),
- one `EpochGC` slot (for safe reclamation).

Mutator threads **never block** on compilation (Rule C.1). If a mutator requests compiled code that isn't ready, it falls back to `granit`. The compiler threads work on a frozen snapshot of the IR (Rule C.3); mutator updates after the snapshot are picked up by the next compilation.

### 4.2 Safepoint Handshake

A safepoint request from any thread reaches all mutator threads within a **bounded number of bytecodes**:

- `granit`: ≤ 1000 bytecodes (polls at every back-edge and return).
- `JADE`/`RUBY`/`DIAMOND`: ≤ 1 back-edge (polls at every loop back-edge in compiled code).

Worst-case latency target: < 10 µs at the 99th percentile under 64 mutator threads.

```cpp
// src/jade/runtime/Safepoint.hpp
class SafepointManager {
public:
    ThreadState* register_thread();
    bool         request_global_safepoint(uint32_t timeout_ms = 1000);
    void         release_safepoint();
    static bool  should_poll(ThreadState* ts) noexcept;          // inlined in compiled code
    static void  enter_safepoint(ThreadState* ts) noexcept;     // blocks until released
};
```

---

## 5. IR Design: Sea of Nodes

The graph is the heart of .JADE. It is target-agnostic — CIL and JVM opcodes lower to the same node kinds. See `02-son-ir.md` for the full specification.

### 5.1 Node layout

```cpp
struct Node {
    NodeKind    kind;         // 1 byte  — switch-dispatch key (Rule B.3)
    NodeFlags   flags;        // 2 bytes — bitmask (Rule 51)
    uint8_t     arity_hint;   // 1 byte
    uint8_t     _pad;
    TypeId      type;         // 2 bytes — type lattice element
    uint16_t    _pad2;
    EdgeSlice   data_inputs;  // 8 bytes — slice into global EdgePool
    EdgeSlice   ctrl_input;   // 8 bytes
    EdgeSlice   effect_input;  // 8 bytes
    FrameStateId state;       // 4 bytes — for guards (Rule A.3)
};
// total ≈ 40 bytes (padded)
```

### 5.2 Stable IDs (SoN Rule 2)

`NodeId` is a 32-bit opaque integer that remains valid until the arena is freed. Raw pointers are forbidden in long-lived data structures — arena growth invalidates them.

```cpp
struct NodeId { uint32_t value{0}; };
```

### 5.3 Edge pool

Inputs are stored in a global `EdgePool` (a `std::vector<NodeId>`). Each `Node` references a contiguous slice `{first_edge, count}`. Adding/removing inputs is "rewrite the slice, update `first_input`". This keeps `sizeof(Node)` small.

### 5.4 Effect chains (Rule 42 invariant)

Every effectful operation (`StoreField`, `Call`, `Allocate`, `Box`, `Throw`, ...) participates in a single-linked effect chain. Pure operations (`Add`, `Cmp`, `ConstInt`, ...) have **no** effect edges and may be moved freely by GCM.

The verifier (Rule 42) checks:

1. Every `Effect`-flagged node has exactly one effect input.
2. Effect chains are acyclic.
3. Effect chains terminate at `Start` or a `Loop` phi.
4. Pure nodes have **zero** effect inputs.

---

## 6. Compilation Pipeline (per function)

```
┌──────────────────┐
│   CIL/JVM bytes  │
└────────┬─────────┘
         │ CilLowerer / JvmLowerer
         ▼
┌──────────────────┐
│   SoN Graph      │   ← BumpAllocator-owned; one per compiler thread
└────────┬─────────┘
         │ RUBY pipeline: ConstantFolding → GVN → DCE → ... → GCM
         ▼
┌──────────────────┐
│ Scheduled Graph  │   ← nodes placed into blocks by GCM
└────────┬─────────┘
         │ Linear Scan RegAlloc
         ▼
┌──────────────────┐
│ asmjit IR        │
└────────┬─────────┘
         │ asmjit::Compiler::finalize()
         ▼
┌──────────────────┐
│ x86-64 machine   │
│ code             │
└──────────────────┘
```

---

## 7. Build Profile

The codebase is split into two compile profiles:

| Library | Compile flags | Allowed to use |
| :-- | :-- | :-- |
| `jade_core` (JIT hot path) | `-fno-exceptions -fno-rtti -fno-unwind-tables` | `Result<T>`, `Flags<E>`, raw pointers, `NodeId`s |
| `jade_granit` (interpreter + lowerers) | (default) | exceptions (for runtime errors), `std::variant` |
| `jadec` (driver) | (default) | exceptions (I/O), `std::print` |

See [`04-cpp23.md`](04-cpp23.md) for the full C++23 usage rules.

---

## 8. File Layout

```
src/jade/
├── core/                 # Arena, Result, NodeId, Flags (Rule 51)
├── ir/                   # Node, Graph, Verifier (Rule 42), Passes
│   └── passes/           # ConstantFolding, DCE, GVN, PassPipeline
├── cil/                  # CIL bytecode + CIL→SoN Lowerer
├── jvm/                  # JVM bytecode + JVM→SoN Lowerer (NEW)
├── metadata/             # PE/JAR metadata parsers (planned)
├── runtime/              # Safepoint, EpochGC (Rule C.4)
├── tier0_granit/         # Bytecode, Value, Interpreter
├── tier1_jade/           # Baseline SSA JIT (planned)
├── tier2_ruby/           # Sea of Nodes JIT (planned)
├── tier3_diamond/        # Peak optimizer (planned)
└── driver/               # CLI driver (jadec)
```

---

## 9. Invariants

These invariants are checked by CI on every PR:

1. The graph verifier (Rule 42) passes after every pass in debug builds.
2. Every pass is idempotent (Rule B.5) — running twice produces identical IR.
3. Every pass is monotonic (Rule B.6) — IR size shrinks or normalizes.
4. Differential testing (Rule 38): `granit` ↔ `JADE` ↔ `RUBY` ↔ `DIAMOND` produce byte-for-byte identical results on every program in `tests/differential/`.
5. Safepoint latency ≤ 10 µs p99 under 64 mutator threads (Definition of Done #5).
