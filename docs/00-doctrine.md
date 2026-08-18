# The .JADE Compiler Doctrine

The master philosophy document. .JADE is a 4-tier, profile-driven, speculation-heavy JIT compiler for a dynamic language. It is designed for **maximum throughput** while preserving **byte-for-byte identical observable behavior** against the reference interpreter (`granit`).

Every architectural decision flows from three principles:

1. **Profile, then speculate.** Static reasoning in dynamic languages is brittle. Profile data is the only reliable basis for speculation.
2. **Every speculation needs a guard, every guard needs a reconstructible deopt state.** No exceptions.
3. **The IR is the heart.** The Sea of Nodes graph is optimized for fast traversal, cheap mutation, and explicit memory/effect dependencies.

---

## Table of Contents

| Doc | Title | Purpose |
| :-- | :-- | :-- |
| `00-doctrine.md` | The .JADE Compiler Doctrine | This overview. The 4-tier pipeline summary. |
| `01-laws.md` | Performance & Correctness Laws | Non-bypassable rules (A.1–A.5, B.1–B.6, C.1–C.4, Rules 36–52). |
| `02-son-ir.md` | Sea of Nodes IR Design | Graph layout, node shape, edge pools, effect chains. |
| `03-testing.md` | Testing, Debugging, Regression | Rule 36–52 enforcement in CI. |
| `04-cpp23.md` | C++23 Usage Rules | What is allowed vs. forbidden in hot paths. |
| `05-milestone.md` | Definition of Done (Initial) | The 8 acceptance criteria. |
| `06-optimization-catalog.md` | Advanced Optimization Catalogue | PEA, SRA, GVN, GCM, LICM, SLP, vectorization, devirtualization. |
| `07-standard-catalog.md` | Standard Optimization Catalogue | 12 categories of mechanical optimizations (constant folding, DCE, CFG simplification, etc.). |

---

## The 4-Tier Execution Pipeline

The industry-standard layered approach, scaled to its absolute limit. Skipping tiers creates a "compilation cliff." .JADE uses a smooth, profile-driven escalation to ensure zero stutter while maximizing peak throughput.

```
              ┌──────────────┐
   source ──► │   granit    │  Tier 0 — Interpreter
              │  (T0, base)  │  Zero compilation latency, profile collection
              └──────┬───────┘
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

### Tier 0 — `granit` (Register-style Interpreter)
- **Goal:** Zero compilation latency, instant startup, aggressive profile collection.
- **Mechanics:** Executes stack bytecode while updating Inline Caches (ICs) and Type Feedback Vectors (TFVs). Maintains invocation/branch counters. Contains the Safepoint Polling logic (every loop back-edge and return checks an atomic flag for GC/JIT yield). Fast paths must perfectly match the runtime's native fast paths (e.g., INT+INT arithmetic inlined).

### Tier 1 — `JADE` (Baseline SSA JIT)
- **Goal:** Eliminate interpreter dispatch overhead in *milliseconds*. Provide stable baseline speed while higher tiers compile.
- **Mechanics:** Standard CFG in SSA form. No heavy optimizations. Fast Linear-Scan register allocation. Emits monomorphic IC stubs (check observed shape → fast path; miss → runtime C++ call). Injects lightweight profiling traps to gather exact branch-taken frequencies for Tier 2/3.

### Tier 2 — `RUBY` (Sea of Nodes Optimizing JIT)
- **Goal:** Maximum throughput on consistently hot paths.
- **Mechanics:** Sea of Nodes IR. Runs the core Gigavolt pipeline: Global Value Numbering (GVN), basic Escape Analysis, Loop Invariant Code Motion (LICM), Bounds Check Elimination (BCE), Global Code Motion (GCM), and On-Stack Replacement (OSR).

### Tier 3 — `DIAMOND` (Peak AOT/JIT Hybrid Optimizer)
- **Goal:** Absolute maximum performance. Practicality is secondary to raw speed and mechanical depth.
- **Mechanics:** Consumes persistent Profile-Guided Optimization (PGO) data. Executes **Partial Escape Analysis (PEA)** to delay allocations to the exact point of escape, enabling Scalar Replacement of Aggregates (SRA) across complex control flow. Performs Superword Level Parallelism (SLP) auto-vectorization, aggressive loop unrolling/unswitching (guarded by strict cost models), and speculative devirtualization with multi-tiered guard chains.
