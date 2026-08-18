# Sea of Nodes IR Design

The graph is the heart of .JADE. It is designed for fast traversal, mutation, cheap allocation, explicit memory/effect dependencies, and easy lowering to `asmjit`.

---

## Design Principles

### 1. Nodes are compact value objects
No separate C++ classes with virtual functions for every node kind. Use a flat `struct Node` with `NodeKind`, `NodeFlags`, `TypeId`, and indices into side pools. Target `sizeof(Node) ≈ 32 bytes`.

```cpp
struct Node {
    NodeKind   kind;        // 1 byte  — switch-dispatch key (B.3)
    NodeFlags  flags;       // 1 byte  — bitmask (Rule 51)
    uint8_t    num_inputs;  // 1 byte
    uint8_t    num_outputs; // 1 byte  (saturated at 255)
    TypeId     type;        // 4 bytes — type lattice element
    EdgeSlice  inputs;     // 8 bytes — {first_edge, count} into global pool
    EdgeSlice  ctrl_in;    // 8 bytes — control input (single, but slice for uniformity)
    EdgeSlice  effect_in;  // 8 bytes — effect input
    FrameStateId state;    // 4 bytes — for guards / deopt reconstruction
};
// total = 32 bytes (padded)
```

### 2. Use stable `NodeId`s, not raw pointers, for long-lived data
Pointers are invalidated when the arena grows. `NodeId`s are stable until the arena is freed. This is also mandatory because arena memory may relocate; IDs index into a side vector of `Node` slots.

```cpp
struct NodeId { uint32_t value; };
```

### 3. Inputs are in a separate edge pool
Each `Node` references a contiguous slice of `NodeId`s in a global edge pool. Adding/removing inputs is "rewrite the slice, update `first_input`". This keeps `sizeof(Node)` at ~32 bytes regardless of arity.

```cpp
struct EdgeSlice {
    uint32_t first_edge;   // index into the global EdgePool
    uint32_t count;
};
```

### 4. Data, control, and effect edges are first-class
Keep them as distinct slices. Mixing them makes passes harder.

- **Data edges**: pure value flow (`Add`, `Const`, `LoadField` value, ...).
- **Control edges**: control flow graph (`Start`, `If`, `Region`, `Loop`, `Return`).
- **Effect edges**: memory ordering (`StoreField`, `Call`, `Allocate`, `LoadField`'s effect input).

### 5. Memory is explicit via effect edges
Every effectful operation (`STORE`, `CALL`, `ALLOC`) is linked in an effect chain. Pure operations (`ADD`, `CMP`) have no effect edges, enabling unrestricted code motion.

```
   Start ──ctrl──► If ──ctrl──► Region
                  │             │
                  ▼             ▼
   ────effect─── Allocate ──effect── StoreField.x ──effect── LoadField.x
                                                ▲
                                                │ data
                                              Const
```

---

## NodeKind Enum

A flat, scoped enum (Rule B.3). Passes dispatch on `kind` via `switch`.

```cpp
enum class NodeKind : uint8_t {
    // Control nodes
    Start, Region, Loop, If, IfTrue, IfFalse, Switch, Jump, Return, Throw,

    // Constants
    ConstInt, ConstFloat, ConstBool, ConstNull, ConstString,

    // Arithmetic (pure)
    Add, Sub, Mul, Div, Mod, Neg,
    And, Or, Xor, Not, Shl, Shr, Sar,
    Eq, Ne, Lt, Gt, Lte, Gte,

    // Type operations
    CheckInt, CheckNotNull, CheckShape, CheckBounds, CheckClass,
    ToFloat, ToInt, ToBool, IsInt, IsFloat, IsNull,

    // Memory
    Allocate, LoadField, StoreField, LoadElement, StoreElement, ArrayLength,

    // Calls
    Call, CallKnown, TailCall,

    // Phi/Copy
    Phi, Copy,

    // Misc
    FrameState, Safepoint, Deopt, Unreachable,
};
```

---

## NodeFlags Bitmask (Rule 51)

All orthogonal boolean properties on a hot-path node are bitmasked. Raw `int` flag fields are forbidden.

```cpp
enum class NodeFlag : uint16_t {
    None        = 0,
    Pure        = 1u << 0,   // no side effects
    Effect      = 1u << 1,   // participates in effect chain
    Control     = 1u << 2,   // participates in control flow
    Commutative = 1u << 3,   // sort inputs before hashing
    Associative = 1u << 4,
    NoThrow     = 1u << 5,   // call cannot throw
    IsGuard     = 1u << 6,   // requires FrameState
    HasState    = 1u << 7,   // FrameState attached
    IsConst     = 1u << 8,
    IsDead      = 1u << 9,
    IsScheduled = 1u << 10,
    HasSideExit = 1u << 11,  // can deopt
};
```

Wrapped via `Flags<NodeFlag>` (see `src/jade/core/Flags.hpp`) — type-safe OR/AND/test operations with symbolic printing.

---

## TypeId Lattice

A small lattice for type propagation. Used by SCCP, type narrowing, and devirtualization.

```cpp
enum class TypeId : uint32_t {
    Bottom  = 0,  // ⊥ — unreachable
    Top     = 1,  // ⊤ — unknown
    Int     = 2,
    Float   = 3,
    Bool    = 4,
    Null    = 5,
    Object  = 6,
    Array   = 7,
    String  = 8,
    // ...
};
```

---

## Effect Chain Invariants (verified by Rule 42)

1. Every node with `Effect` flag must have **exactly one** effect input.
2. Effect chains must be acyclic.
3. Every `StoreField`/`StoreElement`/`Call`/`Allocate` must have a `FrameState` if it can deopt.
4. A pure node (`Pure` flag) must have **zero** effect inputs.
5. Effect chains terminate at the `Start` node (or `Loop` phi for cyclic effect).

---

## Memory Layout of the Graph

```
┌──────────────────────────────────────────────────────────┐
│ Graph (per compilation unit, per compiler thread)        │
│                                                          │
│  ┌────────────────┐   ┌──────────────────────────────┐  │
│  │ Node slots     │   │ Edge pool (NodeId[])         │  │
│  │  [0] Start     │   │  [0..2]  Start.ctrl_in      │  │
│  │  [1] ConstInt 3 │   │  [3..4]  Add.data_in       │  │
│  │  [2] ConstInt 4 │   │  ...                       │  │
│  │  [3] Add        │   │                             │  │
│  │  ...            │   │                             │  │
│  └────────────────┘   └──────────────────────────────┘  │
│                                                          │
│  Side tables: TypeFeedback, ProfileData, FrameStates    │
└──────────────────────────────────────────────────────────┘
        ▲
        │ all allocated from
        │
   BumpAllocator (thread-local arena, B.1)
```

---

## Use-Def Lists

Each `Node` has a `num_outputs` count and a `first_use` index into a `UsePool`. Adding an output is O(1) amortized. Removing an output (when a use rewires) is O(degree of the node) — this is acceptable because pass writers batch rewires.

```cpp
struct UseRef {
    NodeId user;     // the node that uses us
    uint8_t slot;    // which input slot of `user` we occupy
};
```
