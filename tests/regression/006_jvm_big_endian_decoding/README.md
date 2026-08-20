# Bug 006: JVM bytecode multi-byte operands decoded with wrong endianness

**Root cause:** The JVM opcode decoder in `src/jade/jvm/Opcode.cpp` used
`std::memcpy` to read multi-byte operands (u2, s2, s4, u2u1u1, u2u2, u2u1,
wide iinc). On x86 (little-endian), this reads bytes in the wrong order.

JVM bytecode is **big-endian** per JVMS §4.10.1. For example, the branch
offset `0x00 0x0D` (bytes 0 and 13) should decode to `13`, but `memcpy`
on little-endian reads it as `0x0D00 = 3328`.

This affected:
- All branch instructions (`if_*`, `goto`, `goto_w`, `jsr`, `jsr_w`)
- All constant pool references (`getfield`, `putfield`, `getstatic`,
  `putstatic`, `invokevirtual`, `invokespecial`, `invokestatic`,
  `invokeinterface`, `invokedynamic`, `new`, `anewarray`, `multianewarray`,
  `checkcast`, `instanceof`, `ldc_w`, `ldc2_w`)
- `wide` form opcodes (`wide iload`, `wide iinc`, etc.)
- `tableswitch` and `lookupswitch`

**Symptoms:** Branch targets were wrong (offsets 256× too large), causing
the interpreter to jump to invalid PCs or fall through when it should have
branched. Loops with `if_icmge`/`goto` either infinite-looped or returned
`uninit` (fell off the end of the method). `.class` files compiled by
`javac` (which uses correct big-endian) were completely broken.

The bug was discovered when testing `jadec` on real `.class` files: a
simple Java `for` loop returned `uninit` instead of the correct sum.

**Fix:** Replaced all `std::memcpy` calls in the decoder with explicit
big-endian byte assembly:
```cpp
uint16_t v = (static_cast<uint16_t>(bytes[d.length]) << 8)
          | bytes[d.length + 1];
```
Applied to: `OperandFormat::U2`, `S2`, `S4`, `U2U1U1`, `U2U2`, `U2U1`,
and the `wide` form (`WideIinc` and other wide opcodes).

Also fixed the test encoders in `test_granit_jvm_interpreter.cpp`,
`test_jvm_opcode.cpp`, `test_jvm_lowerer.cpp`, `test_pea_java.cpp`, and
`test_regression_jvm.cpp` to emit big-endian bytes (they were emitting
little-endian, matching the buggy decoder).

**Commit:** (this commit)

**Tests:**
1. `01_minimal_reproducer` — branch offset decoded correctly.
2. `02_variant_trigger` — constant pool index decoded correctly.
3. `03_boundary_negative` — negative offset (backward branch) decoded.
4. `04_integration_contextual` — real javac-compiled .class file runs.
5. `05_deopt_state_reconstruction` — wide iinc with big-endian operands.
