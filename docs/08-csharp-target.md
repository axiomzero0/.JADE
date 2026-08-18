---
title: "C# / CIL Target Specification"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule A.4","Rule 42","Rule 51","Rule 52"]
pass_type: "Architecture"
tier: "All"
---

# .JADE — C# Compilation Target

This document specifies how `.JADE` applies to **C#** as its source language. The doctrine's tiered, profile-driven, speculation-heavy design still holds — but the bytecode, type system, and value model now reflect the CLR / ECMA-335 CIL specification.

---

## What .JADE Compiles

- **Source language**: C# (Roslyn-style syntax; .NET 8+ feature set).
- **Bytecode**: CIL (Common Intermediate Language) per ECMA-335.
- **Type system**: CLR — value types, reference types, generics, delegates, interfaces, arrays.
- **Runtime**: CLR-compatible object model, GC-managed heap, managed pointers, exception model.

`.JADE` is *not* a CLR replacement. It is a JIT compiler that consumes CIL bytecode (as produced by `csc`/Roslyn) and produces native machine code, **without** requiring the CoreCLR runtime to be present. The runtime services it needs (GC, exception dispatch, type loading) are provided by `.JADE`'s own runtime library, which can either be self-hosted or delegate to a host runtime (Mono, CoreCLR, NativeAOT-style preinit) when one is available.

---

## The 4-Tier Pipeline, Adapted to C#

| Tier | Name | Role in a C# context |
| :-- | :-- | :-- |
| T0 | `granit` | **CIL interpreter**. Reads `.dll`/`.exe` PE files, parses metadata tables, executes CIL opcodes on a typed evaluation stack. Collects type feedback for value types, call targets, and branch directions. Polls safepoints at back-edges. |
| T1 | `JADE` | **Baseline SSA JIT**. Lowers CIL to a flat SSA graph (not yet Sea of Nodes). Fast Linear-Scan register allocation. Emits monomorphic IC stubs for `callvirt` and interface dispatch. |
| T2 | `RUBY` | **Sea of Nodes optimizing JIT**. Full SoN IR with explicit effect chains. GVN, escape analysis, LICM, BCE, GCM, OSR. Specializes value types (structs) via scalar replacement. |
| T3 | `DIAMOND` | **Peak AOT/JIT hybrid**. Partial Escape Analysis for boxed value types (the single highest-ROI optimization in C# — eliminating `box`/`unbox` overhead). SLP auto-vectorization for `Vector<T>` and array loops. Speculative devirtualization with CHA. WPD in AOT mode. |

---

## C#-Specific IR Extensions

The following `NodeKind` values are added to support C# semantics. They are defined in `src/jade/ir/NodeKind.hpp`.

### Boxing and Type Conversions

| Kind | CIL | Description |
| :-- | :-- | :-- |
| `Box`         | `box`         | Convert a value type to a reference-typed boxed object. The escape state of the box is tracked by PEA. |
| `Unbox`       | `unbox`       | Extract a value-type pointer from a boxed object. Throws `InvalidCastException` on mismatch. |
| `UnboxAny`    | `unbox.any`   | Like `Unbox` but returns the value by-value. |
| `IsInst`      | `isinst`      | C# `is` operator — returns null if cast fails (no throw). |
| `CastClass`   | `castclass`   | C# `(T)x` — throws `InvalidCastException` on failure. |
| `Conv*`       | `conv.*`      | Numeric conversions (`conv.i4`, `conv.i8`, `conv.r4`, `conv.r8`, `conv.u`, etc.). |
| `LdNull`      | `ldnull`      | Push `null` reference. |
| `LdStr`       | `ldstr`      | Load a string literal from the metadata `#US` heap. |

### Field and Array Access

| Kind | CIL | Description |
| :-- | :-- | :-- |
| `LdFld`       | `ldfld`       | Load a field by offset (instance or static). |
| `StFld`       | `stfld`       | Store a field. |
| `LdFlda`      | `ldflda`      | Load the address of a field (managed pointer). |
| `LdElem`      | `ldelem.*`    | Load an array element. |
| `StElem`      | `stelem.*`    | Store an array element. |
| `LdElemA`     | `ldelema`     | Load the address of an array element. |
| `ArrayLength` | `ldlen`       | Length of an array. |
| `NewArr`      | `newarr`      | Allocate a zeroed 1-D array. |

### Object and Method Operations

| Kind | CIL | Description |
| :-- | :-- | :-- |
| `NewObj`      | `newobj`      | Allocate and construct a new object. Calls the constructor. |
| `Call`        | `call`        | Direct, non-virtual call. |
| `CallVirt`    | `callvirt`    | Virtual dispatch — uses the vtable. |
| `Constrained` | `constrained.`| Prefix for `callvirt` on a constrained value type (the JIT may inline the method without boxing). |

### Control Flow with Exceptions

| Kind | CIL | Description |
| :-- | :-- | :-- |
| `Throw`       | `throw`       | Throw an exception object. |
| `Rethrow`     | `rethrow`    | Re-throw the current exception. |
| `Leave`       | `leave`      | Jump out of a try block (calls finally chains). |
| `EndFinally`  | `endfinally`  | End a finally block. |

### Locals and Arguments

| Kind | CIL | Description |
| :-- | :-- | :-- |
| `LdArg`       | `ldarg.*`     | Load an argument. |
| `StArg`       | `starg`       | Store an argument. |
| `LdLoc`       | `ldloc.*`     | Load a local. |
| `StLoc`       | `stloc.*`     | Store a local. |
| `LdArga`      | `ldarga`      | Load the address of an argument (managed pointer). |
| `LdLoca`      | `ldloca`      | Load the address of a local (managed pointer). |

---

## CLR Evaluation Stack Types

The CIL evaluation stack uses a restricted type system. `.JADE` models these as `EvalStackType`:

| Type | CIL | Description |
| :-- | :-- | :-- |
| `Int32`     | `int32`       | 32-bit signed integer. |
| `Int64`     | `int64`       | 64-bit signed integer. |
| `NativeInt` | `native int`  | Pointer-sized integer. |
| `Float32`   | `float32` (F) | 32-bit IEEE-754 single. |
| `Float64`   | `float64` (F) | 64-bit IEEE-754 double. |
| `ObjectRef` | `O`           | Reference to a GC-managed object. |
| `ManagedPtr`| `&`           | Managed pointer to an interior location. |
| `TransientPtr` | `*`        | Unmanaged pointer (only in unsafe contexts). |

**Note**: The CIL spec collapses `float32` and `float64` into a single stack type `F` (stored at native precision). `.JADE` follows this; the precision is determined by the storage location on `stloc`/`stfld`.

---

## Value Type — C# Semantics

`granit`'s runtime `Value` is extended to cover the CLR type system. See `src/jade/tier0_granit/Value.hpp`:

```cpp
using Value = std::variant<
    std::monostate,            // uninitialized
    int32_t,                   // int32 on the eval stack
    int64_t,                   // int64 on the eval stack
    float,                     // float32 (promoted to double on the stack)
    double,                    // float64
    ObjectHandle,              // O — GC-managed reference
    ManagedPointer,           // & — interior pointer
    NativePointer             // * — unmanaged pointer (unsafe only)
>;
```

`ObjectHandle` is a 64-bit ID that resolves through the GC's handle table. Boxes, classes, arrays, strings, and delegates are all `ObjectHandle`.

---

## Exception Handling

C# exceptions are **zero-cost** in compiled code (Rule 12.3). The IR models exception regions as side-tables:

```cpp
struct ExceptionClause {
    uint32_t try_offset;
    uint32_t try_length;
    uint32_t handler_offset;
    uint32_t handler_length;
    ClassId  catch_type;       // for catch; null for finally
    uint32_t filter_offset;    // for filter; 0 otherwise
    enum class Kind : uint8_t { Catch, Finally, Fault, Filter };
};
```

In the IR, `Throw` is an effectful node that begins the unwind. The verifier (Rule 42) checks that every `Throw` has a `FrameState` so the runtime can reconstruct the CIL stack at the throw site for catch handlers that need it.

---

## Profile Feedback for C# Specifically

The `TypeFeedbackVector` (TFV) collected by `granit` tracks, per instruction:

| Field | What it tracks |
| :-- | :-- |
| `call_target_seen[]` | Set of `MethodDef` tokens observed at a `callvirt`/`call` site. Drives devirtualization. |
| `boxed_type_seen[]` | For each `box` instruction, which value type was boxed. PEA uses this. |
| `array_shape_seen[]` | For each `ldelem`/`stelem`, the element type and array rank. |
| `class_at_check[]` | For each `isinst`/`castclass`, the observed runtime class. Drives spec cast elision. |
| `branch_taken`, `branch_total` | Per-branch direction frequencies. |
| `invocation_count` | Per-method invocation count, drives tier escalation. |

---

## Type Id Lattice, Extended for C#

The existing `TypeId` lattice is extended with C#-specific elements:

```cpp
enum class TypeId : uint16_t {
    Bottom, Top,
    Int32, Int64, NativeInt, Float32, Float64,
    Bool, Char,            // primitive value types
    ObjectRef,             // any O
    ManagedPtr, NativePtr,
    String, Array, Delegate,
    ClassBase,             // user class
    StructBase,            // user struct (boxed form)
    Nullable,             // Nullable<T>
    NullRef,              // null literal
    // ...
};
```

For exact types (needed for devirtualization), the side-table `NodeSideData::class_id` stores the precise `TypeDef` token observed at runtime.

---

## Calling Convention

`.JADE` uses the CLR calling convention:

- The first N arguments are passed in registers (RCX, RDX, R8, R9 on x86-64 Windows; RDI, RSI, RDX, RCX, R8, R9 on SysV).
- For instance methods, `this` is the first argument.
- For `varargs`, the SysV-like convention is used (rare in C#).
- Return value in RAX (or XMM0 for floats).
- Caller-cleanup of the stack.
- The stack must be 16-byte aligned at the call site on x86-64.

In `RUBY`/`DIAMOND`, the calling convention is enforced by `asmjit`'s `x86::Compiler::call()` API.

---

## Per-Tier Specializations for C#

### Tier 0 (`granit`)

- **Boxed value type detection**: Every `box` opcode records the exact value type observed. PEA in `DIAMOND` consumes this.
- **Virtual call profiling**: Every `callvirt` records the receiver's exact runtime type. Tier 1 emits a monomorphic IC; Tier 3 devirtualizes.
- **Nullable<T> tracking**: `Nullable<T>` is a struct with `HasValue`/`Value` fields. The interpreter records which path is taken, enabling `DIAMOND` to speculatively flatten `Nullable<T>` into a register pair.

### Tier 1 (`JADE`)

- **Direct struct call optimization**: When `callvirt` is prefixed with `constrained. T` and T is a value type whose method is non-overriding, emit a direct call without boxing.
- **Boxed value type fast path**: If a `box` always boxes the same value type, emit the fast path inline; on type mismatch, deopt.
- **Monomorphic IC for `callvirt`**: Check the receiver's method table pointer; on hit, jump to the known target. On miss, fall back to the vtable lookup and deopt.

### Tier 2 (`RUBY`)

- **SoN lowering of CIL**: Each CIL opcode lowers to 1–5 SoN nodes. The effect chain correctly models `ldfld`/`stfld` ordering.
- **Escape Analysis for boxed structs**: If a boxed struct never escapes, eliminate the `box` and replace `unbox`/`ldfld` on the box with direct field loads from the underlying value.
- **BCE for arrays**: If the array's length is loop-invariant and `i` is an induction variable, eliminate the per-element `ldelem` bounds check.
- **LICM for `ldlen`**: Hoist `array.Length` out of loops.

### Tier 3 (`DIAMOND`)

- **PEA for box/unbox**: Delay the `box` to the exact control-flow path where the boxed reference escapes. In non-escaping paths, the boxed value remains as scalar SSA values.
- **SLP for `Vector<T>`**: Recognize adjacent independent `ldelem`/`stelem` of the same element type and pack them into `vmovups`/`vaddps`.
- **Speculative devirtualization with CHA**: If a virtual method has only one implementation in the loaded type system, emit a direct call with a `CheckClass` guard. Profile data confirms the speculation is valid.
- **Whole-Program Devirtualization (AOT only)**: If the entire program is known at compile time (NativeAOT-style), strip the vtable entry entirely and inline.
- **Nullable<T> flattening**: Replace `Nullable<int>` with a (bool, int) pair in registers, eliminating the struct allocation.

---

## Metadata Model

`.JADE` consumes CLR metadata (ECMA-335 §II). The driver parses:

- `#~` table stream — type defs, method defs, fields, params, etc.
- `#Strings` heap — names.
- `#US` heap — user strings (for `ldstr`).
- `#GUID` heap — type GUIDs (rarely used).
- `#Blob` heap — signatures, field initializers.

The metadata model lives in `src/jade/metadata/` (to be implemented). For the initial milestone, `.JADE` will consume a hand-rolled `.cil` text format for testing — see `tests/cil/`.

---

## Testing Strategy for C#

In addition to the existing test suites:

1. **Per-opcode CIL tests** — at least one test per CIL opcode (Rule 37 analog).
2. **Differential testing against CoreCLR** — run each test on `granit` and on CoreCLR's interpreter; assert byte-for-byte identical output (Rule 38).
3. **Boxing tests** — every `box`/`unbox`/`isinst`/`castclass` permutation gets the 5 mandated regression tests (Rule 36).
4. **Exception path tests** — every throw/catch/finally shape gets a state-reconstruction test (Rule 36 item 5).
5. **Deopt tests** — every speculation in `JADE`/`RUBY`/`DIAMOND` has a deopt test that verifies byte-for-byte identical behavior against `granit` (Rule A.4).

---

## What the Existing Foundation Already Supports

The infrastructure built in the initial scaffold already supports the C# target:

- ✅ `BumpAllocator` (Rule B.1) — used for IR nodes, metadata, side tables.
- ✅ `Result<T>` (Rule B.2) — used for fallible metadata parsing.
- ✅ `Flags<E>` bitmask (Rule 51) — used for `MethodAttributes`, `FieldAttributes`, `TypeAttributes`.
- ✅ `NodeId` / `EdgeSlice` — used for IR edges.
- ✅ `Graph` + `Verifier` (Rule 42) — runs after every pass.
- ✅ `EpochGC` (Rule C.4) — used for retired metadata, retired IR.
- ✅ `SafepointManager` — used for GC pauses in `granit` and compiled code.
- ✅ `ConstantFolding`, `GVN`, `DCE` — directly applicable to CIL folds.

What's new in this sprint:

- ➕ C#-specific `NodeKind` values (Box, Unbox, IsInst, CastClass, NewObj, CallVirt, ...).
- ➕ `CilOpcode` enum (ECMA-335 subset).
- ➕ CLR-flavored `Value` type with `ObjectHandle` and `ManagedPointer`.
- ➕ CIL interpreter (`granit` extended for CIL semantics).
- ➕ Exception clauses as a side table on `Graph`.
- ➕ Updated `TypeId` lattice with C# primitives.
- ➕ Tests for the new IR ops and interpreter.
