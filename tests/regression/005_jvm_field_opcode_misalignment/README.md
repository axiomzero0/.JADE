# Bug 005: JVM field opcodes misaligned (getfield/putfield/getstatic/putstatic)

**Root cause:** The `JvmOpcode` enum in `src/jade/jvm/Opcode.hpp` had the
four field-access opcodes mapped to the wrong opcode numbers:

| Opcode | JVMS §6.5 value | Buggy enum value |
|:--|:--|:--|
| getstatic  | 0xB2 | 0xB4 (wrong) |
| putstatic  | 0xB3 | 0xB5 (wrong) |
| getfield   | 0xB4 | 0xB2 (wrong) |
| putfield   | 0xB5 | 0xB3 (wrong) |

The `getfield`/`putfield` and `getstatic`/`putstatic` pairs were swapped.
The `Opcode.cpp` table had the same misalignment (names matched the enum,
but both were wrong relative to the JVM spec).

**Symptoms:** When JVM bytecode contained `putfield` (0xB5), the decoder
looked up opcode 0xB5 in the table and found `putstatic` — which has a
different operand format (no object reference on the stack). This caused:
- The eval stack to be misaligned (putstatic pops 1 value; putfield pops 2).
- The lowered IR to be incorrect (StFld with wrong inputs, or StFld treated
  as a static store with no object).
- PEA to see incorrect use lists, preventing optimization.

The bug was discovered when writing Java PEA tests: `new + putfield +
getfield + ireturn` should produce a NoEscape allocation (eliminated by
PEA), but the lowerer produced incorrect IR because `putfield` (0xB5) was
decoded as `putstatic`.

**Fix:** Corrected the enum values in `Opcode.hpp` and the name table in
`Opcode.cpp` to match JVMS §6.5:
- `Getstatic = 0xB2`, `Putstatic = 0xB3`
- `Getfield = 0xB4`, `Putfield = 0xB5`

The lowerer's switch cases (`case JvmOpcode::Getfield`, etc.) were already
correct — they dispatched on the enum name, not the numeric value. So the
fix only needed to correct the enum mapping; the lowerer automatically
started decoding the right opcodes.

**Commit:** 7fe6f1a (PEA: 10 golden tests + Box-Unbox round-trip + JVM opcode fix)

**Tests:**
1. `01_minimal_reproducer` — `new + putfield + getfield + ireturn` lowers correctly.
2. `02_variant_trigger` — `putstatic + getstatic` round-trip.
3. `03_boundary_negative` — `getfield` with no preceding `new` (null obj).
4. `04_integration_contextual` — realistic method with multiple field accesses.
5. `05_deopt_state_reconstruction` — field access in a try-catch pattern.
