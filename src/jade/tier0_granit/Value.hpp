// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Value.hpp
//
// Runtime value type for the granit CIL/JVM interpreter.
//
// Performance-critical design (per the performance review):
//   - Compact 16-byte Value: 64-bit payload + 8-bit tag + padding.
//   - No std::variant (avoids 24-byte tagged union + branchy holds_alternative).
//   - Arithmetic ops: single tag switch + single ALU op.
//   - Full 64-bit double precision (no NaN-boxing truncation).
//
// Layout:  [ 64-bit payload | 8-bit tag | 8 bytes padding ]
// The payload is interpreted based on the tag. For Float, the payload
// IS the double (full precision). For Int32/Int64, the payload is the
// sign-extended integer. For ObjectRef, the payload is the ObjectHandle.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <bit>

namespace jade::granit {

// ─────────────────────────────────────────────────────────────────────────────
// ObjectHandle — a 64-bit ID that resolves through the GC's handle table.
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
// ManagedPointer — packed into 64 bits.
// ─────────────────────────────────────────────────────────────────────────────
struct ManagedPointer {
    uint64_t packed{0};

    ManagedPointer() = default;
    ManagedPointer(ObjectHandle b, int64_t o) noexcept {
        packed = (static_cast<uint64_t>(b.index()) << 32) | static_cast<uint32_t>(o);
    }

    [[nodiscard]] constexpr bool is_null() const noexcept { return packed == 0; }
    [[nodiscard]] constexpr bool operator==(const ManagedPointer&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// NativePointer — unmanaged pointer.
// ─────────────────────────────────────────────────────────────────────────────
struct NativePointer {
    uintptr_t value{0};

    NativePointer() = default;
    explicit constexpr NativePointer(uintptr_t v) noexcept : value(v) {}

    [[nodiscard]] constexpr bool is_null() const noexcept { return value == 0; }
    [[nodiscard]] constexpr bool operator==(const NativePointer&) const noexcept = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// ValueTag — the type discriminator.
// ─────────────────────────────────────────────────────────────────────────────
enum class ValueTag : uint8_t {
    Uninit      = 0,
    Int32       = 1,
    Int64       = 2,
    Float       = 3,
    ObjectRef   = 4,
    Null        = 5,
    ManagedPtr  = 6,
    NativePtr   = 7,
};

// ─────────────────────────────────────────────────────────────────────────────
// Value — compact 16-byte tagged value.
//
//   payload (8 bytes): the value data (int, double, or object handle).
//   tag (1 byte): the ValueTag.
//   _pad (7 bytes): explicit padding for alignment.
//
// This is NOT NaN-boxed (which would be 8 bytes). We use 16 bytes for
// full double precision and simpler code. The 16-byte size still fits
// two Values per 32-byte cache line.
//
// The payload is a union; we access it via the tag.
// ─────────────────────────────────────────────────────────────────────────────
class Value {
public:
    Value() noexcept : tag_(ValueTag::Uninit), pad_{0} { payload_.i64 = 0; }

    // ── Type queries (single comparison) ───────────────────────────────
    [[nodiscard]] constexpr ValueTag tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr bool is_int32() const noexcept { return tag_ == ValueTag::Int32; }
    [[nodiscard]] constexpr bool is_int64() const noexcept { return tag_ == ValueTag::Int64; }
    [[nodiscard]] constexpr bool is_float() const noexcept { return tag_ == ValueTag::Float; }
    [[nodiscard]] constexpr bool is_object() const noexcept {
        return tag_ == ValueTag::ObjectRef || tag_ == ValueTag::Null;
    }
    [[nodiscard]] constexpr bool is_managed_ptr() const noexcept { return tag_ == ValueTag::ManagedPtr; }
    [[nodiscard]] constexpr bool is_native_ptr() const noexcept { return tag_ == ValueTag::NativePtr; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return tag_ == ValueTag::Null; }
    [[nodiscard]] constexpr bool is_uninit() const noexcept { return tag_ == ValueTag::Uninit; }

    // ── Payload accessors ──────────────────────────────────────────────
    [[nodiscard]] constexpr int32_t as_int32() const noexcept { return static_cast<int32_t>(payload_.i64); }
    [[nodiscard]] constexpr int64_t as_int64() const noexcept { return payload_.i64; }
    [[nodiscard]] double as_float() const noexcept { return payload_.f64; }
    [[nodiscard]] constexpr ObjectHandle as_object() const noexcept { return payload_.obj; }
    [[nodiscard]] constexpr ManagedPointer as_managed_ptr() const noexcept { return payload_.mptr; }
    [[nodiscard]] constexpr NativePointer as_native_ptr() const noexcept { return payload_.nptr; }

    // ── Constructors ───────────────────────────────────────────────────
    static Value from_int32(int32_t v) noexcept {
        Value r;
        r.tag_ = ValueTag::Int32;
        r.payload_.i64 = static_cast<int64_t>(v);  // sign-extend
        return r;
    }
    static Value from_int64(int64_t v) noexcept {
        Value r;
        r.tag_ = ValueTag::Int64;
        r.payload_.i64 = v;
        return r;
    }
    static Value from_float(double v) noexcept {
        Value r;
        r.tag_ = ValueTag::Float;
        r.payload_.f64 = v;
        return r;
    }
    static Value from_object(ObjectHandle h) noexcept {
        Value r;
        if (h.is_null()) {
            r.tag_ = ValueTag::Null;
            r.payload_.obj = ObjectHandle::null();
        } else {
            r.tag_ = ValueTag::ObjectRef;
            r.payload_.obj = h;
        }
        return r;
    }
    static Value from_managed_ptr(ManagedPointer p) noexcept {
        Value r;
        r.tag_ = ValueTag::ManagedPtr;
        r.payload_.mptr = p;
        return r;
    }
    static Value from_native_ptr(NativePointer p) noexcept {
        Value r;
        r.tag_ = ValueTag::NativePtr;
        r.payload_.nptr = p;
        return r;
    }
    static Value null_value() noexcept {
        Value r;
        r.tag_ = ValueTag::Null;
        r.payload_.obj = ObjectHandle::null();
        return r;
    }
    static Value uninit() noexcept {
        Value r;
        r.tag_ = ValueTag::Uninit;
        return r;
    }

    [[nodiscard]] constexpr bool operator==(const Value& o) const noexcept {
        return tag_ == o.tag_ && payload_.i64 == o.payload_.i64;
    }

private:
    union Payload {
        int64_t       i64;
        double        f64;
        ObjectHandle  obj;
        ManagedPointer mptr;
        NativePointer nptr;
        Payload() : i64(0) {}
    };
    Payload   payload_{};
    ValueTag  tag_{ValueTag::Uninit};
    uint8_t   pad_[7]{0, 0, 0, 0, 0, 0, 0};  // pad to 16 bytes
};

static_assert(sizeof(Value) == 16, "Value must be exactly 16 bytes");

// EvalStackType — for compatibility with existing code.
enum class EvalStackType : uint8_t {
    Uninitialized = 0, Int32 = 1, Int64 = 2, Float = 3,
    ObjectRef = 5, ManagedPtr = 6, NativePtr = 7,
};

[[nodiscard]] inline EvalStackType eval_type_of(const Value& v) noexcept {
    switch (v.tag()) {
        case ValueTag::Int32:      return EvalStackType::Int32;
        case ValueTag::Int64:      return EvalStackType::Int64;
        case ValueTag::Float:      return EvalStackType::Float;
        case ValueTag::ObjectRef:
        case ValueTag::Null:       return EvalStackType::ObjectRef;
        case ValueTag::ManagedPtr:  return EvalStackType::ManagedPtr;
        case ValueTag::NativePtr:   return EvalStackType::NativePtr;
        default:                   return EvalStackType::Uninitialized;
    }
}

// Truthiness — C# semantics. Single tag switch.
[[nodiscard]] inline bool truthy(const Value& v) noexcept {
    switch (v.tag()) {
        case ValueTag::Int32:      return v.as_int32() != 0;
        case ValueTag::Int64:      return v.as_int64() != 0;
        case ValueTag::Float:      return v.as_float() != 0.0;
        case ValueTag::ObjectRef:  return !v.as_object().is_null();
        case ValueTag::Null:       return false;
        case ValueTag::ManagedPtr: return !v.as_managed_ptr().is_null();
        case ValueTag::NativePtr:  return !v.as_native_ptr().is_null();
        default:                   return false;
    }
}

[[nodiscard]] inline bool value_equals(const Value& a, const Value& b) noexcept {
    if (a.tag() != b.tag()) return false;
    return a == b;
}

[[nodiscard]] std::string to_string(const Value& v);

// ── Factory functions (backwards compat) ────────────────────────────────────
inline Value make_int32(int32_t v)        { return Value::from_int32(v); }
inline Value make_int64(int64_t v)        { return Value::from_int64(v); }
inline Value make_float(double v)         { return Value::from_float(v); }
inline Value make_object(ObjectHandle h)  { return Value::from_object(h); }
inline Value make_managed_ptr(ManagedPointer p) { return Value::from_managed_ptr(p); }
inline Value make_native_ptr(NativePointer p)    { return Value::from_native_ptr(p); }
inline Value make_null_object()           { return Value::null_value(); }

// ── Arithmetic helpers ───────────────────────────────────────────────────────
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
