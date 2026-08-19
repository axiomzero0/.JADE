#!/usr/bin/env luajit
-- Benchmark: Dead Code Elimination
-- Loop body has dead code that should be eliminated.

local N = tonumber(arg[1]) or 100000000
local iterations = tonumber(arg[2]) or 5

local function compute(n)
    local sum = 0
    for i = 0, n - 1 do
        sum = sum + i
        local dead = i * 17 + 42
        dead = dead - dead
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

print(string.format("Benchmark: DeadCodeElim (LuaJIT)"))
print(string.format("  N = %d", N))
print(string.format("  Result = %d", result))
print(string.format("  Expected = %d", N * (N - 1) / 2))
print(string.format("  Correct = %s", result == (N * (N - 1) / 2) and "True" or "False"))
print(string.format("  Avg time = %.3f ms", avg_ms))
