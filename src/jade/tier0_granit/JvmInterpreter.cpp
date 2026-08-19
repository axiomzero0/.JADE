// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/JvmInterpreter.cpp
//
// Real JVM bytecode interpreter.

#include "jade/tier0_granit/JvmInterpreter.hpp"
#include "jade/jvm/Opcode.hpp"
#include "jade/tier0_granit/Value.hpp"

#include <stdexcept>
#include <format>

namespace jade::granit {

namespace {

[[nodiscard]] int32_t as_i32(const Value& v) {
    if (std::holds_alternative<int32_t>(v)) return std::get<int32_t>(v);
    if (std::holds_alternative<int64_t>(v)) return static_cast<int32_t>(std::get<int64_t>(v));
    throw std::runtime_error("JVM: expected int on stack");
}

[[nodiscard]] int64_t as_i64(const Value& v) {
    if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
    if (std::holds_alternative<int32_t>(v)) return std::get<int32_t>(v);
    throw std::runtime_error("JVM: expected long on stack");
}

[[nodiscard]] double as_f64(const Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<int32_t>(v)) return static_cast<double>(std::get<int32_t>(v));
    if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
    throw std::runtime_error("JVM: expected float/double on stack");
}

}  // namespace

void JvmInterpreter::poll_safepoint() {
    if (!safepoint_state_) return;
    if (SafepointManager::should_poll(safepoint_state_)) {
        SafepointManager::enter_safepoint(safepoint_state_);
    }
}

Result<Value> JvmInterpreter::run(std::span<const uint8_t> code,
                                    uint16_t num_locals,
                                    uint16_t num_args,
                                    std::vector<Value> args) {
    if (code.empty()) {
        return std::unexpected(make_error(ErrorKind::BadInput, "JVM: empty code"));
    }

    JvmFrame frame;
    frame.code = code;
    frame.locals.resize(num_locals);
    for (uint16_t i = 0; i < num_args && i < args.size(); ++i) {
        frame.locals[i] = std::move(args[i]);
    }

    try {
        while (frame.pc < frame.code.size()) {
            const uint8_t* p = frame.code.data() + frame.pc;
            std::size_t remaining = frame.code.size() - frame.pc;
            auto d = jvm::decode_opcode(p, remaining);
            if (d.op == jvm::JvmOpcode::Invalid) {
                return std::unexpected(make_error(ErrorKind::BadInput,
                    std::format("JVM: invalid opcode at pc={}", frame.pc)));
            }

            switch (d.op) {
                case jvm::JvmOpcode::Nop:
                    break;

                // ── Constants ──
                case jvm::JvmOpcode::AconstNull:  frame.push(make_null_object()); break;
                case jvm::JvmOpcode::IconstM1:    frame.push(int32_t{-1}); break;
                case jvm::JvmOpcode::Iconst0:    frame.push(int32_t{0}); break;
                case jvm::JvmOpcode::Iconst1:    frame.push(int32_t{1}); break;
                case jvm::JvmOpcode::Iconst2:    frame.push(int32_t{2}); break;
                case jvm::JvmOpcode::Iconst3:    frame.push(int32_t{3}); break;
                case jvm::JvmOpcode::Iconst4:    frame.push(int32_t{4}); break;
                case jvm::JvmOpcode::Iconst5:    frame.push(int32_t{5}); break;
                case jvm::JvmOpcode::Lconst0:    frame.push(int64_t{0}); break;
                case jvm::JvmOpcode::Lconst1:    frame.push(int64_t{1}); break;
                case jvm::JvmOpcode::Fconst0:    frame.push(double{0.0}); break;
                case jvm::JvmOpcode::Fconst1:    frame.push(double{1.0}); break;
                case jvm::JvmOpcode::Fconst2:    frame.push(double{2.0}); break;
                case jvm::JvmOpcode::Dconst0:    frame.push(double{0.0}); break;
                case jvm::JvmOpcode::Dconst1:    frame.push(double{1.0}); break;
                case jvm::JvmOpcode::Bipush:    frame.push(static_cast<int32_t>(d.operand_i32)); break;
                case jvm::JvmOpcode::Sipush:    frame.push(static_cast<int32_t>(d.operand_i32)); break;
                case jvm::JvmOpcode::Ldc:
                case jvm::JvmOpcode::LdcW:
                case jvm::JvmOpcode::Ldc2W:
                    frame.push(static_cast<int64_t>(d.operand_u32)); break;

                // ── Locals (load) ──
                case jvm::JvmOpcode::Iload: case jvm::JvmOpcode::Lload:
                case jvm::JvmOpcode::Fload: case jvm::JvmOpcode::Dload:
                case jvm::JvmOpcode::Aload:
                    frame.push(frame.locals[d.operand_u32]); break;
                case jvm::JvmOpcode::Iload0: frame.push(frame.locals[0]); break;
                case jvm::JvmOpcode::Iload1: frame.push(frame.locals[1]); break;
                case jvm::JvmOpcode::Iload2: frame.push(frame.locals[2]); break;
                case jvm::JvmOpcode::Iload3: frame.push(frame.locals[3]); break;
                case jvm::JvmOpcode::Lload0: frame.push(frame.locals[0]); break;
                case jvm::JvmOpcode::Lload1: frame.push(frame.locals[1]); break;
                case jvm::JvmOpcode::Lload2: frame.push(frame.locals[2]); break;
                case jvm::JvmOpcode::Lload3: frame.push(frame.locals[3]); break;
                case jvm::JvmOpcode::Fload0: frame.push(frame.locals[0]); break;
                case jvm::JvmOpcode::Fload1: frame.push(frame.locals[1]); break;
                case jvm::JvmOpcode::Fload2: frame.push(frame.locals[2]); break;
                case jvm::JvmOpcode::Fload3: frame.push(frame.locals[3]); break;
                case jvm::JvmOpcode::Dload0: frame.push(frame.locals[0]); break;
                case jvm::JvmOpcode::Dload1: frame.push(frame.locals[1]); break;
                case jvm::JvmOpcode::Dload2: frame.push(frame.locals[2]); break;
                case jvm::JvmOpcode::Dload3: frame.push(frame.locals[3]); break;
                case jvm::JvmOpcode::Aload0: frame.push(frame.locals[0]); break;
                case jvm::JvmOpcode::Aload1: frame.push(frame.locals[1]); break;
                case jvm::JvmOpcode::Aload2: frame.push(frame.locals[2]); break;
                case jvm::JvmOpcode::Aload3: frame.push(frame.locals[3]); break;

                // ── Locals (store) ──
                case jvm::JvmOpcode::Istore: case jvm::JvmOpcode::Lstore:
                case jvm::JvmOpcode::Fstore: case jvm::JvmOpcode::Dstore:
                case jvm::JvmOpcode::Astore:
                    frame.locals[d.operand_u32] = frame.pop(); break;
                case jvm::JvmOpcode::Istore0: frame.locals[0] = frame.pop(); break;
                case jvm::JvmOpcode::Istore1: frame.locals[1] = frame.pop(); break;
                case jvm::JvmOpcode::Istore2: frame.locals[2] = frame.pop(); break;
                case jvm::JvmOpcode::Istore3: frame.locals[3] = frame.pop(); break;
                case jvm::JvmOpcode::Lstore0: frame.locals[0] = frame.pop(); break;
                case jvm::JvmOpcode::Lstore1: frame.locals[1] = frame.pop(); break;
                case jvm::JvmOpcode::Lstore2: frame.locals[2] = frame.pop(); break;
                case jvm::JvmOpcode::Lstore3: frame.locals[3] = frame.pop(); break;
                case jvm::JvmOpcode::Fstore0: frame.locals[0] = frame.pop(); break;
                case jvm::JvmOpcode::Fstore1: frame.locals[1] = frame.pop(); break;
                case jvm::JvmOpcode::Fstore2: frame.locals[2] = frame.pop(); break;
                case jvm::JvmOpcode::Fstore3: frame.locals[3] = frame.pop(); break;
                case jvm::JvmOpcode::Dstore0: frame.locals[0] = frame.pop(); break;
                case jvm::JvmOpcode::Dstore1: frame.locals[1] = frame.pop(); break;
                case jvm::JvmOpcode::Dstore2: frame.locals[2] = frame.pop(); break;
                case jvm::JvmOpcode::Dstore3: frame.locals[3] = frame.pop(); break;
                case jvm::JvmOpcode::Astore0: frame.locals[0] = frame.pop(); break;
                case jvm::JvmOpcode::Astore1: frame.locals[1] = frame.pop(); break;
                case jvm::JvmOpcode::Astore2: frame.locals[2] = frame.pop(); break;
                case jvm::JvmOpcode::Astore3: frame.locals[3] = frame.pop(); break;

                // ── iinc ──
                case jvm::JvmOpcode::Iinc: {
                    int32_t cur = as_i32(frame.locals[d.operand_u32]);
                    frame.locals[d.operand_u32] = Value{wrap_add_i32(cur, d.operand_i32)};
                    break;
                }

                // ── Stack manipulation ──
                case jvm::JvmOpcode::Pop:    frame.pop(); break;
                case jvm::JvmOpcode::Pop2:   frame.pop(); if (!frame.stack.empty()) frame.pop(); break;
                case jvm::JvmOpcode::Dup:    frame.push(frame.top()); break;
                case jvm::JvmOpcode::DupX1: {
                    Value v1 = frame.pop(); Value v2 = frame.pop();
                    frame.push(v1); frame.push(v2); frame.push(v1);
                    break;
                }
                case jvm::JvmOpcode::Swap: {
                    Value v1 = frame.pop(); Value v2 = frame.pop();
                    frame.push(v1); frame.push(v2);
                    break;
                }

                // ── Arithmetic (int) ──
                case jvm::JvmOpcode::Iadd: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{wrap_add_i32(as_i32(a), as_i32(b))});
                    break;
                }
                case jvm::JvmOpcode::Isub: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{wrap_sub_i32(as_i32(a), as_i32(b))});
                    break;
                }
                case jvm::JvmOpcode::Imul: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{wrap_mul_i32(as_i32(a), as_i32(b))});
                    break;
                }
                case jvm::JvmOpcode::Idiv: {
                    Value b = frame.pop(); Value a = frame.pop();
                    int32_t bv = as_i32(b);
                    if (bv == 0) throw std::runtime_error("ArithmeticException: / by zero");
                    frame.push(Value{as_i32(a) / bv});
                    break;
                }
                case jvm::JvmOpcode::Irem: {
                    Value b = frame.pop(); Value a = frame.pop();
                    int32_t bv = as_i32(b);
                    if (bv == 0) throw std::runtime_error("ArithmeticException: / by zero");
                    frame.push(Value{as_i32(a) % bv});
                    break;
                }
                case jvm::JvmOpcode::Ineg: {
                    Value a = frame.pop();
                    frame.push(Value{-as_i32(a)});
                    break;
                }

                // ── Arithmetic (long) ──
                case jvm::JvmOpcode::Ladd: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{wrap_add_i64(as_i64(a), as_i64(b))});
                    break;
                }
                case jvm::JvmOpcode::Lsub: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{wrap_sub_i64(as_i64(a), as_i64(b))});
                    break;
                }
                case jvm::JvmOpcode::Lmul: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{wrap_mul_i64(as_i64(a), as_i64(b))});
                    break;
                }

                // ── Arithmetic (float/double) ──
                case jvm::JvmOpcode::Fadd:
                case jvm::JvmOpcode::Dadd: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_f64(a) + as_f64(b)});
                    break;
                }
                case jvm::JvmOpcode::Fsub:
                case jvm::JvmOpcode::Dsub: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_f64(a) - as_f64(b)});
                    break;
                }
                case jvm::JvmOpcode::Fmul:
                case jvm::JvmOpcode::Dmul: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_f64(a) * as_f64(b)});
                    break;
                }
                case jvm::JvmOpcode::Fdiv:
                case jvm::JvmOpcode::Ddiv: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_f64(a) / as_f64(b)});
                    break;
                }

                // ── Bitwise ──
                case jvm::JvmOpcode::Iand: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_i32(a) & as_i32(b)});
                    break;
                }
                case jvm::JvmOpcode::Ior: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_i32(a) | as_i32(b)});
                    break;
                }
                case jvm::JvmOpcode::Ixor: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_i32(a) ^ as_i32(b)});
                    break;
                }
                case jvm::JvmOpcode::Ishl: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_i32(a) << (as_i32(b) & 31)});
                    break;
                }
                case jvm::JvmOpcode::Ishr: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{as_i32(a) >> (as_i32(b) & 31)});
                    break;
                }
                case jvm::JvmOpcode::Iushr: {
                    Value b = frame.pop(); Value a = frame.pop();
                    frame.push(Value{static_cast<int32_t>(static_cast<uint32_t>(as_i32(a)) >> (as_i32(b) & 31))});
                    break;
                }

                // ── Conversions ──
                case jvm::JvmOpcode::I2l: { Value a = frame.pop(); frame.push(Value{static_cast<int64_t>(as_i32(a))}); break; }
                case jvm::JvmOpcode::I2f:
                case jvm::JvmOpcode::I2d: { Value a = frame.pop(); frame.push(Value{static_cast<double>(as_i32(a))}); break; }
                case jvm::JvmOpcode::L2i: { Value a = frame.pop(); frame.push(Value{static_cast<int32_t>(as_i64(a))}); break; }
                case jvm::JvmOpcode::L2f:
                case jvm::JvmOpcode::L2d: { Value a = frame.pop(); frame.push(Value{static_cast<double>(as_i64(a))}); break; }
                case jvm::JvmOpcode::F2i:
                case jvm::JvmOpcode::D2i: { Value a = frame.pop(); frame.push(Value{static_cast<int32_t>(as_f64(a))}); break; }
                case jvm::JvmOpcode::F2l:
                case jvm::JvmOpcode::D2l: { Value a = frame.pop(); frame.push(Value{static_cast<int64_t>(as_f64(a))}); break; }
                case jvm::JvmOpcode::F2d:
                case jvm::JvmOpcode::D2f: { break; }  // both are double on our stack
                case jvm::JvmOpcode::I2b: { Value a = frame.pop(); frame.push(Value{static_cast<int32_t>(static_cast<int8_t>(as_i32(a)))}); break; }
                case jvm::JvmOpcode::I2c: { Value a = frame.pop(); frame.push(Value{static_cast<int32_t>(static_cast<uint16_t>(as_i32(a)))}); break; }
                case jvm::JvmOpcode::I2s: { Value a = frame.pop(); frame.push(Value{static_cast<int32_t>(static_cast<int16_t>(as_i32(a)))}); break; }

                // ── Comparisons ──
                case jvm::JvmOpcode::Lcmp: {
                    Value b = frame.pop(); Value a = frame.pop();
                    int64_t av = as_i64(a), bv = as_i64(b);
                    frame.push(Value{av < bv ? int32_t{-1} : (av > bv ? int32_t{1} : int32_t{0})});
                    break;
                }
                case jvm::JvmOpcode::Fcmpl:
                case jvm::JvmOpcode::Fcmpg:
                case jvm::JvmOpcode::Dcmpl:
                case jvm::JvmOpcode::Dcmpg: {
                    Value b = frame.pop(); Value a = frame.pop();
                    double av = as_f64(a), bv = as_f64(b);
                    frame.push(Value{av < bv ? int32_t{-1} : (av > bv ? int32_t{1} : int32_t{0})});
                    break;
                }

                // ── Branches ──
                case jvm::JvmOpcode::Ifeq: { Value v = frame.pop(); if (as_i32(v) == 0) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Ifne: { Value v = frame.pop(); if (as_i32(v) != 0) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Iflt: { Value v = frame.pop(); if (as_i32(v) <  0) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Ifge: { Value v = frame.pop(); if (as_i32(v) >= 0) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Ifgt: { Value v = frame.pop(); if (as_i32(v) >  0) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Ifle: { Value v = frame.pop(); if (as_i32(v) <= 0) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::IfIcmpeq: { Value b = frame.pop(); Value a = frame.pop(); if (as_i32(a) == as_i32(b)) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::IfIcmpne: { Value b = frame.pop(); Value a = frame.pop(); if (as_i32(a) != as_i32(b)) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::IfIcmplt: { Value b = frame.pop(); Value a = frame.pop(); if (as_i32(a) <  as_i32(b)) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::IfIcmpge: { Value b = frame.pop(); Value a = frame.pop(); if (as_i32(a) >= as_i32(b)) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::IfIcmpgt: { Value b = frame.pop(); Value a = frame.pop(); if (as_i32(a) >  as_i32(b)) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::IfIcmple: { Value b = frame.pop(); Value a = frame.pop(); if (as_i32(a) <= as_i32(b)) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Ifnull: { Value v = frame.pop(); if (std::holds_alternative<ObjectHandle>(v) && std::get<ObjectHandle>(v).is_null()) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Ifnonnull: { Value v = frame.pop(); if (!(std::holds_alternative<ObjectHandle>(v) && std::get<ObjectHandle>(v).is_null())) { frame.pc += d.operand_i32; poll_safepoint(); continue; } break; }
                case jvm::JvmOpcode::Goto: { frame.pc += d.operand_i32; poll_safepoint(); continue; }
                case jvm::JvmOpcode::GotoW: { frame.pc += d.operand_i32; poll_safepoint(); continue; }

                // ── Return ──
                case jvm::JvmOpcode::Return: { poll_safepoint(); return Value{std::monostate{}}; }
                case jvm::JvmOpcode::Ireturn: { Value r = frame.pop(); poll_safepoint(); return r; }
                case jvm::JvmOpcode::Lreturn:
                case jvm::JvmOpcode::Freturn:
                case jvm::JvmOpcode::Dreturn:
                case jvm::JvmOpcode::Areturn: { Value r = frame.pop(); poll_safepoint(); return r; }

                default:
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("JVM interpreter: opcode '{}' (0x{:04X}) not yet supported at pc={}",
                                    jvm::opcode_name(d.op), static_cast<uint32_t>(d.op), frame.pc)));
            }

            frame.pc += d.length;
        }

        return frame.stack.empty() ? Value{std::monostate{}} : frame.pop();
    } catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorKind::Internal,
            std::format("JVM interpreter error at pc={}: {}", frame.pc, e.what())));
    }
}

}  // namespace jade::granit
