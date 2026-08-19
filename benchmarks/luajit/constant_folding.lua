#!/usr/bin/env luajit
-- Benchmark: Constant Folding
-- (3+4)*5 = 35, should be folded by the JIT.

local N = tonumber(arg[1]) or 100000000
local iterations = tonumber(arg[2]) or 5

local function compute(n)
    local sum = 0
    for i = 1, n do
        sum = sum + (3 + 4) * 5
    end
    return sum
end

compute(N)

local result = 0
local start = os.clock()
for rep = 1, iterations do
    result = compute(N)
end
local elapsed = os.clock()
local avg_ms = (elapsed / iterations) * 1000

print(string.format("Benchmark: ConstantFolding (LuaJIT)"))
print(string.format("  N = %d", N))
print(string.format("  Result = %d", result))
print(string.format("  Expected = %d", N * 35))
print(string.format("  Correct = %s", result == N * 35 and "True" or "False"))
print(string.format("  Avg time = %.3f ms", avg_ms))
