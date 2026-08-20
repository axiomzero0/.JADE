// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/CilInterpreter.cpp
//
// Real CIL bytecode interpreter — exception-free hot path.
// All errors are reported via CilFrame::error flag, checked by the
// main loop after each opcode. No throws, no try/catch, no unwind tables.

#include "jade/tier0_granit/CilInterpreter.hpp"
#include "jade/cil/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"

#include <format>

namespace jade::granit {

namespace {

// Type-access helpers — return a default value on type mismatch and
// set frame.error. No throws.
[[nodiscard]] inline int32_t as_int32(const Value& v, CilFrame& f) {
    if (__builtin_expect(v.is_int32(), 1)) return v.as_int32();
    if (v.is_int64()) return static_cast<int32_t>(v.as_int64());
    f.error = true; f.error_msg = "CIL: expected int32";
    return 0;
}

[[nodiscard]] inline int64_t as_int64(const Value& v, CilFrame& f) {
    if (__builtin_expect(v.is_int64(), 1)) return v.as_int64();
    if (v.is_int32()) return v.as_int32();
    f.error = true; f.error_msg = "CIL: expected int64";
    return 0;
}

[[nodiscard]] inline double as_float64(const Value& v, CilFrame& f) {
    if (v.is_float()) return v.as_float();
    if (v.is_int32()) return static_cast<double>(v.as_int32());
    if (v.is_int64()) return static_cast<double>(v.as_int64());
    f.error = true; f.error_msg = "CIL: expected float";
    return 0.0;
}

// Arithmetic — no throws, return Value::uninit() on error.
[[nodiscard]] inline Value arith_add(Value a, Value b, CilFrame& f) {
    if (__builtin_expect(a.is_int32() && b.is_int32(), 1))
        return Value::from_int32(wrap_add_i32(a.as_int32(), b.as_int32()));
    if (a.is_int64() && b.is_int64())
        return Value::from_int64(wrap_add_i64(a.as_int64(), b.as_int64()));
    if (a.is_float() || b.is_float())
        return Value::from_float(as_float64(a, f) + as_float64(b, f));
    f.error = true; f.error_msg = "CIL add: invalid types";
    return Value::uninit();
}

[[nodiscard]] inline Value arith_sub(Value a, Value b, CilFrame& f) {
    if (__builtin_expect(a.is_int32() && b.is_int32(), 1))
        return Value::from_int32(wrap_sub_i32(a.as_int32(), b.as_int32()));
    if (a.is_int64() && b.is_int64())
        return Value::from_int64(wrap_sub_i64(a.as_int64(), b.as_int64()));
    if (a.is_float() || b.is_float())
        return Value::from_float(as_float64(a, f) - as_float64(b, f));
    f.error = true; f.error_msg = "CIL sub: invalid types";
    return Value::uninit();
}

[[nodiscard]] inline Value arith_mul(Value a, Value b, CilFrame& f) {
    if (__builtin_expect(a.is_int32() && b.is_int32(), 1))
        return Value::from_int32(wrap_mul_i32(a.as_int32(), b.as_int32()));
    if (a.is_int64() && b.is_int64())
        return Value::from_int64(wrap_mul_i64(a.as_int64(), b.as_int64()));
    if (a.is_float() || b.is_float())
        return Value::from_float(as_float64(a, f) * as_float64(b, f));
    f.error = true; f.error_msg = "CIL mul: invalid types";
    return Value::uninit();
}

[[nodiscard]] inline Value arith_div(Value a, Value b, CilFrame& f) {
    if (a.is_int32() && b.is_int32()) {
        if (__builtin_expect(b.as_int32() == 0, 0)) {
            f.error = true; f.error_msg = "DivideByZeroException";
            return Value::uninit();
        }
        return Value::from_int32(a.as_int32() / b.as_int32());
    }
    if (a.is_int64() && b.is_int64()) {
        if (__builtin_expect(b.as_int64() == 0, 0)) {
            f.error = true; f.error_msg = "DivideByZeroException";
            return Value::uninit();
        }
        return Value::from_int64(a.as_int64() / b.as_int64());
    }
    if (a.is_float() || b.is_float())
        return Value::from_float(as_float64(a, f) / as_float64(b, f));
    f.error = true; f.error_msg = "CIL div: invalid types";
    return Value::uninit();
}

}  // namespace

void CilInterpreter::poll_safepoint(CilFrame& /*frame*/) {
    if (!safepoint_state_) return;
    if (SafepointManager::should_poll(safepoint_state_)) {
        SafepointManager::enter_safepoint(safepoint_state_);
    }
}

Result<Value> CilInterpreter::run(std::span<const uint8_t> il_code,
                                    uint16_t num_locals,
                                    uint16_t num_args,
                                    std::vector<Value> args) {
    if (il_code.empty()) {
        return std::unexpected(make_error(ErrorKind::BadInput, "CIL: empty IL code"));
    }

    CilFrame frame;
    frame.il_code = il_code;
    frame.locals.resize(num_locals);
    frame.args = std::move(args);
    if (frame.args.size() < num_args) frame.args.resize(num_args);

    // No try/catch — all errors are via frame.error.
    while (frame.pc < frame.il_code.size()) {
        const uint8_t* p = frame.il_code.data() + frame.pc;
        std::size_t remaining = frame.il_code.size() - frame.pc;
        auto d = cil::decode_opcode(p, remaining);
        if (d.op == cil::CilOpcode::Invalid) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("CIL: invalid opcode at pc={}", frame.pc)));
        }

        switch (d.op) {
            case cil::CilOpcode::Nop: break;
            case cil::CilOpcode::Break: break;

            // ── Constants ──
            case cil::CilOpcode::LdI4_0:    frame.push(Value::from_int32(0)); break;
            case cil::CilOpcode::LdI4_1:    frame.push(Value::from_int32(1)); break;
            case cil::CilOpcode::LdI4_2:    frame.push(Value::from_int32(2)); break;
            case cil::CilOpcode::LdI4_3:    frame.push(Value::from_int32(3)); break;
            case cil::CilOpcode::LdI4_4:    frame.push(Value::from_int32(4)); break;
            case cil::CilOpcode::LdI4_5:    frame.push(Value::from_int32(5)); break;
            case cil::CilOpcode::LdI4_6:    frame.push(Value::from_int32(6)); break;
            case cil::CilOpcode::LdI4_7:    frame.push(Value::from_int32(7)); break;
            case cil::CilOpcode::LdI4_8:    frame.push(Value::from_int32(8)); break;
            case cil::CilOpcode::LdI4_M1:   frame.push(Value::from_int32(-1)); break;
            case cil::CilOpcode::LdI4_S:    frame.push(Value::from_int32(static_cast<int32_t>(d.operand_i32))); break;
            case cil::CilOpcode::LdI4:      frame.push(Value::from_int32(static_cast<int32_t>(d.operand_i32))); break;
            case cil::CilOpcode::LdI8:     frame.push(Value::from_int64(d.operand_i64)); break;
            case cil::CilOpcode::LdR4:      frame.push(Value::from_float(static_cast<double>(d.operand_r4))); break;
            case cil::CilOpcode::LdR8:      frame.push(Value::from_float(d.operand_r8)); break;
            case cil::CilOpcode::LdNull:    frame.push(Value::null_value()); break;
            case cil::CilOpcode::LdStr:     frame.push(Value::from_int32(static_cast<int32_t>(d.operand_u32))); break;

            // ── Locals ──
            case cil::CilOpcode::LdLoc_0:  frame.push(frame.locals[0]); break;
            case cil::CilOpcode::LdLoc_1:  frame.push(frame.locals[1]); break;
            case cil::CilOpcode::LdLoc_2:  frame.push(frame.locals[2]); break;
            case cil::CilOpcode::LdLoc_3:  frame.push(frame.locals[3]); break;
            case cil::CilOpcode::LdLoc_S:  frame.push(frame.locals[d.operand_u32]); break;
            case cil::CilOpcode::StLoc_0:  frame.locals[0] = frame.pop(); break;
            case cil::CilOpcode::StLoc_1:  frame.locals[1] = frame.pop(); break;
            case cil::CilOpcode::StLoc_2:  frame.locals[2] = frame.pop(); break;
            case cil::CilOpcode::StLoc_3:  frame.locals[3] = frame.pop(); break;
            case cil::CilOpcode::StLoc_S:  frame.locals[d.operand_u32] = frame.pop(); break;

            // ── Args ──
            case cil::CilOpcode::LdArg_0:  frame.push(frame.args[0]); break;
            case cil::CilOpcode::LdArg_1:  frame.push(frame.args[1]); break;
            case cil::CilOpcode::LdArg_2:  frame.push(frame.args[2]); break;
            case cil::CilOpcode::LdArg_3:  frame.push(frame.args[3]); break;
            case cil::CilOpcode::LdArg_S:  frame.push(frame.args[d.operand_u32]); break;
            case cil::CilOpcode::StArg_S:  frame.args[d.operand_u32] = frame.pop(); break;

            // ── Stack manipulation ──
            case cil::CilOpcode::Dup:  frame.push(frame.top()); break;
            case cil::CilOpcode::Pop:  frame.pop(); break;

            // ── Arithmetic ──
            case cil::CilOpcode::Add: { Value b = frame.pop(); Value a = frame.pop(); frame.push(arith_add(a, b, frame)); break; }
            case cil::CilOpcode::Sub: { Value b = frame.pop(); Value a = frame.pop(); frame.push(arith_sub(a, b, frame)); break; }
            case cil::CilOpcode::Mul: { Value b = frame.pop(); Value a = frame.pop(); frame.push(arith_mul(a, b, frame)); break; }
            case cil::CilOpcode::Div: { Value b = frame.pop(); Value a = frame.pop(); frame.push(arith_div(a, b, frame)); break; }
            case cil::CilOpcode::Neg: {
                Value a = frame.pop();
                if (a.is_int32()) frame.push(Value::from_int32(-a.as_int32()));
                else if (a.is_int64()) frame.push(Value::from_int64(-a.as_int64()));
                else if (a.is_float()) frame.push(Value::from_float(-a.as_float()));
                else { frame.error = true; frame.error_msg = "CIL neg: invalid type"; }
                break;
            }

            // ── Bitwise ──
            case cil::CilOpcode::And: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a, frame) & as_int32(b, frame))); break; }
            case cil::CilOpcode::Or:  { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a, frame) | as_int32(b, frame))); break; }
            case cil::CilOpcode::Xor: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a, frame) ^ as_int32(b, frame))); break; }
            case cil::CilOpcode::Not: { Value a = frame.pop(); frame.push(Value::from_int32(~as_int32(a, frame))); break; }
            case cil::CilOpcode::Shl: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a, frame) << (as_int32(b, frame) & 31))); break; }
            case cil::CilOpcode::Shr: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a, frame) >> (as_int32(b, frame) & 31))); break; }
            case cil::CilOpcode::Shr_Un: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<uint32_t>(as_int32(a, frame)) >> (as_int32(b, frame) & 31)))); break; }

            // ── Conversions ──
            case cil::CilOpcode::Conv_I1: { Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<int8_t>(as_int32(a, frame))))); break; }
            case cil::CilOpcode::Conv_I2: { Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<int16_t>(as_int32(a, frame))))); break; }
            case cil::CilOpcode::Conv_I4: { Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a, frame))); break; }
            case cil::CilOpcode::Conv_I8: { Value a = frame.pop(); frame.push(Value::from_int64(as_int64(a, frame))); break; }
            case cil::CilOpcode::Conv_R4: { Value a = frame.pop(); frame.push(Value::from_float(as_float64(a, frame))); break; }
            case cil::CilOpcode::Conv_R8: { Value a = frame.pop(); frame.push(Value::from_float(as_float64(a, frame))); break; }
            case cil::CilOpcode::Conv_U4: { Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<uint32_t>(as_int32(a, frame))))); break; }
            case cil::CilOpcode::Conv_U8: { Value a = frame.pop(); frame.push(Value::from_int64(static_cast<int64_t>(static_cast<uint64_t>(as_int64(a, frame))))); break; }

            // ── Comparisons ──
            case cil::CilOpcode::Ceq: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(value_equals(a, b) ? 1 : 0)); break; }
            case cil::CilOpcode::Cgt: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_float64(a, frame) > as_float64(b, frame) ? 1 : 0)); break; }
            case cil::CilOpcode::Clt: { Value b = frame.pop(); Value a = frame.pop(); frame.push(Value::from_int32(as_float64(a, frame) < as_float64(b, frame) ? 1 : 0)); break; }

            // ── Branches ──
            case cil::CilOpcode::Br_S: case cil::CilOpcode::Br: {
                frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                poll_safepoint(frame); continue;
            }
            case cil::CilOpcode::Brtrue_S: case cil::CilOpcode::Brtrue: {
                Value v = frame.pop();
                if (truthy(v)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Brfalse_S: case cil::CilOpcode::Brfalse: {
                Value v = frame.pop();
                if (!truthy(v)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Beq_S: case cil::CilOpcode::Beq: {
                Value b = frame.pop(); Value a = frame.pop();
                if (value_equals(a, b)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Bne_Un_S: case cil::CilOpcode::Bne_Un: {
                Value b = frame.pop(); Value a = frame.pop();
                if (!value_equals(a, b)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Blt_S: case cil::CilOpcode::Blt: {
                Value b = frame.pop(); Value a = frame.pop();
                if (as_float64(a, frame) < as_float64(b, frame)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Bgt_S: case cil::CilOpcode::Bgt: {
                Value b = frame.pop(); Value a = frame.pop();
                if (as_float64(a, frame) > as_float64(b, frame)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Ble_S: case cil::CilOpcode::Ble: {
                Value b = frame.pop(); Value a = frame.pop();
                if (as_float64(a, frame) <= as_float64(b, frame)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }
            case cil::CilOpcode::Bge_S: case cil::CilOpcode::Bge: {
                Value b = frame.pop(); Value a = frame.pop();
                if (as_float64(a, frame) >= as_float64(b, frame)) { frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32); poll_safepoint(frame); continue; }
                break;
            }

            // ── Return ──
            case cil::CilOpcode::Ret: {
                Value r = frame.sp > 0 ? frame.pop() : Value::uninit();
                poll_safepoint(frame);
                return r;
            }

            default:
                return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                    std::format("CIL: opcode '{}' (0x{:04X}) at pc={}",
                                cil::opcode_name(d.op), static_cast<uint32_t>(d.op), frame.pc)));
        }

        // Check for errors after each op.
        if (__builtin_expect(frame.has_error(), 0)) {
            return std::unexpected(make_error(ErrorKind::Internal,
                std::format("CIL error at pc={}: {}", frame.pc,
                            frame.error_msg ? frame.error_msg : "unknown")));
        }

        frame.pc += d.length;
    }

    return frame.sp > 0 ? frame.pop() : Value::uninit();
}

}  // namespace jade::granit
