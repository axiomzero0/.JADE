#!/usr/bin/env luajit
-- Macrobenchmarks for LuaJIT — equivalent to .JADE macro_bench.cpp

local N = tonumber(arg[1]) or 100000000
local trials = tonumber(arg[2]) or 5

-- 1. JSON scan
local function json_scan(n)
    local count = 0
    for i = 0, n - 1 do
        local ch = i % 128
        if ch ~= 32 and ch ~= 9 and ch ~= 10 then
            count = count + 1
        end
    end
    return count
end

-- 2. ECS update
local function ecs_update(n)
    local sum = 0
    for i = 0, n - 1 do
        sum = sum + i * 5 + 3
    end
    return sum
end

-- 3. Matmul inner
local function matmul_inner(n)
    local sum = 0
    for i = 0, n - 1 do
        sum = sum + i * i
    end
    return sum
end

-- 4. String interner
local function string_intern(n)
    local hash = 0
    for i = 0, n - 1 do
        hash = hash * 31 + i
    end
    return hash
end

-- 5. Expression evaluator
local function expr_eval(n)
    local result = 0
    for i = 0, n - 1 do
        if i % 2 == 1 then
            result = result + i * 3
        else
            result = result + i * 5
        end
    end
    return result
end

local benches = {
    {"json_scan", json_scan},
    {"ecs_update", ecs_update},
    {"matmul_inner", matmul_inner},
    {"string_intern", string_intern},
    {"expr_eval", expr_eval},
}

print("=== LuaJIT Macrobenchmark Suite ===")
print(string.format("Iterations: %d, Trials: %d\n", N, trials))

for _, b in ipairs(benches) do
    local name, fn = b[1], b[2]
    fn(N) -- warmup
    local start = os.clock()
    for rep = 1, trials do
        fn(N)
    end
    local elapsed = os.clock()
    local ns_per = (elapsed / trials / N) * 1e9
    print(string.format("%-22s %10.3f ns/iter", name, ns_per))
end
