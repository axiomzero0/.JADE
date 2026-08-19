// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/CilInterpreter.cpp
//
// Real CIL bytecode interpreter.

#include "jade/tier0_granit/CilInterpreter.hpp"
#include "jade/cil/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"

#include <stdexcept>
#include <format>
#include <bit>
#include <cstring>

namespace jade::granit {

namespace {

[[nodiscard]] int32_t as_int32(const Value& v) {
    if (v.is_int32()) return v.as_int32();
    if (v.is_int64()) return static_cast<int32_t>(v.as_int64());
    throw std::runtime_error("CIL: expected int32 on stack");
}

[[nodiscard]] int64_t as_int64(const Value& v) {
    if (v.is_int64()) return v.as_int64();
    if (v.is_int32()) return v.as_int32();
    throw std::runtime_error("CIL: expected int64 on stack");
}

[[nodiscard]] double as_float64(const Value& v) {
    if (v.is_float()) return v.as_float();
    if (v.is_float()) return v.as_float();
    if (v.is_int32()) return static_cast<double>(v.as_int32());
    if (v.is_int64()) return static_cast<double>(v.as_int64());
    throw std::runtime_error("CIL: expected float on stack");
}

[[nodiscard]] Value arith_add(CilFrame& frame) {
    Value b = frame.pop();
    Value a = frame.pop();
    if (a.is_int32() && b.is_int32()) {
        return Value::from_int32(wrap_add_i32(a.as_int32(), b.as_int32()));
    }
    if (a.is_int64() && b.is_int64()) {
        return Value::from_int64(wrap_add_i64(a.as_int64(), b.as_int64()));
    }
    if (a.is_float() || b.is_float()) {
        return Value::from_float(as_float64(a) + as_float64(b));
    }
    throw std::runtime_error("CIL add: invalid operand types");
}

[[nodiscard]] Value arith_sub(CilFrame& frame) {
    Value b = frame.pop();
    Value a = frame.pop();
    if (a.is_int32() && b.is_int32()) {
        return Value::from_int32(wrap_sub_i32(a.as_int32(), b.as_int32()));
    }
    if (a.is_int64() && b.is_int64()) {
        return Value::from_int64(wrap_sub_i64(a.as_int64(), b.as_int64()));
    }
    if (a.is_float() || b.is_float()) {
        return Value::from_float(as_float64(a) - as_float64(b));
    }
    throw std::runtime_error("CIL sub: invalid operand types");
}

[[nodiscard]] Value arith_mul(CilFrame& frame) {
    Value b = frame.pop();
    Value a = frame.pop();
    if (a.is_int32() && b.is_int32()) {
        return Value::from_int32(wrap_mul_i32(a.as_int32(), b.as_int32()));
    }
    if (a.is_int64() && b.is_int64()) {
        return Value::from_int64(wrap_mul_i64(a.as_int64(), b.as_int64()));
    }
    if (a.is_float() || b.is_float()) {
        return Value::from_float(as_float64(a) * as_float64(b));
    }
    throw std::runtime_error("CIL mul: invalid operand types");
}

[[nodiscard]] Value arith_div(CilFrame& frame) {
    Value b = frame.pop();
    Value a = frame.pop();
    if (a.is_int32() && b.is_int32()) {
        const auto bv = b.as_int32();
        if (bv == 0) throw std::runtime_error("DivideByZeroException");
        return Value::from_int32(a.as_int32() / bv);
    }
    if (a.is_int64() && b.is_int64()) {
        const auto bv = b.as_int64();
        if (bv == 0) throw std::runtime_error("DivideByZeroException");
        return Value::from_int64(a.as_int64() / bv);
    }
    if (a.is_float() || b.is_float()) {
        return Value::from_float(as_float64(a) / as_float64(b));
    }
    throw std::runtime_error("CIL div: invalid operand types");
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
    if (frame.args.size() < num_args) {
        frame.args.resize(num_args);
    }

    try {
        while (frame.pc < frame.il_code.size()) {
            const uint8_t* p = frame.il_code.data() + frame.pc;
            std::size_t remaining = frame.il_code.size() - frame.pc;
            auto d = cil::decode_opcode(p, remaining);
            if (d.op == cil::CilOpcode::Invalid) {
                return std::unexpected(make_error(ErrorKind::BadInput,
                    std::format("CIL: invalid opcode at pc={}", frame.pc)));
            }

            switch (d.op) {
                case cil::CilOpcode::Nop:
                    break;
                case cil::CilOpcode::Break:
                    break;

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
                case cil::CilOpcode::Add:  frame.push(arith_add(frame)); break;
                case cil::CilOpcode::Sub:  frame.push(arith_sub(frame)); break;
                case cil::CilOpcode::Mul:  frame.push(arith_mul(frame)); break;
                case cil::CilOpcode::Div:  frame.push(arith_div(frame)); break;
                case cil::CilOpcode::Neg: {
                    Value a = frame.pop();
                    if (a.is_int32()) frame.push(Value::from_int32(-a.as_int32()));
                    else if (a.is_int64()) frame.push(Value::from_int64(-a.as_int64()));
                    else if (a.is_float()) frame.push(Value::from_float(-a.as_float()));
                    else throw std::runtime_error("CIL neg: invalid operand type");
                    break;
                }

                // ── Bitwise ──
                case cil::CilOpcode::And: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value::from_int32(as_int32(a) & as_int32(b)));
                    break;
                }
                case cil::CilOpcode::Or: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value::from_int32(as_int32(a) | as_int32(b)));
                    break;
                }
                case cil::CilOpcode::Xor: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value::from_int32(as_int32(a) ^ as_int32(b)));
                    break;
                }
                case cil::CilOpcode::Not: {
                    Value a = frame.pop();
                    frame.push(Value::from_int32(~as_int32(a)));
                    break;
                }
                case cil::CilOpcode::Shl: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value::from_int32(as_int32(a) << (as_int32(b) & 31)));
                    break;
                }
                case cil::CilOpcode::Shr: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value::from_int32(as_int32(a) >> (as_int32(b) & 31)));
                    break;
                }
                case cil::CilOpcode::Shr_Un: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value::from_int32(static_cast<int32_t>(static_cast<uint32_t>(as_int32(a)) >> (as_int32(b) & 31))));
                    break;
                }

                // ── Conversions ──
                case cil::CilOpcode::Conv_I1: {
                    Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<int8_t>(as_int32(a)))));
                    break;
                }
                case cil::CilOpcode::Conv_I2: {
                    Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<int16_t>(as_int32(a)))));
                    break;
                }
                case cil::CilOpcode::Conv_I4: {
                    Value a = frame.pop(); frame.push(Value::from_int32(as_int32(a)));
                    break;
                }
                case cil::CilOpcode::Conv_I8: {
                    Value a = frame.pop(); frame.push(Value::from_int64(as_int64(a)));
                    break;
                }
                case cil::CilOpcode::Conv_R4: {
                    Value a = frame.pop(); frame.push(Value::from_float(as_float64(a)));
                    break;
                }
                case cil::CilOpcode::Conv_R8: {
                    Value a = frame.pop(); frame.push(Value::from_float(as_float64(a)));
                    break;
                }
                case cil::CilOpcode::Conv_U4: {
                    Value a = frame.pop(); frame.push(Value::from_int32(static_cast<int32_t>(static_cast<uint32_t>(as_int32(a)))));
                    break;
                }
                case cil::CilOpcode::Conv_U8: {
                    Value a = frame.pop(); frame.push(Value::from_int64(static_cast<int64_t>(static_cast<uint64_t>(as_int64(a)))));
                    break;
                }

                // ── Comparisons (CIL ceq/cgt/clt produce 0/1) ──
                case cil::CilOpcode::Ceq: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool eq = value_equals(a, b);
                    if (!eq) {
                        // Try numeric comparison
                        if (a.is_int32() && b.is_int32())
                            eq = a.as_int32() == b.as_int32();
                        else if (a.is_float() && b.is_float())
                            eq = a.as_float() == b.as_float();
                    }
                    frame.push(Value::from_int32(eq ? 1 : 0));
                    break;
                }
                case cil::CilOpcode::Cgt: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool gt = false;
                    if (a.is_int32() && b.is_int32())
                        gt = a.as_int32() > b.as_int32();
                    else if (a.is_int64() && b.is_int64())
                        gt = a.as_int64() > b.as_int64();
                    else
                        gt = as_float64(a) > as_float64(b);
                    frame.push(Value::from_int32(gt ? 1 : 0));
                    break;
                }
                case cil::CilOpcode::Clt: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool lt = false;
                    if (a.is_int32() && b.is_int32())
                        lt = a.as_int32() < b.as_int32();
                    else if (a.is_int64() && b.is_int64())
                        lt = a.as_int64() < b.as_int64();
                    else
                        lt = as_float64(a) < as_float64(b);
                    frame.push(Value::from_int32(lt ? 1 : 0));
                    break;
                }

                // ── Branches ──
                // CIL branch offsets are relative to the START of the NEXT
                // instruction (i.e., pc + instruction_length + offset).
                case cil::CilOpcode::Br_S:
                case cil::CilOpcode::Br: {
                    frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                    poll_safepoint(frame);
                    continue;
                }
                case cil::CilOpcode::Brtrue_S:
                case cil::CilOpcode::Brtrue: {
                    Value v = frame.pop();
                    if (truthy(v)) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Brfalse_S:
                case cil::CilOpcode::Brfalse: {
                    Value v = frame.pop();
                    if (!truthy(v)) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Beq_S:
                case cil::CilOpcode::Beq: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool eq = (a.is_int32() && b.is_int32())
                                  ? a.as_int32() == b.as_int32()
                                  : as_float64(a) == as_float64(b);
                    if (eq) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Bne_Un_S:
                case cil::CilOpcode::Bne_Un: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool eq = (a.is_int32() && b.is_int32())
                                  ? a.as_int32() == b.as_int32()
                                  : as_float64(a) == as_float64(b);
                    if (!eq) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Blt_S:
                case cil::CilOpcode::Blt: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool lt = (a.is_int32() && b.is_int32())
                                  ? a.as_int32() < b.as_int32()
                                  : as_float64(a) < as_float64(b);
                    if (lt) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Bgt_S:
                case cil::CilOpcode::Bgt: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool gt = (a.is_int32() && b.is_int32())
                                  ? a.as_int32() > b.as_int32()
                                  : as_float64(a) > as_float64(b);
                    if (gt) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Ble_S:
                case cil::CilOpcode::Ble: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool le = (a.is_int32() && b.is_int32())
                                  ? a.as_int32() <= b.as_int32()
                                  : as_float64(a) <= as_float64(b);
                    if (le) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }
                case cil::CilOpcode::Bge_S:
                case cil::CilOpcode::Bge: {
                    Value b = frame.pop(); Value a = frame.pop();
                    bool ge = (a.is_int32() && b.is_int32())
                                  ? a.as_int32() >= b.as_int32()
                                  : as_float64(a) >= as_float64(b);
                    if (ge) {
                        frame.pc = static_cast<uint32_t>(static_cast<int32_t>(frame.pc + d.length) + d.operand_i32);
                        poll_safepoint(frame);
                        continue;
                    }
                    break;
                }

                // ── Return ──
                case cil::CilOpcode::Ret: {
                    Value r = frame.eval_stack.empty() ? Value::uninit() : frame.pop();
                    poll_safepoint(frame);
                    return r;
                }

                default:
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("CIL interpreter: opcode '{}' (0x{:04X}) not yet supported at pc={}",
                                    cil::opcode_name(d.op), static_cast<uint32_t>(d.op), frame.pc)));
            }

            frame.pc += d.length;
        }

        // Fell off the end without Ret → return top of stack or null.
        return frame.eval_stack.empty() ? Value::uninit() : frame.pop();
    } catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorKind::Internal,
            std::format("CIL interpreter error at pc={}: {}", frame.pc, e.what())));
    }
}

}  // namespace jade::granit
