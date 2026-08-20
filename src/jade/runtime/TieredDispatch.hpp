// SPDX-License-Identifier: MIT
// .JADE Compiler — runtime/TieredDispatch.hpp
//
// Ties the TierManager to the actual compilation pipelines:
//   granit (interpreter) → JADE (baseline JIT) → RUBY (optimizing) → DIAMOND (peak)
//
// On each method invocation:
//   1. Increment the invocation count.
//   2. If a tier threshold is crossed, compile the method at the new tier.
//   3. Execute via the best available entry point.
//
// Per Rule C.1: if compilation fails, fall back to the lower tier gracefully.

#pragma once

#include "jade/core/Result.hpp"
#include "jade/runtime/TierManager.hpp"
#include "jade/tier0_granit/JvmInterpreter.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/jvm/ClassFile.hpp"
#include "jade/jvm/Lowerer.hpp"
#include "jade/ir/Graph.hpp"
#include "jade/ir/passes/PassPipeline.hpp"

#ifdef JADE_HAVE_ASMJIT
#include "jade/tier1_jade/JadeJit.hpp"
#endif

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <print>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// CompiledMethod — holds the compiled code for one method at one tier.
// ─────────────────────────────────────────────────────────────────────────────

struct CompiledMethod {
    Tier tier{Tier::Interpreter};
    // For the interpreter tier, this is null (we use the JvmInterpreter directly).
    // For JADE/RUBY/DIAMOND tiers, this holds the JIT-compiled entry point.
    void* entry{nullptr};
    // The IR graph (for debugging / dump-ir).
    std::unique_ptr<Graph> graph;
#ifdef JADE_HAVE_ASMJIT
    // The JadeJit instance (keeps the asmjit runtime alive).
    std::unique_ptr<tier1::JadeJit> jit;
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// TieredDispatch — the tier escalation dispatch loop.
//
// Usage:
//   TieredDispatch dispatch;
//   auto result = dispatch.invoke(method_bytes, max_locals);
//
// The first N invocations run in the interpreter. At N=100, the method is
// compiled at Tier 1 (JADE baseline JIT). At M=1000, it's recompiled at
// Tier 2 (RUBY optimizing). At K=10000, Tier 3 (DIAMOND peak).
// ─────────────────────────────────────────────────────────────────────────────

class TieredDispatch {
public:
    TieredDispatch() = default;
    explicit TieredDispatch(TierThresholds thresholds) : tier_mgr_(thresholds) {}

    // Invoke a method by its bytecode. The method is identified by a key
    // (typically "ClassName.method_name"). Returns the return value.
    [[nodiscard]] Result<granit::Value> invoke(
        std::string_view method_key,
        std::span<const uint8_t> bytecode,
        uint16_t max_locals,
        uint16_t num_args = 0,
        std::vector<granit::Value> args = {})
    {
        // Get or create the MethodHandle for this method.
        MethodHandle* handle = get_or_create_handle(method_key);
        if (!handle) {
            return std::unexpected(make_error(ErrorKind::Internal,
                "TieredDispatch: failed to create method handle"));
        }

        // Ask the TierManager what tier should be active.
        Tier desired = tier_mgr_.on_invocation(*handle);

        // If the desired tier is different from the current tier, compile.
        if (desired != handle->current_tier) {
            auto compile_r = compile_at_tier(*handle, desired, bytecode, max_locals, num_args);
            if (!compile_r) {
                // Compilation failed — fall back to interpreter.
                std::println(stderr, "[tier] compilation to tier {} failed: {}, falling back to interpreter",
                             static_cast<int>(desired), compile_r.error().what());
                // Continue with interpreter.
            }
        }

        // Execute via the best available entry point.
        return execute(*handle, bytecode, max_locals, num_args, std::move(args));
    }

    // Get the current tier for a method (for testing/inspection).
    [[nodiscard]] Tier current_tier(std::string_view method_key) const {
        auto it = handles_.find(std::string{method_key});
        if (it == handles_.end()) return Tier::Interpreter;
        return it->second.current_tier;
    }

    // Get the invocation count for a method (for testing/inspection).
    [[nodiscard]] uint32_t invocation_count(std::string_view method_key) const {
        auto it = handles_.find(std::string{method_key});
        if (it == handles_.end()) return 0;
        return it->second.invocation_count.load(std::memory_order_relaxed);
    }

    // Set thresholds (for testing with lower thresholds).
    void set_thresholds(TierThresholds t) { tier_mgr_ = TierManager{t}; }

private:
    TierManager tier_mgr_;
    std::unordered_map<std::string, MethodHandle> handles_;
    std::unordered_map<std::string, CompiledMethod> compiled_;

    [[nodiscard]] MethodHandle* get_or_create_handle(std::string_view key) {
        auto [it, inserted] = handles_.try_emplace(std::string{key});
        if (inserted) {
            it->second.name = std::string{key};
        }
        return &it->second;
    }

    [[nodiscard]] Result<void> compile_at_tier(
        MethodHandle& handle,
        Tier tier,
        std::span<const uint8_t> bytecode,
        uint16_t max_locals,
        uint16_t num_args)
    {
        // Lower the bytecode to IR.
        jvm::JvmLowerer lowerer;
        auto g_r = lowerer.lower(bytecode, max_locals, num_args);
        if (!g_r) {
            return std::unexpected(g_r.error());
        }

        // Run the optimization pipeline for the target tier.
        PassContext ctx;
        if (tier >= Tier::Optimizing) {
            auto pipe = (tier >= Tier::Peak)
                ? build_diamond_pipeline()
                : build_ruby_pipeline();
            auto pr = pipe->run(*g_r, ctx);
            if (!pr) {
                return std::unexpected(pr.error());
            }
        }

        // Store the IR graph (for debugging).
        auto graph_ptr = std::make_unique<Graph>(std::move(*g_r));

#ifdef JADE_HAVE_ASMJIT
        // For Tier 1 (Baseline) and above, compile via JADE JIT.
        if (tier >= Tier::Baseline) {
            auto jit = std::make_unique<tier1::JadeJit>();
            auto compile_r = jit->compile(*graph_ptr);
            if (!compile_r) {
                return std::unexpected(compile_r.error());
            }
            void* entry = compile_r->entry_point;

            // Update the MethodHandle.
            tier_mgr_.mark_compiled(handle, tier, entry);

            // Store the compiled method (keeps the JIT runtime alive).
            std::string key = handle.name;
            compiled_[key] = CompiledMethod{
                tier, entry, std::move(graph_ptr), std::move(jit)
            };

            std::println(stderr, "[tier] {} → tier {} (invocations={})",
                         handle.name, static_cast<int>(tier),
                         handle.invocation_count.load(std::memory_order_relaxed));
            return {};
        }
#else
        (void)tier;
        (void)handle;
#endif
        // No JIT available — stay at interpreter tier.
        return {};
    }

    [[nodiscard]] Result<granit::Value> execute(
        MethodHandle& handle,
        std::span<const uint8_t> bytecode,
        uint16_t max_locals,
        uint16_t num_args,
        std::vector<granit::Value> args)
    {
        // For now, always execute via the interpreter (the JIT-compiled code
        // needs a runtime calling convention we haven't fully wired).
        // The JIT compilation still happens (for verification and future use),
        // but execution goes through the interpreter.
        //
        // This is a stepping stone: the tier escalation is REAL (the IR is
        // compiled and optimized at each tier), but execution falls back to
        // the interpreter until the calling convention is wired.
        (void)handle;
        (void)max_locals;
        (void)num_args;
        granit::JvmInterpreter interp;
        return interp.run(bytecode, max_locals, num_args, std::move(args));
    }
};

}  // namespace jade
