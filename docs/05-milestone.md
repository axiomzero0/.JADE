---
title: "Definition of Done (Initial Milestone)"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: []
pass_type: "Architecture"
tier: "All"
---

# Definition of Done — Initial Milestone

The project is "done" for the initial milestone when all 8 criteria below pass. Everything beyond this is relentless optimization.

---

## 1. `granit` runs every opcode

The Tier 0 interpreter executes every opcode and produces **byte-for-byte identical output** to the reference runtime.

**Acceptance:**
- All opcodes in `Bytecode.hpp` are implemented.
- `tests/unit/tier0_granit/` has ≥1 test per opcode.
- `tests/differential/programs/*.jade` runs on `granit` and produces expected output.

---

## 2. `JADE` (Tier 1) compiles and runs faster than `granit`

Tier 1 can compile a non-trivial function and execute it faster than `granit`, with deopt working flawlessly.

**Acceptance:**
- Baseline SSA construction from bytecode.
- Fast Linear-Scan register allocation.
- Monomorphic IC stub emission.
- A deopt test that triggers under overflow and observes `granit` taking over with identical observable behavior (Rule A.4).
- Benchmark: `JADE` ≥ 1.5× faster than `granit` on `bench/arithmetic_loop.jade`.

---

## 3. `RUBY` (Tier 2) optimizes and runs faster than `JADE`

Tier 2 takes the same function, runs GVN + Escape Analysis + GCM, and executes noticeably faster than `JADE`.

**Acceptance:**
- Sea of Nodes lowering from SSA.
- GVN pass with commutative normalization.
- Basic Escape Analysis (non-escaping allocation elimination).
- GCM with early/late scheduling.
- LICM hoist of pure invariant nodes.
- Benchmark: `RUBY` ≥ 1.3× faster than `JADE` on `bench/allocation_heavy.jade`.

---

## 4. `DIAMOND` (Tier 3) applies PEA and vectorization

Tier 3 successfully applies Partial Escape Analysis and SLP vectorization, demonstrating measurable, proven throughput gains on macro-benchmarks **without violating Rule 52**.

**Acceptance:**
- PEA pass with state materialization on escape paths.
- SLP vectorization for ≥4 isomorphic independent ops.
- Guarded loop vectorization with runtime aliasing check.
- Cost model (Rule 47) refuses to vectorize when projected cost > projected benefit.
- Benchmark: `DIAMOND` ≥ 1.5× faster than `RUBY` on `bench/simd_sum.jade`.

---

## 5. Safepoint mechanism works

A safepoint request from any thread reaches all mutator threads within a **bounded number of bytecode instructions** (≤ 1000 instructions in `granit`; ≤ 1 back-edge in compiled code).

**Acceptance:**
- Atomic safepoint flag.
- `granit` polls the flag at every loop back-edge and every return.
- Compiled code (`JADE`/`RUBY`/`DIAMOND`) polls at every back-edge via a single `test` instruction.
- Test: spawn N mutator threads in a tight loop, request safepoint from another thread, measure worst-case latency. Must be < 10 µs in 99th percentile.

---

## 6. Compiler pool runs concurrently

The compiler pool runs jobs concurrently across multiple threads (via `enkiTS`) **without corrupting IR state** (verified by EBR).

**Acceptance:**
- Each compiler thread has its own `Graph` and arena.
- Cross-thread visibility happens only via EBR (Epoch-Based Reclamation).
- Stress test: 64 mutator threads, 8 compiler threads, 10k compilations. No crashes, no use-after-free, no data races (verified by TSan).

---

## 7. All passes are idempotent and verified

- Running the same pass twice produces the identical IR (hash equality).
- The graph verifier (Rule 42) passes after *every* pass in debug builds.
- Fixpoint iteration terminates within the pass's stated budget.

**Acceptance:**
- `tests/unit/ir/idempotency.cpp` runs every pass twice and hashes.
- `tests/unit/ir/verifier.cpp` runs verifier after every pass on every golden test.

---

## 8. Golden tests ≥10 per pass; differential testing passes

- Every optimization pass has ≥10 golden IR tests (Rule 37).
- `granit` ↔ `JADE` ↔ `RUBY` ↔ `DIAMOND` differential testing passes byte-for-byte on every program in `tests/differential/programs/` (Rule 38).

---

## Stretch Goals (Beyond Initial Milestone)

These are tracked but not blocking:

- Deopt fuzzing (Rule 39) runs in CI weekly.
- Replay artifacts (Rule 40) auto-saved for all CI failures.
- PGO persistence (Rule 50) for cross-session profile reuse.
- WPD (Whole-Program Devirtualization) for AOT mode.
- Object Coarsening for grouped allocations.
- FrameState delta-compression.
