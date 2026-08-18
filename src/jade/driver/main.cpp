// SPDX-License-Identifier: MIT
// .JADE Compiler — driver/main.cpp

#include "jade/driver/Driver.hpp"
#include <cstdlib>

int main(int argc, char** argv) {
    auto r = jade::run_driver(argc, argv);
    if (!r) {
        return 1;
    }
    return *r;
}
