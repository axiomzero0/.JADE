---
title: "The .JADE Compiler Doctrine"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 36", "Rule 42", "Rule 51", "Rule 52"]
pass_type: "Architecture"
tier: "All"
---

# The .JADE Compiler Doctrine

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule 36 (5 regression tests), Rule 42 (Verifier), Rule 51 (Bitmasks), Rule 52 (Correctness-preserving fixes)

---

## Mission

`.JADE` is a 4-tier, profile-driven, speculation-heavy JIT compiler written in C++23. It targets **both C# (CIL bytecode, ECMA-335) and Java (JVM bytecode, JVMS §6.5)** with a unified Sea-of-Nodes IR. The compiler is designed for **maximum throughput** while preserving **byte-for-byte identical observable behavior** against the reference interpreter (`granit`) on every supported source language.

Every architectural decision flows from three principles:

1. **Profile, then speculate.** Even in statically-typed languages like C# and Java, virtual dispatch, generics (reified in CLR, erased in JVM), nullable types, and `dynamic` create runtime polymorphism. Profile data is the only reliable basis for speculation.
2. **Every speculation needs a guard, every guard needs a reconstructible deopt state.** No exceptions.
3. **The IR is the heart.** The Sea of Nodes graph is optimized for fast traversal, cheap mutation, and explicit memory/effect dependencies. The IR is target-agnostic; CIL and JVM opcodes lower to the same node kinds.

See [`08-csharp-target.md`](08-csharp-target.md) for the C#-specific design and [`09-java-target.md`](09-java-target.md) for the Java-specific design.

## Table of Contents

### Doctrine & Philosophy
| Doc | Title | Purpose |
| :-- | :-- | :-- |
| `00-doctrine.md` | The .JADE Compiler Doctrine | This overview. The 4-tier pipeline summary. |
| `01-laws.md` | Performance & Correctness Laws | Non-bypassable rules (A.1–A.5, B.1–B.6, C.1–C.4, Rules 36–52). |
| `02-son-ir.md` | Sea of Nodes IR Design | Graph layout, node shape, edge pools, effect chains. |
| `NO_STUBS_POLICY.md` | No Stubs / No Placeholders Policy | No TODOs in critical paths; fallback tier instead. |
| `STRICT_ERROR_HANDLING.md` | Strict Error Handling Policy | Result<T> everywhere; fail-fast fail-safe. |

### Architecture & Verification
| Doc | Title | Purpose |
| :-- | :-- | :-- |
| `ARCHITECTURE.md` | High-Level Architecture | Memory model, threading, IR design, pipeline overview. |
| `DEOPT_PROTOCOL.md` | Deoptimization Protocol | FrameState layout, reconstruction logic, guard types, A.4 correctness proof. |
| `BYTECODE_SPEC.md` | CIL/JVM Bytecode Specification | Full opcode coverage for both C# (ECMA-335) and Java (JVMS §6.5); unified type lattice. |
| `04-cpp23.md` | C++23 Usage Rules | What is allowed vs. forbidden in hot paths. |

### Passes & Optimization
| Doc | Title | Purpose |
| :-- | :-- | :-- |
| `PASS_LIST.md` | Pass Catalogue | Every optimization pass, dependencies, idempotency proof, monotonicity proof. |
| `passes/pea_specification.md` | Partial Escape Analysis (PEA) | Per-pass spec following the mandatory template. |
| `06-optimization-catalog.md` | Advanced Optimization Catalogue | PEA, SRA, GVN, GCM, LICM, SLP, vectorization, devirtualization. |
| `07-standard-catalog.md` | Standard Optimization Catalogue | 12 categories of mechanical optimizations. |

### Testing & Performance
| Doc | Title | Purpose |
| :-- | :-- | :-- |
| `TESTING_DOCTRINE.md` | Testing Doctrine | Rule 36 enforcement, Rule 37 golden tests, Rule 38 differential testing, Rule 42 verifier invariants. |
| `03-testing.md` | Testing, Debugging, Regression | Original CI layout (kept for back-compat). |
| `BENCHMARK_RECORD.md` | Benchmark Record | Gold-standard history of perf results per commit. |
| `05-milestone.md` | Definition of Done (Initial) | The 8 acceptance criteria. |

### Target Language Specs
| Doc | Title | Purpose |
| :-- | :-- | :-- |
| `08-csharp-target.md` | C# / CIL Target Specification | ECMA-335 CIL, CLR type system, value vs reference types, per-tier C# specializations. |
| `09-java-target.md` | Java / JVM Target Specification | JVMS bytecode, JVM type system, erased generics, monitorenter/exit, per-tier Java specializations. |

---

## The 4-Tier Execution Pipeline

The industry-standard layered approach, scaled to its absolute limit. Skipping tiers creates a "compilation cliff." .JADE uses a smooth, profile-driven escalation to ensure zero stutter while maximizing peak throughput.

```
              ┌──────────────┐
   CIL ─────► │   granit    │  Tier 0 — CIL Interpreter
   (.dll)     │  (T0, base)  │  Zero compilation latency, profile collection
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
              │   (T3)       │  PEA (box/unbox), SRA, SLP, vectorization, devirtualization
              └──────────────┘
```

### Tier 0 — `granit` (CIL Interpreter)
- **Goal:** Zero compilation latency, instant startup, aggressive profile collection.
- **Mechanics:** Executes CIL bytecode on a typed evaluation stack (int32/int64/F/object/&). Updates Inline Caches (ICs) and Type Feedback Vectors (TFVs). Maintains invocation/branch counters. Contains the Safepoint Polling logic (every loop back-edge and return checks an atomic flag for GC/JIT yield). Records observed boxed value types, callvirt receiver classes, and array shapes for downstream tiers.

### Tier 1 — `JADE` (Baseline SSA JIT)
- **Goal:** Eliminate interpreter dispatch overhead in *milliseconds*. Provide stable baseline speed while higher tiers compile.
- **Mechanics:** Flat SSA graph (not yet SoN). Fast Linear-Scan register allocation. Emits monomorphic IC stubs for `callvirt` (check receiver's method table → fast path; miss → runtime stub). Honors `constrained.` prefix for value-type `callvirt` (avoids boxing). Injects lightweight profiling traps for Tier 2/3.

### Tier 2 — `RUBY` (Sea of Nodes Optimizing JIT)
- **Goal:** Maximum throughput on consistently hot paths.
- **Mechanics:** Full Sea of Nodes IR with explicit effect chains. Runs the core pipeline: Global Value Numbering (GVN), basic Escape Analysis (eliminates non-escaping allocations and `box`/`unbox` pairs), Loop Invariant Code Motion (LICM, including `ldlen` hoist), Bounds Check Elimination (BCE, for affine loop induction variables), Global Code Motion (GCM), and On-Stack Replacement (OSR).

### Tier 3 — `DIAMOND` (Peak AOT/JIT Hybrid Optimizer)
- **Goal:** Absolute maximum performance. Practicality is secondary to raw speed and mechanical depth.
- **Mechanics:** Consumes persistent Profile-Guided Optimization (PGO) data. Executes **Partial Escape Analysis (PEA)** to delay `box` allocations to the exact control-flow path of escape, enabling Scalar Replacement of Aggregates (SRA) for boxed value types. Performs Superword Level Parallelism (SLP) auto-vectorization (e.g., for `Vector<T>` operations on adjacent array elements), aggressive loop unrolling/unswitching (guarded by strict cost models), and speculative devirtualization via Class Hierarchy Analysis (CHA). In AOT mode, performs Whole-Program Devirtualization (WPD).


