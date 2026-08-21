// SPDX-License-Identifier: MIT
// .JADE Compiler — driver/Driver.hpp

#pragma once

#include "jade/core/Result.hpp"
#include "jade/tier0_granit/Bytecode.hpp"
#include "jade/tier0_granit/Interpreter.hpp"

#include <string>
#include <vector>

namespace jade {

struct DriverOptions {
    std::vector<std::string> input_files;
    bool print_version{false};
    bool print_help{false};
    bool dump_ast{false};
    bool dump_bytecode{false};
    bool dump_ir{false};
    bool run_program{true};
    bool tiered{false};           // --tiered: use TieredDispatch for tier escalation
    uint32_t invocations{1};      // --invocations N: how many times to invoke (for tier testing)
    int  optimization_level{0};   // 0=granit only, 1=JADE, 2=RUBY, 3=DIAMOND
};

[[nodiscard]] Result<int> run_driver(int argc, char** argv);

}  // namespace jade
