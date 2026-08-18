---
title: "Advanced Optimization Catalogue"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 36","Rule 42","Rule 47","Rule 52"]
pass_type: "Architecture"
tier: "RUBY, DIAMOND"
---

# Advanced Optimization Catalogue — The "PEA Suite"

The highest-ROI optimizations for dynamic languages, eliminating heap allocation and GC pressure entirely.

---

## 1. Memory & Escape Analysis

### 1.1 Partial Escape Analysis (PEA)
**Research:** Stadler et al., "Partial Escape Analysis and Shape Analysis", 2013.

Instead of binary escape analysis, delay the `Allocate` node to the **exact control-flow path** where the object escapes. In non-escaping paths, the object's fields remain as independent SSA scalar values, enabling register allocation.

### 1.2 Scalar Replacement of Aggregates (SRA)
If an `Allocate` node is proven non-escaping (or partially non-escaping via PEA), eliminate the node entirely. Replace `LoadField`/`StoreField` with direct data edges to the scalar SSA values.

### 1.3 Load/Store Forwarding & Elimination
If a `StoreField(A, offset, v)` is immediately followed by a `LoadField(A, offset)` with no intervening effectful nodes on the effect chain, replace the `LoadField` with a direct data edge to `v`. The `StoreField` is then eliminated by DCE.

### 1.4 Implicit Null Check Elimination (NCE)
**Research:** Click, "Global Code Motion & Global Value Numbering", 1995.

Instead of emitting explicit `CheckNotNull` guards, rely on the OS page fault handler (SEH on Windows, `sigaction` on Linux). If a null dereference occurs, the signal handler deoptimizes. Explicit checks are only emitted if the compiler cannot prove dominance.

### 1.5 Redundant Bounds Check Elimination (BCE)
**Research:** Bodik et al., "Lightweight and Scalable Array Bounds Check Elimination".

Use affine range analysis on loop induction variables. If `0 <= i < length` is proven at the loop header, dominate and eliminate all inner `CheckBounds` nodes for that array.

---

## 2. Dataflow & Value Optimization

### 2.1 Sparse Conditional Constant Propagation (SCCP) + GVN
**Research:** Wegman & Zadeck, 1991.

Integrate constant propagation directly with Global Value Numbering. If a value is proven constant, propagate it immediately, which often unlocks further algebraic simplifications and DCE in a single fixpoint iteration.

### 2.2 Global Value Numbering (GVN)
Hash-based value numbering across the entire SoN graph. Deduplicate identical computations regardless of basic-block boundaries. Commutative operations (`Add`, `Eq`) must be normalized (sort inputs by `NodeId`) before hashing to catch `a + b` == `b + a`.

### 2.3 Algebraic Simplification & Strength Reduction
- `x * 2^k` → `x << k`
- `x / 2^k` → arithmetic right shift (with bias correction for negative numbers)
- `x + 0` → `x`, `x * 1` → `x`, `!!x` → `x`

### 2.4 Partial Redundancy Elimination (PRE)
**Research:** Morel & Renvoise, 1979.

The ultimate code motion tool. If an expression is computed on *some* but not *all* paths to a join point, PRE inserts the computation on the missing paths and removes it from the join point. (Implemented as part of Global Code Motion.)

---

## 3. Control Flow & Loop Optimization

### 3.1 Global Code Motion (GCM)
**Research:** Click, 1995. The crown jewel of SoN.

- **Schedule Early:** Move pure nodes as close to their operands as possible to hide memory latency and expose Instruction-Level Parallelism (ILP).
- **Schedule Late:** Move pure nodes as close to their uses as possible to minimize register pressure and keep values out of loop bodies.

### 3.2 Loop Invariant Code Motion (LICM)
Hoist pure, loop-invariant operations out of the `Loop` region. Requires strict dominance checks on the effect chain.

### 3.3 Loop Peeling & Specialization
Peel the first iteration of a loop. This allows the JIT to specialize the peeled iteration against the *exact* types observed at runtime (e.g., first element is `Int`), while the rest of the loop handles the generic case.

### 3.4 Loop Unswitching
If a loop contains a loop-invariant conditional (`if (global_flag)`), hoist the condition outside the loop, duplicating the loop body. This creates two simpler, highly optimizable loops.

### 3.5 Tail Call Optimization (TCO)
Detect `Call` nodes that are immediately followed by `Return`. Transform them into a `Jump` to the target function's entry, reusing the current stack frame and eliminating call overhead.

---

## 4. Speculative & Type-Based Optimization

### 4.1 Guarded Polymorphic Inline Caching (PIC) with Bitmasking
For `Call` or `LoadField` nodes, emit a chain of up to $N$ guards. **Rule 51 Application:** The first guard is a *bitmask subtype check* (`type_mask & expected_mask == expected_mask`). This is an $O(1)$, single-cycle bitwise `AND` that fast-rejects 90% of mismatches before falling back to a full pointer comparison or deopt.

### 4.2 Speculative Devirtualization via CHA
**Research:** Dean et al., "Optimizing Dynamically Typed Object-Oriented Languages", 1991.

Use Class Hierarchy Analysis + Tier 0/1 profiles. If 99% of calls to a virtual method hit `ClassA`, emit a `CheckClass(ClassA)` guard followed by a direct `CallKnown`. If it fails, deopt to the generic virtual dispatch.

### 4.3 Type Specialization (Monomorphization)
Clone functions based on observed type signatures. If `add(a, b)` is profiled as `(Int, Int)` 95% of the time, generate a specialized `add_Int_Int` version. The generic version remains as the deopt fallback.

### 4.4 Profile-Guided Block Reordering
Use Tier 0/1 branch frequencies to lay out basic blocks in memory. Hot paths are placed sequentially (fall-through) to maximize I-cache locality and hardware branch predictor accuracy. Cold paths (e.g., error handling, deopt stubs) are pushed to the end of the function.

---

## 5. Vectorization & SIMD

### 5.1 Superword Level Parallelism (SLP)
**Research:** Larsen & Amarasinghe, 2000.

Scan the SoN graph for adjacent, independent, isomorphic scalar operations (e.g., four separate `Add` nodes operating on array elements). Pack them into a single SIMD instruction (e.g., `vpaddd` on AVX2/AVX-512).

### 5.2 Guarded Loop Vectorization
Attempt to vectorize a loop. Because dynamic languages make aliasing and bounds hard to prove, emit a *runtime versioning guard*. Check at runtime: "Are these arrays contiguous, non-overlapping, and properly aligned?" If yes, jump to the vectorized loop. If no, deopt or fall back to the scalar loop.

### 5.3 Autovectorization of Reductions
Recognize patterns like `sum += array[i]` and transform them into SIMD horizontal additions, handling the remainder with a scalar cleanup loop.

---

## 6. Backend & `asmjit`-Specific Optimizations

### 6.1 Rematerialization
During register allocation, instead of spilling a value to the stack, check if it is cheap to recompute (e.g., a constant, or a simple `Add` of two unspilled registers). If so, emit the recomputation instead of a `mov` from the stack.

### 6.2 Two-Phase Register Allocation
- **Tier 1 (`JADE`)**: Fast Linear Scan Register Allocation (LSRA) with basic spill code insertion.
- **Tier 2/3 (`RUBY`/`DIAMOND`)**: LSRA enhanced with live-range splitting and rematerialization, or a PBQP (Partitioned Boolean Quadratic Programming) allocator for maximum register assignment quality on hot loops.

### 6.3 Peephole Optimization & Instruction Combining
Post-regalloc, scan the `asmjit` instruction stream. Combine redundant moves (`mov rax, rbx` followed by `mov rcx, rax` → `mov rcx, rbx`). Fold immediates into instructions (`mov rax, 1` + `add rax, rbx` → `lea rax, [rbx + 1]`).

### 6.4 Safepoint Polling Optimization
Instead of a full memory load at every back-edge, use a single, cache-line-aligned atomic flag checked via a fast `test` instruction. The GC only toggles this flag, minimizing mutator overhead.

---

## 7. Advanced / Bleeding-Edge Research

### 7.1 Object Coarsening / Allocation Sinking
**Research:** Kotzmann et al., "Design of the Graal Compiler".

If PEA identifies multiple small objects that are allocated together and escape together, allocate them as a single, contiguous memory block. This drastically improves CPU cache locality and reduces GC metadata overhead.

### 7.2 Value Speculation
If profiling shows a variable is *almost always* a specific constant (e.g., a loop counter or a specific enum), speculate that constant, emit a guard, and optimize the entire downstream graph for that constant. On guard failure, deopt.

### 7.3 Deoptimization State Compression
**Research:** Wimmer et al., "Linear Scan Register Allocation for SSA Form".

`FrameState` nodes can consume massive amounts of memory. Compress them using delta-encoding of register maps and variable locations. Only store the *difference* from the previous `FrameState` in the same basic block.

### 7.4 Interprocedural Optimization (IPO) for AOT
In the `DIAMOND` tier, perform Whole-Program Devirtualization (WPD). If the entire program is known (AOT), and a virtual method has only one implementation, strip the virtual dispatch table entirely and inline the call directly, even across module boundaries.

---

## Tier → Optimization Mapping

| Tier | Name | Primary Optimizations Enabled |
| :-- | :-- | :-- |
| **T0** | `granit` | Profile Collection (ICs, TFVs), Safepoint Polling, Fast-path arithmetic. |
| **T1** | `JADE` | SSA Construction, Monomorphic IC Inlining, Type Feedback Incorporation, Fast Linear Scan RegAlloc, Basic Constant Folding. |
| **T2** | `RUBY` | SoN Lowering, GVN, SCCP, LICM, GCM, Basic Escape Analysis, BCE, NCE, Loop Peeling. |
| **T3** | `DIAMOND` | **PEA**, SRA, SLP Vectorization, Guarded Loop Vectorization, Speculative Devirtualization, PRE, Object Coarsening, Aggressive Inlining (with strict cost models). |

---

## Implementation Mandate

For **every single optimization** added from this catalog, you must enforce:

1. **Rule 47 (Cost Model)** — No unrolling, vectorization, or inlining without a mathematical or profiled cost justification.
2. **Rule 42 (Verifier)** — The SoN graph verifier must pass after the optimization runs, proving effect chains and dominance are intact.
3. **Rule 36 (5 Regression Tests)** — If the optimization introduces a new deopt path or speculation mechanism, you must write the 5 mandated regression tests (Minimal, Variant, Boundary, Integration, Deopt State Reconstruction) before the PR is merged.
