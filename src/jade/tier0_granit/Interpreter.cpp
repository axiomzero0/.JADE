// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Interpreter.cpp
//
// Legacy `Op`-based interpreter. Uses the new `Value` type so it stays
// compatible with the new C# runtime. CIL execution lives in CilInterpreter.

#include "jade/tier0_granit/Interpreter.hpp"
#include "jade/tier0_granit/Bytecode.hpp"

#include <format>
#include <cstring>
#include <bit>

namespace jade::granit {

namespace {

// Error sentinel — returned by arith helpers when types don't match.
// The main loop checks for this and returns an error Result.
static Value kErrorVal = Value::from_int32(0x7FFF'FFFF);

[[nodiscard]] bool is_error(const Value& v) noexcept {
    // Compare by tag + int32 payload.
    return v.is_int32() && v.as_int32() == 0x7FFF'FFFF;
}

[[nodiscard]] Value arith_add(Value a, Value b) {
    if (__builtin_expect(a.is_int32() && b.is_int32(), 1)) {
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
    return kErrorVal;
}

[[nodiscard]] Value arith_sub(Value a, Value b) {
    if (__builtin_expect(a.is_int32() && b.is_int32(), 1)) {
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
    return kErrorVal;
}

[[nodiscard]] Value arith_mul(Value a, Value b) {
    if (__builtin_expect(a.is_int32() && b.is_int32(), 1)) {
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
    return kErrorVal;
}

[[nodiscard]] Value arith_div(Value a, Value b) {
    if (a.is_int32() && b.is_int32()) {
        const auto bv = b.as_int32();
        if (__builtin_expect(bv == 0, 0)) return kErrorVal;
        return Value::from_int32(a.as_int32() / bv);
    }
    if (a.is_int64() && b.is_int64()) {
        const auto bv = b.as_int64();
        if (__builtin_expect(bv == 0, 0)) return kErrorVal;
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
    return kErrorVal;
}

}  // namespace

void Interpreter::push(Value v) {
    stack_.push_back(std::move(v));
    if (__builtin_expect(stack_.size() > max_stack_depth_, 0))
        max_stack_depth_ = stack_.size();
}

Value Interpreter::pop() {
    if (__builtin_expect(stack_.empty(), 0))
        return kErrorVal;
    Value v = std::move(stack_.back());
    stack_.pop_back();
    return v;
}

Value& Interpreter::top() {
    if (__builtin_expect(stack_.empty(), 0)) {
        static Value dummy = Value::uninit();
        return dummy;
    }
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

    // No try/catch — errors are returned via kErrorVal sentinel.
    // This eliminates exception-handling overhead on the hot path.
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
                    if (stack_.size() < 2) return std::unexpected(make_error(ErrorKind::Internal, "stack underflow on Swap"));
                    std::swap(stack_[stack_.size()-1], stack_[stack_.size()-2]);
                    break;
                }
                case Op::Add: { Value b = pop(); Value a = pop(); Value r = arith_add(a, b); if (__builtin_expect(is_error(r), 0)) return std::unexpected(make_error(ErrorKind::Internal, "Add: invalid operand types")); push(r); break; }
                case Op::Sub: { Value b = pop(); Value a = pop(); Value r = arith_sub(a, b); if (__builtin_expect(is_error(r), 0)) return std::unexpected(make_error(ErrorKind::Internal, "Sub: invalid operand types")); push(r); break; }
                case Op::Mul: { Value b = pop(); Value a = pop(); Value r = arith_mul(a, b); if (__builtin_expect(is_error(r), 0)) return std::unexpected(make_error(ErrorKind::Internal, "Mul: invalid operand types")); push(r); break; }
                case Op::Div: { Value b = pop(); Value a = pop(); Value r = arith_div(a, b); if (__builtin_expect(is_error(r), 0)) return std::unexpected(make_error(ErrorKind::Internal, "Div: divide by zero or invalid types")); push(r); break; }
                case Op::Neg: {
                    Value a = pop();
                    if (a.is_int32()) push(Value::from_int32(-a.as_int32()));
                    else if (a.is_int64()) push(Value::from_int64(-a.as_int64()));
                    else if (a.is_float()) push(Value::from_float(-a.as_float()));
                    else return std::unexpected(make_error(ErrorKind::Internal, "Neg: invalid operand type"));
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
                    else return std::unexpected(make_error(ErrorKind::Internal, "Lt: invalid operand types"));
                    break;
                }
                case Op::Gt:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() > b.as_int32()));
                    else if (a.is_int64() && b.is_int64())
                        push(Value::from_int32(a.as_int64() > b.as_int64()));
                    else return std::unexpected(make_error(ErrorKind::Internal, "Gt: invalid operand types"));
                    break;
                }
                case Op::Le:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() <= b.as_int32()));
                    else return std::unexpected(make_error(ErrorKind::Internal, "Le: invalid operand types"));
                    break;
                }
                case Op::Ge:  {
                    Value b = pop(); Value a = pop();
                    if (a.is_int32() && b.is_int32())
                        push(Value::from_int32(a.as_int32() >= b.as_int32()));
                    else return std::unexpected(make_error(ErrorKind::Internal, "Ge: invalid operand types"));
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
}

}  // namespace jade::granit
