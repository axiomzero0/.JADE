// SPDX-License-Identifier: MIT
// .JADE Compiler — ir/Verifier.cpp

#include "jade/ir/Verifier.hpp"
#include "jade/ir/NodeKind.hpp"
#include "jade/ir/NodeFlag.hpp"

#include <vector>
#include <string>
#include <format>

namespace jade {

namespace {

struct Verifier {
    const Graph& g;
    std::vector<std::string> errors;

    explicit Verifier(const Graph& graph) : g(graph) {}

    void check_no_dangling() {
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);

            // Check data inputs.
            for (NodeId in : g.data_inputs(id)) {
                if (!in.valid() || in.value > g.size()) {
                    errors.push_back(
                        std::format("node %{} ({}): dangling data input %{}",
                                    id.value, node_kind_name(n.kind), in.value));
                }
            }

            // Check control input.
            if (auto c = g.ctrl_input(id); c.valid()) {
                if (c.value > g.size()) {
                    errors.push_back(
                        std::format("node %{} ({}): dangling ctrl input %{}",
                                    id.value, node_kind_name(n.kind), c.value));
                }
            }

            // Check effect input.
            if (auto e = g.effect_input(id); e.valid()) {
                if (e.value > g.size()) {
                    errors.push_back(
                        std::format("node %{} ({}): dangling effect input %{}",
                                    id.value, node_kind_name(n.kind), e.value));
                }
            }
        }
    }

    void check_effect_chain() {
        // Every Effect-flagged node must have exactly one effect input,
        // except Start (which begins the chain) and Loop phi (which cycles).
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (!n.is_effect()) continue;
            if (n.kind == NodeKind::Start) continue;
            if (n.kind == NodeKind::Loop) continue;  // loop phi of effect

            const NodeId e = g.effect_input(id);
            if (!e.valid()) {
                errors.push_back(
                    std::format("node %{} ({}): Effect flag set but no effect input",
                                id.value, node_kind_name(n.kind)));
            } else if (e.value > g.size()) {
                // Already reported by check_no_dangling.
            }
        }

        // Pure nodes must NOT have effect inputs.
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (!n.is_pure()) continue;
            if (n.is_effect()) continue;  // both flags set is suspicious but legal
            if (auto e = g.effect_input(id); e.valid()) {
                errors.push_back(
                    std::format("node %{} ({}): Pure flag set but has effect input %{}",
                                id.value, node_kind_name(n.kind), e.value));
            }
        }
    }

    void check_guards_have_framestate() {
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (!n.is_guard()) continue;
            if (!n.state.valid()) {
                errors.push_back(
                    std::format("node %{} ({}): IsGuard set but no FrameState attached",
                                id.value, node_kind_name(n.kind)));
            }
        }
    }

    void check_dead_not_referenced() {
        // If a node is marked IsDead, no live node may reference it.
        for (std::size_t i = 0; i < g.size(); ++i) {
            const NodeId id{static_cast<uint32_t>(i + 1)};
            const Node& n = g.node(id);
            if (!n.is_dead()) continue;

            // Search all nodes for a reference to id.
            for (std::size_t j = 0; j < g.size(); ++j) {
                if (i == j) continue;
                const NodeId other_id{static_cast<uint32_t>(j + 1)};
                const Node& other = g.node(other_id);
                if (other.is_dead()) continue;

                for (NodeId in : g.data_inputs(other_id)) {
                    if (in == id) {
                        errors.push_back(
                            std::format("node %{} marked IsDead but referenced by %{} data input",
                                        id.value, other_id.value));
                    }
                }
                if (auto c = g.ctrl_input(other_id); c == id) {
                    errors.push_back(
                        std::format("node %{} marked IsDead but referenced by %{} ctrl input",
                                    id.value, other_id.value));
                }
                if (auto e = g.effect_input(other_id); e == id) {
                    errors.push_back(
                        std::format("node %{} marked IsDead but referenced by %{} effect input",
                                    id.value, other_id.value));
                }
            }
        }
    }
};

}  // namespace

Result<void> verify_graph(const Graph& g) noexcept {
    // Note: this function is compiled with -fno-exceptions (it lives in jade_core,
    // the JIT hot path). We rely on no allocation throwing — std::vector and
    // std::string will simply abort the process on OOM, which is acceptable for
    // the verifier (it only runs in debug builds per Rule 42).

    Verifier v{g};
    v.check_no_dangling();
    v.check_effect_chain();
    v.check_guards_have_framestate();
    v.check_dead_not_referenced();

    if (!v.errors.empty()) {
        std::string msg = "graph verification failed:\n";
        for (const auto& e : v.errors) {
            msg += "  - ";
            msg += e;
            msg += '\n';
        }
        return std::unexpected(make_error_verification(std::move(msg)));
    }
    return {};
}

}  // namespace jade
