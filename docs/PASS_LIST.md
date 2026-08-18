---
title: "Pass Catalogue"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule B.5", "Rule B.6", "Rule 42", "Rule 47", "Rule 52"]
pass_type: "Optimization"
tier: "JADE, RUBY, DIAMOND"
---

# Pass Catalogue

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule B.5 (Idempotency), B.6 (Monotonicity), Rule 42 (Verifier), Rule 47 (Cost Model), Rule 52 (Correctness-preserving fixes)

---

## 1. Why This Document Exists

This is the canonical list of every optimization pass implemented in `.JADE`. CI scrapes this document to verify that:

1. Every pass listed here has a corresponding `*.cpp` file in `src/jade/ir/passes/` or `src/jade/tier*/passes/`.
2. Every pass has ≥10 golden tests in `tests/golden/<pass_name>/` (Rule 37).
3. Every pass has a per-pass specification in `docs/passes/<pass_name>.md`.

A pass that is not listed here **does not exist** for the purposes of the pipeline.

---

## 2. Pass Catalogue

| # | Pass Name | Tier | File | Spec Doc | Status |
| :-- | :-- | :-- | :-- | :-- | :-- |
| 1 | `ConstantFolding` | JADE, RUBY | `ir/passes/ConstantFolding.cpp` | `passes/constant_folding.md` | ✅ Implemented |
| 2 | `GVN` (Global Value Numbering) | RUBY | `ir/passes/GVN.cpp` | `passes/gvn.md` | ✅ Implemented |
| 3 | `DCE` (Dead Code Elimination) | JADE, RUBY | `ir/passes/DeadCodeElimination.cpp` | `passes/dce.md` | ✅ Implemented |
| 4 | `CSE` (Common Subexpression Elimination) | RUBY | (planned) | `passes/cse.md` | 🚧 Stub |
| 5 | `SCCP` (Sparse Conditional Constant Propagation) | RUBY | (planned) | `passes/sccp.md` | 🚧 Stub |
| 6 | `AlgebraicSimplification` | RUBY | (planned) | `passes/algebraic.md` | 🚧 Stub |
| 7 | `ControlFlowSimplification` | RUBY | (planned) | `passes/cfg_simplify.md` | 🚧 Stub |
| 8 | `LICM` (Loop Invariant Code Motion) | RUBY | (planned) | `passes/licm.md` | 🚧 Stub |
| 9 | `GCM` (Global Code Motion) | RUBY | (planned) | `passes/gcm.md` | 🚧 Stub |
| 10 | `BCE` (Bounds Check Elimination) | RUBY | (planned) | `passes/bce.md` | 🚧 Stub |
| 11 | `NCE` (Null Check Elimination) | RUBY | (planned) | `passes/nce.md` | 🚧 Stub |
| 12 | `EscapeAnalysis` (basic) | RUBY | (planned) | `passes/escape_analysis.md` | 🚧 Stub |
| 13 | `PEA` (Partial Escape Analysis) | DIAMOND | (planned) | `passes/pea_specification.md` | 🚧 Stub |
| 14 | `SRA` (Scalar Replacement of Aggregates) | DIAMOND | (planned) | `passes/sra.md` | 🚧 Stub |
| 15 | `SLP` (Superword Level Parallelism) | DIAMOND | (planned) | `passes/slp.md` | 🚧 Stub |
| 16 | `LoopVectorization` (guarded) | DIAMOND | (planned) | `passes/loop_vec.md` | 🚧 Stub |
| 17 | `LoopUnrolling` | DIAMOND | (planned) | `passes/loop_unroll.md` | 🚧 Stub |
| 18 | `LoopPeeling` | RUBY | (planned) | `passes/loop_peel.md` | 🚧 Stub |
| 19 | `LoopUnswitching` | DIAMOND | (planned) | `passes/loop_unswitch.md` | 🚧 Stub |
| 20 | `Inlining` (with cost model) | RUBY, DIAMOND | (planned) | `passes/inlining.md` | 🚧 Stub |
| 21 | `TailCallElimination` | RUBY | (planned) | `passes/tce.md` | 🚧 Stub |
| 22 | `Devirtualization` (CHA + profile) | DIAMOND | (planned) | `passes/devirt.md` | 🚧 Stub |
| 23 | `TypeNarrowing` | RUBY | (planned) | `passes/type_narrow.md` | 🚧 Stub |
| 24 | `ICStubEmission` | JADE | (planned) | `passes/ic_stubs.md` | 🚧 Stub |
| 25 | `ProfileGuidedBlockReorder` | RUBY, DIAMOND | (planned) | `passes/block_reorder.md` | 🚧 Stub |
| 26 | `LinearScanRegAlloc` | JADE, RUBY | (planned) | `passes/lsra.md` | 🚧 Stub |
| 27 | `Peephole` (post-regalloc) | JADE, RUBY, DIAMOND | (planned) | `passes/peephole.md` | 🚧 Stub |

---

## 3. Pipeline Ordering

### 3.1 RUBY (Tier 2) pipeline

```
1. ConstantFolding
2. SCCP                    (planned)
3. GVN
4. AlgebraicSimplification (planned)
5. ControlFlowSimplification (planned)
6. CSE                     (planned)
7. LICM                    (planned)
8. LoopPeeling             (planned)
9. BCE                     (planned)
10. NCE                    (planned)
11. EscapeAnalysis         (planned)
12. DCE
13. GCM                    (planned)
14. LinearScanRegAlloc     (planned)
15. Peephole               (planned)
```

### 3.2 DIAMOND (Tier 3) pipeline

RUBY pipeline, plus:

```
16. PEA                    (planned)
17. SRA                    (planned)
18. Inlining               (planned)
19. LoopUnrolling          (planned)
20. LoopUnswitching        (planned)
21. SLP                    (planned)
22. LoopVectorization      (planned)
23. Devirtualization       (planned)
24. ProfileGuidedBlockReorder (planned)
```

---

## 4. Dependencies

| Pass | Must run before | Must run after |
| :-- | :-- | :-- |
| `ConstantFolding` | `GVN`, `SCCP`, `DCE` | — |
| `GVN` | `DCE` | `ConstantFolding` |
| `DCE` | `GCM` | `GVN`, `CSE`, `Inlining` |
| `LICM` | `GCM` | `ControlFlowSimplification` |
| `GCM` | `LinearScanRegAlloc` | `LICM`, `GVN`, `EscapeAnalysis` |
| `PEA` | `SRA`, `DCE` | `EscapeAnalysis` |
| `Inlining` | `DCE`, `GVN` | `Devirtualization` |

---

## 5. Idempotency Proof (Rule B.5)

**Definition:** A pass P is idempotent iff `P(P(G)) == P(G)` for any graph G.

**Proof pattern (applies to every pass in this catalogue):**

1. Define a normal form N for the IR (e.g., "commutative inputs sorted by NodeId").
2. Show that P produces IR in form N.
3. Show that P is a no-op when run on IR already in form N.

### Worked example: `ConstantFolding`

- **Normal form:** every pure binary node whose inputs are both `ConstInt` has the `IsConst` flag set and the folded value stored in `NodeSideData::const_value`.
- **P produces N:** every iteration either marks a node const (progress) or skips it (already const).
- **P is a no-op on N:** if every foldable node is already marked const, `try_fold_binary` returns false on every node, so `changed = false` after one iteration and the loop exits.
- **Therefore** `P(P(G)) == P(G)`. QED.

### Worked example: `GVN`

- **Normal form:** for every pure node, no two nodes have the same `(kind, sorted_inputs, const_value)` signature.
- **P produces N:** the hash table deduplicates any pair sharing a signature.
- **P is a no-op on N:** if no duplicates exist, no node is marked dead.
- **Therefore** `P(P(G)) == P(G)`. QED.

### Worked example: `DCE`

- **Normal form:** every pure node with zero uses is marked `IsDead`.
- **P produces N:** the fixpoint loop marks nodes dead until no more candidates exist.
- **P is a no-op on N:** if every dead-eligible node is already marked, no new dead marks happen.
- **Therefore** `P(P(G)) == P(G)`. QED.

---

## 6. Monotonicity Proof (Rule B.6)

**Definition:** A pass P is monotonic iff running P either reduces node count, reduces the number of "non-normal" nodes, or terminates inside a bounded fixpoint budget.

### Worked example: `ConstantFolding`

- Each call to `try_fold_binary` either returns false (no change) or sets `IsConst` on one node (one step toward normal form).
- The number of pure binary nodes is fixed at pass entry; the number that lack `IsConst` strictly decreases when a fold happens.
- **Therefore** P terminates in at most O(N) iterations of the outer fixpoint loop, and the IR is "more normal" after each iteration.
- Bounded budget: `PassContext::max_iterations` defaults to 1000.

### Worked example: `GVN`

- Each dedup marks exactly one node dead. The number of pure nodes is fixed; the number of live pure nodes strictly decreases by 1 each dedup.
- **Therefore** P terminates in O(N) time. IR shrinks by the number of deduped nodes.

### Worked example: `DCE`

- Each iteration marks at least one node dead (otherwise the loop exits).
- The number of pure nodes is fixed; the number of live pure nodes strictly decreases.
- **Therefore** P terminates in O(N) iterations.

### Passes that can grow the IR

`LoopUnrolling`, `LoopPeeling`, `LoopUnswitching`, `Inlining`, `PEA`, `SLP` can grow the IR. They **must** run inside a guarded fixpoint with a strict budget (Rule B.6):

```cpp
struct PassContext {
    uint32_t max_iterations{1000};
    uint32_t max_node_growth{4096};  // pass must abort if it would grow IR by more than this
};
```

If a pass exceeds its growth budget, it aborts and falls back to a lower tier (Rule B.2 — compilation errors are recoverable).

---

## 7. Cost Model (Rule 47)

Every aggressive pass **must** invoke a `Regulator` cost model before transforming the IR. The cost model returns `Allow` or `Deny`:

| Pass | Cost model inputs | Allow threshold |
| :-- | :-- | :-- |
| `LoopUnrolling` | loop body size, iteration count (profile), unroll factor | projected speedup ≥ 1.2× |
| `Inlining` | callee size, call site hotness, monomorphism | callee size < 35 nodes AND site hotness > 0.5 |
| `SLP` | pack width, op latency, alignment | speedup ≥ 1.5× |
| `LoopVectorization` | trip count, aliasing proven, remainder cost | speedup ≥ 2.0× |
| `PEA` materialization | escape path frequency | escape probability < 5% |
| `BlockReorder` | branch frequencies, code size | — (always allowed if profile exists) |

The cost model itself is unit-tested in `tests/unit/test_regulator.cpp` (planned).

---

## 8. Per-Pass Test Coverage (Rule 37)

Each implemented pass has ≥10 golden IR tests in `tests/golden/<pass_name>/`:

```
tests/golden/
├── constant_folding/
│   ├── 01_basic_add.in.ir / .out.ir
│   ├── 02_chained_sub_mul.in.ir / .out.ir
│   ├── 03_div_by_zero.in.ir / .out.ir
│   ├── ...
│   └── 10_overflow_wraps.in.ir / .out.ir
├── gvn/
├── dce/
└── (planned: cse, sccp, licm, gcm, ...)
```

A CI script (`tools/check_pass_coverage.py`, planned) verifies that every pass listed in this document has ≥10 golden tests.

---

## 9. Adding a New Pass

1. Add an entry to the catalogue (§2).
2. Implement `src/jade/ir/passes/<Name>.{hpp,cpp}` extending `Pass`.
3. Prove idempotency and monotonicity (§5, §6) in this document.
4. Write ≥10 golden tests (Rule 37).
5. Write 5 regression tests for any new deopt path (Rule 36).
6. Add the per-pass spec `docs/passes/<name>.md`.
7. Wire into the pipeline (§3) and update dependencies (§4).
