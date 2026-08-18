---
title: "Partial Escape Analysis (PEA) Specification"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 36", "Rule 42", "Rule 47", "Rule 52"]
pass_type: "Optimization"
tier: "DIAMOND"
---

# Partial Escape Analysis (PEA) Specification

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule 36 (5 regression tests), Rule 42 (Verifier), Rule 47 (Cost Model), Rule 52 (Correctness-preserving fixes)

---

## 1. Motivation

### 1.1 Problem Statement

Standard Escape Analysis is binary: an object either escapes the method or it doesn't. If it escapes on **any** control-flow path, the entire allocation is preserved on **all** paths, even paths where it never escapes.

For C# this is especially painful: a boxed value type (`box int`) on a rarely-taken exception path forces the box to be heap-allocated on every path, including the hot path that never throws.

For Java, the same problem applies to autoboxing (`Integer.valueOf(int)`) and to short-lived `ArrayList` / `StringBuilder` allocations that escape only when an exception is thrown.

### 1.2 Target Metrics

- Reduce heap allocations by ≥40% in allocation-heavy microbenchmarks.
- Eliminate ≥80% of `box`/`unbox` pairs in C# code that has `try`/`catch`.
- Zero observable behavior change vs `granit` (Rule A.4).

### 1.3 Research Basis

- Stadler et al., "Partial Escape Analysis and Shape Analysis", 2013.
- Kotzmann & Mössenböck, "Escape Analysis for Java — Theory and Practice", 2005 (Graal).
- Click, "Global Code Motion & Global Value Numbering", 1995 (foundational SoN work that PEA builds on).

---

## 2. Mechanical Specification

### 2.1 Input

A Sea-of-Nodes graph with:

- `Allocate` / `NewObj` / `NewArr` nodes (heap allocations).
- `Box` nodes (C# value-type boxing).
- `StoreField` / `LoadField` / `StoreElement` / `LoadElement` nodes.
- `FrameState` nodes attached to every guard (Rule A.3).

### 2.2 Output

The same graph with:

- `Allocate` / `Box` nodes split across control-flow paths.
- Non-escaping paths: the allocation is removed entirely; field values become SSA scalars.
- Escaping paths: a `Materialize` node is inserted at the exact escape point, which performs the allocation lazily.
- `StoreField`/`LoadField` on non-escaping paths: replaced by direct SSA edges.

### 2.3 Algorithm (C++23 pseudocode)

```cpp
auto run_pea(Graph& g) -> Result<void> {
    // 1. Find all Allocate/Box/NewObj/NewArr nodes.
    Vector<NodeId> allocations = find_allocations(g);

    // 2. For each allocation, compute escape state per control-flow path.
    for (NodeId alloc : allocations) {
        EscapeLattice state = analyze_escape(g, alloc);
        // state is per-basic-block: { NoEscape, Escape, Materialized }

        // 3. If state is uniformly NoEscape: scalar-replace and remove the alloc.
        if (state.is_uniformly_no_escape()) {
            scalar_replace(g, alloc);
            g.mark_dead(alloc);
            continue;
        }

        // 4. Otherwise: split the allocation. Non-escaping paths get scalar
        //    replacement; escaping paths get a Materialize node.
        if (state.has_partial_escape()) {
            split_allocation(g, alloc, state);
            // This inserts a Phi at the merge point: scalar fields on one
            // input, materialized object on the other.
        }
    }

    // 5. Run DCE to clean up dead Allocate/StoreField/LoadField nodes.
    DCEPass dce;
    dce.run(g, ctx);

    return {};
}
```

### 2.4 Graph Mutation

Consider the C# example:

```csharp
int M(bool flag) {
    int x = 42;
    object boxed = x;            // box
    if (flag) {
        Console.WriteLine(boxed);  // escape!
        return (int)boxed;          // unbox
    }
    return x;
}
```

Before PEA:

```
%1 = Start
%2 = ConstInt 42
%3 = Box %2                    ← heap allocation on every path
%4 = If flag
%5 = IfTrue %4
%6 = Call Console.WriteLine %3   ← escape
%7 = UnboxAny %3
%8 = Return %7
%9 = IfFalse %4
%10 = Return %2
```

After PEA:

```
%1 = Start
%2 = ConstInt 42
%3 = ConstBool (was: Box)     ← eliminated; we keep only the int value
%4 = If flag
%5 = IfTrue %4
%6 = Materialize %2            ← heap allocation deferred to here
%7 = Call Console.WriteLine %6
%8 = Return %2                 ← unbox folded to the underlying int
%9 = IfFalse %4
%10 = Return %2
```

The hot path (`if (flag)` is false) executes zero allocations. The cold path allocates only when actually needed.

### 2.5 Edge Cases

| Case | Handling |
| :-- | :-- |
| Allocation stored into a global | Escape — allocation preserved. |
| Allocation passed as argument to a non-inlined call | Escape — assume the callee stores it. |
| Allocation used in a `try` block where the catch handler accesses it | Escape on the catch path; PEA still optimizes the try block. |
| Allocation stored into another allocation's field | Escape if the outer allocation escapes; otherwise propagate NoEscape. |
| Allocation used by a deopt FrameState | **Must materialize on the deopt path** — the interpreter needs the actual object. PEA inserts a Materialize at the deopt point. |
| Allocation used by a `Throw` | Escape — exception propagation may store the object. |
| Recursive allocation chains | Use a fixpoint; allocations are processed in topological order. |

### 2.6 Cost Model (Rule 47)

PEA runs only if:

1. Tier is `DIAMOND` (Tier 3).
2. Profile confidence (Rule 46) for the allocation site is > 0.5 (i.e., we've seen it execute).
3. The allocation's escape probability is < 5% (i.e., the cold path is rare).

If the escape probability is ≥ 5%, PEA still runs but does **not** materialize — it falls back to standard escape analysis (binary escape/no-escape).

The cost model returns `Allow` if projected speedup ≥ 1.5× (heap allocation cost ≈ 50ns; materialization cost ≈ 50ns; the speedup comes from making the hot path zero-allocation).

---

## 3. Verification

### 3.1 Invariants (checked by Rule 42 verifier after the pass)

1. **Effect chain continuity** — every `Materialize` node is in the effect chain at the escape point.
2. **No dangling scalar uses** — every use of a scalar-replaced field is dominated by either the original `Allocate` (now dead) or a `Phi` that merges scalar and materialized forms.
3. **FrameState completeness** — every deopt point in the optimized region has a `Materialize` node (or the allocation was not scalar-replaced on that path).
4. **Idempotency** (Rule B.5) — running PEA twice produces identical IR. The second run finds no allocations to optimize (they've all been split or removed).
5. **Monotonicity** (Rule B.6) — PEA either removes nodes (`Allocate`, `StoreField`, `LoadField`) or splits them (one allocation → one Materialize + Phi). The split is bounded by the number of escape paths.

### 3.2 Golden Tests

`tests/golden/pea/`:

```
01_simple_non_escape.in.ir / .out.ir
02_partial_escape_with_if.in.ir / .out.ir
03_partial_escape_with_loop.in.ir / .out.ir
04_box_unbox_pair.in.ir / .out.ir
05_allocation_in_try_catch.in.ir / .out.ir
06_allocation_stored_into_array.in.ir / .out.ir
07_allocation_used_in_throw.in.ir / .out.ir
08_recursive_allocation.in.ir / .out.ir
09_deopt_materialization.in.ir / .out.ir
10_no_escape_after_inlining.in.ir / .out.ir
```

### 3.3 Regression Suite (Rule 36)

`tests/regression/<issue>_<slug>/`:

```
pea_materialize_on_deopt_path/
├── 01_minimal_reproducer.cil       # smallest case where PEA missed the materialize
├── 02_variant_trigger.cil          # same root cause, different shape
├── 03_boundary_negative.cil        # ensure PEA doesn't fire when escape prob is high
├── 04_integration_contextual.cil   # in a realistic method
└── 05_deopt_state_reconstruction.cil  # force deopt and verify byte-for-byte match
```

---

## 4. Performance Validation

### 4.1 Benchmark results (planned)

| Benchmark | Target | Before PEA | After PEA | Ratio | Notes |
| :-- | :-- | :-- | :-- | :-- | :-- |
| `allocation_heavy.cs` | C# (.NET 9) | TBD | TBD | TBD | Initial PEA implementation |
| `box_unbox_storm.cs` | C# (.NET 9) | TBD | TBD | TBD | Box/unbox in a hot loop |
| `allocation_heavy.java` | Java (OpenJDK 21) | TBD | TBD | TBD | autoboxing elimination |
| `mandelbrot.cs` | C# (.NET 9) | TBD | TBD | TBD | No allocations expected |

### 4.2 Cost

- Time complexity: O(N) per allocation site, where N is the number of nodes in the graph.
- Compilation budget: max 5 ms per method.
- Memory: O(N) for the escape lattice.

### 4.3 Rule 47 justification

PEA is worth the compilation time because:

- Heap allocation cost ≈ 50 ns each (in .NET 9's gen-0 GC).
- A typical C# method allocates 1-10 objects per invocation.
- If even one allocation can be eliminated on the hot path, the speedup is ≥ 50 ns per call.
- For a method called 1M times/sec, that's ≥ 50 ms/sec saved.

The cost model (§2.6) ensures PEA only runs when the projected speedup justifies the compilation cost.

---

## 5. Implementation Notes

### 5.1 File layout

```
src/jade/ir/passes/
├── PEA.hpp              # Public API
├── PEA.cpp              # Main pass
├── EscapeAnalysis.hpp   # Supporting: escape lattice
├── EscapeAnalysis.cpp
└── ScalarReplacement.hpp # Supporting: SRA
    ScalarReplacement.cpp
```

### 5.2 Dependencies

- Must run after `EscapeAnalysis` (basic).
- Must run after `Inlining` (so we can see through call sites).
- Must run before `DCE` (so dead Allocate nodes are removed).
- Must run before `GCM` (so the Materialize nodes are scheduled correctly).

### 5.3 Open issues

- PEA for `multianewarray` (JVM) is not yet implemented.
- PEA for `Nullable<T>` (C#) is not yet implemented.
- Delta-compression of FrameState (advanced §7.3) is not yet implemented.
