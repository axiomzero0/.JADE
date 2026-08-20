// SPDX-License-Identifier: MIT
// .JADE Compiler — jvm/ClassFile.hpp
//
// .class file parser. Reads a JVM .class file (per JVMS §4) and extracts:
//   - Constant pool (for resolving field/method/class references)
//   - Method definitions (name, descriptor, max_locals, max_stack, code)
//   - Class name, super class, interfaces
//
// The parser is a frontend: it takes raw .class bytes and produces a
// structured representation that the JvmLowerer can consume.
//
// Reference: JVMS §4 "The class File Format"

#pragma once

#include "jade/core/Result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <optional>

namespace jade::jvm {

// ─────────────────────────────────────────────────────────────────────────────
// Constant pool entries (JVMS §4.4)
// ─────────────────────────────────────────────────────────────────────────────

enum class CpTag : uint8_t {
    Utf8               = 1,
    Integer            = 3,
    Float              = 4,
    Long              = 5,
    Double             = 6,
    Class              = 7,
    String             = 8,
    Fieldref           = 9,
    Methodref          = 10,
    InterfaceMethodref = 11,
    NameAndType        = 12,
    MethodHandle       = 15,
    MethodType         = 16,
    Dynamic            = 17,
    InvokeDynamic      = 18,
    Module             = 19,
    Package            = 20,
};

struct CpEntry {
    CpTag tag;
    // For Utf8: the string data.
    std::string utf8;
    // For Integer/Long: the value.
    int64_t i64{0};
    // For Float/Double: the value.
    double f64{0.0};
    // For Class/String/Methodref/etc.: index into constant pool.
    uint16_t index1{0};
    uint16_t index2{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Method info (JVMS §4.6)
// ─────────────────────────────────────────────────────────────────────────────

struct MethodInfo {
    uint16_t access_flags{0};
    uint16_t name_index{0};        // index into constant pool (Utf8)
    uint16_t descriptor_index{0};  // index into constant pool (Utf8)
    // Code attribute (JVMS §4.7.3)
    uint16_t max_stack{0};
    uint16_t max_locals{0};
    std::vector<uint8_t> code;
    // The method's name and descriptor (resolved from constant pool).
    std::string name;
    std::string descriptor;
};

// ─────────────────────────────────────────────────────────────────────────────
// ClassFile — the parsed .class file.
// ─────────────────────────────────────────────────────────────────────────────

struct ClassFile {
    uint16_t minor_version{0};
    uint16_t major_version{0};
    std::vector<CpEntry> constant_pool;   // 1-indexed (index 0 is unused)
    uint16_t access_flags{0};
    uint16_t this_class{0};               // index into constant pool (Class)
    uint16_t super_class{0};              // index into constant pool (Class)
    std::vector<uint16_t> interfaces;
    std::vector<MethodInfo> methods;
    std::string class_name;               // resolved from this_class

    // ── Constant pool queries ──────────────────────────────────────────

    [[nodiscard]] const CpEntry* cp(uint16_t index) const {
        if (index == 0 || index > constant_pool.size()) return nullptr;
        return &constant_pool[index - 1];
    }

    [[nodiscard]] std::string_view cp_utf8(uint16_t index) const {
        if (const CpEntry* e = cp(index); e && e->tag == CpTag::Utf8) {
            return e->utf8;
        }
        return {};
    }

    [[nodiscard]] std::string_view cp_class_name(uint16_t index) const {
        if (const CpEntry* e = cp(index); e && e->tag == CpTag::Class) {
            return cp_utf8(e->index1);
        }
        return {};
    }

    // Find a method by name. Returns nullptr if not found.
    [[nodiscard]] const MethodInfo* find_method(std::string_view name) const {
        for (const auto& m : methods) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }

    // Find the main method (standard Java entry point).
    [[nodiscard]] const MethodInfo* find_main() const {
        return find_method("main");
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// parse_class_file — parse a .class file from raw bytes.
//
// Returns a ClassFile on success, or an error explaining what went wrong.
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] Result<ClassFile> parse_class_file(std::span<const uint8_t> bytes);

// ─────────────────────────────────────────────────────────────────────────────
// parse_class_file_from_file — convenience: read a .class file from disk.
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] Result<ClassFile> parse_class_file_from_file(std::string_view path);

}  // namespace jade::jvm
