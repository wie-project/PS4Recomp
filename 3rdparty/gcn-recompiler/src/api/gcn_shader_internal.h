// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Definition of the opaque gcn_shader object, shared by the C API implementation
// (gcn_recompiler.cpp) and its persistent cache support (gcn_shader_cache.cpp).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "gcn_recompiler.h"

#include "api/gcn_msl.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"
#include "shader_recompiler/specialization.h"

struct gcn_shader {
    std::array<u32, 16> user_data{};
    Shader::Info info;
    Shader::RuntimeInfo runtime{};
    Shader::Profile profile{};
    Shader::StageSpecialization spec;
    std::vector<u32> gs_copy; // copy shader code (GS only), referenced by runtime.hw.gs.vs_copy
    std::vector<u32> spirv;
    std::string msl;
    std::string msl_error;
    u64 code_addr{};
    u32 code_size{};
    GcnMsl::Bindings msl_bindings;
};
