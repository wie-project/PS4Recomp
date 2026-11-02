// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>
#include "api/gcn_spirv_geometry.h"
#include "gcn_recompiler.h"

namespace GcnMsl {

enum class StageRole : uint8_t {
    Plain = 0,
    TessVertex,
    TessControl,
    TessEval,
    GeometryKernel,
};

struct StageVariant {
    StageRole role = StageRole::Plain;
    uint32_t index_type = 0;      // TessVertex: 0 = unindexed, 1 = uint16, 2 = uint32
    uint32_t domain = 0;          // TessControl fallback: GcnTessDomain
    uint32_t output_vertices = 0; // TessEval: control points per patch
    uint32_t patch_first = 0;     // TessEval: first per-patch location
    uint32_t patch_end = 0;       // TessEval: one past last per-patch location
};

struct TessLayout {
    uint32_t input = 0;
    uint32_t output = 0;
    uint32_t patch_input = 0;
    uint32_t patch_output = 0;
    std::vector<GcnSpirv::InterfaceMember> members;
};

struct SpirvScan {
    bool valid = false;
    uint32_t local_size[3] = {0, 0, 0};
    uint32_t domain = 0;  // GcnTessDomain
    uint32_t spacing = 0; // GcnTessSpacing
    bool clockwise = false;
    bool point_mode = false;
    uint32_t output_vertices = 0;
    uint32_t patch_first = 0;
    uint32_t patch_end = 0;
};

SpirvScan ScanSpirv(std::span<const uint32_t> code);

// SPIR-V (set 0) binding -> MSL slot(s) chosen by SPIRV-Cross automatic assignment.
struct Bindings {
    std::map<uint32_t, std::vector<uint32_t>> buffers; // aliases share one SPIR-V binding
    std::map<uint32_t, uint32_t> textures;             // first slot (arrays take consecutive)
    std::map<uint32_t, uint32_t> samplers;
    int32_t push_constant_buffer = -1;
    uint32_t first_free_buffer = 0;
    uint32_t num_textures = 0;
    uint32_t num_samplers = 0;
    // Storage buffers moved into the set 0 argument buffer (only when the discrete slots would
    // exceed gcn_msl_options.max_buffer_slots): SPIR-V binding -> [[id(n)]] entry index.
    std::map<uint32_t, uint32_t> arg_entries;
    int32_t argument_buffer = -1; // [[buffer(n)]] of the argument buffer, -1 if none
    uint32_t num_arg_entries = 0;
};

// arg_candidates: SPIR-V bindings of storage buffers that may be reached through an argument
// buffer (guest memory buffers). nullptr disables the argument buffer fallback.
bool Convert(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt, std::string& out,
             std::string& err, Bindings* bindings,
             const std::vector<uint32_t>* arg_candidates = nullptr,
             const StageVariant* variant = nullptr, TessLayout* layout = nullptr);

// Buffer slots of a vertex shader's capture variant: the stage output struct of vertex v of
// instance i is written to output[i * params[0] + v] ([[buffer(output_buffer)]], indexed from the
// draw's base vertex / base instance; params is [[buffer(params_buffer)]]).
struct CaptureSlots {
    uint32_t output_buffer = 0;
    uint32_t params_buffer = 0;
};

// MSL of the vertex shader `spirv` that stores its outputs instead of rasterizing them, with the
// resource slots of `like` (the Bindings of the shader's own MSL, from Convert). Fails if a slot
// would differ.
bool ConvertCapture(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt,
                    const Bindings& like, const CaptureSlots& slots, std::string& out,
                    std::string& err);

} // namespace GcnMsl
