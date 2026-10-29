// Push constants of the translated shaders (gcn_push_data, MSL struct AuxData).
#pragma once

#include <cstddef>
#include <cstring>

#include "gcn_recompiler.h"

namespace ps4gnm_push {

// SPIRV-Cross declares AuxData with 16-byte alignment, so the MSL struct is gcn_push_data
// rounded up to 16 bytes (288 for the 280-byte C struct). Metal reads the whole struct: binding
// only sizeof(gcn_push_data) leaves its tail undefined (and Metal API validation rejects it).
constexpr size_t kBindBytes = (sizeof(gcn_push_data) + 15) & ~size_t(15);

struct Padded {
    alignas(16) unsigned char bytes[kBindBytes];
    explicit Padded(const gcn_push_data& pd) {
        std::memset(bytes, 0, sizeof(bytes));
        std::memcpy(bytes, &pd, sizeof(pd));
    }
};

} // namespace ps4gnm_push
