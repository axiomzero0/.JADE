// SPDX-License-Identifier: MIT
// .JADE Compiler — driver/Driver.cpp

#include "jade/driver/Driver.hpp"
#include "jade/core/Result.hpp"
#include "jade/tier0_granit/Bytecode.hpp"
#include "jade/tier0_granit/Interpreter.hpp"
#include "jade/tier0_granit/JvmInterpreter.hpp"
#include "jade/tier0_granit/Value.hpp"
#include "jade/jvm/ClassFile.hpp"
#include "jade/jvm/Lowerer.hpp"
#include "jade/ir/Graph.hpp"
#include "jade/ir/Verifier.hpp"
#include "jade/ir/passes/PassPipeline.hpp"

#include <print>
#include <format>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <cstring>

namespace jade {

namespace {

void print_version() {
    std::println("jadec (.JADE Compiler) version 0.1.0");
    std::println("  Tier 0 (granit)  : enabled");
    std::println("  Tier 1 (JADE)    : enabled (LinearScanRegAlloc + asmjit emitter)");
    std::println("  Tier 2 (RUBY)    : enabled (passes: ConstantFolding, GVN, DCE)");
    std::println("  Tier 3 (DIAMOND) : not yet enabled (falls back to RUBY)");
}

void print_help(const char* prog) {
    std::println("Usage: {} [options] file...", prog);
    std::println();
    std::println("Options:");
    std::println("  -h, --help               Show this help and exit");
    std::println("  -V, --version            Show version and exit");
    std::println("  -O<level>                Optimization level (0=granit, 1=JADE, 2=RUBY, 3=DIAMOND)");
    std::println("  --dump-bytecode          Print the bytecode for the program");
    std::println("  --dump-ir                Print the SoN IR after optimization");
    std::println("  --no-run                 Compile only; do not execute");
}

[[nodiscard]] Result<DriverOptions> parse_args(int argc, char** argv) {
    DriverOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view a{argv[i]};
        if (a == "-h" || a == "--help") {
            opts.print_help = true;
        } else if (a == "-V" || a == "--version") {
            opts.print_version = true;
        } else if (a == "--dump-bytecode") {
            opts.dump_bytecode = true;
        } else if (a == "--dump-ir") {
            opts.dump_ir = true;
        } else if (a == "--no-run") {
            opts.run_program = false;
        } else if (a.starts_with("-O")) {
            // -O0..3
            if (a.size() == 3 && a[2] >= '0' && a[2] <= '3') {
                opts.optimization_level = a[2] - '0';
            } else {
                return std::unexpected(make_error(ErrorKind::BadInput,
                    std::format("invalid optimization level: {}", a)));
            }
        } else if (a.starts_with("-")) {
            return std::unexpected(make_error(ErrorKind::BadInput,
                std::format("unknown option: {}", a)));
        } else {
            opts.input_files.emplace_back(a);
        }
    }
    return opts;
}

// ─────────────────────────────────────────────────────────────────────────────
// A tiny built-in program — computes (3 + 4) * 5 = 35.
// Used to verify the interpreter runs end-to-end before we have a real
// parser for .jade source files.
// ─────────────────────────────────────────────────────────────────────────────
granit::Program make_demo_program() {
    using namespace granit;
    ProgramBuilder b;
    b.push_const_i(3);
    b.push_const_i(4);
    b.add();
    b.push_const_i(5);
    b.mul();
    b.ret();
    return b.build();
}

}  // namespace

Result<int> run_driver(int argc, char** argv) {
    auto opts_r = parse_args(argc, argv);
    if (!opts_r) return std::unexpected(opts_r.error());
    auto opts = std::move(*opts_r);

    if (opts.print_help) {
        print_help(argv[0]);
        return 0;
    }
    if (opts.print_version) {
        print_version();
        return 0;
    }

    if (opts.input_files.empty()) {
        // Run the built-in demo.
        auto prog = make_demo_program();
        if (opts.dump_bytecode) {
            for (std::size_t i = 0; i < prog.size(); ++i) {
                std::println("{:4}: {} {}", i, op_name(prog[i].op), prog[i].imm);
            }
        }

        granit::Interpreter interp;
        auto r = interp.run(prog);
        if (!r) {
            std::println(stderr, "granit error: {}", r.error().what());
            return 1;
        }
        if (opts.run_program) {
            std::println("granit result: {}", granit::to_string(*r));
        }

        // Exercise the IR + verifier.
        Graph g;
        GraphBuilder gb(g);
        auto start  = gb.start();
        auto three  = gb.const_int(3);
        auto four   = gb.const_int(4);
        auto seven  = gb.add(three, four);
        auto five   = gb.const_int(5);
        auto result = gb.mul(seven, five);
        auto ret    = gb.return_node(result);
        g.set_ctrl_input(ret, start);
        g.set_effect_input(ret, start);   // Return participates in the effect chain

        if (opts.dump_ir) {
            std::println("--- IR (before optimization) ---");
            std::print("{}", g.dump());
        }

        if (auto v = verify_graph(g); !v) {
            std::println(stderr, "verifier error: {}", v.error().what());
            return 1;
        }

        if (opts.optimization_level >= 2) {
            PassContext ctx;
            std::unique_ptr<PassPipeline> pipe;
            if (opts.optimization_level >= 3) {
                pipe = build_diamond_pipeline();
            } else {
                pipe = build_ruby_pipeline();
            }
            auto pr = pipe->run(g, ctx);
            if (!pr) {
                std::println(stderr, "pipeline error: {}", pr.error().what());
                return 1;
            }
            if (opts.dump_ir) {
                if (opts.optimization_level >= 3) {
                    std::println("--- IR (after DIAMOND pipeline) ---");
                } else {
                    std::println("--- IR (after RUBY pipeline) ---");
                }
                std::print("{}", g.dump());
            }
        }
        return 0;
    }

    // ── Handle .class files ────────────────────────────────────────────────
    // Parse the .class file, find the main method, lower its bytecode to IR,
    // optionally optimize, and execute via the granit JVM interpreter.
    for (const auto& file : opts.input_files) {
        // Check if it's a .class file by extension.
        if (file.size() >= 6 && file.substr(file.size() - 6) == ".class") {
            auto cf_r = jvm::parse_class_file_from_file(file);
            if (!cf_r) {
                std::println(stderr, "jadec: failed to parse {}: {}", file, cf_r.error().what());
                return 1;
            }
            const auto& cf = *cf_r;

            std::println("jadec: loaded class '{}' (major version {})", cf.class_name, cf.major_version);
            std::println("  methods: {}", cf.methods.size());
            for (const auto& m : cf.methods) {
                std::println("    {} {} (code: {} bytes, max_locals={}, max_stack={})",
                             m.name, m.descriptor, m.code.size(), m.max_locals, m.max_stack);
            }

            // Find the main method.
            const jvm::MethodInfo* main = cf.find_main();
            if (!main) {
                std::println(stderr, "jadec: no main method found in {}", file);
                return 1;
            }
            if (main->code.empty()) {
                std::println(stderr, "jadec: main method has no code");
                return 1;
            }

            std::println("\n--- main method bytecode ---");
            for (std::size_t i = 0; i < main->code.size(); ++i) {
                std::println("  {:4}: 0x{:02X}", i, main->code[i]);
            }

            // Lower the bytecode to IR.
            jvm::JvmLowerer lowerer;
            auto g_r = lowerer.lower(main->code, main->max_locals, 0);
            if (!g_r) {
                std::println(stderr, "jadec: lowering failed: {}", g_r.error().what());
                return 1;
            }

            if (opts.dump_ir) {
                std::println("\n--- IR (before optimization) ---");
                std::print("{}", g_r->dump());
            }

            if (opts.optimization_level >= 2) {
                PassContext ctx;
                std::unique_ptr<PassPipeline> pipe;
                if (opts.optimization_level >= 3) {
                    pipe = build_diamond_pipeline();
                } else {
                    pipe = build_ruby_pipeline();
                }
                auto pr = pipe->run(*g_r, ctx);
                if (!pr) {
                    std::println(stderr, "pipeline error: {}", pr.error().what());
                    return 1;
                }
                if (opts.dump_ir) {
                    std::println("\n--- IR (after pipeline) ---");
                    std::print("{}", g_r->dump());
                }
            }

            // Execute via the granit JVM interpreter.
            if (opts.run_program) {
                granit::JvmInterpreter interp;
                std::vector<granit::Value> args;  // no args for now
                auto exec_r = interp.run(main->code, main->max_locals, 0, std::move(args));
                if (!exec_r) {
                    std::println(stderr, "jadec: execution failed: {}", exec_r.error().what());
                    return 1;
                }
                std::println("\n--- execution result ---");
                std::println("return value: {}", granit::to_string(*exec_r));
            }

            return 0;
        }

        std::println(stderr, "jadec: unsupported file type: {}", file);
        std::println(stderr, "       .class files are supported. Run without arguments for the demo.");
        return 1;
    }

    return 0;
}

}  // namespace jade
