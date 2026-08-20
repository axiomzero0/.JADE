// SPDX-License-Identifier: MIT
// .JADE Compiler — jvm/ClassFile.cpp
//
// .class file parser implementation (JVMS §4).

#include "jade/jvm/ClassFile.hpp"

#include <fstream>
#include <sstream>
#include <cstring>

namespace jade::jvm {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// ByteReader — a cursor over raw bytes with bounds checking.
// ─────────────────────────────────────────────────────────────────────────────

class ByteReader {
public:
    ByteReader(std::span<const uint8_t> data) : data_(data) {}

    [[nodiscard]] bool eof() const { return pos_ >= data_.size(); }
    [[nodiscard]] size_t pos() const { return pos_; }
    [[nodiscard]] size_t remaining() const { return data_.size() - pos_; }

    [[nodiscard]] Result<uint8_t> read_u1() {
        if (pos_ + 1 > data_.size()) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("ClassFile: unexpected EOF reading u1 at offset {}", pos_)));
        }
        return data_[pos_++];
    }

    [[nodiscard]] Result<uint16_t> read_u2() {
        if (pos_ + 2 > data_.size()) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("ClassFile: unexpected EOF reading u2 at offset {}", pos_)));
        }
        uint16_t v = (static_cast<uint16_t>(data_[pos_]) << 8) | data_[pos_ + 1];
        pos_ += 2;
        return v;
    }

    [[nodiscard]] Result<uint32_t> read_u4() {
        if (pos_ + 4 > data_.size()) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("ClassFile: unexpected EOF reading u4 at offset {}", pos_)));
        }
        uint32_t v = (static_cast<uint32_t>(data_[pos_]) << 24)
                    | (static_cast<uint32_t>(data_[pos_ + 1]) << 16)
                    | (static_cast<uint32_t>(data_[pos_ + 2]) << 8)
                    | data_[pos_ + 3];
        pos_ += 4;
        return v;
    }

    [[nodiscard]] Result<int32_t> read_s4() {
        auto r = read_u4();
        if (!r) return std::unexpected(r.error());
        return static_cast<int32_t>(*r);
    }

    [[nodiscard]] Result<std::span<const uint8_t>> read_bytes(size_t n) {
        if (pos_ + n > data_.size()) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("ClassFile: unexpected EOF reading {} bytes at offset {}", n, pos_)));
        }
        auto s = data_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

    // Read a modified UTF-8 string (JVMS §4.4.7).
    [[nodiscard]] Result<std::string> read_utf8(uint16_t length) {
        if (pos_ + length > data_.size()) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("ClassFile: unexpected EOF reading Utf8 (len={}) at offset {}", length, pos_)));
        }
        // For simplicity, treat as raw bytes → string. Modified UTF-8 differs
        // from standard UTF-8 only for: null char (0xC0 0x80), supplementary
        // chars (6 bytes). For Java identifiers and descriptors, raw bytes
        // are sufficient.
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), length);
        pos_ += length;
        return s;
    }

private:
    std::span<const uint8_t> data_;
    size_t pos_{0};
};

[[nodiscard]] Result<CpEntry> read_constant_pool_entry(ByteReader& r, CpTag tag) {
    CpEntry e;
    e.tag = tag;
    switch (tag) {
        case CpTag::Utf8: {
            auto len_r = r.read_u2();
            if (!len_r) return std::unexpected(len_r.error());
            auto s_r = r.read_utf8(*len_r);
            if (!s_r) return std::unexpected(s_r.error());
            e.utf8 = std::move(*s_r);
            break;
        }
        case CpTag::Integer:
        case CpTag::Float: {
            auto v_r = r.read_u4();
            if (!v_r) return std::unexpected(v_r.error());
            if (tag == CpTag::Integer) {
                e.i64 = static_cast<int64_t>(static_cast<int32_t>(*v_r));
            } else {
                uint32_t bits = *v_r;
                float f;
                std::memcpy(&f, &bits, 4);
                e.f64 = static_cast<double>(f);
            }
            break;
        }
        case CpTag::Long:
        case CpTag::Double: {
            auto hi_r = r.read_u4();
            if (!hi_r) return std::unexpected(hi_r.error());
            auto lo_r = r.read_u4();
            if (!lo_r) return std::unexpected(lo_r.error());
            uint64_t bits = (static_cast<uint64_t>(*hi_r) << 32) | *lo_r;
            if (tag == CpTag::Long) {
                e.i64 = static_cast<int64_t>(bits);
            } else {
                double d;
                std::memcpy(&d, &bits, 8);
                e.f64 = d;
            }
            break;
        }
        case CpTag::Class:
        case CpTag::String:
        case CpTag::MethodType:
        case CpTag::Module:
        case CpTag::Package: {
            auto i1_r = r.read_u2();
            if (!i1_r) return std::unexpected(i1_r.error());
            e.index1 = *i1_r;
            break;
        }
        case CpTag::Fieldref:
        case CpTag::Methodref:
        case CpTag::InterfaceMethodref:
        case CpTag::NameAndType:
        case CpTag::Dynamic:
        case CpTag::InvokeDynamic: {
            auto i1_r = r.read_u2();
            if (!i1_r) return std::unexpected(i1_r.error());
            auto i2_r = r.read_u2();
            if (!i2_r) return std::unexpected(i2_r.error());
            e.index1 = *i1_r;
            e.index2 = *i2_r;
            break;
        }
        case CpTag::MethodHandle: {
            auto ref_kind_r = r.read_u1();
            if (!ref_kind_r) return std::unexpected(ref_kind_r.error());
            auto ref_r = r.read_u2();
            if (!ref_r) return std::unexpected(ref_r.error());
            e.index1 = *ref_kind_r;  // reuse index1 for ref_kind
            e.index2 = *ref_r;
            break;
        }
        default:
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("ClassFile: unknown constant pool tag {}", static_cast<int>(tag))));
    }
    return e;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// parse_class_file
// ─────────────────────────────────────────────────────────────────────────────

Result<ClassFile> parse_class_file(std::span<const uint8_t> bytes) {
    ByteReader r(bytes);

    // Magic number: 0xCAFEBABE
    auto magic_r = r.read_u4();
    if (!magic_r) return std::unexpected(magic_r.error());
    if (*magic_r != 0xCAFEBABE) {
        return std::unexpected(make_error(ErrorKind::BadInput,
            std::format("ClassFile: bad magic number 0x{:08X} (expected 0xCAFEBABE)", *magic_r)));
    }

    ClassFile cf;

    // Version
    auto minor_r = r.read_u2();
    if (!minor_r) return std::unexpected(minor_r.error());
    auto major_r = r.read_u2();
    if (!major_r) return std::unexpected(major_r.error());
    cf.minor_version = *minor_r;
    cf.major_version = *major_r;

    // Constant pool count
    auto cp_count_r = r.read_u2();
    if (!cp_count_r) return std::unexpected(cp_count_r.error());
    uint16_t cp_count = *cp_count_r;
    cf.constant_pool.reserve(cp_count - 1);

    // Read constant pool entries (1-indexed).
    for (uint16_t i = 1; i < cp_count; ++i) {
        auto tag_r = r.read_u1();
        if (!tag_r) return std::unexpected(tag_r.error());
        auto e_r = read_constant_pool_entry(r, static_cast<CpTag>(*tag_r));
        if (!e_r) return std::unexpected(e_r.error());
        cf.constant_pool.push_back(std::move(*e_r));
        // Long and Double take 2 slots (JVMS §4.4.5).
        if (e_r->tag == CpTag::Long || e_r->tag == CpTag::Double) {
            cf.constant_pool.push_back(CpEntry{});  // unused slot (Long/Double take 2)
            ++i;  // skip the next slot
        }
    }

    // Access flags, this_class, super_class
    auto access_r = r.read_u2();
    if (!access_r) return std::unexpected(access_r.error());
    cf.access_flags = *access_r;

    auto this_r = r.read_u2();
    if (!this_r) return std::unexpected(this_r.error());
    cf.this_class = *this_r;

    auto super_r = r.read_u2();
    if (!super_r) return std::unexpected(super_r.error());
    cf.super_class = *super_r;

    // Resolve class name
    cf.class_name = std::string{cf.cp_class_name(cf.this_class)};

    // Interfaces
    auto iface_count_r = r.read_u2();
    if (!iface_count_r) return std::unexpected(iface_count_r.error());
    uint16_t iface_count = *iface_count_r;
    cf.interfaces.reserve(iface_count);
    for (uint16_t i = 0; i < iface_count; ++i) {
        auto iface_r = r.read_u2();
        if (!iface_r) return std::unexpected(iface_r.error());
        cf.interfaces.push_back(*iface_r);
    }

    // Fields (JVMS §4.5) — we skip them; .JADE doesn't use field layout yet.
    auto field_count_r = r.read_u2();
    if (!field_count_r) return std::unexpected(field_count_r.error());
    uint16_t field_count = *field_count_r;
    for (uint16_t i = 0; i < field_count; ++i) {
        // access_flags, name_index, descriptor_index
        for (int j = 0; j < 3; ++j) {
            auto v = r.read_u2();
            if (!v) return std::unexpected(v.error());
        }
        // attributes_count
        auto attr_count_r = r.read_u2();
        if (!attr_count_r) return std::unexpected(attr_count_r.error());
        uint16_t attr_count = *attr_count_r;
        for (uint16_t a = 0; a < attr_count; ++a) {
            // attribute_name_index (u2), attribute_length (u4)
            auto name_idx_r = r.read_u2();
            if (!name_idx_r) return std::unexpected(name_idx_r.error());
            auto attr_len_r = r.read_u4();
            if (!attr_len_r) return std::unexpected(attr_len_r.error());
            // Skip the attribute bytes.
            auto skip_r = r.read_bytes(*attr_len_r);
            if (!skip_r) return std::unexpected(skip_r.error());
        }
    }

    // Methods (JVMS §4.6)
    auto method_count_r = r.read_u2();
    if (!method_count_r) return std::unexpected(method_count_r.error());
    uint16_t method_count = *method_count_r;
    cf.methods.reserve(method_count);

    for (uint16_t i = 0; i < method_count; ++i) {
        MethodInfo m;
        auto m_access_r = r.read_u2();
        if (!m_access_r) return std::unexpected(m_access_r.error());
        m.access_flags = *m_access_r;

        auto m_name_r = r.read_u2();
        if (!m_name_r) return std::unexpected(m_name_r.error());
        m.name_index = *m_name_r;
        m.name = std::string{cf.cp_utf8(m.name_index)};

        auto m_desc_r = r.read_u2();
        if (!m_desc_r) return std::unexpected(m_desc_r.error());
        m.descriptor_index = *m_desc_r;
        m.descriptor = std::string{cf.cp_utf8(m.descriptor_index)};

        // attributes_count
        auto attr_count_r = r.read_u2();
        if (!attr_count_r) return std::unexpected(attr_count_r.error());
        uint16_t attr_count = *attr_count_r;
        for (uint16_t a = 0; a < attr_count; ++a) {
            auto attr_name_r = r.read_u2();
            if (!attr_name_r) return std::unexpected(attr_name_r.error());
            auto attr_len_r = r.read_u4();
            if (!attr_len_r) return std::unexpected(attr_len_r.error());
            uint32_t attr_len = *attr_len_r;
            std::string_view attr_name = cf.cp_utf8(*attr_name_r);

            if (attr_name == "Code") {
                // Code attribute (JVMS §4.7.3):
                //   u2 max_stack
                //   u2 max_locals
                //   u4 code_length
                //   u1 code[code_length]
                //   u2 exception_table_length
                //   exception_table[]
                //   u2 attributes_count
                //   attributes[]
                auto ms_r = r.read_u2();
                if (!ms_r) return std::unexpected(ms_r.error());
                m.max_stack = *ms_r;

                auto ml_r = r.read_u2();
                if (!ml_r) return std::unexpected(ml_r.error());
                m.max_locals = *ml_r;

                auto cl_r = r.read_u4();
                if (!cl_r) return std::unexpected(cl_r.error());
                uint32_t code_length = *cl_r;
                auto code_r = r.read_bytes(code_length);
                if (!code_r) return std::unexpected(code_r.error());
                m.code.assign((*code_r).begin(), (*code_r).end());

                // exception_table_length
                auto etl_r = r.read_u2();
                if (!etl_r) return std::unexpected(etl_r.error());
                uint16_t etl = *etl_r;
                // Skip exception_table (8 bytes each: start_pc, end_pc, handler_pc, catch_type)
                for (uint16_t e = 0; e < etl; ++e) {
                    for (int j = 0; j < 4; ++j) {
                        auto v = r.read_u2();
                        if (!v) return std::unexpected(v.error());
                    }
                }

                // Code attribute's attributes (nested) — skip all.
                auto code_attr_count_r = r.read_u2();
                if (!code_attr_count_r) return std::unexpected(code_attr_count_r.error());
                uint16_t code_attr_count = *code_attr_count_r;
                for (uint16_t ca = 0; ca < code_attr_count; ++ca) {
                    auto ca_name_r = r.read_u2();
                    if (!ca_name_r) return std::unexpected(ca_name_r.error());
                    auto ca_len_r = r.read_u4();
                    if (!ca_len_r) return std::unexpected(ca_len_r.error());
                    auto skip_r = r.read_bytes(*ca_len_r);
                    if (!skip_r) return std::unexpected(skip_r.error());
                }
            } else {
                // Skip unknown attributes.
                auto skip_r = r.read_bytes(attr_len);
                if (!skip_r) return std::unexpected(skip_r.error());
            }
        }

        cf.methods.push_back(std::move(m));
    }

    // Class-level attributes (skip all).
    auto class_attr_count_r = r.read_u2();
    if (!class_attr_count_r) return std::unexpected(class_attr_count_r.error());
    uint16_t class_attr_count = *class_attr_count_r;
    for (uint16_t a = 0; a < class_attr_count; ++a) {
        auto name_r = r.read_u2();
        if (!name_r) return std::unexpected(name_r.error());
        auto len_r = r.read_u4();
        if (!len_r) return std::unexpected(len_r.error());
        auto skip_r = r.read_bytes(*len_r);
        if (!skip_r) return std::unexpected(skip_r.error());
    }

    return cf;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_class_file_from_file
// ─────────────────────────────────────────────────────────────────────────────

Result<ClassFile> parse_class_file_from_file(std::string_view path) {
    std::ifstream file(std::string{path}, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(make_error(ErrorKind::BadInput,
            std::format("ClassFile: cannot open file: {}", path)));
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buf.data()), size)) {
        return std::unexpected(make_error(ErrorKind::BadInput,
            std::format("ClassFile: cannot read file: {}", path)));
    }
    return parse_class_file(std::span<const uint8_t>{buf});
}

}  // namespace jade::jvm
