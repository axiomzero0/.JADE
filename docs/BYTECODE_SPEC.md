---
title: "Bytecode Specification: CIL + JVM → IR"
status: "Stable"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule A.4", "Rule 42", "Rule 50"]
pass_type: "Architecture"
tier: "granit, JADE"
---

# Bytecode Specification: CIL + JVM → .JADE IR

**Status:** Stable  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule A.4 (Deopt byte-for-byte match), Rule 42 (Verifier), Rule 50 (Versioned persistent state)

---

## 1. Why This Document Exists

`.JADE` consumes **both C# (CIL bytecode, ECMA-335) and Java (JVM bytecode, JVMS §6.5)**. This document specifies:

1. The full opcode coverage for both bytecode formats (we do **not** support a subset — we support the complete opcode space of each).
2. How each opcode maps to a `NodeKind` in our Sea-of-Nodes IR.
3. The unified type lattice that represents both CLR types and JVM types in a single `TypeId` enum.
4. How try-catch-finally blocks are represented in the SoN graph (as effect-chain side tables, not control-flow nodes).

If the implementation disagrees with this document, the document is correct.

---

## 2. CIL Bytecode (C#)

### 2.1 Source spec

ECMA-335, §III. The .JADE CIL decoder is at `src/jade/cil/Opcode.{hpp,cpp}`. It implements the **complete** opcode set: every opcode defined in ECMA-335, including:

- All stack-manipulation opcodes (`nop`, `dup`, `pop`, `ret`, `jmp`).
- All constant-loading opcodes (`ldc.i4.*`, `ldc.i8`, `ldc.r4`, `ldc.r8`, `ldnull`, `ldstr`, `ldtoken`).
- All local/argument opcodes (`ldloc.*`, `stloc.*`, `ldarg.*`, `starg.*`, `ldloca.*`, `ldarga.*`).
- All arithmetic opcodes (signed/unsigned, overflow-checked).
- All conversion opcodes (`conv.*`, `conv.ovf.*`).
- All comparison and branch opcodes (short and long form).
- All control-flow opcodes (`br`, `brtrue`, `brfalse`, `beq`, `bne.un`, `bge`, `bgt`, `ble`, `blt`, `switch`, `leave`, `leave.s`, `endfinally`).
- All method-call opcodes (`call`, `calli`, `callvirt`, `jmp`, `tail.`, `ret`).
- All object opcodes (`newobj`, `newarr`, `initobj`, `box`, `unbox`, `unbox.any`, `isinst`, `castclass`, `ldftn`, `ldvirtftn`).
- All field opcodes (`ldfld`, `ldflda`, `stfld`, `ldsfld`, `ldsflda`, `stsfld`).
- All array opcodes (`ldelem.*`, `stelem.*`, `ldelema`, `ldlen`, `readonly.`).
- All indirect load/store opcodes (`ldind.*`, `stind.*`).
- All exception opcodes (`throw`, `rethrow`, `endfilter`, `leave`).
- All prefix opcodes (`constrained.`, `unaligned.`, `volatile.`, `tail.`, `no.`, `readonly.`).
- All misc opcodes (`cpblk`, `initblk`, `sizeof`, `refanyval`, `refanytype`, `mkrefany`, `arglist`).

### 2.2 Operand formats

CIL opcodes have 11 operand formats (ECMA-335 §II.15.4):

| Format | Size | Used for |
| :-- | :-- | :-- |
| `InlineNone` | 0 | no operand |
| `ShortInlineBrTarget` | 1 | `br.s`, `beq.s`, ... |
| `InlineBrTarget` | 4 | `br`, `beq`, ... |
| `ShortInlineI` | 1 | `ldc.i4.s` |
| `InlineI` | 4 | `ldc.i4` |
| `InlineI8` | 8 | `ldc.i8` |
| `ShortInlineR` | 4 | `ldc.r4` |
| `InlineR` | 8 | `ldc.r8` |
| `ShortInlineVar` | 1 | `ldloc.s`, `ldarg.s` |
| `InlineVar` | 2 | `ldloc`, `ldarg` |
| `InlineMethod` / `InlineField` / `InlineType` / `InlineString` / `InlineTok` / `InlineSig` | 4 | metadata tokens |
| `InlineSwitch` | 4 + 4*N | `switch` |

### 2.3 CIL → NodeKind mapping (selected)

| CIL opcode | .JADE NodeKind | Notes |
| :-- | :-- | :-- |
| `nop` | (no node) | |
| `ldc.i4.*` / `ldc.i8` | `ConstInt` | |
| `ldc.r4` / `ldc.r8` | `ConstFloat` | |
| `ldnull` | `LdNull` | |
| `ldstr` | `LdStr` | operand is the #US heap token |
| `ldloc.*` / `stloc.*` | `LdLoc` / `StLoc` | slot number in side data |
| `ldarg.*` / `starg.*` | `LdArg` / `StArg` | |
| `ldloca.*` / `ldarga.*` | `LdLoca` / `LdArga` | produces a `ManagedPointer` |
| `dup` | (rewire stack) | no node; the lowerer aliases |
| `pop` | (drop stack top) | |
| `add` / `sub` / `mul` / `div` / `rem` | `Add` / `Sub` / `Mul` / `Div` / `Mod` | |
| `add.ovf.*` etc. | `Add` + `CheckOverflow` guard | |
| `and` / `or` / `xor` / `not` / `shl` / `shr` / `shr.un` | `And` / `Or` / `Xor` / `Not` / `Shl` / `Shr` / `Sar` | |
| `neg` | `Neg` | |
| `conv.*` | `ConvI1`/`ConvI2`/`ConvI4`/`ConvI8`/`ConvU1`/.../`ConvR4`/`ConvR8` | |
| `conv.ovf.*` | `ConvOvfI1`/.../`ConvOvfU8` (guarded) | |
| `ceq` / `cgt` / `clt` / `cgt.un` / `clt.un` | `Eq` / `Gt` / `Lt` / `Gt`/`Lt` (unsigned variant in side data) | |
| `br` / `br.s` | `Jump` | |
| `brtrue` / `brfalse` / `brtrue.s` / `brfalse.s` | `If` + `IfTrue` / `IfFalse` | |
| `beq` / `bge` / `bgt` / `ble` / `blt` / `bne.un` / etc. | `Eq`/`Gt`/... + `If` + `IfTrue` | |
| `switch` | `Switch` | |
| `ret` | `Return` | |
| `call` | `Call` | |
| `calli` | `Call` (with sig operand) | |
| `callvirt` | `CallVirt` | |
| `newobj` | `NewObj` | |
| `newarr` | `NewArr` | |
| `initobj` | (effect chain write of zero) | |
| `box` | `Box` | |
| `unbox` | `Unbox` | produces a managed pointer |
| `unbox.any` | `UnboxAny` | produces the value |
| `isinst` | `IsInst` | null on mismatch, no throw |
| `castclass` | `CastClass` | throws InvalidCastException |
| `ldfld` / `ldflda` / `stfld` | `LdFld` / `LdFlda` / `StFld` | |
| `ldsfld` / `ldsflda` / `stsfld` | `LdFld` / `LdFlda` / `StFld` (static variant in side data) | |
| `ldelem.*` / `stelem.*` / `ldelema` | `LdElem` / `StElem` / `LdElemA` | |
| `ldlen` | `ArrayLength` | |
| `ldind.*` / `stind.*` | `LdElem`-style with managed ptr | |
| `throw` | `Throw` | |
| `rethrow` | `Rethrow` | |
| `leave` / `leave.s` | `Leave` | runs finally chains |
| `endfinally` | `EndFinally` | |
| `constrained.` | `Constrained` (prefix) | modifies the following `callvirt` |

---

## 3. JVM Bytecode (Java)

### 3.1 Source spec

JVMS (Java Virtual Machine Specification), §6.5. The .JADE JVM decoder is at `src/jade/jvm/Opcode.{hpp,cpp}`. It implements the **complete** opcode set: every opcode defined in JVMS §6.5, including:

- All constant-loading opcodes (`aconst_null`, `iconst_<i>`, `lconst_<l>`, `fconst_<f>`, `dconst_<d>`, `bipush`, `sipush`, `ldc`, `ldc_w`, `ldc2_w`).
- All load/store opcodes (`iload`/`lload`/`fload`/`dload`/`aload`, with short forms `iload_<n>` etc., and arrays `iaload`/`laload`/...).
- All store opcodes (`istore`/`lstore`/`fstore`/`dstore`/`astore` + short forms + arrays `iastore`/...).
- All stack manipulation (`pop`, `pop2`, `dup`, `dup_x1`, `dup_x2`, `dup2`, `dup2_x1`, `dup2_x2`, `swap`).
- All arithmetic (`iadd`/`ladd`/`fadd`/`dadd`, `isub`/`lsub`/`fsub`/`dsub`, `imul`/`lmul`/`fmul`/`dmul`, `idiv`/`ldiv`/`fdiv`/`ddiv`, `irem`/`lrem`/`frem`/`drem`, `ineg`/`lneg`/`fneg`/`dneg`, `ishl`/`lshl`, `ishr`/`lshr`, `iushr`/`lushr`, `iand`/`land`, `ior`/`lor`, `ixor`/`lxor`, `iinc`).
- All conversion opcodes (`i2l`/`i2f`/`i2d`, `l2i`/`l2f`/`l2d`, `f2i`/`f2l`/`f2d`, `d2i`/`d2l`/`d2f`, `i2b`/`i2c`/`i2s`).
- All comparison/branch opcodes (`ifeq`/`ifne`/`iflt`/`ifge`/`ifgt`/`ifle`, `if_icmpeq`/`if_icmpne`/.../`if_acmpeq`/`if_acmpne`, `ifnull`/`ifnonnull`, `goto`/`goto_w`, `jsr`/`jsr_w`/`ret`, `tableswitch`/`lookupswitch`).
- All method invocation (`invokevirtual`/`invokespecial`/`invokestatic`/`invokeinterface`/`invokedynamic`).
- All object/array creation (`new`, `newarray`, `anewarray`, `multianewarray`, `arraylength`).
- All type check (`checkcast`, `instanceof`).
- All monitor ops (`monitorenter`, `monitorexit`).
- All control (`return`/`ireturn`/`lreturn`/`freturn`/`dreturn`/`areturn`/`athrow`).
- All wide (`wide` prefix).
- All reference (`getfield`/`putfield`/`getstatic`/`putstatic`).
- Misc (`nop`, `aconst_null`, `breakpoint`, `impdep1`, `impdep2`).

### 3.2 Operand formats

JVM opcodes have the following operand sizes (JVMS §6.5):

| Format | Size | Used for |
| :-- | :-- | :-- |
| (none) | 0 | no operand |
| `u1` | 1 | local index (short), `bipush`, `newarray` atype, `iinc` const |
| `u2` | 2 | local index (long), constant-pool index, branch offset (long), `sipush` |
| `s1` | 1 | signed byte (`bipush`) |
| `s2` | 2 | signed short (`sipush`) |
| `branch_u2` | 2 | signed branch offset (most branches) |
| `branch_s4` | 4 | signed branch offset (`goto_w`, `jsr_w`) |
| `tableswitch` | variable | 0–3 bytes padding + default + low + high + N offsets |
| `lookupswitch` | variable | 0–3 bytes padding + default + npairs + 2*N pairs |

### 3.3 JVM → NodeKind mapping (selected)

| JVM opcode | .JADE NodeKind | Notes |
| :-- | :-- | :-- |
| `nop` | (no node) | |
| `aconst_null` | `LdNull` | |
| `iconst_<i>` / `bipush` / `sipush` / `ldc` (int) | `ConstInt` | |
| `ldc` (float) / `ldc2_w` (double) | `ConstFloat` | |
| `ldc` (string) / `ldc_w` (string) | `LdStr` | resolves through constant pool |
| `iload` / `iload_<n>` | `LdLoc` | |
| `lload` / `lload_<n>` | `LdLoc` (with int64 type) | |
| `aload` / `aload_<n>` | `LdLoc` (with object type) | |
| `istore` / `istore_<n>` | `StLoc` | |
| `iinc` | `LdLoc` + `Add(ConstInt)` + `StLoc` | expanded |
| `iaload` / `laload` / `faload` / `daload` / `aaload` / `baload` / `caload` / `saload` | `LdElem` | element type in side data |
| `iastore` / `lastore` / `fastore` / `dastore` / `aastore` / `bastore` / `castore` / `sastore` | `StElem` | |
| `pop` / `pop2` | (drop stack top) | |
| `dup` / `dup_x1` / `dup_x2` / `dup2` / `dup2_x1` / `dup2_x2` | (rewire stack) | |
| `swap` | (swap top two) | |
| `iadd` / `ladd` / `fadd` / `dadd` | `Add` | type determined by stack type |
| `isub` / `lsub` / ... | `Sub` | |
| `imul` / `lmul` / ... | `Mul` | |
| `idiv` / `ldiv` / `fdiv` / `ddiv` | `Div` | JVM throws `ArithmeticException` on int/long div-by-zero |
| `irem` / `lrem` / `frem` / `drem` | `Mod` | |
| `ineg` / `lneg` / `fneg` / `dneg` | `Neg` | |
| `ishl` / `lshl` | `Shl` | |
| `ishr` / `lshr` | `Sar` | arithmetic shift (signed) |
| `iushr` / `lushr` | `Shr` | logical shift (unsigned) |
| `iand` / `land` | `And` | |
| `ior` / `lor` | `Or` | |
| `ixor` / `lxor` | `Xor` | |
| `i2l` / `i2f` / `i2d` / `l2i` / `l2f` / `l2d` / `f2i` / `f2l` / `f2d` / `d2i` / `d2l` / `d2f` | `ConvI1`/`ConvI2`/`ConvI4`/`ConvI8`/`ConvR4`/`ConvR8` (as appropriate) | |
| `i2b` / `i2c` / `i2s` | `ConvI1` / `ConvU2` / `ConvI2` | |
| `lcmp` / `fcmpl` / `fcmpg` / `dcmpl` / `dcmpg` | `Lt`/`Gt`/`Eq` | normalized to -1/0/1 |
| `ifeq` / `ifne` / `iflt` / `ifge` / `ifgt` / `ifle` | `Eq`/`Ne`/`Lt`/`Gte`/`Gt`/`Lte` + `If` + `IfTrue` | |
| `if_icmpeq` / ... | same | compares two stack values |
| `ifnull` / `ifnonnull` | `IsNull` + `If` | |
| `goto` / `goto_w` | `Jump` | |
| `jsr` / `jsr_w` / `ret` | (deprecated; modeled as `Call`/`Return`) | |
| `tableswitch` / `lookupswitch` | `Switch` | |
| `ireturn` / `lreturn` / `freturn` / `dreturn` / `areturn` / `return` | `Return` | |
| `invokevirtual` | `CallVirt` | |
| `invokespecial` | `Call` (direct; constructors/private/super) | |
| `invokestatic` | `Call` (static) | |
| `invokeinterface` | `CallVirt` (with interface bit) | |
| `invokedynamic` | `Call` (with indy call site descriptor) | |
| `new` | `NewObj` (or `Allocate` if no constructor follows) | |
| `newarray` | `NewArr` (primitive element type) | |
| `anewarray` | `NewArr` (reference element type) | |
| `multianewarray` | `NewArr` (multi-dim; depth in side data) | |
| `arraylength` | `ArrayLength` | |
| `athrow` | `Throw` | |
| `checkcast` | `CastClass` (throws ClassCastException) | |
| `instanceof` | `IsInst` (returns boolean; no throw) | |
| `monitorenter` | `MonitorEnter` (planned NodeKind) | |
| `monitorexit` | `MonitorExit` (planned NodeKind) | |
| `getfield` / `putfield` | `LdFld` / `StFld` | |
| `getstatic` / `putstatic` | `LdFld` / `StFld` (static) | |
| `wide` | (prefix; modifies next opcode's operand to u2) | |
| `multianewarray` | `NewArr` with dim count | |

---

## 4. Unified Type Lattice

`.JADE` represents both CLR and JVM types in a single `TypeId` lattice. Where the two systems overlap, we use the same `TypeId`. Where they differ, we have target-specific entries.

```cpp
enum class TypeId : uint16_t {
    // Lattice top/bottom
    Bottom = 0,    // ⊥ — unreachable
    Top    = 1,    // ⊤ — unknown

    // Primitive value types (shared between C# and Java)
    Int32  = 2,    // C# int / Java int (or short, byte, char after narrowing)
    Int64  = 3,    // C# long / Java long
    Float32 = 4,   // C# float / Java float
    Float64 = 5,   // C# double / Java double
    Bool    = 6,   // C# bool / Java boolean
    Char    = 7,    // C# char / Java char (16-bit unsigned)

    // Reference types
    ObjectRef = 8, // any O — GC-managed reference
    String    = 9,
    Array     = 10,
    ClassBase = 11, // user class (C# reference type / Java class)
    Interface = 12,
    Delegate  = 13, // C# only

    // Pointer types
    ManagedPtr  = 14, // C# & (interior pointer)
    NativePtr   = 15, // C# * (unmanaged) / unsafe Java
    FuncPtr     = 16, // C# delegate* / Java MethodHandle

    // C#-specific
    StructBase    = 17, // user struct (boxed form is ObjectRef)
    Nullable      = 18, // Nullable<T>
    Span          = 19, // Span<T> / ReadOnlySpan<T>

    // Java-specific
    BigInteger = 20, // java.math.BigInteger (promoted from long on overflow)
    Optional   = 21, // java.util.Optional

    // Special
    NullRef = 22, // the literal null
    Void    = 23, // void return type

    kCount,
};
```

### 4.1 Type narrowing (C#)

If `granit` profile says a value is always `Int32`, the JIT inserts `CheckInt(value)` and propagates `TypeId::Int32` through the graph. Downstream `Add` nodes become integer-only (no float promotion check).

### 4.2 Type narrowing (Java)

JVM bytecode is more strongly typed at the bytecode level (e.g., `iadd` requires int operands, `ladd` requires long). The lowerer infers types from the opcode. Profiles still drive speculative specialization (e.g., receiver class for `invokevirtual`).

### 4.3 Lattice operations

`type_meet(a, b)` (greatest lower bound) and `type_join(a, b)` (least upper bound) are defined in `src/jade/ir/TypeId.cpp`. The verifier (Rule 42) does not check lattice correctness; passes are responsible for maintaining it.

---

## 5. Exception Tables

### 5.1 C# / CIL exception clauses

CIL methods have an exception table (ECMA-335 §II.19.1) with clauses of four kinds: `Catch`, `Filter`, `Finally`, `Fault`. Each clause has:

- `try_offset`, `try_length` — the protected range.
- `handler_offset`, `handler_length` — the handler body.
- `catch_type` (token) — for `Catch` only.
- `filter_offset` — for `Filter` only.

`.JADE` models these as a side table on `Graph`:

```cpp
struct ExceptionClause {
    uint32_t try_offset;
    uint32_t try_length;
    uint32_t handler_offset;
    uint32_t handler_length;
    ClassId  catch_type;       // for Catch; null for Filter/Finally/Fault
    uint32_t filter_offset;    // for Filter; 0 otherwise
    enum class Kind : uint8_t { Catch, Finally, Fault, Filter };
    Kind kind;
};
```

In the IR, `Throw` is an effectful node that begins the unwind. The unwind walks the exception table at runtime to find a matching handler. The verifier (Rule 42) checks that every `Throw` has a `FrameState` so the runtime can reconstruct the CIL stack at the throw site.

### 5.2 Java exception table

JVM methods have an exception table (JVMS §4.7.3) with entries: `start_pc`, `end_pc`, `handler_pc`, `catch_type` (constant-pool index, or 0 for "any"). `.JADE` models these with the same `ExceptionClause` struct:

| JVM field | .JADE field |
| :-- | :-- |
| `start_pc` | `try_offset` |
| `end_pc` | `try_offset + try_length` |
| `handler_pc` | `handler_offset` |
| `catch_type == 0` (catch-all) | `catch_type = null`, `kind = Finally` |
| `catch_type != 0` (class index) | `catch_type = resolve(catch_type)`, `kind = Catch` |

Java does not have filters or faults as separate kinds; the JVM catch-all (`catch_type == 0`) is modeled as `Finally`.

### 5.3 Zero-cost exception model (Rule 12.3)

In compiled code, exceptions are zero-cost on the non-exceptional path. There are **no runtime checks** in the hot path. Exception metadata lives in the side table; the OS page fault handler (for null derefs) or the runtime's `athrow`/`throw` implementation walks the table.

```cpp
// Hot path: no checks
mov rax, [rcx + 8]        // load field at offset 8

// Exception path (only on throw):
//   1. Look up the method's ExceptionClause table.
//   2. Find the clause whose try-range contains the current pc.
//   3. If catch_type matches the exception class, jump to handler_offset.
//   4. Otherwise, propagate to caller.
```

---

## 6. Constant Pool / Metadata

### 6.1 C# / CIL metadata

CIL metadata comes from PE files (ECMA-335 §II.22-24):

- `#~` table stream — type defs, method defs, fields, params, etc.
- `#Strings` heap — names.
- `#US` heap — user strings (for `ldstr`).
- `#GUID` heap — type GUIDs.
- `#Blob` heap — signatures, field initializers.

`.JADE` resolves tokens at lower time (the lowerer has a `MetadataResolver` that maps tokens to `MethodId`, `FieldId`, `ClassId`, `StringId`).

### 6.2 Java / JVM constant pool

JVM metadata comes from `.class` files (JVMS §4.4-4.7):

- `constant_pool` — entries of 14 kinds (`CONSTANT_Class`, `CONSTANT_Fieldref`, `CONSTANT_Methodref`, `CONSTANT_InterfaceMethodref`, `CONSTANT_String`, `CONSTANT_Integer`, `CONSTANT_Float`, `CONSTANT_Long`, `CONSTANT_Double`, `CONSTANT_NameAndType`, `CONSTANT_Utf8`, `CONSTANT_MethodHandle`, `CONSTANT_MethodType`, `CONSTANT_InvokeDynamic`, `CONSTANT_Dynamic`).
- `attributes` — `Code`, `Exceptions`, `LineNumberTable`, `LocalVariableTable`, `StackMapTable`, etc.

`.JADE` resolves constant-pool indices at lower time.

### 6.3 Versioning (Rule 50)

Profile caches, code caches, and serialized AOT artifacts are all versioned with a `CompilerVersion` field. A change in the IR layout, `NodeKind` enum, or `TypeId` enum bumps the version and invalidates all caches.

---

## 7. Test Coverage

Every CIL opcode and every JVM opcode has at least one test in `tests/cil/` or `tests/jvm/`. The CI script `tools/check_opcode_coverage.py` verifies this. See `TESTING_DOCTRINE.md` for the full testing protocol.
