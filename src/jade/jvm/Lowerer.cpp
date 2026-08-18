// SPDX-License-Identifier: MIT
// .JADE Compiler — jvm/Lowerer.cpp
//
// JVM bytecode → Sea of Nodes IR lowering.

#include "jade/jvm/Lowerer.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"
#include "jade/ir/Verifier.hpp"

#include <stdexcept>
#include <format>

namespace jade::jvm {

JvmLowerer::JvmLowerer() : b_(g_) {
    start_ = b_.start();
    current_effect_ = start_;
    current_ctrl_   = start_;
}

void JvmLowerer::push(NodeId v) {
    eval_stack_.push_back(v);
}

NodeId JvmLowerer::pop() {
    if (eval_stack_.empty()) {
        throw std::runtime_error("JVM lowerer: eval stack underflow");
    }
    NodeId v = eval_stack_.back();
    eval_stack_.pop_back();
    return v;
}

NodeId JvmLowerer::peek(std::size_t depth) {
    if (depth >= eval_stack_.size()) {
        throw std::runtime_error("JVM lowerer: eval stack underflow on peek");
    }
    return eval_stack_[eval_stack_.size() - 1 - depth];
}

NodeId JvmLowerer::make_effectful(NodeKind kind, std::span<const NodeId> inputs) {
    NodeId id = g_.create(kind, inputs, current_ctrl_, current_effect_);
    if (g_.node(id).is_effect()) {
        current_effect_ = id;
    }
    return id;
}

Result<Graph> JvmLowerer::lower(std::span<const uint8_t> jvm_bytes,
                                 uint16_t num_locals,
                                 uint16_t num_args) {
    locals_.clear();
    args_.clear();
    locals_.reserve(num_locals);
    args_.reserve(num_args);
    for (uint16_t i = 0; i < num_locals; ++i) {
        locals_.push_back(g_.create(NodeKind::Phi));
    }
    for (uint16_t i = 0; i < num_args; ++i) {
        args_.push_back(g_.create(NodeKind::Phi));
    }

    try {
        std::size_t pc = 0;
        while (pc < jvm_bytes.size()) {
            const uint8_t* p = jvm_bytes.data() + pc;
            std::size_t remaining = jvm_bytes.size() - pc;
            DecodedInstruction d = decode_opcode(p, remaining);
            if (d.op == JvmOpcode::Invalid) {
                return std::unexpected(make_error(ErrorKind::BadInput,
                    std::format("invalid JVM opcode at pc={}", pc)));
            }

            switch (d.op) {
                case JvmOpcode::Nop:
                    break;

                // ── Constants ──
                case JvmOpcode::IconstM1:    push(b_.const_int(-1)); break;
                case JvmOpcode::Iconst0:    push(b_.const_int(0)); break;
                case JvmOpcode::Iconst1:    push(b_.const_int(1)); break;
                case JvmOpcode::Iconst2:    push(b_.const_int(2)); break;
                case JvmOpcode::Iconst3:    push(b_.const_int(3)); break;
                case JvmOpcode::Iconst4:    push(b_.const_int(4)); break;
                case JvmOpcode::Iconst5:    push(b_.const_int(5)); break;
                case JvmOpcode::Lconst0:    push(b_.const_int(int64_t{0})); break;
                case JvmOpcode::Lconst1:    push(b_.const_int(int64_t{1})); break;
                case JvmOpcode::Fconst0:    push(b_.const_float(0.0)); break;
                case JvmOpcode::Fconst1:    push(b_.const_float(1.0)); break;
                case JvmOpcode::Fconst2:    push(b_.const_float(2.0)); break;
                case JvmOpcode::Dconst0:    push(b_.const_float(0.0)); break;
                case JvmOpcode::Dconst1:    push(b_.const_float(1.0)); break;
                case JvmOpcode::AconstNull: push(g_.create(NodeKind::LdNull)); break;
                case JvmOpcode::Bipush:    push(b_.const_int(static_cast<int64_t>(d.operand_i32))); break;
                case JvmOpcode::Sipush:    push(b_.const_int(static_cast<int64_t>(d.operand_i32))); break;
                case JvmOpcode::Ldc:
                case JvmOpcode::LdcW:
                case JvmOpcode::Ldc2W:
                    // We don't resolve constant-pool entries in the lowerer;
                    // emit a ConstInt carrying the cp index. The
                    // metadata resolver will rewrite this later.
                    push(b_.const_int(static_cast<int64_t>(d.operand_u32)));
                    break;

                // ── Locals (load) ──
                case JvmOpcode::Iload:
                case JvmOpcode::Lload:
                case JvmOpcode::Fload:
                case JvmOpcode::Dload:
                case JvmOpcode::Aload:
                    push(locals_.at(d.operand_u32));
                    break;
                case JvmOpcode::Iload0: case JvmOpcode::Iload1:
                case JvmOpcode::Iload2: case JvmOpcode::Iload3:
                    push(locals_.at(static_cast<uint32_t>(d.op) - 0x1A));   // 0x1A..0x1D → 0..3
                    break;
                case JvmOpcode::Lload0: case JvmOpcode::Lload1:
                case JvmOpcode::Lload2: case JvmOpcode::Lload3:
                    push(locals_.at(static_cast<uint32_t>(d.op) - 0x1E));   // 0x1E..0x21 → 0..3
                    break;
                case JvmOpcode::Fload0: case JvmOpcode::Fload1:
                case JvmOpcode::Fload2: case JvmOpcode::Fload3:
                    push(locals_.at(static_cast<uint32_t>(d.op) - 0x22));   // 0x22..0x25 → 0..3
                    break;
                case JvmOpcode::Dload0: case JvmOpcode::Dload1:
                case JvmOpcode::Dload2: case JvmOpcode::Dload3:
                    push(locals_.at(static_cast<uint32_t>(d.op) - 0x26));   // 0x26..0x29 → 0..3
                    break;
                case JvmOpcode::Aload0: case JvmOpcode::Aload1:
                case JvmOpcode::Aload2: case JvmOpcode::Aload3:
                    push(locals_.at(static_cast<uint32_t>(d.op) - 0x2A));   // 0x2A..0x2D → 0..3
                    break;

                // ── Locals (store) ──
                case JvmOpcode::Istore:
                case JvmOpcode::Lstore:
                case JvmOpcode::Fstore:
                case JvmOpcode::Dstore:
                case JvmOpcode::Astore:
                    locals_.at(d.operand_u32) = pop();
                    break;
                case JvmOpcode::Istore0: case JvmOpcode::Istore1:
                case JvmOpcode::Istore2: case JvmOpcode::Istore3:
                    locals_.at(static_cast<uint32_t>(d.op) - 0x3B) = pop();   // 0x3B..0x3E → 0..3
                    break;
                case JvmOpcode::Lstore0: case JvmOpcode::Lstore1:
                case JvmOpcode::Lstore2: case JvmOpcode::Lstore3:
                    locals_.at(static_cast<uint32_t>(d.op) - 0x3F) = pop();   // 0x3F..0x42 → 0..3
                    break;
                case JvmOpcode::Fstore0: case JvmOpcode::Fstore1:
                case JvmOpcode::Fstore2: case JvmOpcode::Fstore3:
                    locals_.at(static_cast<uint32_t>(d.op) - 0x43) = pop();   // 0x43..0x46 → 0..3
                    break;
                case JvmOpcode::Dstore0: case JvmOpcode::Dstore1:
                case JvmOpcode::Dstore2: case JvmOpcode::Dstore3:
                    locals_.at(static_cast<uint32_t>(d.op) - 0x47) = pop();   // 0x47..0x4A → 0..3
                    break;
                case JvmOpcode::Astore0: case JvmOpcode::Astore1:
                case JvmOpcode::Astore2: case JvmOpcode::Astore3:
                    locals_.at(static_cast<uint32_t>(d.op) - 0x4B) = pop();   // 0x4B..0x4E → 0..3
                    break;

                // ── iinc ──
                case JvmOpcode::Iinc: {
                    NodeId cur = locals_.at(d.operand_u32);
                    NodeId delta = b_.const_int(d.operand_i32);
                    NodeId inputs[] = {cur, delta};
                    locals_.at(d.operand_u32) = g_.create(NodeKind::Add, inputs);
                    break;
                }

                // ── Array loads ──
                case JvmOpcode::Iaload:
                case JvmOpcode::Laload:
                case JvmOpcode::Faload:
                case JvmOpcode::Daload:
                case JvmOpcode::Aaload:
                case JvmOpcode::Baload:
                case JvmOpcode::Caload:
                case JvmOpcode::Saload: {
                    NodeId idx = pop();
                    NodeId arr = pop();
                    NodeId inputs[] = {arr, idx};
                    push(make_effectful(NodeKind::LdElem, inputs));
                    break;
                }

                // ── Array stores ──
                case JvmOpcode::Iastore:
                case JvmOpcode::Lastore:
                case JvmOpcode::Fastore:
                case JvmOpcode::Dastore:
                case JvmOpcode::Aastore:
                case JvmOpcode::Bastore:
                case JvmOpcode::Castore:
                case JvmOpcode::Sastore: {
                    NodeId v = pop();
                    NodeId idx = pop();
                    NodeId arr = pop();
                    NodeId inputs[] = {arr, idx, v};
                    make_effectful(NodeKind::StElem, inputs);
                    break;
                }

                // ── Stack manipulation ──
                case JvmOpcode::Pop:    pop(); break;
                case JvmOpcode::Pop2:   pop(); if (!eval_stack_.empty()) pop(); break;
                case JvmOpcode::Dup:    push(peek(0)); break;
                case JvmOpcode::DupX1: {
                    NodeId v1 = pop();
                    NodeId v2 = pop();
                    push(v1); push(v2); push(v1);
                    break;
                }
                case JvmOpcode::DupX2: {
                    NodeId v1 = pop();
                    NodeId v2 = pop();
                    NodeId v3 = pop();
                    push(v1); push(v3); push(v2); push(v1);
                    break;
                }
                case JvmOpcode::Dup2: {
                    NodeId v1 = peek(0);
                    NodeId v2 = peek(1);
                    push(v2); push(v1);
                    break;
                }
                case JvmOpcode::Swap: {
                    NodeId v1 = pop();
                    NodeId v2 = pop();
                    push(v1); push(v2);
                    break;
                }

                // ── Arithmetic ──
                case JvmOpcode::Iadd: case JvmOpcode::Ladd:
                case JvmOpcode::Fadd: case JvmOpcode::Dadd: {
                    NodeId b = pop(); NodeId a = pop(); push(b_.add(a, b)); break;
                }
                case JvmOpcode::Isub: case JvmOpcode::Lsub:
                case JvmOpcode::Fsub: case JvmOpcode::Dsub: {
                    NodeId b = pop(); NodeId a = pop(); push(b_.sub(a, b)); break;
                }
                case JvmOpcode::Imul: case JvmOpcode::Lmul:
                case JvmOpcode::Fmul: case JvmOpcode::Dmul: {
                    NodeId b = pop(); NodeId a = pop(); push(b_.mul(a, b)); break;
                }
                case JvmOpcode::Idiv: case JvmOpcode::Ldiv:
                case JvmOpcode::Fdiv: case JvmOpcode::Ddiv: {
                    NodeId b = pop(); NodeId a = pop(); push(b_.div(a, b)); break;
                }
                case JvmOpcode::Irem: case JvmOpcode::Lrem:
                case JvmOpcode::Frem: case JvmOpcode::Drem: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Mod, inputs));
                    break;
                }
                case JvmOpcode::Ineg: case JvmOpcode::Lneg:
                case JvmOpcode::Fneg: case JvmOpcode::Dneg: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::Neg, inputs));
                    break;
                }

                // ── Bitwise/shift ──
                case JvmOpcode::Ishl: case JvmOpcode::Lshl: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Shl, inputs));
                    break;
                }
                case JvmOpcode::Ishr: case JvmOpcode::Lshr: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Sar, inputs));   // arithmetic
                    break;
                }
                case JvmOpcode::Iushr: case JvmOpcode::Lushr: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Shr, inputs));   // logical
                    break;
                }
                case JvmOpcode::Iand: case JvmOpcode::Land: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::And, inputs));
                    break;
                }
                case JvmOpcode::Ior: case JvmOpcode::Lor: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Or, inputs));
                    break;
                }
                case JvmOpcode::Ixor: case JvmOpcode::Lxor: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    push(g_.create(NodeKind::Xor, inputs));
                    break;
                }

                // ── Conversions ──
                case JvmOpcode::I2l: case JvmOpcode::F2l: case JvmOpcode::D2l: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvI8, inputs));
                    break;
                }
                case JvmOpcode::I2f: case JvmOpcode::L2f: case JvmOpcode::D2f: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvR4, inputs));
                    break;
                }
                case JvmOpcode::I2d: case JvmOpcode::F2d: case JvmOpcode::L2d: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvR8, inputs));
                    break;
                }
                case JvmOpcode::L2i: case JvmOpcode::F2i: case JvmOpcode::D2i: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvI4, inputs));
                    break;
                }
                case JvmOpcode::I2b: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvI1, inputs));
                    break;
                }
                case JvmOpcode::I2c: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvU2, inputs));
                    break;
                }
                case JvmOpcode::I2s: {
                    NodeId a = pop();
                    NodeId inputs[] = {a};
                    push(g_.create(NodeKind::ConvI2, inputs));
                    break;
                }

                // ── Comparisons (produce -1/0/1) ──
                case JvmOpcode::Lcmp:
                case JvmOpcode::Fcmpl: case JvmOpcode::Fcmpg:
                case JvmOpcode::Dcmpl: case JvmOpcode::Dcmpg: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    // Model as Lt: a < b → -1, a == b → 0, a > b → 1 (handled at lower)
                    push(g_.create(NodeKind::Lt, inputs));
                    break;
                }

                // ── Branches ──
                case JvmOpcode::Ifeq: case JvmOpcode::Ifne:
                case JvmOpcode::Iflt: case JvmOpcode::Ifge:
                case JvmOpcode::Ifgt: case JvmOpcode::Ifle: {
                    NodeId v = pop();
                    NodeId zero = b_.const_int(0);
                    NodeId inputs[] = {v, zero};
                    NodeKind k = NodeKind::Eq;
                    if (d.op == JvmOpcode::Ifeq || d.op == JvmOpcode::Ifne) k = NodeKind::Eq;
                    else if (d.op == JvmOpcode::Iflt || d.op == JvmOpcode::Ifge) k = NodeKind::Lt;
                    else k = NodeKind::Gt;
                    push(g_.create(k, inputs));
                    break;
                }
                case JvmOpcode::IfIcmpeq: case JvmOpcode::IfIcmpne:
                case JvmOpcode::IfIcmplt: case JvmOpcode::IfIcmpge:
                case JvmOpcode::IfIcmpgt: case JvmOpcode::IfIcmple: {
                    NodeId b = pop(); NodeId a = pop();
                    NodeId inputs[] = {a, b};
                    NodeKind k = NodeKind::Eq;
                    if (d.op == JvmOpcode::IfIcmpeq || d.op == JvmOpcode::IfIcmpne) k = NodeKind::Eq;
                    else if (d.op == JvmOpcode::IfIcmplt || d.op == JvmOpcode::IfIcmpge) k = NodeKind::Lt;
                    else k = NodeKind::Gt;
                    push(g_.create(k, inputs));
                    break;
                }
                case JvmOpcode::IfAcmpeq: case JvmOpcode::IfAcmpne:
                case JvmOpcode::Ifnull: case JvmOpcode::Ifnonnull: {
                    NodeId v = pop();
                    NodeId null_v = g_.create(NodeKind::LdNull);
                    NodeId inputs[] = {v, null_v};
                    push(g_.create(NodeKind::Eq, inputs));
                    break;
                }
                case JvmOpcode::Goto:
                case JvmOpcode::GotoW:
                    // Simplified: just record the jump target.
                    break;
                case JvmOpcode::Return: {
                    NodeId ret = b_.return_node(g_.create(NodeKind::LdNull));
                    g_.set_ctrl_input(ret, current_ctrl_);
                    g_.set_effect_input(ret, current_effect_);
                    break;
                }
                case JvmOpcode::Ireturn: case JvmOpcode::Lreturn:
                case JvmOpcode::Freturn: case JvmOpcode::Dreturn:
                case JvmOpcode::Areturn: {
                    NodeId v = pop();
                    NodeId ret = b_.return_node(v);
                    g_.set_ctrl_input(ret, current_ctrl_);
                    g_.set_effect_input(ret, current_effect_);
                    break;
                }

                // ── Field access ──
                case JvmOpcode::Getfield: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::LdFld, inputs));
                    break;
                }
                case JvmOpcode::Putfield: {
                    NodeId v = pop();
                    NodeId obj = pop();
                    NodeId inputs[] = {obj, v};
                    make_effectful(NodeKind::StFld, inputs);
                    break;
                }
                case JvmOpcode::Getstatic: {
                    // No obj input for static; emit LdFld with no data inputs.
                    NodeId id = make_effectful(NodeKind::LdFld, {});
                    push(id);
                    break;
                }
                case JvmOpcode::Putstatic: {
                    NodeId v = pop();
                    NodeId inputs[] = {v};
                    make_effectful(NodeKind::StFld, inputs);
                    break;
                }

                // ── Method invocation ──
                case JvmOpcode::Invokevirtual:
                case JvmOpcode::Invokespecial:
                case JvmOpcode::Invokestatic:
                case JvmOpcode::Invokeinterface: {
                    // We don't know the arity without metadata resolution.
                    // For now, pop the receiver (if not invokestatic) and
                    // emit a CallVirt node carrying the cp index. Real impl needs the
                    // method signature.
                    // Pseudo-impl: don't pop anything; let the caller wire it.
                    NodeId id = make_effectful(NodeKind::CallVirt, {});
                    push(id);
                    break;
                }
                case JvmOpcode::Invokedynamic: {
                    NodeId id = make_effectful(NodeKind::InvokeDynamic, {});
                    push(id);
                    break;
                }

                // ── Object/array creation ──
                case JvmOpcode::New: {
                    NodeId id = make_effectful(NodeKind::NewObj, {});
                    push(id);
                    break;
                }
                case JvmOpcode::Newarray:
                case JvmOpcode::Anewarray: {
                    NodeId len = pop();
                    NodeId inputs[] = {len};
                    NodeId id = make_effectful(NodeKind::NewArr, inputs);
                    push(id);
                    break;
                }
                case JvmOpcode::Multianewarray: {
                    // Pop `dim` lengths off the stack.
                    int dims = d.switch_count;
                    if (dims < 0) dims = 0;
                    std::vector<NodeId> lens;
                    for (int i = 0; i < dims; ++i) {
                        if (eval_stack_.empty()) {
                            return std::unexpected(make_error(ErrorKind::BadInput,
                                std::format("multianewarray: stack underflow for dim {}", i)));
                        }
                        lens.push_back(pop());
                    }
                    NodeId id = make_effectful(NodeKind::NewArr, lens);
                    push(id);
                    break;
                }
                case JvmOpcode::Arraylength: {
                    NodeId arr = pop();
                    NodeId inputs[] = {arr};
                    NodeId id = make_effectful(NodeKind::ArrayLength, inputs);
                    push(id);
                    break;
                }

                // ── Type checks ──
                case JvmOpcode::Checkcast: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::CastClass, inputs));
                    break;
                }
                case JvmOpcode::Instanceof: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    push(make_effectful(NodeKind::IsInst, inputs));
                    break;
                }

                // ── Exception ──
                case JvmOpcode::Athrow: {
                    NodeId exc = pop();
                    NodeId inputs[] = {exc};
                    make_effectful(NodeKind::Throw, inputs);
                    break;
                }

                // ── Monitor ──
                case JvmOpcode::Monitorenter: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    make_effectful(NodeKind::MonitorEnter, inputs);
                    break;
                }
                case JvmOpcode::Monitorexit: {
                    NodeId obj = pop();
                    NodeId inputs[] = {obj};
                    make_effectful(NodeKind::MonitorExit, inputs);
                    break;
                }

                // ── Switch ── (simplified: do nothing for now)
                case JvmOpcode::TableSwitch:
                case JvmOpcode::LookupSwitch:
                    // We'd need control-flow handling. Mark as unsupported for now.
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("JVM lowerer: switch opcodes not yet supported at pc={}", pc)));

                // ── Wide forms (handled in decode) ──
                case JvmOpcode::WideIload: case JvmOpcode::WideLload:
                case JvmOpcode::WideFload: case JvmOpcode::WideDload:
                case JvmOpcode::WideAload:
                    push(locals_.at(d.operand_u32));
                    break;
                case JvmOpcode::WideIstore: case JvmOpcode::WideLstore:
                case JvmOpcode::WideFstore: case JvmOpcode::WideDstore:
                case JvmOpcode::WideAstore:
                    locals_.at(d.operand_u32) = pop();
                    break;
                case JvmOpcode::WideRet:
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        "JVM lowerer: wide ret not yet supported"));
                case JvmOpcode::WideIinc: {
                    NodeId cur = locals_.at(d.operand_u32);
                    NodeId delta = b_.const_int(d.operand_i32);
                    NodeId inputs[] = {cur, delta};
                    locals_.at(d.operand_u32) = g_.create(NodeKind::Add, inputs);
                    break;
                }

                // ── jsr/ret (deprecated) ──
                case JvmOpcode::Jsr:
                case JvmOpcode::JsrW:
                case JvmOpcode::Ret:
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("JVM lowerer: jsr/ret deprecated and not supported at pc={}", pc)));

                // ── Breakpoint (debugger) ──
                case JvmOpcode::Breakpoint:
                case JvmOpcode::Impdep1:
                case JvmOpcode::Impdep2:
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("JVM lowerer: reserved opcode {} at pc={}",
                                    opcode_name(d.op), pc)));

                default:
                    return std::unexpected(make_error(ErrorKind::UnsupportedNode,
                        std::format("JVM lowerer: opcode '{}' (0x{:04X}) not yet supported at pc={}",
                                    opcode_name(d.op), static_cast<uint32_t>(d.op), pc)));
            }

            pc += d.length;
        }

        // Implicit return if no explicit one was emitted.
        if (!eval_stack_.empty()) {
            NodeId ret_val = pop();
            NodeId ret = b_.return_node(ret_val);
            g_.set_ctrl_input(ret, current_ctrl_);
            g_.set_effect_input(ret, current_effect_);
        } else {
            // Add an implicit `return void`.
            NodeId null_v = g_.create(NodeKind::LdNull);
            NodeId ret = b_.return_node(null_v);
            g_.set_ctrl_input(ret, current_ctrl_);
            g_.set_effect_input(ret, current_effect_);
        }
    } catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorKind::Internal, e.what()));
    }

    if (auto r = verify_graph(g_); !r) {
        return std::unexpected(r.error());
    }
    return std::move(g_);
}

}  // namespace jade::jvm
