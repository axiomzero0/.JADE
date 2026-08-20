# Bug 004: PEA does not insert Materialize nodes (silently falls back to optimistic SRA)

**Root cause:** The PEA pass in `tier3_diamond/PEA.cpp` correctly classified
allocations as `PartialEscape` but then ignored that classification — it ran
the same SRA + box-elimination logic for both `NoEscape` and `PartialEscape`,
keeping the original `Allocate` alive whenever there was a live escaping use.
The spec's §2.4 headline transformation (`Materialize` insertion at escape
points, `Allocate` elimination on the hot path) was never executed.

The PEA code itself documented the gap (PEA.cpp lines 211–224 in the pre-fix
version):
```
// For PartialEscape, we'd also insert Materialize at escape points —
// but that requires Graph::create_node_at_block() which we don't have yet.
// For now, treat PartialEscape like NoEscape (optimistic): do SRA on
// all non-escaping uses, and if the escaping uses are still live,
// keep the allocation for them.
```

The deeper root cause was that `Graph` had no per-use rewire primitive
(`replace_all_uses` is too coarse — it would also rewire the escaping uses,
which must keep pointing at a heap object). The `Materialize` node kind
existed but was never created by any pass.

**Symptoms:**
- `PartialEscapeLoadForwardedButAllocKept` test asserted the alloc was kept
  (i.e., it documented the bug as expected behavior).
- The hot path of any function with a partial escape kept the allocation,
  defeating the entire purpose of PEA.
- The `Materialize` NodeKind had no live producers anywhere in the codebase.

**Fix (Phase 0 + Phase 1 of the PEA roadmap):**

1. **Phase 0 — IR primitives (Graph.cpp/.hpp):**
   - Added `Graph::replace_one_use(old, new, user, slot)` — selectively
     rewires a single data-input slot, leaving other uses (including other
     slots of the same user) untouched. This is what PEA needs to rewire
     non-escaping uses while leaving escaping uses pointing at the alloc.

2. **Phase 1 — Materialize insertion (PEA.cpp):**
   - Added `insert_materialize_for_partial_escape()` — for each escaping use
     of a `PartialEscape` allocation:
       1. Collect the latest stored value per field (from `FieldState`).
       2. Create a `Materialize` node with those field values as data inputs.
       3. Wire the `Materialize` into the effect chain immediately before the
          escaping use (so materialization happens at the exact point of
          escape — the cold path).
       4. Rewire the escaping use from `alloc` → `materialize` via
          `replace_one_use`.
   - Wired the new function into `process_allocation()` for the
     `PartialEscape` case.
   - Applied the same transformation to `process_box()` for partial-escape
     `Box` nodes.
   - After Materialize insertion, the original `Allocate` has no live uses
     (escaping uses were rewired to `Materialize`; non-escaping uses were
     forwarded to the stored values by SRA) → it gets eliminated.

**Commit:** (this commit)

**Tests:**
1. `01_minimal_reproducer` — Smallest partial-escape: alloc + store + load + return(alloc).
2. `02_variant_trigger` — Box partial escape with non-escaping unbox.
3. `03_boundary_negative` — GlobalEscape: no Materialize should be inserted.
4. `04_integration_contextual` — Realistic partial-escape with multiple fields.
5. `05_deopt_state_reconstruction` — Partial escape with a guard; verify Materialize is still inserted.
