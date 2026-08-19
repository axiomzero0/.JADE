// SPDX-License-Identifier: MIT
// .JADE Compiler — tier1_jade/FrameLayout.cpp

#include "jade/tier1_jade/FrameLayout.hpp"

namespace jade::tier1 {

FrameLayout build_frame_layout(uint32_t num_spill_slots,
                               uint32_t num_locals,
                               uint8_t num_reg_args) {
    FrameLayout fl;
    fl.num_reg_args = num_reg_args;

    // Spill slots: [rbp - 8], [rbp - 16], ..., [rbp - 8*N]
    int32_t offset = -8;
    for (uint32_t i = 0; i < num_spill_slots; ++i) {
        fl.spill_slot_offsets.push_back(offset);
        offset -= 8;
    }

    // Locals: [rbp - 8*N - 8], [rbp - 8*N - 16], ...
    for (uint32_t i = 0; i < num_locals; ++i) {
        fl.local_offsets.push_back(offset);
        offset -= 8;
    }

    // Total size = -offset (rounded up to 16-byte alignment).
    uint32_t size = static_cast<uint32_t>(-offset);
    if (size % 16 != 0) {
        size += 16 - (size % 16);
    }
    fl.frame_size = size;
    return fl;
}

}  // namespace jade::tier1
