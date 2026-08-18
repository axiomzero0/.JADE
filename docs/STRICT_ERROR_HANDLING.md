---
title: "Strict Error Handling Policy"
status: "Mandatory"
owner: "JADE Dev Team"
last_updated: "2026-08-19"
related_rules: ["Rule B.2", "Rule 52", "Rule A.4"]
pass_type: "Policy"
tier: "All"
---

# Strict Error Handling Policy

**Status:** Mandatory  
**Owner:** JADE Dev Team  
**Last Updated:** 2026-08-19  
**Related Rules:** Rule B.2 (No exceptions in hot path), Rule 52 (Correctness-Preserving Fixes), Rule A.4 (Deopt Correctness)

---

## 1. Core Principle: "Fail Fast, Fail Safe, Never Crash"

`.JADE` is a system-level component. It must never crash the host process (the C#/Java application). However, it must also never silently generate incorrect code.

*   **If the Compiler Fails:** The compilation job is aborted, and execution falls back to a lower tier (`granit` interpreter).
*   **If the Runtime Fails:** The process terminates immediately with a diagnostic dump. There is no "recovery" from a corrupted GC or broken safepoint.

---

## 2. The `Result<T>` Mandate

All fallible operations in the JIT pipeline **must** return `std::expected<T, Error>` (aliased as `Result<T>`).

*   **Forbidden:** `throw`, `catch`, `std::optional` (for fallible logic), or raw error codes.
*   **Required:** Every function that can fail must explicitly declare its error type in its signature.

```cpp
// ✅ Correct
Result<NodeId> add_node(NodeKind kind, TypeId type);

// ❌ Forbidden
NodeId add_node(NodeKind kind, TypeId type); // How do we know if it failed?
```

The `Result<T>` alias and `Error` struct are defined in `src/jade/core/Result.hpp`.

---

## 3. Error Classification

| Category | Examples | Action |
| :--- | :--- | :--- |
| **Recoverable (Compiler)** | Out of memory (Arena), unsupported CIL/JVM opcode, verification failure, register spill overflow. | **Abort Compilation.** Log the error, discard the current IR, and force the method to run in `granit` (T0). |
| **Fatal (Runtime)** | GC heap corruption, safepoint handshake deadlock, invalid machine code execution, stack overflow in interpreter. | **Immediate Termination.** Dump core, print stack trace, and exit with code 1. Do not attempt to continue. |
| **Speculative (Deopt)** | Guard failure (e.g., `CheckInt` fails because value is `float`). | **Deoptimize.** Capture `FrameState`, jump to `granit` at the correct bytecode offset. This is **not** an error; it is a control flow transition. |

The `ErrorKind` enum in `src/jade/core/Result.hpp` distinguishes these:

```cpp
enum class ErrorKind : uint8_t {
    // Recoverable (compiler)
    InvalidIR, VerificationFailed, UnsupportedNode, OutOfBudget,
    ProfileUnavailable, DeoptMissingState, BadInput,

    // Fatal (runtime)
    Internal, IO,
};
```

---

## 4. The "No Silent Failure" Rule

*   **Every `Result` must be checked.** Ignoring a `Result` is a compile-time error enforced by `-Werror=unused-result` (via `[[nodiscard]]` on `Result<T>`).
*   **Every error must have a message.** `Error{ErrorKind::OutOfBudget, "Arena exhausted in GVN pass"}` is required. No empty error objects.
*   **Every fallback must be logged.** If T1 fails and we fall back to T0, a warning is emitted to the debug log:
    ```
    [WARN] JADE compilation failed for MethodX: Arena exhausted. Falling back to granit.
    ```

The logger is in `src/jade/runtime/Logger.hpp` (planned). For the initial milestone, warnings go to `stderr`.

---

## 5. Error Propagation Chain

1.  **Leaf Function:** Detects error → Returns `Result::error(...)`.
2.  **Parent Function:** Checks result → If error, performs cleanup (arena reset) → Returns `Result::error(...)`.
3.  **Top-Level Driver:** Receives error → Logs diagnostic → Switches method tier to T0 → Continues execution.

### Worked example: T1 compilation fails

```cpp
// src/jade/tier1_jade/JadeJit.cpp
Result<void*> JadeJit::compile(const Graph& graph) {
    // 1. Allocate registers
    auto regs = allocator_.allocate(graph);
    if (!regs) {
        log_warn("JADE register allocation failed: {}. Falling back to granit.",
                 regs.error().what());
        return std::unexpected(regs.error());
    }

    // 2. Emit code
    auto emitted = emitter_.emit(graph, *regs);
    if (!emitted) {
        log_warn("JADE code emission failed: {}. Falling back to granit.",
                 emitted.error().what());
        return std::unexpected(emitted.error());
    }

    return *emitted;
}
```

The caller (the dispatch loop) sees the error and switches the method's tier to T0 (`granit`).

---

## 6. Diagnostic Requirements

When a recoverable error occurs, the following must be captured for the **Replay Log (Rule 40)**:

*   The specific `Error` code and message.
*   The current state of the IR (dumped to `.ir` file).
*   The profiling data that triggered the compilation.
*   The RNG seed used for any non-deterministic passes.

The replay artifacts are saved to `tests/replay/failed/<test-name>-<commit-sha>/`. See `docs/TESTING_DOCTRINE.md` §7.

---

## 7. Implementation Example

```cpp
Result<void*> JadeJit::compile(const ir::Graph& graph) {
    // 1. Initialize asmjit
    auto code = emitter_.init();
    if (!code) return std::unexpected(code.error());

    // 2. Allocate registers
    auto regs = allocator_.allocate(graph);
    if (!regs) return std::unexpected(make_error(
        ErrorKind::OutOfBudget,
        std::format("register allocation failed: {}", regs.error().what())));

    // 3. Emit code
    for (std::size_t i = 0; i < graph.size(); ++i) {
        const NodeId id{static_cast<uint32_t>(i + 1)};
        auto res = emitter_.emit_node(graph, id, *regs);
        if (!res) {
            arena_.reset();  // cleanup
            return std::unexpected(make_error(
                ErrorKind::UnsupportedNode,
                std::format("emit_node {} failed: {}",
                            static_cast<uint32_t>(id), res.error().what())));
        }
    }

    // 4. Finalize
    void* ptr = nullptr;
    auto err = emitter_.finalize(&ptr);
    if (!err) return std::unexpected(err.error());

    return ptr;
}
```

---

## 8. Enforcement

*   **Static Analysis:** CI runs `tools/check_error_handling.py` (planned), which flags any function returning `Result<T>` that is not explicitly handled via `.has_value()` / `.error()` checks.
*   **Code Review:** Any PR that adds a `throw` or `catch` block to `src/jade/` (other than `src/jade/tier0_granit/` and `src/jade/driver/`) is automatically rejected.
*   **Compile-time:** `-fno-exceptions` on `jade_core` enforces no `throw` at the language level.
*   **`[[nodiscard]]`:** Every `Result<T>` returning function is `[[nodiscard]]`, so ignoring the result is a compile warning (escalated to error via `-Werror=unused-result`).

---

## 9. The `JADE_ASSERT` Macro

For invariant checks that should never fail in production (Rule 42 verifier invariants, internal consistency checks), we use `JADE_ASSERT`:

```cpp
#define JADE_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            ::jade::detail::fatal_assert(__FILE__, __LINE__, #cond, msg); \
        } \
    } while (0)
```

`JADE_ASSERT` calls `std::abort()` — it is for "this can never happen" cases, not for recoverable errors. Recoverable errors use `Result<T>`.

In release builds, `JADE_ASSERT` is compiled out (`NDEBUG`), so it has zero cost. In debug builds, it catches invariant violations early.

---

## 10. Summary

| Pattern | Use |
| :--- | :--- |
| `Result<T>` | Every fallible operation. |
| `JADE_ASSERT` | Invariant checks that "can never fail". |
| `throw` / `catch` | Forbidden in `src/jade/core/`, `src/jade/ir/`, `src/jade/tier1_jade/`, `src/jade/tier2_ruby/`, `src/jade/tier3_diamond/`. Allowed in `src/jade/tier0_granit/` (interpreter runtime errors) and `src/jade/driver/` (I/O). |
| `std::optional` | Allowed for "value may be absent" semantics (e.g., `std::optional<NodeId> find_node(...)`). Not for error reporting. |
| `std::abort()` | Used by `JADE_ASSERT` and fatal runtime errors. |
