// SPDX-License-Identifier: MIT
// .JADE Compiler — tier0_granit/Interpreter.cpp

#include "jade/tier0_granit/Interpreter.hpp"
#include "jade/tier0_granit/Bytecode.hpp"

#include <format>
#include <stdexcept>
#include <cstring>
#include <bit>

namespace jade::granit {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Arithmetic helpers — match granit semantics (Rule A.4).
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] int64_t wrap_add(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
[[nodiscard]] int64_t wrap_sub(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
[[nodiscard]] int64_t wrap_mul(int64_t a, int64_t b) noexcept {
    return std::bit_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

[[nodiscard]] Value arith_add(Value a, Value b) {
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        return wrap_add(std::get<int64_t>(a), std::get<int64_t>(b));
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        const double af = std::holds_alternative<int64_t>(a)
                              ? static_cast<double>(std::get<int64_t>(a))
                              : std::get<double>(a);
        const double bf = std::holds_alternative<int64_t>(b)
                              ? static_cast<double>(std::get<int64_t>(b))
                              : std::get<double>(b);
        return af + bf;
    }
    throw std::runtime_error("Add: invalid operand types");
}

[[nodiscard]] Value arith_sub(Value a, Value b) {
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        return wrap_sub(std::get<int64_t>(a), std::get<int64_t>(b));
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        const double af = std::holds_alternative<int64_t>(a)
                              ? static_cast<double>(std::get<int64_t>(a))
                              : std::get<double>(a);
        const double bf = std::holds_alternative<int64_t>(b)
                              ? static_cast<double>(std::get<int64_t>(b))
                              : std::get<double>(b);
        return af - bf;
    }
    throw std::runtime_error("Sub: invalid operand types");
}

[[nodiscard]] Value arith_mul(Value a, Value b) {
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        return wrap_mul(std::get<int64_t>(a), std::get<int64_t>(b));
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        const double af = std::holds_alternative<int64_t>(a)
                              ? static_cast<double>(std::get<int64_t>(a))
                              : std::get<double>(a);
        const double bf = std::holds_alternative<int64_t>(b)
                              ? static_cast<double>(std::get<int64_t>(b))
                              : std::get<double>(b);
        return af * bf;
    }
    throw std::runtime_error("Mul: invalid operand types");
}

[[nodiscard]] Value arith_div(Value a, Value b) {
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        const auto bv = std::get<int64_t>(b);
        if (bv == 0) throw std::runtime_error("Div: divide by zero");
        return std::get<int64_t>(a) / bv;
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        const double af = std::holds_alternative<int64_t>(a)
                              ? static_cast<double>(std::get<int64_t>(a))
                              : std::get<double>(a);
        const double bf = std::holds_alternative<int64_t>(b)
                              ? static_cast<double>(std::get<int64_t>(b))
                              : std::get<double>(b);
        return af / bf;
    }
    throw std::runtime_error("Div: invalid operand types");
}

[[nodiscard]] bool truthy(const Value& v) {
    if (std::holds_alternative<bool>(v))    return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v) != 0;
    if (std::holds_alternative<double>(v))  return std::get<double>(v) != 0.0;
    if (std::holds_alternative<std::nullptr_t>(v)) return false;
    return false;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Interpreter
// ─────────────────────────────────────────────────────────────────────────────

std::string to_string(Value v) {
    if (std::holds_alternative<int64_t>(v)) return std::format("int:{}", std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))  return std::format("float:{}", std::get<double>(v));
    if (std::holds_alternative<bool>(v))    return std::format("bool:{}", std::get<bool>(v));
    if (std::holds_alternative<std::nullptr_t>(v)) return "null";
    return "<unknown>";
}

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

    // Type feedback sizing.
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
                case Op::PushConst0:   push(int64_t{0}); break;
                case Op::PushConst1:   push(int64_t{1}); break;
                case Op::PushConstI:    push(int64_t{instr.imm}); break;
                case Op::PushConstF: {
                    double v;
                    std::memcpy(&v, &instr.imm, sizeof(v));
                    push(v);
                    break;
                }
                case Op::PushConstN:   push(nullptr); break;
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
                    if (std::holds_alternative<int64_t>(a)) push(-std::get<int64_t>(a));
                    else if (std::holds_alternative<double>(a)) push(-std::get<double>(a));
                    else throw std::runtime_error("Neg: invalid operand type");
                    break;
                }
                case Op::Eq:  { Value b = pop(); Value a = pop(); push(a == b); break; }
                case Op::Ne:  { Value b = pop(); Value a = pop(); push(!(a == b)); break; }
                case Op::Lt:  { Value b = pop(); Value a = pop();
                                push(std::get<int64_t>(a) < std::get<int64_t>(b)); break; }
                case Op::Gt:  { Value b = pop(); Value a = pop();
                                push(std::get<int64_t>(a) > std::get<int64_t>(b)); break; }
                case Op::Le:  { Value b = pop(); Value a = pop();
                                push(std::get<int64_t>(a) <= std::get<int64_t>(b)); break; }
                case Op::Ge:  { Value b = pop(); Value a = pop();
                                push(std::get<int64_t>(a) >= std::get<int64_t>(b)); break; }
                case Op::Jump: {
                    pc = static_cast<std::size_t>(instr.imm);
                    poll_safepoint();  // back-edge / forward jump
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
                    Value r = stack_.empty() ? Value{nullptr} : pop();
                    poll_safepoint();
                    return r;
                }
                case Op::Halt:
                    return stack_.empty() ? Value{nullptr} : pop();
                default:
                    return std::unexpected(make_error(
                        ErrorKind::UnsupportedNode,
                        std::format("unknown opcode: {}", static_cast<int>(instr.op))));
            }
            ++pc;
        }
        return stack_.empty() ? Value{nullptr} : pop();
    } catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorKind::Internal, e.what()));
    }
}

}  // namespace jade::granit
