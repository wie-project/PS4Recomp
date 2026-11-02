// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SPIR-V -> MSL via SPIRV-Cross (vendored at PS4Recomp/3rdparty/spirv-cross).

#include "api/gcn_msl.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "api/gcn_shader_internal.h"
#include "compat/host_fp_env.h"
#include "spirv.hpp"

#ifdef GCN_HAVE_MSL
#include "spirv_msl.hpp"
#endif

namespace GcnMsl {

SpirvScan ScanSpirv(std::span<const uint32_t> code) {
    SpirvScan scan;
    if (code.size() < 5 || code[0] != spv::MagicNumber) {
        return scan;
    }
    scan.valid = true;
    std::unordered_set<uint32_t> patch_ids;
    std::unordered_map<uint32_t, uint32_t> locations;
    for (size_t i = 5; i < code.size();) {
        const uint32_t word = code[i];
        const uint32_t length = word >> spv::WordCountShift;
        const uint32_t op = word & spv::OpCodeMask;
        if (length == 0 || i + length > code.size()) {
            scan.valid = false;
            return scan;
        }
        switch (op) {
        case spv::OpExecutionMode:
            if (length >= 6 && code[i + 2] == spv::ExecutionModeLocalSize) {
                scan.local_size[0] = code[i + 3];
                scan.local_size[1] = code[i + 4];
                scan.local_size[2] = code[i + 5];
            }
            if (length >= 3) {
                switch (code[i + 2]) {
                case spv::ExecutionModeTriangles:
                    scan.domain = GCN_TESS_DOMAIN_TRIANGLES;
                    break;
                case spv::ExecutionModeQuads:
                    scan.domain = GCN_TESS_DOMAIN_QUADS;
                    break;
                case spv::ExecutionModeIsolines:
                    scan.domain = GCN_TESS_DOMAIN_ISOLINES;
                    break;
                case spv::ExecutionModeSpacingEqual:
                    scan.spacing = GCN_TESS_SPACING_EQUAL;
                    break;
                case spv::ExecutionModeSpacingFractionalOdd:
                    scan.spacing = GCN_TESS_SPACING_FRACTIONAL_ODD;
                    break;
                case spv::ExecutionModeSpacingFractionalEven:
                    scan.spacing = GCN_TESS_SPACING_FRACTIONAL_EVEN;
                    break;
                case spv::ExecutionModeVertexOrderCw:
                    scan.clockwise = true;
                    break;
                case spv::ExecutionModeVertexOrderCcw:
                    scan.clockwise = false;
                    break;
                case spv::ExecutionModePointMode:
                    scan.point_mode = true;
                    break;
                case spv::ExecutionModeOutputVertices:
                    if (length >= 4) {
                        scan.output_vertices = code[i + 3];
                    }
                    break;
                default:
                    break;
                }
            }
            break;
        case spv::OpDecorate:
            if (length >= 3 && code[i + 2] == spv::DecorationPatch) {
                patch_ids.insert(code[i + 1]);
            }
            if (length >= 4 && code[i + 2] == spv::DecorationLocation) {
                locations[code[i + 1]] = code[i + 3];
            }
            break;
        default:
            break;
        }
        i += length;
    }
    for (const uint32_t id : patch_ids) {
        if (const auto it = locations.find(id); it != locations.end()) {
            const bool first = scan.patch_end == 0;
            scan.patch_first = first ? it->second : std::min(scan.patch_first, it->second);
            scan.patch_end = std::max(scan.patch_end, it->second + 1);
        }
    }
    return scan;
}

#ifdef GCN_HAVE_MSL
namespace {

GcnSpirv::ScalarType ToScalarType(spirv_cross::SPIRType::BaseType base) {
    switch (base) {
    case spirv_cross::SPIRType::Float:
        return GcnSpirv::ScalarType::Float;
    case spirv_cross::SPIRType::Int:
        return GcnSpirv::ScalarType::Int;
    case spirv_cross::SPIRType::UInt:
        return GcnSpirv::ScalarType::Uint;
    default:
        return GcnSpirv::ScalarType::Other;
    }
}

/// CompilerMSL subclass with access to the interface structs SPIRV-Cross synthesizes for
/// inter-stage data in tessellation and geometry pipelines.
class TessCompilerMSL final : public spirv_cross::CompilerMSL {
public:
    using spirv_cross::CompilerMSL::CompilerMSL;

    TessLayout Layout() {
        TessLayout layout;
        if (stage_in_var_id) {
            layout.input = InterfaceStructSize(get_stage_in_struct_type());
        }
        if (stage_out_var_id) {
            layout.output = InterfaceStructSize(get_stage_out_struct_type());
            layout.members = InterfaceMembers(get_stage_out_struct_type());
        }
        if (patch_stage_in_var_id) {
            layout.patch_input = InterfaceStructSize(get_patch_stage_in_struct_type());
        }
        if (patch_stage_out_var_id) {
            layout.patch_output = InterfaceStructSize(get_patch_stage_out_struct_type());
        }
        return layout;
    }

private:
    uint32_t InterfaceStructSize(const spirv_cross::SPIRType& type) const {
        uint32_t offset = 0, alignment = 1;
        for (uint32_t i = 0; i < type.member_types.size(); ++i) {
            const uint32_t member_alignment = get_declared_struct_member_alignment_msl(type, i);
            offset = (offset + member_alignment - 1) / member_alignment * member_alignment;
            offset += static_cast<uint32_t>(get_declared_struct_member_size_msl(type, i));
            alignment = std::max(alignment, member_alignment);
        }
        return (offset + alignment - 1) / alignment * alignment;
    }

    std::vector<GcnSpirv::InterfaceMember> InterfaceMembers(
        const spirv_cross::SPIRType& type) const {
        std::vector<GcnSpirv::InterfaceMember> members;
        uint32_t offset = 0;
        for (uint32_t i = 0; i < type.member_types.size(); ++i) {
            const uint32_t member_alignment = get_declared_struct_member_alignment_msl(type, i);
            offset = (offset + member_alignment - 1) / member_alignment * member_alignment;
            const spirv_cross::SPIRType& member_type = get_type(type.member_types[i]);
            const bool builtin = has_member_decoration(type.self, i, spv::DecorationBuiltIn);
            if (member_type.array.empty() && member_type.columns == 1 &&
                (builtin || has_member_decoration(type.self, i, spv::DecorationLocation))) {
                GcnSpirv::InterfaceMember m;
                m.offset = offset;
                m.components = member_type.vecsize;
                m.type = ToScalarType(member_type.basetype);
                if (builtin) {
                    m.position = get_member_decoration(type.self, i, spv::DecorationBuiltIn) ==
                                 spv::BuiltInPosition;
                } else {
                    m.location = get_member_decoration(type.self, i, spv::DecorationLocation);
                }
                if (builtin ? m.position : true) {
                    members.push_back(m);
                }
            }
            offset += static_cast<uint32_t>(get_declared_struct_member_size_msl(type, i));
        }
        return members;
    }
};

// One SPIRV-Cross run. arg_ids: SPIR-V binding -> [[id(n)]] of the storage buffers to place in
// the descriptor set 0 argument buffer (empty: every resource gets a discrete slot).
// capture: non-null for a vertex shader whose outputs go to a buffer instead of the rasterizer
// (SPIRV-Cross capture_output_to_buffer).
bool ConvertOnce(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt,
                 const std::map<uint32_t, uint32_t>& arg_ids, std::string& out, Bindings* bindings,
                 uint32_t* num_buffer_slots, const CaptureSlots* capture = nullptr,
                 const StageVariant* variant = nullptr, TessLayout* layout = nullptr) {
    using namespace spirv_cross;
    TessCompilerMSL msl(spirv);
    auto mo = msl.get_msl_options();
    mo.platform = CompilerMSL::Options::macOS;
    mo.set_msl_version(opt.msl_version_major ? opt.msl_version_major : 2,
                       opt.msl_version_major ? opt.msl_version_minor : 3);
    const bool use_arg_buffer = !arg_ids.empty();
    mo.argument_buffers = opt.argument_buffers || use_arg_buffer;
    if (use_arg_buffer) {
        // Tier 2 (all Apple GPUs): resources are plain 64-bit GPU addresses in [[id]] order, so
        // the runtime can fill the argument buffer without an MTLArgumentEncoder.
        mo.argument_buffers_tier = CompilerMSL::Options::ArgumentBuffersTier::Tier2;
    }
    mo.pad_fragment_output_components = opt.pad_fragment_output_components;
    mo.enable_point_size_builtin = opt.enable_point_size_builtin;
    mo.invariant_float_math = opt.invariant_float_math;
    if (capture) {
        mo.capture_output_to_buffer = true;
        mo.shader_output_buffer_index = capture->output_buffer;
        mo.indirect_params_buffer_index = capture->params_buffer;
    }
    const StageRole role = variant ? variant->role : StageRole::Plain;
    if (role != StageRole::Plain && role != StageRole::GeometryKernel) {
        mo.shader_output_buffer_index = GCN_TESS_OUTPUT_BUFFER_SLOT;
        mo.shader_index_buffer_index = GCN_TESS_INDEX_BUFFER_SLOT;
        mo.shader_patch_output_buffer_index = GCN_TESS_PATCH_BUFFER_SLOT;
        mo.shader_patch_input_buffer_index = GCN_TESS_PATCH_BUFFER_SLOT;
        mo.shader_tess_factor_buffer_index = GCN_TESS_FACTOR_BUFFER_SLOT;
        mo.indirect_params_buffer_index = GCN_TESS_PARAMS_BUFFER_SLOT;
        mo.shader_input_buffer_index = GCN_TESS_INPUT_BUFFER_SLOT;
        mo.tess_domain_origin_lower_left = false;
        switch (role) {
        case StageRole::TessVertex:
            mo.vertex_for_tessellation = true;
            mo.capture_output_to_buffer = true;
            mo.vertex_index_type =
                variant->index_type == 1   ? CompilerMSL::Options::IndexType::UInt16
                : variant->index_type == 2 ? CompilerMSL::Options::IndexType::UInt32
                                           : CompilerMSL::Options::IndexType::None;
            break;
        case StageRole::TessControl:
            mo.multi_patch_workgroup = true;
            mo.capture_output_to_buffer = true;
            break;
        case StageRole::TessEval:
            mo.raw_buffer_tese_input = true;
            break;
        case StageRole::Plain:
        case StageRole::GeometryKernel:
            break;
        }
    }
    msl.set_msl_options(mo);

    if (role == StageRole::TessControl) {
        const auto& modes = msl.get_execution_mode_bitset();
        if (!modes.get(spv::ExecutionModeTriangles) && !modes.get(spv::ExecutionModeQuads) &&
            !modes.get(spv::ExecutionModeIsolines)) {
            if (variant->domain != GCN_TESS_DOMAIN_TRIANGLES &&
                variant->domain != GCN_TESS_DOMAIN_QUADS &&
                variant->domain != GCN_TESS_DOMAIN_ISOLINES) {
                throw CompilerError("no triangle, quad, or isoline domain for the control shader");
            }
            msl.set_execution_mode(variant->domain == GCN_TESS_DOMAIN_TRIANGLES
                                       ? spv::ExecutionModeTriangles
                                   : variant->domain == GCN_TESS_DOMAIN_ISOLINES
                                       ? spv::ExecutionModeIsolines
                                       : spv::ExecutionModeQuads);
        }
    } else if (role == StageRole::TessEval) {
        if (variant->output_vertices == 0) {
            throw CompilerError("no output vertex count for the evaluation shader");
        }
        msl.set_execution_mode(spv::ExecutionModeOutputVertices, variant->output_vertices);
        for (uint32_t location = variant->patch_first; location < variant->patch_end; ++location) {
            MSLShaderInterfaceVariable input{};
            input.location = location;
            input.vecsize = 4;
            input.rate = MSL_SHADER_VARIABLE_RATE_PER_PATCH;
            msl.add_msl_shader_input(input);
        }
    }

    const auto model = msl.get_execution_model();
    if (role == StageRole::GeometryKernel) {
        for (uint32_t binding = 0; binding < 4; ++binding) {
            MSLResourceBinding rb{};
            rb.stage = model;
            rb.basetype = SPIRType::Struct;
            rb.desc_set = GcnSpirv::kGeometryKernelSet;
            rb.binding = binding;
            rb.count = 1;
            rb.msl_buffer = GCN_GEO_ES_BUFFER_SLOT + binding;
            msl.add_msl_resource_binding(rb);
        }
    }

    std::set<uint32_t> coherent_ids; // [[id]]s of Coherent/Volatile storage buffers
    if (use_arg_buffer) {
        // Only the chosen storage buffers stay in set 0 (the argument buffer); everything else
        // in set 0 moves to set 1, which keeps discrete [[buffer/texture/sampler(n)]] slots.
        // Geometry kernel inter-stage buffers in set kGeometryKernelSet stay discrete in set 7.
        const auto res = msl.get_shader_resources();
        for (const auto& r : res.storage_buffers) {
            if (msl.get_decoration(r.id, spv::DecorationDescriptorSet) != 0) {
                continue;
            }
            const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
            if (arg_ids.count(b) && (msl.has_decoration(r.id, spv::DecorationCoherent) ||
                                     msl.has_decoration(r.id, spv::DecorationVolatile))) {
                coherent_ids.insert(arg_ids.at(b));
            }
        }
        const auto to_discrete = [&](const spirv_cross::SmallVector<Resource>& list,
                                     bool keep_args) {
            for (const auto& r : list) {
                if (msl.get_decoration(r.id, spv::DecorationDescriptorSet) != 0) {
                    continue;
                }
                const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
                if (!(keep_args && arg_ids.count(b))) {
                    msl.set_decoration(r.id, spv::DecorationDescriptorSet, 1);
                }
            }
        };
        to_discrete(res.storage_buffers, true);
        to_discrete(res.uniform_buffers, false);
        to_discrete(res.separate_images, false);
        to_discrete(res.storage_images, false);
        to_discrete(res.sampled_images, false);
        to_discrete(res.separate_samplers, false);
        msl.add_discrete_descriptor_set(1);
        if (role == StageRole::GeometryKernel) {
            msl.add_discrete_descriptor_set(GcnSpirv::kGeometryKernelSet);
        }
        for (const auto& [b, id] : arg_ids) {
            MSLResourceBinding rb{};
            rb.stage = model;
            rb.desc_set = 0;
            rb.binding = b;
            rb.count = 1;
            rb.msl_buffer = id;
            msl.add_msl_resource_binding(rb);
        }
        // The argument buffer itself takes slot 0; automatic discrete slots start after it.
        MSLResourceBinding ab{};
        ab.stage = model;
        ab.desc_set = 0;
        ab.binding = kArgumentBufferBinding;
        ab.msl_buffer = 0;
        msl.add_msl_resource_binding(ab);
    }
    out = msl.compile();

    if (use_arg_buffer) {
        // The runtime writes entry n at byte offset 8*n: make sure the emitted struct holds
        // exactly the expected members, in order, with nothing else in between.
        const size_t sb = out.find("struct spvDescriptorSetBuffer0");
        const size_t se = sb == std::string::npos ? sb : out.find("};", sb);
        if (se == std::string::npos) {
            throw CompilerError("argument buffer struct missing");
        }
        std::string body = out.substr(sb, se - sb);
        uint32_t n = 0;
        for (size_t p = body.find(';'); p != std::string::npos; p = body.find(';', p + 1)) {
            const size_t ls = body.rfind('\n', p);
            const std::string member = body.substr(ls + 1, p - ls - 1);
            const std::string want = "[[id(" + std::to_string(n) + ")]]";
            if (member.find('*') == std::string::npos || member.find(want) == std::string::npos) {
                throw CompilerError("unexpected argument buffer member: " + member);
            }
            // SPIRV-Cross drops the Coherent -> volatile qualifier on argument buffer members;
            // restore it so these buffers behave as in discrete-slot shaders.
            const size_t dev = member.find("device ");
            if (coherent_ids.count(n) && dev != std::string::npos &&
                member.find("volatile") == std::string::npos) {
                body.insert(ls + 1 + dev, "volatile ");
                p += 9;
            }
            n++;
        }
        if (n != arg_ids.size()) {
            throw CompilerError("argument buffer member count mismatch");
        }
        out.replace(sb, se - sb, body);
    }

    uint32_t max_buf = use_arg_buffer ? 1 : 0;
    Bindings local;
    Bindings& bnd = bindings ? *bindings : local;
    bnd = {};
    const auto res = msl.get_shader_resources();
    const auto note_buf = [&](uint32_t slot) {
        if (slot != uint32_t(-1)) {
            max_buf = std::max(max_buf, slot + 1);
        }
    };
    const auto add_buffers = [&](const spirv_cross::SmallVector<Resource>& list) {
        for (const auto& r : list) {
            const uint32_t set = msl.get_decoration(r.id, spv::DecorationDescriptorSet);
            if (role == StageRole::GeometryKernel && set == GcnSpirv::kGeometryKernelSet) {
                continue; // inter-stage buffers at fixed slots 13..16
            }
            const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
            if (use_arg_buffer && set == 0) {
                continue; // in the argument buffer
            }
            const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
            if (slot != uint32_t(-1)) {
                auto& v = bnd.buffers[b];
                if (std::find(v.begin(), v.end(), slot) == v.end()) {
                    v.push_back(slot);
                }
            }
            note_buf(slot);
        }
    };
    add_buffers(res.storage_buffers);
    add_buffers(res.uniform_buffers);
    for (const auto& r : res.push_constant_buffers) {
        const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
        if (slot != uint32_t(-1)) {
            bnd.push_constant_buffer = int32_t(slot);
        }
        note_buf(slot);
    }
    uint32_t max_tex = 0;
    const auto add_tex = [&](const Resource& r) {
        const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
        const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
        if (slot == uint32_t(-1)) {
            return;
        }
        bnd.textures[b] = slot;
        const auto& type = msl.get_type(r.type_id);
        uint32_t n = 1;
        for (auto a : type.array) {
            n *= a;
        }
        max_tex = std::max(max_tex, slot + n);
    };
    for (const auto& r : res.separate_images) {
        add_tex(r);
    }
    for (const auto& r : res.storage_images) {
        add_tex(r);
    }
    for (const auto& r : res.sampled_images) {
        add_tex(r);
    }
    uint32_t max_samp = 0;
    for (const auto& r : res.separate_samplers) {
        const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
        const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
        if (slot == uint32_t(-1)) {
            continue;
        }
        bnd.samplers[b] = slot;
        max_samp = std::max(max_samp, slot + 1);
    }
    bnd.first_free_buffer = max_buf;
    bnd.num_textures = max_tex;
    bnd.num_samplers = max_samp;
    if (use_arg_buffer) {
        bnd.arg_entries = arg_ids;
        bnd.argument_buffer = 0;
        bnd.num_arg_entries = uint32_t(arg_ids.size());
    }
    if (layout) {
        *layout = msl.Layout();
    }
    *num_buffer_slots = max_buf;
    return true;
}

} // namespace

bool Convert(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt, std::string& out,
             std::string& err, Bindings* bindings, const std::vector<uint32_t>* arg_candidates,
             const StageVariant* variant, TessLayout* layout) {
    try {
        uint32_t slots = 0;
        ConvertOnce(spirv, opt, {}, out, bindings, &slots, nullptr, variant, layout);
        const bool is_tess_or_gs = variant && variant->role != StageRole::Plain;
        const uint32_t default_limit = is_tess_or_gs ? 8u : 31u;
        const uint32_t limit = opt.max_buffer_slots
                                   ? (is_tess_or_gs ? std::min<uint32_t>(opt.max_buffer_slots,
                                                                         GCN_TESS_OUTPUT_BUFFER_SLOT)
                                                    : opt.max_buffer_slots)
                                   : default_limit;
        const bool force_arg_buf =
            is_tess_or_gs && arg_candidates && arg_candidates->size() > 8;
        if ((slots <= limit && !force_arg_buf) || !arg_candidates || arg_candidates->empty() ||
            opt.argument_buffers) {
            return true;
        }
        // Too many discrete buffers for Metal (or > 8 buffers in a tessellation / geometry stage
        // where inter-stage buffers occupy slots 13..17): reach the active guest storage buffers
        // through one argument buffer at [[buffer(0)]]. Bindings with several typed aliases stay
        // discrete (they would overlap in it).
        spirv_cross::CompilerMSL probe(spirv);
        const auto active = probe.get_active_interface_variables();
        const auto res = probe.get_shader_resources(active);
        std::map<uint32_t, uint32_t> count;
        for (const auto& r : res.storage_buffers) {
            if (probe.get_decoration(r.id, spv::DecorationDescriptorSet) == 0) {
                count[probe.get_decoration(r.id, spv::DecorationBinding)]++;
            }
        }
        std::map<uint32_t, uint32_t> arg_ids;
        for (const auto& [b, n] : count) {
            if (n == 1 && std::find(arg_candidates->begin(), arg_candidates->end(), b) !=
                              arg_candidates->end()) {
                const uint32_t id = uint32_t(arg_ids.size());
                arg_ids[b] = id;
            }
        }
        if (arg_ids.empty()) {
            return true; // nothing movable; Metal will reject the shader with a clear error
        }
        std::string out2;
        Bindings b2;
        TessLayout l2;
        ConvertOnce(spirv, opt, arg_ids, out2, &b2, &slots, nullptr, variant,
                    layout ? &l2 : nullptr);
        out = std::move(out2);
        if (bindings) {
            *bindings = std::move(b2);
        }
        if (layout) {
            *layout = std::move(l2);
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool ConvertCapture(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt,
                    const Bindings& like, const CaptureSlots& slots, std::string& out,
                    std::string& err) {
    try {
        // The same argument buffer choice as the shader's own MSL, so that every resource
        // keeps its slot.
        uint32_t n = 0;
        Bindings b;
        ConvertOnce(spirv, opt, like.arg_entries, out, &b, &n, &slots);
        if (b.buffers != like.buffers || b.textures != like.textures || b.samplers != like.samplers ||
            b.push_constant_buffer != like.push_constant_buffer ||
            b.argument_buffer != like.argument_buffer) {
            err = "resource slots of the capture variant differ from the shader's";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}
#else
bool Convert(const std::vector<uint32_t>&, const gcn_msl_options&, std::string&, std::string& err,
             Bindings*, const std::vector<uint32_t>*, const StageVariant*, TessLayout*) {
    err = "gcn_recompiler was built without SPIRV-Cross (GCN_BUILD_MSL=OFF)";
    return false;
}
bool ConvertCapture(const std::vector<uint32_t>&, const gcn_msl_options&, const Bindings&,
                    const CaptureSlots&, std::string&, std::string& err) {
    err = "gcn_recompiler was built without SPIRV-Cross (GCN_BUILD_MSL=OFF)";
    return false;
}
#endif

} // namespace GcnMsl

namespace {

char* DupStr(const std::string& s) {
    char* r = static_cast<char*>(std::malloc(s.size() + 1));
    if (r) {
        std::memcpy(r, s.c_str(), s.size() + 1);
    }
    return r;
}

std::vector<uint32_t> GuestBufferCandidates(const GcnShader* sh) {
    std::vector<uint32_t> candidates;
    if (!sh) {
        return candidates;
    }
    for (uint32_t i = 0; i < sh->info.buffers.size(); ++i) {
        if (sh->info.buffers[i].buffer_type == Shader::BufferType::Guest) {
            candidates.push_back(i);
        }
    }
    return candidates;
}

uint32_t DomainFromTessType(AmdGpu::TessellationType type) {
    switch (type) {
    case AmdGpu::TessellationType::Isoline:
        return GCN_TESS_DOMAIN_ISOLINES;
    case AmdGpu::TessellationType::Triangle:
        return GCN_TESS_DOMAIN_TRIANGLES;
    case AmdGpu::TessellationType::Quad:
        return GCN_TESS_DOMAIN_QUADS;
    }
    return GCN_TESS_DOMAIN_QUADS;
}

} // namespace

extern "C" {

GCN_API bool gcn_compile_tess_shaders_msl(const GcnShader* ls, const GcnShader* hs,
                                          const GcnShader* vs, const GcnMslOptions* opts,
                                          GcnTessShaderSet* out) {
    if (!out) {
        return false;
    }
    std::memset(out, 0, sizeof(*out));
    if (!ls || !hs || !vs || ls->spirv.empty() || hs->spirv.empty() || vs->spirv.empty()) {
        out->error = DupStr("invalid tessellation shader arguments");
        return false;
    }
    GcnMslOptions mo;
    if (opts) {
        mo = *opts;
    } else {
        gcn_msl_options_default(&mo);
    }
    const GcnShim::HostFpEnvScope fp_env;

    const GcnMsl::SpirvScan scan_hs = GcnMsl::ScanSpirv(hs->spirv);
    const GcnMsl::SpirvScan scan_vs = GcnMsl::ScanSpirv(vs->spirv);
    if (!scan_hs.valid || !scan_vs.valid) {
        out->error = DupStr("invalid tessellation SPIR-V module");
        return false;
    }

    uint32_t domain = scan_vs.domain ? scan_vs.domain : scan_hs.domain;
    if (domain == GCN_TESS_DOMAIN_NONE) {
        domain = DomainFromTessType(vs->runtime.sw_stage == Shader::SwStage::TessellationEval
                                        ? vs->runtime.sw.tes.tess_type
                                        : hs->runtime.sw.tcs.tess_type);
    }
    uint32_t output_vertices =
        scan_hs.output_vertices ? scan_hs.output_vertices : scan_vs.output_vertices;
    if (output_vertices == 0) {
        output_vertices = hs->runtime.sw.tcs.num_threads ? hs->runtime.sw.tcs.num_threads : 3u;
    }
    const uint32_t patch_first = scan_hs.patch_end ? scan_hs.patch_first : scan_vs.patch_first;
    const uint32_t patch_end = std::max(scan_hs.patch_end, scan_vs.patch_end);

    const std::vector<uint32_t> ls_args = GuestBufferCandidates(ls);
    const std::vector<uint32_t> hs_args = GuestBufferCandidates(hs);
    const std::vector<uint32_t> vs_args = GuestBufferCandidates(vs);

    std::string ls_msl0, ls_msl1, ls_msl2, hs_msl, vs_msl, err;
    GcnMsl::Bindings ls_bnd, hs_bnd, vs_bnd;
    GcnMsl::TessLayout ls_layout, hs_layout, vs_layout;

    const GcnMsl::StageVariant v_ls0{GcnMsl::StageRole::TessVertex, 0,           domain,
                                     output_vertices,               patch_first, patch_end};
    const GcnMsl::StageVariant v_ls1{GcnMsl::StageRole::TessVertex, 1,           domain,
                                     output_vertices,               patch_first, patch_end};
    const GcnMsl::StageVariant v_ls2{GcnMsl::StageRole::TessVertex, 2,           domain,
                                     output_vertices,               patch_first, patch_end};
    const GcnMsl::StageVariant v_hs{GcnMsl::StageRole::TessControl, 0,           domain,
                                    output_vertices,                patch_first, patch_end};
    const GcnMsl::StageVariant v_vs{GcnMsl::StageRole::TessEval, 0,           domain,
                                    output_vertices,             patch_first, patch_end};

    if (!GcnMsl::Convert(ls->spirv, mo, ls_msl0, err, &ls_bnd, &ls_args, &v_ls0, &ls_layout) ||
        !GcnMsl::Convert(ls->spirv, mo, ls_msl1, err, nullptr, &ls_args, &v_ls1, nullptr) ||
        !GcnMsl::Convert(ls->spirv, mo, ls_msl2, err, nullptr, &ls_args, &v_ls2, nullptr) ||
        !GcnMsl::Convert(hs->spirv, mo, hs_msl, err, &hs_bnd, &hs_args, &v_hs, &hs_layout) ||
        !GcnMsl::Convert(vs->spirv, mo, vs_msl, err, &vs_bnd, &vs_args, &v_vs, &vs_layout)) {
        out->error = DupStr(err);
        return false;
    }

    auto* m_ls = const_cast<GcnShader*>(ls);
    auto* m_hs = const_cast<GcnShader*>(hs);
    auto* m_vs = const_cast<GcnShader*>(vs);
    m_ls->msl_bindings = ls_bnd;
    m_hs->msl_bindings = hs_bnd;
    m_vs->msl_bindings = vs_bnd;
    if (m_ls->msl.empty()) {
        m_ls->msl = ls_msl0;
    }
    if (m_hs->msl.empty()) {
        m_hs->msl = hs_msl;
    }
    if (m_vs->msl.empty()) {
        m_vs->msl = vs_msl;
    }

    out->ls_msl = DupStr(ls_msl0);
    out->ls_u16_msl = DupStr(ls_msl1);
    out->ls_u32_msl = DupStr(ls_msl2);
    out->hs_msl = DupStr(hs_msl);
    out->vs_msl = DupStr(vs_msl);
    out->ls_stride = std::max(ls_layout.output, hs_layout.input);
    out->cp_stride = std::max(hs_layout.output, vs_layout.input);
    out->patch_stride = std::max(hs_layout.patch_output, vs_layout.patch_input);
    out->hs_input_stride = hs_layout.input;
    out->vs_input_stride = vs_layout.input;
    out->vs_patch_stride = vs_layout.patch_input;
    out->output_vertices = output_vertices;
    out->domain = domain;
    out->spacing = scan_vs.spacing;
    out->clockwise = scan_vs.clockwise;
    out->point_mode = scan_vs.point_mode;
    out->patch_first = patch_first;
    out->patch_end = patch_end;
    for (int i = 0; i < 3; ++i) {
        out->hs_local_size[i] = scan_hs.local_size[i];
    }
    return true;
}

GCN_API void gcn_free_tess_shaders_msl(GcnTessShaderSet* set) {
    if (!set) {
        return;
    }
    std::free(set->ls_msl);
    std::free(set->ls_u16_msl);
    std::free(set->ls_u32_msl);
    std::free(set->hs_msl);
    std::free(set->vs_msl);
    std::free(set->error);
    std::memset(set, 0, sizeof(*set));
}

GCN_API bool gcn_compile_gs_shaders_msl(const GcnShader* es, const GcnShader* gs,
                                        bool emulate_depth_negative_one_to_one,
                                        const GcnMslOptions* opts, GcnGsShaderSet* out) {
    if (!out) {
        return false;
    }
    std::memset(out, 0, sizeof(*out));
    if (!es || !gs || es->spirv.empty() || gs->spirv.empty()) {
        out->error = DupStr("invalid geometry shader arguments");
        return false;
    }
    GcnMslOptions mo;
    if (opts) {
        mo = *opts;
    } else {
        gcn_msl_options_default(&mo);
    }
    const GcnShim::HostFpEnvScope fp_env;

    const std::vector<uint32_t> es_args = GuestBufferCandidates(es);
    const std::vector<uint32_t> gs_args = GuestBufferCandidates(gs);

    std::string es_msl0, es_msl1, es_msl2, gs_msl, err;
    GcnMsl::Bindings es_bnd, gs_bnd;
    GcnMsl::TessLayout es_layout;

    const GcnMsl::StageVariant v_es0{GcnMsl::StageRole::TessVertex, 0};
    const GcnMsl::StageVariant v_es1{GcnMsl::StageRole::TessVertex, 1};
    const GcnMsl::StageVariant v_es2{GcnMsl::StageRole::TessVertex, 2};

    if (!GcnMsl::Convert(es->spirv, mo, es_msl0, err, &es_bnd, &es_args, &v_es0, &es_layout) ||
        !GcnMsl::Convert(es->spirv, mo, es_msl1, err, nullptr, &es_args, &v_es1, nullptr) ||
        !GcnMsl::Convert(es->spirv, mo, es_msl2, err, nullptr, &es_args, &v_es2, nullptr)) {
        out->error = DupStr(err);
        return false;
    }

    GcnSpirv::GeometryEsLayout geom_es_layout;
    geom_es_layout.stride = std::max<uint32_t>((es_layout.output + 3u) & ~3u, 4u);
    geom_es_layout.members = es_layout.members;

    GcnSpirv::GeometryRewrite rewrite = GcnSpirv::RewriteGeometryShader(gs->spirv, geom_es_layout);
    if (!rewrite.ok) {
        out->error = DupStr(rewrite.error);
        return false;
    }

    const GcnMsl::StageVariant v_gs{GcnMsl::StageRole::GeometryKernel};
    if (!GcnMsl::Convert(rewrite.spirv, mo, gs_msl, err, &gs_bnd, &gs_args, &v_gs, nullptr)) {
        out->error = DupStr(err);
        return false;
    }

    std::string pass_err;
    std::string vs_msl = GcnSpirv::PassThroughSource(
        rewrite.info, emulate_depth_negative_one_to_one, pass_err);
    if (vs_msl.empty()) {
        out->error = DupStr(pass_err);
        return false;
    }

    auto* m_es = const_cast<GcnShader*>(es);
    auto* m_gs = const_cast<GcnShader*>(gs);
    m_es->msl_bindings = es_bnd;
    m_gs->msl_bindings = gs_bnd;
    if (m_es->msl.empty()) {
        m_es->msl = es_msl0;
    }
    if (m_gs->msl.empty()) {
        m_gs->msl = gs_msl;
    }

    out->es_msl = DupStr(es_msl0);
    out->es_u16_msl = DupStr(es_msl1);
    out->es_u32_msl = DupStr(es_msl2);
    out->gs_msl = DupStr(gs_msl);
    out->vs_msl = DupStr(vs_msl);
    out->es_stride = geom_es_layout.stride;
    out->record_stride = rewrite.info.RecordStride();
    out->index_slots = rewrite.info.IndexSlots();
    out->input_primitive = static_cast<uint32_t>(rewrite.info.input);
    out->input_vertices = rewrite.info.input_vertices;
    out->output_primitive = static_cast<uint32_t>(rewrite.info.output);
    out->max_vertices = rewrite.info.max_vertices;
    out->num_varyings = static_cast<uint32_t>(rewrite.info.varyings.size());
    out->gs_local_size[0] = GcnSpirv::kGeometryThreads;
    out->gs_local_size[1] = 1;
    out->gs_local_size[2] = 1;
    return true;
}

GCN_API void gcn_free_gs_shaders_msl(GcnGsShaderSet* set) {
    if (!set) {
        return;
    }
    std::free(set->es_msl);
    std::free(set->es_u16_msl);
    std::free(set->es_u32_msl);
    std::free(set->gs_msl);
    std::free(set->vs_msl);
    std::free(set->error);
    std::memset(set, 0, sizeof(*set));
}

} // extern "C"
