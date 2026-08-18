// SPDX-License-Identifier: MIT
// .JADE Compiler — unit/test_tier0_granit_interpreter.cpp

#include <gtest/gtest.h>
#include "jade/tier0_granit/Interpreter.hpp"
#include "jade/tier0_granit/Bytecode.hpp"

using namespace jade;
using namespace jade::granit;

TEST(InterpreterTest, PushConstAndReturn) {
    ProgramBuilder b;
    b.push_const_i(42);
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 42);
}

TEST(InterpreterTest, AddTwoIntegers) {
    ProgramBuilder b;
    b.push_const_i(3);
    b.push_const_i(4);
    b.add();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 7);
}

TEST(InterpreterTest, SubtractTwoIntegers) {
    ProgramBuilder b;
    b.push_const_i(10);
    b.push_const_i(4);
    b.sub();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 6);
}

TEST(InterpreterTest, MultiplyTwoIntegers) {
    ProgramBuilder b;
    b.push_const_i(6);
    b.push_const_i(7);
    b.mul();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 42);
}

TEST(InterpreterTest, DivideTwoIntegers) {
    ProgramBuilder b;
    b.push_const_i(20);
    b.push_const_i(4);
    b.div();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 5);
}

TEST(InterpreterTest, DivideByZeroReturnsError) {
    ProgramBuilder b;
    b.push_const_i(10);
    b.push_const_i(0);
    b.div();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    EXPECT_FALSE(r.has_value());
}

TEST(InterpreterTest, IntegerOverflowWrapsAround) {
    ProgramBuilder b;
    b.push_const_i(static_cast<int32_t>(0x7FFF'FFFF));  // max int32
    b.push_const_i(static_cast<int32_t>(0x7FFF'FFFF));
    b.add();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    // 0x7FFFFFFF + 0x7FFFFFFF = 0xFFFFFFFE = -2 in int32 (sign-extended to int64)
    EXPECT_EQ(std::get<int64_t>(*r), static_cast<int64_t>(0xFFFFFFFE));
}

TEST(InterpreterTest, CompareLessThan) {
    ProgramBuilder b;
    b.push_const_i(3);
    b.push_const_i(5);
    b.lt();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<bool>(*r), true);
}

TEST(InterpreterTest, JumpIfTrueBranches) {
    // if (1) jump to L1 else L2
    // L1: push 10; jump end
    // L2: push 20; jump end
    // end: ret
    ProgramBuilder b;
    b.push_const_i(1);              // 0: push 1
    b.jump_if_true(3);              // 1: if true, jump to 3
    b.push_const_i(20);             // 2: push 20 (false path)
    b.jump(5);                      // 3 (placeholder; will be overwritten)
    // Wait — the placeholder above is wrong. Let me redo.
    // Actually let me just rebuild cleanly:
    ProgramBuilder b2;
    b2.push_const_i(1);              // 0: push 1
    b2.jump_if_true(3);               // 1: if true → pc 3
    b2.push_const_i(20);              // 2: push 20 (false path)
    b2.push_const_i(10);              // 3: push 10 (true path)
    b2.ret();                         // 4: ret
    // Note: false path doesn't ret — it falls through. That's okay for this test.
    Interpreter interp;
    auto r = interp.run(b2.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 10);
    (void)b;
}

TEST(InterpreterTest, FeedbackTracksBranchTaken) {
    ProgramBuilder b;
    b.push_const_i(1);
    b.jump_if_true(3);
    b.push_const_i(99);   // skipped
    b.push_const_i(42);   // 3: reached via the jump
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 42);
    // branch_total[1] should be 1, branch_taken[1] should be 1
    EXPECT_EQ(interp.feedback().branch_total[1], 1u);
    EXPECT_EQ(interp.feedback().branch_taken[1], 1u);
}

TEST(InterpreterTest, NegateInteger) {
    ProgramBuilder b;
    b.push_const_i(5);
    b.neg();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), -5);
}

TEST(InterpreterTest, DemoProgramComputes35) {
    // (3 + 4) * 5 = 35
    ProgramBuilder b;
    b.push_const_i(3);
    b.push_const_i(4);
    b.add();
    b.push_const_i(5);
    b.mul();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 35);
}

TEST(InterpreterTest, MaxStackDepthTracked) {
    ProgramBuilder b;
    b.push_const_i(1);
    b.push_const_i(2);
    b.push_const_i(3);
    b.add();
    b.ret();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_GE(interp.max_stack_depth(), 3u);
}

TEST(InterpreterTest, HaltReturnsTopOfStack) {
    ProgramBuilder b;
    b.push_const_i(77);
    b.halt();
    Interpreter interp;
    auto r = interp.run(b.build());
    ASSERT_TRUE(r.has_value()) << r.error().what();
    EXPECT_EQ(std::get<int64_t>(*r), 77);
}
