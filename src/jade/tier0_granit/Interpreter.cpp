// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Interpreter.cpp
//
// Legacy `Op`-based interpreter. Uses the new `Value` type so it stays
// compatible with the new C# runtime. CIL execution lives in CilInterpreter.

#include "jade/tier0_granit/Interpreter.hpp"
#include "jade/tier0_granit/Bytecode.hpp"

#include <format>
#include <stdexcept>
#include <cstring>
#include <bit>

namespace jade::granit {

namespace {

[[nodiscard]] Value arith_add(Value a, Value b) {
    if (a.is_int32() && b.is_int32()) {
        return Value::from_int32(wrap_add_i32(a.as_int32(), b.as_int32()));
    }
    if (a.is_int64() && b.is_int64()) {
        return Value::from_int64(wrap_add_i64(a.as_int64(), b.as_int64()));
    }
    if (a.is_float() || b.is_float()) {
        const double af = a.is_int32() ? static_cast<double>(a.as_int32())
                          : (a.is_int64() ? static_cast<double>(a.as_int64())
                                          : a.as_float());
        const double bf = b.is_int32() ? static_cast<double>(b.as_int32())
                          : (b.is_int64() ? static_cast<double>(b.as_int64())
                                          : b.as_float());
        return Value::from_float(af + bf);
    }
    throw std::runtime_error("Add: invalid operand types");
}

[[nodiscard]] Value arith_sub(Value a, Value b) {
    if (a.is_int32() && b.is_int32()) {
        return Value::from_int32(wrap_sub_i32(a.as_int32(), b.as_int32()));
    }
    if (a.is_int64() && b.is_int64()) {
        return Value::from_int64(wrap_sub_i64(a.as_int64(), b.as_int64()));
    }
    if (a.is_float() || b.is_float()) {
        const double af = a.is_int32() ? static_cast<double>(a.as_int32())
                          : (a.is_int64() ? static_cast<double>(a.as_int64())
                                          : a.as_float());
        const double bf = b.is_int32() ? static_cast<double>(b.as_int32())
                          : (b.is_int64() ? static_cast<double>(b.as_int64())
                                          : b.as_float());
        return Value::from_float(af - bf);
    }
    throw std::runtime_error("Sub: invalid operand types");
}

[[nodiscard]] Value arith_mul(Value a, Value b) {
    if (a.is_int32() && b.is_int32()) {
        return Value::from_int32(wrap_mul_i32(a.as_int32(), b.as_int32()));
    }
    if (a.is_int64() && b.is_int64()) {
        return Value::from_int64(wrap_mul_i64(a.as_int64(), b.as_int64()));
    }
    if (a.is_float() || b.is_float()) {
        const double af = a.is_int32() ? static_cast<double>(a.as_int32())
                          : (a.is_int64() ? static_cast<double>(a.as_int64())
                                          : a.as_float());
        const double bf = b.is_int32() ? static_cast<double>(b.as_int32())
                          : (b.is_int64() ? static_cast<double>(b.as_int64())
                                          : b.as_float());
        return Value::from_float(af * bf);
    }
    throw std::runtime_error("Mul: invalid operand types");
}

[[nodiscard]] Value arith_div(Value a, Value b) {
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
        const double af = a.is_int32() ? static_cast<double>(a.as_int32())
                          : (a.is_int64() ? static_cast<double>(a.as_int64())
                                          : a.as_float());
        const double bf = b.is_int32() ? static_cast<double>(b.as_int32())
                          : (b.is_int64() ? static_cast<double>(b.as_int64())
                                          : b.as_float());
        return Value::from_float(af / bf);
    }
    throw std::runtime_error("Div: invalid operand types");
}

}  // namespace

void Interpreter::push(Value v) {
    stack_.push_back(std::move(v));
    if (stack_.size() > max_stack_depth_) max_stack_depth_ = stack_.size();
}

Value Interpreter::pop() {
    if (stack_.empty()) throw std::runtime_error("stack underflow");
    Value v = std::move(stack_.back());
    stack_.pop_back();
    return v;
}

Value& Interpreter::top() {
    if (stack_.empty()) throw std::runtime_error("stack empty");
    return stack_.back();
}

void Interpreter::poll_safepoint() {
    if (!safepoint_state_) return;
    if (SafepointManager::should_poll(safepoint_state_)) {
        SafepointManager::enter_safepoint(safepoint_state_);
    }
}

Result<Value> Interpreter::run(const Program& prog) {
    if (prog.empty()) {
        return std::unexpected(make_error(ErrorKind::BadInput, "empty program"));
    }

    feedback_.observed_types.resize(prog.size(), 0);
    feedback_.branch_taken.resize(prog.size(), 0);
    feedback_.branch_total.resize(prog.size(), 0);

    try {
        std::size_t pc = 0;
        while (pc < prog.size()) {
            const Instruction& instr = prog[pc];

            switch (instr.op) {
                case Op::Nop:
                    break;
                case Op::PushConst0:   push(Value::from_int32(0)); break;
                case Op::PushConst1:   push(Value::from_int32(1)); break;
                case Op::PushConstI:   push(Value::from_int32(static_cast<int32_t>(instr.imm))); break;
                case Op::PushConstF: {
                    double v;
                    std::memcpy(&v, &instr.imm, sizeof(v));
                    push(Value::from_int32(v));
                    break;
                }
                case Op::PushConstN:   push(make_null_object()); break;
                case Op::Pop:           pop(); break;
                case Op::Dup:           push(top()); break;
                case Op::Swap: {
                    if (stack_.size() < 2) throw std::runtime_error("stack underflow on Swap");
                    std::swap(stack_[stack_.size()-1], stack_[stack_.size()-2]);
                    break;
                }
                case Op::Add: { Value b = pop(); Value a = pop(); push(arith_add(a, b)); break; }
                case Op::Sub: { Value b = pop(); Value a = pop(); push(arith_sub(a, b)); break; }
                case Op::Mul: { Value b = pop(); Value a = pop(); push(arith_mul(a, b)); break; }
                case Op::Div: { Value b = pop(); Value a = pop(); push(arith_div(a, b)); break; }
                case Op::Neg: {
                    Value a = pop();
                    if (a.is_int32()) push(Value::from_int32(-a.as_int32()));
                    else if (a.is_int64()) push(Value::from_int32(-a.as_int64()));
                    else if (a.is_float()) push(Value::from_int32(-a.as_float()));
                    else throw std::runtime_error("Neg: invalid operand type");
                    break;
                }
                case Op::Eq:  { Value b = pop(); Value a = pop(); push(Value::from_int32(value_equals(a, b))); break; }
                case Op::Ne:  { Value b = pop(); Value a = pop(); push(Value::from_int32(!value_equals(a, b))); break; }
                case Op::Lt:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() < b.as_int32()));
                    else if (a.is_int64() && b.is_int64())
                        push(Value::from_int32(a.as_int64() < b.as_int64()));
                    else throw std::runtime_error("Lt: invalid operand types");
                    break;
                }
                case Op::Gt:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() > b.as_int32()));
                    else if (a.is_int64() && b.is_int64())
                        push(Value::from_int32(a.as_int64() > b.as_int64()));
                    else throw std::runtime_error("Gt: invalid operand types");
                    break;
                }
                case Op::Le:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() <= b.as_int32()));
                    else throw std::runtime_error("Le: invalid operand types");
                    break;
                }
                case Op::Ge:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() >= b.as_int32()));
                    else throw std::runtime_error("Ge: invalid operand types");
                    break;
                }
                case Op::Jump: {
                    pc = static_cast<std::size_t>(instr.imm);
                    poll_safepoint();
                    continue;
                }
                case Op::JumpIfTrue: {
                    Value v = pop();
                    feedback_.branch_total[pc]++;
                    if (truthy(v)) {
                        feedback_.branch_taken[pc]++;
                        pc = static_cast<std::size_t>(instr.imm);
                        poll_safepoint();
                        continue;
                    }
                    break;
                }
                case Op::JumpIfFalse: {
                    Value v = pop();
                    feedback_.branch_total[pc]++;
                    if (!truthy(v)) {
                        feedback_.branch_taken[pc]++;
                        pc = static_cast<std::size_t>(instr.imm);
                        poll_safepoint();
                        continue;
                    }
                    break;
                }
                case Op::Safepoint:
                    poll_safepoint();
                    break;
                case Op::Return: {
                    Value r = stack_.empty() ? Value::uninit() : pop();
                    poll_safepoint();
                    return r;
                }
                case Op::Halt:
                    return stack_.empty() ? Value::uninit() : pop();
                default:
                    return std::unexpected(make_error(
                        ErrorKind::UnsupportedNode,
                        std::format("unknown opcode: {}", static_cast<int>(instr.op))));
            }
            ++pc;
        }
        return stack_.empty() ? Value::uninit() : pop();
    } catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorKind::Internal, e.what()));
    }
}

}  // namespace jade::granit
