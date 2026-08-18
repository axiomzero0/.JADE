// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Value.hpp
//
// Runtime value type for the granit CIL interpreter.
// Models the CLR evaluation stack types (ECMA-335 §III.1.1):
//   - int32, int64, native int
//   - float32 / float64 (collapsed to F on the eval stack)
//   - object reference (O)
//   - managed pointer (&)
//   - transient pointer (*) — unmanaged; unsafe contexts only

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <optional>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// ObjectHandle — a 64-bit ID that resolves through the GC's handle table.
// All reference types (classes, arrays, strings, delegates) are ObjectHandles.
//
// We use a tagged 64-bit value: the low 32 bits are the object index,
// the high 32 bits are a generation counter (so a stale handle fails to
// resolve after the object is collected).
// ─────────────────────────────────────────────────────────────────────────────
struct ObjectHandle {
    uint64_t value{0};

    ObjectHandle() = default;
    explicit constexpr ObjectHandle(uint64_t v) noexcept : value(v) {}

    [[nodiscard]] constexpr bool is_null() const noexcept { return value == 0; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const ObjectHandle&) const noexcept = default;

    [[nodiscard]] constexpr uint32_t index() const noexcept {
        return static_cast<uint32_t>(value & 0xFFFF'FFFFu);
    }
    [[nodiscard]] constexpr uint32_t generation() const noexcept {
        return static_cast<uint32_t>(value >> 32);
    }

    static constexpr ObjectHandle null() noexcept { return ObjectHandle{0}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// ManagedPointer — interior pointer to a field of a GC-managed object,
//   or to an element of a managed array. Tracked by the GC.
// ─────────────────────────────────────────────────────────────────────────────
struct ManagedPointer {
    ObjectHandle base;       // owning object (or null for stack-allocated)
    int64_t      offset{0};  // byte offset from base

    ManagedPointer() = default;
    ManagedPointer(ObjectHandle b, int64_t o) : base(b), offset(o) {}

    [[nodiscard]] constexpr bool is_null() const noexcept { return base.is_null() && offset == 0; }
    [[nodiscard]] constexpr bool operator==(const ManagedPointer&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// NativePointer — unmanaged pointer. Only used in unsafe contexts.
// ─────────────────────────────────────────────────────────────────────────────
struct NativePointer {
    uintptr_t value{0};

    NativePointer() = default;
    explicit constexpr NativePointer(uintptr_t v) noexcept : value(v) {}

    [[nodiscard]] constexpr bool is_null() const noexcept { return value == 0; }
    [[nodiscard]] constexpr bool operator==(const NativePointer&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Value — tagged union of all CIL evaluation-stack types.
//
// The variant index doubles as the EvalStackType (with a fixed mapping).
// All primitive types are stored by value; reference types use ObjectHandle.
// ─────────────────────────────────────────────────────────────────────────────
using Value = std::variant<
    std::monostate,         // 0 — uninitialized
    int32_t,                // 1 — int32
    int64_t,                // 2 — int64
    float,                  // 3 — F (float32 promoted)
    double,                 // 4 — F (float64)
    ObjectHandle,           // 5 — O
    ManagedPointer,         // 6 — &
    NativePointer           // 7 — *
>;

// Index helpers — map EvalStackType to variant index and back.
enum class EvalStackType : uint8_t {
    Uninitialized = 0,
    Int32          = 1,
    Int64          = 2,
    Float          = 3,  // both float variants collapse to F (CIL §III.1.1)
    ObjectRef      = 5,
    ManagedPtr     = 6,
    NativePtr      = 7,
};

// Returns the CIL evaluation-stack type of a Value. Per ECMA-335 §III.1.1,
// float32 and float64 collapse to a single type F on the eval stack.
[[nodiscard]] inline EvalStackType eval_type_of(const Value& v) noexcept {
    // Both `float` (variant index 3) and `double` (index 4) map to EvalStackType::Float.
    const auto idx = v.index();
    if (idx == 3 || idx == 4) return EvalStackType::Float;
    return static_cast<EvalStackType>(idx);
}

// Truthiness — matches C# semantics.
[[nodiscard]] bool truthy(const Value& v);

// Equality — matches C# `==` for primitive types.
[[nodiscard]] bool value_equals(const Value& a, const Value& b);

// Pretty-printing for debug (NOT used in hot loops).
[[nodiscard]] std::string to_string(const Value& v);

// ─────────────────────────────────────────────────────────────────────────────
// Constructors — convenient Value factories.
// ─────────────────────────────────────────────────────────────────────────────
inline Value make_int32(int32_t v)  { return Value{v}; }
inline Value make_int64(int64_t v)  { return Value{v}; }
inline Value make_float(double v)  { return Value{v}; }
inline Value make_object(ObjectHandle h) { return Value{h}; }
inline Value make_managed_ptr(ManagedPointer p) { return Value{p}; }
inline Value make_native_ptr(NativePointer p) { return Value{p}; }
inline Value make_null_object() { return Value{ObjectHandle::null()}; }

// ─────────────────────────────────────────────────────────────────────────────
// Integer overflow-safe arithmetic helpers (Rule A.4 — must match granit).
// C# integer arithmetic uses two's-complement wraparound for unchecked context;
// for `checked` context, OverflowException is thrown.
// ─────────────────────────────────────────────────────────────────────────────
[[nodiscard]] int32_t wrap_add_i32(int32_t a, int32_t b) noexcept;
[[nodiscard]] int32_t wrap_sub_i32(int32_t a, int32_t b) noexcept;
[[nodiscard]] int32_t wrap_mul_i32(int32_t a, int32_t b) noexcept;
[[nodiscard]] int64_t wrap_add_i64(int64_t a, int64_t b) noexcept;
[[nodiscard]] int64_t wrap_sub_i64(int64_t a, int64_t b) noexcept;
[[nodiscard]] int64_t wrap_mul_i64(int64_t a, int64_t b) noexcept;

[[nodiscard]] bool checked_add_i32(int32_t a, int32_t b, int32_t& out) noexcept;
[[nodiscard]] bool checked_sub_i32(int32_t a, int32_t b, int32_t& out) noexcept;
[[nodiscard]] bool checked_mul_i32(int32_t a, int32_t b, int32_t& out) noexcept;

}  // namespace jade::granit
