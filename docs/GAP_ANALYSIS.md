---
title: "Gap Analysis: Current State vs Target"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule 09", "Rule 42", "Rule 52"]
pass_type: "Architecture"
tier: "All"
---

# Gap Analysis: What's Actually Running

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19

---

## 1. What's Actually Fast Today

### Real, working, and effective:

| Component | Status | Notes |
| :-- | :-- | :-- |
| `LinearScanRegAlloc` | ✅ Real | Wimmer-Franz LSRA with spill/reload, 16-byte frame alignment. |
| `CodeEmitter` (straight-line) | ✅ Real | asmjit emission for ~20 NodeKinds: ConstInt, Add, Sub, Mul, Div, Mod, Neg, And/Or/Xor/Not/Shl/Shr/Sar, Eq/Ne/Lt/Gt/Lte/Gte, LdLoc, StLoc, LdArg, LdFld, StFld, Return, Safepoint. |
| `ConstantFolding` | ✅ Real | Iterates to fixpoint; folds Add/Sub/Mul/Div/Mod. |
| `GVN` | ✅ Real | Hash-based dedup with commutative normalization and use rewiring. |
| `DCE` | ✅ Real | Fixpoint iteration; respects effect chains. |
| `SCCP` | ✅ Real | Wegman-Zadeck lattice; propagates through arithmetic + comparisons. |
| `CSE` | ✅ Real | Local CSE within effect-chain runs; rewires uses. |
| `AlgebraicSimplification` | ✅ Real | x+0→x, x*1→x, x*0→0, x&0→0, x^x→0, x<<0→x. |
| `BCE` (constants) | ✅ Real | Eliminates CheckBounds where idx and length are both ConstInt. |
| `NCE` | ✅ Real | Eliminates CheckNotNull on NewObj/NewArr/Box/LdStr results. |
| `ControlFlowSimplification` | ✅ Real | Eliminates constant-folded If branches. |
| `CilInterpreter` | ✅ Real | Executes ~60 CIL opcodes on a typed eval stack. |
| `JvmInterpreter` | ✅ Real | Executes ~80 JVM opcodes on a typed eval stack. |
| `Value` (16-byte) | ✅ Real | Compact tagged union; single tag switch per op. |
| `SafepointManager` | ✅ Real | Atomic flag polling; enters/exits safepoint. |
| `EpochGC` | ✅ Real | Epoch-based reclamation for compiler-thread IR. |
| `Graph::replace_all_uses` | ✅ Real | Rewires data-input references for GVN/CSE/SRA. |

### Partially working:

| Component | Status | What's missing |
| :-- | :-- | :-- |
| `CodeEmitter` (control flow) | 🟡 Partial | `If`/`IfTrue`/`IfFalse` labels are created but emission order is wrong — nodes between IfTrue and IfFalse are emitted before the label is bound. Needs block scheduling. |
| `PEA` | 🟡 Partial | Does straight-line EA only (eliminates allocations with zero live uses). No materialization splitting (needs block structure + Phi-per-field). |
| `SRA` | 🟡 Partial | Store→load forwarding for straight-line code. No Phi-per-field for loop-carried fields. |
| `SLP` | 🟡 Analysis-only | Finds candidate packs of isomorphic independent nodes. No SIMD emission (emitter has no vector instruction support). |
| `Peephole` | 🟡 Partial | Strength reduction marker (x*2^k → shift). No post-regalloc instruction combining. |
| `EscapeAnalysis` | 🟡 Partial | Binary escape (zero live uses → dead). No per-path escape state. |

---

## 2. What Silently Does Nothing (No-Op Passes)

Every pass that needs loop/region structure is a no-op, because the lowerers emit `If`/`IfTrue`/`IfFalse`/`Phi` but **no `Loop`, `Region`, `Switch`, or `Jump` nodes with proper block structure, and no dominator tree**:

| Pass | Status | Why It's a No-Op |
| :-- | :-- | :-- |
| `LICM` | no-op | "requires Loop region in the graph" — no Loop nodes are produced by the lowerer. |
| `GCM` | no-op | "requires basic block structure / dominator tree / loop detection" — none exist. |
| `LoopUnrolling` | no-op | Needs Loop regions to identify loops to unroll. |
| `LoopUnswitching` | no-op | Needs Loop regions to hoist invariant conditionals. |
| `LoopPeeling` | no-op | Needs Loop regions to peel first iteration. |
| `Inlining` | no-op | Needs call graph + callee bodies (metadata resolver not wired). |
| `TailCallElimination` | partial | Detects Call→Return pattern but can't verify "no intervening effectful ops" without block structure. |
| `Devirtualization` | no-op | Needs CHA (class hierarchy analysis) + profile data. |
| `ICStubEmission` | no-op | Needs profile data wiring from granit. |
| `ProfileGuidedBlockReorder` | no-op | Needs basic block structure + profile data. |

---

## 3. The CodeEmitter Fallback Problem

The `CodeEmitter` returns `ErrorKind::UnsupportedNode` for any NodeKind it doesn't handle, which triggers a fallback to `granit` (Tier 0 interpreter). The **unhandled** NodeKinds include:

```
If, IfTrue, IfFalse, Phi, Region, Loop, Switch, Jump,
Call, CallVirt, CallKnown, TailCall, InvokeDynamic,
NewObj, NewArr, Allocate, Box, Unbox, UnboxAny,
LoadElement, StoreElement, LoadFieldA, LdElemA, ArrayLength,
IsInst, CastClass, CheckInt, CheckNotNull, CheckShape, CheckBounds, CheckClass,
Throw, Rethrow, Leave, EndFinally,
MonitorEnter, MonitorExit, LdNull, LdStr,
ConvI1, ConvI2, ConvI4, ConvI8, ConvR4, ConvR8, ConvU*, ConvOvf*,
LdArga, LdLoca, StArg, StLoc (CIL-specific),
ConstBool, ConstFloat, ConstNull, ConstString, Neg, Mod
```

**Any function with a branch, call, or allocation falls back to the interpreter.**

---

## 4. The Root Cause: No Block Structure

The single root cause of ~10 no-op passes and the CodeEmitter fallback is: **the IR has no basic-block structure**.

The lowerers (`CilLowerer`, `JvmLowerer`) emit `If`/`IfTrue`/`IfFalse`/`Phi` nodes, but:
- No `Region` nodes at merge points.
- No `Loop` nodes with pre-headers.
- No dominator tree.
- No loop nesting forest.
- No block scheduling for the emitter.

Without block structure:
- LICM has nothing to hoist against (no loop pre-header).
- GCM has no blocks to schedule nodes into.
- LoopUnrolling/Peeling/Unswitching have no loops to transform.
- PEA can't track per-path escape state.
- SRA can't insert Phi-per-field at merge points.
- The emitter can't lay out blocks in the right order.

---

## 5. The Fix: BuildRegions + Block-Scheduled Emission

### 5.1 BuildRegions Pass

Add a pass that runs after lowering and before optimization:

1. **Walk `If`/`IfTrue`/`IfFalse`/`Jump` nodes** to identify basic blocks.
2. **Insert `Region` nodes** at merge points (where two control-flow edges converge).
3. **Detect back-edges** (control edges that target an earlier block) and insert `Loop` nodes with pre-headers.
4. **Compute the dominator tree** (Lengauer-Tarjan, O(E·α(E,N))).
5. **Compute the loop nesting forest** (natural loops identified by back-edges).

This one pass unlocks: LICM, GCM, LoopUnrolling, LoopPeeling, LoopUnswitching, PEA-materialization, SRA-with-Phi.

### 5.2 Block-Scheduled Emission

Update `CodeEmitter` to:
1. **Walk blocks in reverse post-order** (dominator-tree order).
2. **Emit `If` as `test; jcc`** with proper label binding for true/false targets.
3. **Emit `Jump` as `jmp label`**.
4. **Emit `Phi` as a `mov` chain** at merge points (Wimmer-Franz §3.4).
5. **Emit `Region` as a label** (the merge point).
6. **Emit `Loop` as a back-edge label + safepoint poll**.

### 5.3 Additional Emission

After block structure:
- **`Call`/`CallKnown`**: load args into RDI/RSI/RDX/RCX/R8/R9, align stack, `call` indirect.
- **`NewObj`/`NewArr`/`Allocate`**: bump-allocate from the thread-local arena, zero-fill.
- **`LoadElement`/`StoreElement`**: `mov reg, [arr + idx*scale + offset]` with optional bounds check.
- **`ArrayLength`**: `mov reg, [arr + 8]` (length is at offset 8 in the array header).
- **`Conv*`**: `movsxd`/`cvtsi2sd`/`cvttsd2si` as appropriate.
- **`CheckInt`/`CheckNotNull`/`CheckBounds`**: `test`/`cmp; jne deopt`.
- **`Box`/`Unbox`**: allocate box, copy value (PEA will optimize this away on hot paths).
- **`Throw`**: store exception, `jmp unwind_handler`.

---

## 6. Other Gaps

### 6.1 No Tiering Logic

There is no `TierManager`, no invocation counter, no compilation queue. The driver runs the demo and exits. The tier escalation policy (granit→JADE at N=100, →RUBY at M=1000, →DIAMOND at K=10000) is documented but **not implemented**.

### 6.2 No OSR

Long-running loops stuck in `granit` can't be promoted to JIT mid-execution. Without OSR, a tight loop that starts in the interpreter stays there forever.

### 6.3 No Code Cache

Each `JitRuntime` is per-function. For a real runtime: single code cache, `mmap(PROT_READ|PROT_WRITE)` for emission, `mprotect(PROT_READ|PROT_EXEC)` after finalization.

### 6.4 Interpreter Uses Exceptions

`Interpreter::run` uses `throw std::runtime_error` for DivideByZero, stack underflow, type mismatches. Each throw walks the stack and is ~100× slower than a return. Should use `Result<Value>` propagation instead.

### 6.5 No Computed-Threaded Dispatch

The interpreter's giant `switch` defeats branch prediction. GCC's "labels as values" (`&&label`) with a dispatch table gives ~20-30% speedup.

### 6.6 LTO Off by Default

`JADE_USE_LTO` defaults OFF. The hot-path flags are good (`-fno-exceptions -fno-rtti`) but without LTO the compiler can't inline across TUs.

---

## 7. Priority Order

| Priority | Work | Unblocks |
| :-- | :-- | :-- |
| P0 | `BuildRegions` pass (Region/Loop/DominatorTree) | LICM, GCM, LoopUnroll, LoopPeel, LoopUnswitch, PEA-materialization, SRA-with-Phi |
| P0 | Block-scheduled `CodeEmitter` (If/Jump/Phi/Region/Loop) | Real JIT loops (no fallback to granit) |
| P1 | `LICM` (real) + `BCE` (affine) + `ArrayLength`/`LoadElement`/`StoreElement` emission | Array loops at near-native speed |
| P1 | Interpreter: fixed stack + computed goto + no-throw | 2-3× faster fallback |
| P2 | `Inlining` (real) + `Call`/`CallKnown` emission | OO code |
| P2 | `Peephole` (post-regalloc) | 5-10% everywhere |
| P3 | `LiveRangeSplitting` + `Rematerialization` in LSRA | Fewer spills |
| P3 | `TierManager` + compilation queue | Actual tiered compilation |
| P4 | `Devirtualization` (CHA + profile) + `ICStubEmission` | Virtual calls |
| P4 | `PEA` (full) + `SRA` (with Phi-per-field) | Eliminate allocations |
| P5 | `SLP` (with SIMD emission) + `LoopVectorization` | SIMD |
| P5 | OSR + code cache (W^X) + LTO defaults | Long-running loops, deployment |
