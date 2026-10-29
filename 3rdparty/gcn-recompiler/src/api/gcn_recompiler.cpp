// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// C API (include/gcn_recompiler.h) over shadPS4's shader recompiler. The register decoding
// mirrors shadPS4 src/video_core/renderer_vulkan/vk_pipeline_cache.cpp (BuildRuntimeInfo,
// RefreshGraphicsKey) and vk_rasterizer.cpp (BindBuffers/BindTextures).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gcn_recompiler.h"

#include <fmt/format.h>

#include "api/gcn_msl.h"
#include "api/gcn_shader_internal.h"
#include "common/assert.h"
#include "compat/guest_memory.h"
#include "compat/host_fp_env.h"
#include "compat/shim.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/resource.h"
#include "shader_recompiler/recompiler.h"
#include "shader_recompiler/runtime_info.h"
#include "shader_recompiler/specialization.h"
#include "video_core/amdgpu/regs.h"

using namespace Shader;

// The public GCN_GUEST_* values are the translator's (resource.h).
static_assert(GCN_GUEST_PAGE_SHIFT == Shader::GuestMemory::PageShift);
static_assert(GCN_GUEST_PAGES == Shader::GuestMemory::NumPages);
static_assert(GCN_GUEST_TRACK_SHIFT == Shader::GuestMemory::TrackShift);
static_assert(GCN_GUEST_FIRST_MAPPED == Shader::GuestMemory::FirstMappedAddress);
static_assert(GCN_GUEST_MODE == Shader::GuestMemory::ModeDword);
static_assert(GCN_GUEST_STATUS == Shader::GuestMemory::StatusDword);
static_assert(GCN_GUEST_UNMAPPED == Shader::GuestMemory::UnmappedDword);
static_assert(GCN_GUEST_READ == Shader::GuestMemory::ReadDword);
static_assert(GCN_GUEST_WRITTEN == Shader::GuestMemory::WrittenDword);
static_assert(GCN_GUEST_FAULT_DWORDS == Shader::GuestMemory::FaultDwords);
static_assert(GCN_GUEST_STATUS_UNMAPPED == Shader::GuestMemory::StatusUnmapped);
static_assert(GCN_GUEST_STATUS_FORMAT == Shader::GuestMemory::StatusFormat);
static_assert(GCN_GUEST_STATUS_SWIZZLE == Shader::GuestMemory::StatusSwizzle);
static_assert(GCN_GUEST_STATUS_RANGE == Shader::GuestMemory::StatusRange);
static_assert(GCN_GUEST_STATUS_UNALIGNED == Shader::GuestMemory::StatusUnaligned);

namespace {

void CopyErr(char* err, size_t err_size, const std::string& msg) {
    if (err && err_size) {
        std::snprintf(err, err_size, "%s", msg.c_str());
    }
}

Profile ToProfile(const gcn_profile& p) {
    Profile r{};
    r.supported_spirv = p.supported_spirv;
    r.subgroup_size = p.subgroup_size;
    r.max_viewport_width = p.max_viewport_width;
    r.max_viewport_height = p.max_viewport_height;
    r.max_shared_memory_size = p.max_shared_memory_size;
    r.support_int8 = p.support_int8;
    r.support_int16 = p.support_int16;
    r.support_int64 = p.support_int64;
    r.support_float16 = p.support_float16;
    r.support_float64 = p.support_float64;
    r.supports_image_load_store_lod = p.supports_image_load_store_lod;
    r.supports_native_cube_calc = p.supports_native_cube_calc;
    r.supports_trinary_minmax = p.supports_trinary_minmax;
    r.supports_buffer_fp32_atomic_min_max = p.supports_buffer_fp32_atomic_min_max;
    r.supports_image_fp32_atomic_min_max = p.supports_image_fp32_atomic_min_max;
    r.supports_buffer_int64_atomics = p.supports_buffer_int64_atomics;
    r.supports_shared_int64_atomics = p.supports_shared_int64_atomics;
    r.supports_workgroup_explicit_memory_layout = p.supports_workgroup_explicit_memory_layout;
    r.supports_amd_shader_explicit_vertex_parameter =
        p.supports_amd_shader_explicit_vertex_parameter;
    r.supports_fragment_shader_barycentric = p.supports_fragment_shader_barycentric;
    r.supports_shader_stencil_export = p.supports_shader_stencil_export;
    r.needs_lds_barriers = p.needs_lds_barriers;
    r.needs_buffer_offsets = p.needs_buffer_offsets;
    r.needs_buffer_bounds_checks = p.needs_buffer_bounds_checks;
    r.needs_unorm_fixup = p.needs_unorm_fixup;
    r.needs_clip_distance_emulation = p.needs_clip_distance_emulation;
    r.lower_left_origin_mode = p.lower_left_origin_mode;
    // Float-controls execution modes are ignored by SPIRV-Cross; leave them unsupported.
    return r;
}

void ToOutputs(std::array<OutputMap, 3>& dst, const uint8_t (&src)[3][4]) {
    for (u32 i = 0; i < 3; i++) {
        for (u32 j = 0; j < 4; j++) {
            dst[i][j] = static_cast<Output>(src[i][j]);
        }
    }
}

// C runtime struct -> Shader::RuntimeInfo. gs_copy must outlive the result (GS only).
bool ToRuntimeInfo(const gcn_runtime_info& c, RuntimeInfo& r, std::vector<u32>* gs_copy) {
    const auto hw = static_cast<HwStage>(c.hw_stage);
    const auto sw = static_cast<SwStage>(c.sw_stage);
    r.Initialize(hw, sw);
    r.props.fp_denorm_mode32 = static_cast<AmdGpu::FpDenormMode>(c.fp_denorm_mode32 & 3);
    r.props.fp_denorm_mode16_64 = static_cast<AmdGpu::FpDenormMode>(c.fp_denorm_mode16_64 & 3);
    r.props.fp_round_mode32 = static_cast<AmdGpu::FpRoundMode>(c.fp_round_mode32 & 3);
    r.props.fp_round_mode16_64 = static_cast<AmdGpu::FpRoundMode>(c.fp_round_mode16_64 & 3);
    r.props.num_allocated_vgprs = c.num_allocated_vgprs;
    r.props.num_user_data = c.num_user_data;
    r.props.num_input_vgprs = c.num_input_vgprs;
    r.props.dx10_clamp = c.dx10_clamp;
    switch (hw) {
    case HwStage::Local:
        r.hw.ls.ls_stride = c.ls.ls_stride;
        break;
    case HwStage::Export:
        r.hw.es.vertex_data_size = c.es.vertex_data_size;
        break;
    case HwStage::Geometry: {
        auto& g = r.hw.gs;
        g.num_outputs = c.gs.num_outputs;
        ToOutputs(g.outputs, c.gs.outputs);
        g.num_invocations = c.gs.num_invocations;
        g.output_vertices = c.gs.output_vertices;
        g.in_vertex_data_size = c.gs.in_vertex_data_size;
        g.out_vertex_data_size = c.gs.out_vertex_data_size;
        g.in_primitive = static_cast<AmdGpu::PrimitiveType>(c.gs.in_primitive);
        for (u32 i = 0; i < GsMaxOutputStreams; i++) {
            g.out_primitive[i] = static_cast<AmdGpu::GsOutputPrimitiveType>(c.gs.out_primitive[i]);
        }
        g.mode = static_cast<AmdGpu::GsScenario>(c.gs.mode);
        g.vs_copy_hash = c.gs.vs_copy_hash;
        if (gs_copy && c.gs.vs_copy_addr) {
            const auto& gm = GcnShim::CurrentGuestMemory();
            u32 size = c.gs.vs_copy_size;
            if (size == 0) {
                u64 hash;
                if (!gcn_find_binary_info(gm.read, gm.ctx, c.gs.vs_copy_addr, &size, &hash)) {
                    return false;
                }
                if (!g.vs_copy_hash) {
                    g.vs_copy_hash = hash;
                }
            }
            gs_copy->resize(size / 4);
            if (!GcnShim::ReadGuest(c.gs.vs_copy_addr, gs_copy->data(), gs_copy->size() * 4)) {
                return false;
            }
            g.vs_copy = std::span<const u32>(*gs_copy);
        }
        break;
    }
    case HwStage::Vertex: {
        auto& v = r.hw.vs;
        v.num_outputs = c.vs.num_outputs;
        ToOutputs(v.outputs, c.vs.outputs);
        v.emulate_depth_negative_one_to_one = c.vs.emulate_depth_negative_one_to_one;
        v.clip_disable = c.vs.clip_disable;
        v.user_clip_plane_mask = c.vs.user_clip_plane_mask;
        break;
    }
    case HwStage::Fragment: {
        auto& f = r.hw.fs;
        static_assert(sizeof(AmdGpu::PsInput) == 4);
        const u32 en = c.fs.en_flags, addr = c.fs.addr_flags;
        std::memcpy(&f.en_flags, &en, 4);
        std::memcpy(&f.addr_flags, &addr, 4);
        f.num_inputs = std::min<u32>(c.fs.num_inputs, 32);
        for (u32 i = 0; i < 32; i++) {
            f.inputs[i].param_index = c.fs.inputs[i].param_index & 31;
            f.inputs[i].is_default = c.fs.inputs[i].is_default & 1;
            f.inputs[i].is_flat = c.fs.inputs[i].is_flat & 1;
            f.inputs[i].default_value = c.fs.inputs[i].default_value & 3;
        }
        for (u32 i = 0; i < MaxColorBuffers; i++) {
            const auto& s = c.fs.color_buffers[i];
            auto& d = f.color_buffers[i];
            d.data_format = static_cast<AmdGpu::DataFormat>(s.data_format);
            d.num_format = static_cast<AmdGpu::NumberFormat>(s.num_format);
            d.num_conversion = static_cast<AmdGpu::NumberConversion>(s.num_conversion);
            d.export_format = static_cast<AmdGpu::ShaderExportFormat>(s.export_format);
            d.blend_self_scale = s.blend_self_scale;
            d.swizzle.r = static_cast<AmdGpu::CompSwizzle>(s.swizzle[0]);
            d.swizzle.g = static_cast<AmdGpu::CompSwizzle>(s.swizzle[1]);
            d.swizzle.b = static_cast<AmdGpu::CompSwizzle>(s.swizzle[2]);
            d.swizzle.a = static_cast<AmdGpu::CompSwizzle>(s.swizzle[3]);
        }
        f.z_export_format = static_cast<AmdGpu::ShaderExportFormat>(c.fs.z_export_format);
        f.num_samples = c.fs.num_samples ? c.fs.num_samples : 1;
        f.mrtz_mask = c.fs.mrtz_mask;
        f.front_face_all_bits = c.fs.front_face_all_bits;
        f.dual_source_blending = c.fs.dual_source_blending;
        f.clip_distance_emulation = c.fs.clip_distance_emulation;
        f.depth_before_shader = c.fs.depth_before_shader;
        break;
    }
    case HwStage::Compute:
        r.hw.cs.shared_memory_size = c.cs.shared_memory_size;
        for (u32 i = 0; i < 3; i++) {
            r.hw.cs.workgroup_size[i] = c.cs.workgroup_size[i];
            r.hw.cs.tgid_enable[i] = c.cs.tgid_enable[i];
        }
        break;
    default:
        break;
    }
    switch (sw) {
    case SwStage::Vertex:
        r.sw.vs.step_rate_0 = c.vs_sw.step_rate_0;
        r.sw.vs.step_rate_1 = c.vs_sw.step_rate_1;
        r.sw.vs.vertex_sgpr_offset = c.vs_sw.vertex_sgpr_offset;
        r.sw.vs.instance_sgpr_offset = c.vs_sw.instance_sgpr_offset;
        r.sw.vs.is_indirect = c.vs_sw.is_indirect;
        r.sw.vs.tess_emulated_primitive = c.vs_sw.tess_emulated_primitive;
        break;
    case SwStage::TessellationControl:
        r.sw.tcs.num_input_control_points = c.tcs.num_input_control_points;
        r.sw.tcs.num_threads = c.tcs.num_threads;
        r.sw.tcs.tess_type = static_cast<AmdGpu::TessellationType>(c.tcs.tess_type);
        r.sw.tcs.offchip_lds_enable = c.tcs.offchip_lds_enable;
        break;
    case SwStage::TessellationEval:
        r.sw.tes.tess_type = static_cast<AmdGpu::TessellationType>(c.tes.tess_type);
        r.sw.tes.tess_topology = static_cast<AmdGpu::TessellationTopology>(c.tes.tess_topology);
        r.sw.tes.tess_partitioning =
            static_cast<AmdGpu::TessellationPartitioning>(c.tes.tess_partitioning);
        break;
    default:
        break;
    }
    return true;
}

template <typename T>
void FromSharpFetch(gcn_sharp_fetch& d, const SharpFetch<T>& s) {
    std::memset(&d, 0, sizeof(d));
    d.kind = static_cast<u8>(s.summary);
    d.num_dwords = static_cast<u8>(SharpFetch<T>::N);
    d.load_mask = s.load_mask;
    for (size_t i = 0; i < SharpFetch<T>::N; i++) {
        d.offsets[i] = s.offsets[i];
        d.immediates[i] = s.immediates[i];
    }
}

u64 Fnv1a(const u32* data, size_t n) {
    u64 h = 0xcbf29ce484222325ULL;
    const u8* p = reinterpret_cast<const u8*>(data);
    for (size_t i = 0; i < n * 4; i++) {
        h = (h ^ p[i]) * 0x100000001b3ULL;
    }
    return h;
}

bool IsDualSourceBlendFactor(AmdGpu::BlendControl::BlendFactor f) {
    using F = AmdGpu::BlendControl::BlendFactor;
    return f == F::Src1Color || f == F::InvSrc1Color || f == F::Src1Alpha || f == F::InvSrc1Alpha;
}

void MapOutputs(uint8_t (&outputs)[3][4], u32& num_outputs, const AmdGpu::VsOutputControl& ctl) {
    std::memset(outputs, 0, sizeof(outputs));
    num_outputs = 0;
    const auto cd = [&](u32 i) -> u8 {
        return ctl.IsClipDistEnabled(i)   ? u8(GCN_OUT_CLIP_DIST0 + i)
               : ctl.IsCullDistEnabled(i) ? u8(GCN_OUT_CULL_DIST0 + i)
                                          : u8(GCN_OUT_NONE);
    };
    if (ctl.vs_out_misc_enable) {
        auto& m = outputs[num_outputs++];
        m[0] = ctl.use_vtx_point_size ? GCN_OUT_POINT_SIZE : GCN_OUT_NONE;
        m[1] = ctl.use_vtx_edge_flag
                   ? GCN_OUT_EDGE_FLAG
                   : (ctl.use_vtx_gs_cut_flag ? GCN_OUT_GS_CUT_FLAG : GCN_OUT_NONE);
        m[2] = ctl.use_vtx_kill_flag
                   ? GCN_OUT_KILL_FLAG
                   : (ctl.use_vtx_render_target_idx ? GCN_OUT_RENDER_TARGET_INDEX : GCN_OUT_NONE);
        m[3] = ctl.use_vtx_viewport_idx ? GCN_OUT_VIEWPORT_INDEX : GCN_OUT_NONE;
    }
    if (ctl.vs_out_ccdist0_enable) {
        auto& m = outputs[num_outputs++];
        for (u32 i = 0; i < 4; i++) {
            m[i] = cd(i);
        }
    }
    if (ctl.vs_out_ccdist1_enable) {
        auto& m = outputs[num_outputs++];
        for (u32 i = 0; i < 4; i++) {
            m[i] = cd(4 + i);
        }
    }
}

template <typename S>
void FillCommon(gcn_runtime_info& out, const S& settings) {
    out.fp_denorm_mode32 = static_cast<u8>(settings.fp_denorm_mode32);
    out.fp_denorm_mode16_64 = static_cast<u8>(settings.fp_denorm_mode64);
    out.fp_round_mode32 = static_cast<u8>(settings.fp_round_mode32);
    out.fp_round_mode16_64 = static_cast<u8>(settings.fp_round_mode64);
    out.num_allocated_vgprs = (settings.num_vgprs + 1) * 4;
    out.num_user_data = settings.num_user_regs;
    out.dx10_clamp = settings.dx10_clamp;
}

// Minimal Info-like view used with *Resource::GetSharp at draw time.
struct FlatView {
    std::span<const u32> flattened_ud_buf;
    VAddr pgm_base;
    template <typename T>
    T ReadUdSharp(u32 idx) const {
        T t;
        std::memcpy(&t, &flattened_ud_buf[idx], sizeof(T));
        return t;
    }
};

} // namespace

extern "C" {

GCN_API void gcn_profile_default_metal(gcn_profile* p) {
    if (!p) {
        return;
    }
    std::memset(p, 0, sizeof(*p));
    p->supported_spirv = 0x00010600;
    p->subgroup_size = 32;
    p->max_viewport_width = 16384;
    p->max_viewport_height = 16384;
    p->max_shared_memory_size = 32768;
    p->support_int8 = true;
    p->support_int16 = true;
    p->support_int64 = true;
    p->support_float16 = true;
    p->support_float64 = false;
    p->supports_shader_stencil_export = true;
    p->supports_depth_clip_control = false;
    p->needs_lds_barriers = true;
    p->needs_buffer_offsets = false;
    p->needs_buffer_bounds_checks = true;
    p->needs_unorm_fixup = true;
    p->needs_clip_distance_emulation = false;
}

GCN_API void gcn_msl_options_default(gcn_msl_options* o) {
    if (!o) {
        return;
    }
    std::memset(o, 0, sizeof(*o));
    o->msl_version_major = 2;
    o->msl_version_minor = 3;
    o->argument_buffers = false;
    o->pad_fragment_output_components = true;
    o->enable_point_size_builtin = true;
    o->max_buffer_slots = 31; // Metal [[buffer(0..30)]]
}

GCN_API bool gcn_stage_from_regs(const uint32_t* regs_raw, size_t num_regs, gcn_hw_stage hw,
                                 uint64_t* code_addr, uint32_t user_data[16]) {
    if (!regs_raw || num_regs < AmdGpu::Regs::NumRegs) {
        return false;
    }
    const auto& regs = *reinterpret_cast<const AmdGpu::Regs*>(regs_raw);
    if (hw == GCN_HW_CS) {
        if (code_addr) {
            *code_addr = u64(regs.cs_program.address) << 8;
        }
        if (user_data) {
            std::memcpy(user_data, regs.cs_program.user_data.data(), 64);
        }
        return true;
    }
    const auto* pgm = regs.ProgramForStage(static_cast<u32>(hw));
    if (!pgm) {
        return false;
    }
    if (code_addr) {
        *code_addr = u64(pgm->address) << 8;
    }
    if (user_data) {
        std::memcpy(user_data, pgm->user_data.data(), 64);
    }
    return true;
}

GCN_API bool gcn_runtime_info_from_regs(const uint32_t* regs_raw, size_t num_regs,
                                        gcn_hw_stage hw_stage, gcn_sw_stage sw_stage,
                                        const gcn_profile* profile_in, gcn_runtime_info* out) {
    if (!regs_raw || !out || num_regs < AmdGpu::Regs::NumRegs) {
        return false;
    }
    gcn_profile profile;
    if (profile_in) {
        profile = *profile_in;
    } else {
        gcn_profile_default_metal(&profile);
    }
    const auto& regs = *reinterpret_cast<const AmdGpu::Regs*>(regs_raw);
    std::memset(out, 0, sizeof(*out));
    out->hw_stage = hw_stage;
    out->sw_stage = sw_stage;
    const auto common = [&](const AmdGpu::ShaderProgram& p) {
        FillCommon(*out, p.settings);
        out->num_input_vgprs = p.settings.vgpr_comp_cnt;
    };
    switch (hw_stage) {
    case GCN_HW_LS:
        common(regs.ls_program);
        // ls_stride comes from the hull shader's tess constant buffer (left 0 here).
        break;
    case GCN_HW_HS:
        common(regs.hs_program);
        break;
    case GCN_HW_ES:
        common(regs.es_program);
        out->es.vertex_data_size = regs.vgt_esgs_ring_itemsize;
        break;
    case GCN_HW_GS: {
        common(regs.gs_program);
        auto& g = out->gs;
        MapOutputs(g.outputs, g.num_outputs, regs.vs_output_control);
        g.output_vertices = regs.vgt_gs_max_vert_out;
        g.num_invocations =
            regs.vgt_gs_instance_cnt.IsEnabled() ? regs.vgt_gs_instance_cnt.count : 1;
        if (regs.stage_enable.raw == AmdGpu::ShaderStageEnable::LsHsEsGs) {
            switch (regs.tess_config.topology) {
            case AmdGpu::TessellationTopology::Point:
                g.in_primitive = u32(AmdGpu::PrimitiveType::PointList);
                break;
            case AmdGpu::TessellationTopology::Line:
                g.in_primitive = u32(AmdGpu::PrimitiveType::LineList);
                break;
            default:
                g.in_primitive = u32(AmdGpu::PrimitiveType::TriangleList);
                break;
            }
        } else {
            g.in_primitive = u32(regs.primitive_type);
        }
        for (u32 i = 0; i < 4; i++) {
            g.out_primitive[i] = u32(regs.vgt_gs_out_prim_type.GetPrimitiveType(i));
        }
        g.in_vertex_data_size = regs.vgt_esgs_ring_itemsize;
        g.out_vertex_data_size = regs.vgt_gs_vert_itemsize[0];
        g.mode = u32(regs.vgt_gs_mode.mode);
        g.vs_copy_addr = u64(regs.vs_program.address) << 8;
        g.vs_copy_size = 0;
        break;
    }
    case GCN_HW_VS: {
        common(regs.vs_program);
        auto& v = out->vs;
        v.user_clip_plane_mask = regs.clipper_control.user_clip_plane_enable;
        MapOutputs(v.outputs, v.num_outputs, regs.vs_output_control);
        v.emulate_depth_negative_one_to_one =
            !profile.supports_depth_clip_control &&
            regs.clipper_control.clip_space == AmdGpu::ClipSpace::MinusWToW;
        v.clip_disable = regs.IsClipDisabled();
        break;
    }
    case GCN_HW_PS: {
        common(regs.ps_program);
        auto& f = out->fs;
        std::memcpy(&f.en_flags, &regs.ps_input_ena, 4);
        std::memcpy(&f.addr_flags, &regs.ps_input_addr, 4);
        f.num_inputs = regs.num_interp;
        f.front_face_all_bits = regs.barycentric_control.front_face_all_bits;
        f.depth_before_shader = regs.depth_shader_control.depth_before_shader;
        f.num_samples =
            regs.ps_input_addr.sample_coverage_ena && regs.ps_input_ena.sample_coverage_ena
                ? regs.aa_config.NumSamples()
                : 1;
        f.z_export_format = u32(regs.z_export_format);
        const u32 stencil_ref_export_enable =
            regs.depth_shader_control.stencil_op_val_export_enable |
            regs.depth_shader_control.stencil_test_val_export_enable;
        f.mrtz_mask = regs.depth_shader_control.z_export_enable |
                      (stencil_ref_export_enable << 1) |
                      (regs.depth_shader_control.mask_export_enable << 2) |
                      (regs.depth_shader_control.coverage_to_mask_enable << 3);
        const auto& cb0 = regs.blend_control[0];
        if (cb0.enable) {
            f.dual_source_blending = IsDualSourceBlendFactor(cb0.color_dst_factor) ||
                                     IsDualSourceBlendFactor(cb0.color_src_factor);
            if (cb0.separate_alpha_blend) {
                f.dual_source_blending |= IsDualSourceBlendFactor(cb0.alpha_dst_factor) ||
                                          IsDualSourceBlendFactor(cb0.alpha_src_factor);
            }
        }
        // PS4Recomp: a PS input that refers to a parameter the VS does not export reads
        // undefined data on GCN; Metal instead rejects the pipeline when a fragment input
        // has no matching vertex output. Treat such inputs as default (0) values.
        const u32 vs_param_exports = regs.vs_output_config.NumExports();
        for (u32 i = 0; i < regs.num_interp && i < 32; i++) {
            const auto& in = regs.ps_inputs[i];
            f.inputs[i].param_index = u8(in.input_offset);
            f.inputs[i].is_default = u8(in.use_default || in.input_offset >= vs_param_exports);
            f.inputs[i].is_flat = u8(in.flat_shade);
            f.inputs[i].default_value = u8(in.default_value);
        }
        // Colour buffers: first pass of PipelineCache::RefreshGraphicsKey.
        const bool skip_cb =
            regs.color_control.mode == AmdGpu::ColorControl::OperationMode::Disable;
        for (u32 cb = 0; cb < AmdGpu::NUM_COLOR_BUFFERS && !skip_cb; ++cb) {
            const auto& col_buf = regs.color_buffers[cb];
            if (!col_buf || !regs.color_target_mask.GetMask(cb)) {
                continue;
            }
            auto& d = f.color_buffers[cb];
            d.data_format = u8(col_buf.GetDataFmt());
            d.num_format = u8(col_buf.GetNumberFmt());
            d.num_conversion = u8(col_buf.GetNumberConversion());
            d.export_format = u8(regs.color_export_format.GetFormat(cb));
            const auto swz = col_buf.Swizzle();
            d.swizzle[0] = u8(swz.r);
            d.swizzle[1] = u8(swz.g);
            d.swizzle[2] = u8(swz.b);
            d.swizzle[3] = u8(swz.a);
            const auto& bc = regs.blend_control[cb];
            using BF = AmdGpu::BlendControl::BlendFactor;
            using BFn = AmdGpu::BlendControl::BlendFunc;
            d.blend_self_scale = bc.enable && !col_buf.info.blend_bypass &&
                                 (bc.color_func == BFn::Min || bc.color_func == BFn::Max) &&
                                 bc.color_src_factor == BF::SrcColor &&
                                 bc.color_dst_factor == BF::DstColor;
        }
        const bool lowers_user_clip_planes =
            regs.clipper_control.user_clip_plane_enable &&
            !regs.stage_enable.IsStageEnabled(static_cast<u32>(HwStage::Geometry));
        f.clip_distance_emulation =
            ((regs.vs_output_control.clip_distance_enable &&
              !regs.stage_enable.IsStageEnabled(static_cast<u32>(HwStage::Local))) ||
             lowers_user_clip_planes) &&
            profile.needs_clip_distance_emulation;
        break;
    }
    case GCN_HW_CS: {
        const auto& cs = regs.cs_program;
        FillCommon(*out, cs.settings);
        out->cs.workgroup_size[0] = cs.num_thread_x.full;
        out->cs.workgroup_size[1] = cs.num_thread_y.full;
        out->cs.workgroup_size[2] = cs.num_thread_z.full;
        for (u32 i = 0; i < 3; i++) {
            out->cs.tgid_enable[i] = cs.IsTgidEnabled(i);
        }
        out->cs.shared_memory_size = cs.SharedMemSize();
        break;
    }
    default:
        return false;
    }
    switch (sw_stage) {
    case GCN_SW_VERTEX:
        out->vs_sw.step_rate_0 = regs.vgt_instance_step_rate_0;
        out->vs_sw.step_rate_1 = regs.vgt_instance_step_rate_1;
        out->vs_sw.tess_emulated_primitive =
            regs.primitive_type == AmdGpu::PrimitiveType::RectList ||
            regs.primitive_type == AmdGpu::PrimitiveType::QuadList;
        break;
    case GCN_SW_TESS_CONTROL:
        out->tcs.num_input_control_points = regs.ls_hs_config.hs_input_control_points;
        out->tcs.num_threads = regs.ls_hs_config.hs_output_control_points;
        out->tcs.tess_type = u32(regs.tess_config.type);
        out->tcs.offchip_lds_enable = regs.hs_program.settings.oc_lds_en;
        break;
    case GCN_SW_TESS_EVAL:
        out->tes.tess_type = u32(regs.tess_config.type);
        out->tes.tess_topology = u32(regs.tess_config.topology);
        out->tes.tess_partitioning = u32(regs.tess_config.partitioning);
        break;
    default:
        break;
    }
    return true;
}

GCN_API bool gcn_find_binary_info(gcn_read_guest_fn read, void* ctx, uint64_t code_addr,
                                  uint32_t* size_bytes, uint64_t* shader_hash) {
    if (!read) {
        return false;
    }
    // AmdGpu::SearchBinaryInfo: check the slot after the s_mov_b32 vcc_hi header first,
    // then scan up to 0x4000 dwords for the "OrbShdr" signature.
    const auto try_at = [&](u64 addr) -> bool {
        AmdGpu::BinaryInfo bi;
        if (!read(ctx, addr, &bi, sizeof(bi)) || !bi.Valid()) {
            return false;
        }
        if (size_bytes) {
            *size_bytes = bi.length;
        }
        if (shader_hash) {
            *shader_hash = bi.shader_hash;
        }
        return true;
    };
    u32 head[2];
    if (!read(ctx, code_addr, head, sizeof(head))) {
        return false;
    }
    if (head[0] == 0xBEEB03FF && try_at(code_addr + u64(head[1] + 1) * 8)) {
        return true;
    }
    constexpr u32 Chunk = 256;
    std::vector<u32> buf(Chunk + 8);
    for (u32 base = 0; base < 0x4000; base += Chunk) {
        const u64 a = code_addr + u64(base) * 4;
        if (!read(ctx, a, buf.data(), (Chunk + 8) * 4)) {
            if (!read(ctx, a, buf.data(), Chunk * 4)) {
                return false;
            }
            std::fill(buf.begin() + Chunk, buf.end(), 0);
        }
        for (u32 i = 0; i < Chunk; i++) {
            if (std::memcmp(&buf[i], AmdGpu::BinaryInfo::signature_ref.data(), 7) == 0 &&
                try_at(a + u64(i) * 4)) {
                return true;
            }
        }
    }
    return false;
}

namespace {
// PS4Recomp: one view of guest memory for a whole translation. A translation reads the sharps
// more than once: the translator decides from them (Translator::EmitFetch: Metal vertex fetch or
// the fetch shader inline; buffer misalignment, formats), and the specialization recorded after
// it (StageSpecialization, what the permutation lookup compares draws against) reads them again.
// The game rewrites its per-draw V#/T# tables while a translation runs (on the renderer's worker,
// after the draw that asked for it), so two reads could disagree: a VS translated for a
// dword-aligned V# was recorded as serving draws whose V# Metal cannot fetch, and the renderer
// skipped them all. Every read is served from a copy of the 256-byte chunks taken at their first
// read. A chunk the reader refuses as a whole (the end of a mapping) is read as asked, uncached.
struct CompileSnapshot {
    static constexpr u64 kChunk = 256;
    static constexpr u32 kUnreadable = ~0u;
    Shader::SrtReadFn read;
    void* ctx;
    std::unordered_map<u64, u32> chunks; // chunk address -> offset of its copy in data
    std::vector<u8> data;

    static bool Read(void* self, u64 addr, void* dst, u64 size) {
        auto* s = static_cast<CompileSnapshot*>(self);
        if (addr == 0 || size == 0 || addr + size < addr) {
            return Shader::SrtRead(s->read, s->ctx, addr, dst, size);
        }
        u8* out = static_cast<u8*>(dst);
        for (u64 a = addr, end = addr + size; a < end;) {
            const u64 c = a & ~(kChunk - 1);
            const u64 n = std::min(end, c + kChunk) - a;
            auto it = s->chunks.find(c);
            if (it == s->chunks.end()) {
                const u32 off = u32(s->data.size());
                s->data.resize(off + kChunk);
                const bool ok = Shader::SrtRead(s->read, s->ctx, c, s->data.data() + off, kChunk);
                if (!ok) {
                    s->data.resize(off);
                }
                it = s->chunks.emplace(c, ok ? off : kUnreadable).first;
            }
            if (it->second == kUnreadable) {
                if (!Shader::SrtRead(s->read, s->ctx, a, out, n)) {
                    return false;
                }
            } else {
                std::memcpy(out, s->data.data() + it->second + (a - c), n);
            }
            out += n;
            a += n;
        }
        return true;
    }
};
} // namespace

GCN_API gcn_status gcn_compile(const gcn_compile_desc* desc, gcn_shader** out, char* err,
                               size_t err_size) {
    if (!desc || !out || !desc->runtime) {
        CopyErr(err, err_size, "invalid arguments");
        return GCN_ERR_INVALID_ARG;
    }
    *out = nullptr;
    if (desc->hw_stage > GCN_HW_CS || desc->sw_stage > GCN_SW_COMPUTE) {
        CopyErr(err, err_size, "invalid stage");
        return GCN_ERR_INVALID_ARG;
    }
    CompileSnapshot snapshot{desc->read_guest, desc->read_ctx, {}, {}};
    if (desc->read_guest) {
        snapshot.chunks.reserve(64);
        snapshot.data.reserve(64 * CompileSnapshot::kChunk);
    }
    GcnShim::GuestMemoryScope scope{desc->read_guest ? CompileSnapshot::Read : nullptr,
                                    desc->read_guest ? &snapshot : nullptr};
    const GcnShim::HostFpEnvScope fp_env; // exact float bits whatever the caller's FZ
    GcnShim::LastError().clear();

    // Locate & copy the program.
    u32 size = desc->code_size;
    u64 hash = desc->hash;
    if (size == 0 || hash == 0) {
        u32 fsize = 0;
        u64 fhash = 0;
        const bool found =
            desc->read_guest &&
            gcn_find_binary_info(desc->read_guest, desc->read_ctx, desc->code_addr, &fsize, &fhash);
        if (size == 0) {
            if (!found) {
                CopyErr(err, err_size, "shader size not given and OrbShdr footer not found");
                return GCN_ERR_READ_CODE;
            }
            size = fsize;
        }
        if (hash == 0 && found) {
            hash = fhash;
        }
    }
    if (size < 8 || size > (1u << 22)) {
        CopyErr(err, err_size, "bad shader size");
        return GCN_ERR_READ_CODE;
    }
    std::vector<u32> code(size / 4);
    if (desc->code) {
        std::memcpy(code.data(), desc->code, code.size() * 4);
    } else if (!desc->read_guest ||
               !desc->read_guest(desc->read_ctx, desc->code_addr, code.data(), code.size() * 4)) {
        CopyErr(err, err_size, "shader code not readable");
        return GCN_ERR_READ_CODE;
    }
    if (hash == 0) {
        hash = Fnv1a(code.data(), code.size());
    }

    gcn_profile cprof;
    if (desc->profile) {
        cprof = *desc->profile;
    } else {
        gcn_profile_default_metal(&cprof);
    }

    auto sh = std::make_unique<gcn_shader>();
    sh->code_addr = desc->code_addr;
    sh->code_size = size;
    sh->profile = ToProfile(cprof);
    sh->profile.wave64_force_fallback = (desc->flags & GCN_COMPILE_WAVE64_FALLBACK) != 0;
    std::memcpy(sh->user_data.data(), desc->user_data, 64);

    try {
        if (!ToRuntimeInfo(*desc->runtime, sh->runtime, &sh->gs_copy)) {
            CopyErr(err, err_size, "GS copy shader not readable");
            return GCN_ERR_READ_CODE;
        }
        const auto hw = static_cast<HwStage>(desc->hw_stage);
        const auto sw = static_cast<SwStage>(desc->sw_stage);
        const ShaderParams params{
            .user_data = std::span<const u32, ShaderParams::NumShaderUserData>(sh->user_data),
            .code = std::span<const u32>(code),
            .hash = hash,
        };
        sh->info = Info(hw, sw, params);
        // Inline constant buffers (OffsetByProgramBase) are relative to the guest code address.
        sh->info.pgm_base = static_cast<VAddr>(desc->code_addr);

        Pools pools;
        RuntimeInfo rt = sh->runtime;
        const auto program = TranslateProgram(code, pools, sh->info, rt, sh->profile);
        Backend::Bindings binding{};
        sh->spirv = Backend::SPIRV::EmitSPIRV(sh->profile, rt, program, binding);
        sh->info.RefreshFlatBuf();
        sh->spec = StageSpecialization(sh->info, sh->runtime, sh->profile, Backend::Bindings{});
    } catch (const std::exception& e) {
        CopyErr(err, err_size, std::string("recompiler: ") + e.what());
        return GCN_ERR_COMPILE;
    } catch (...) {
        CopyErr(err, err_size, "recompiler: unknown exception");
        return GCN_ERR_COMPILE;
    }

    gcn_status status = GCN_OK;
    if (desc->flags & GCN_COMPILE_EMIT_MSL) {
        gcn_msl_options mo;
        if (desc->msl) {
            mo = *desc->msl;
        } else {
            gcn_msl_options_default(&mo);
        }
        // Guest memory buffers may move into an argument buffer if the stage needs more
        // buffer slots than Metal has (their SPIR-V binding is the buffer index).
        std::vector<u32> arg_candidates;
        for (u32 i = 0; i < sh->info.buffers.size(); i++) {
            if (sh->info.buffers[i].buffer_type == BufferType::Guest) {
                arg_candidates.push_back(i);
            }
        }
        if (!GcnMsl::Convert(sh->spirv, mo, sh->msl, sh->msl_error, &sh->msl_bindings,
                             &arg_candidates)) {
            sh->msl.clear();
            CopyErr(err, err_size, "SPIRV-Cross: " + sh->msl_error);
            status = GCN_ERR_MSL;
        }
    }
    *out = sh.release();
    return status;
}

GCN_API void gcn_shader_destroy(gcn_shader* shader) {
    delete shader;
}

GCN_API const uint32_t* gcn_shader_spirv(const gcn_shader* s, size_t* num_words) {
    if (!s) {
        return nullptr;
    }
    if (num_words) {
        *num_words = s->spirv.size();
    }
    return s->spirv.data();
}

GCN_API const char* gcn_shader_msl(const gcn_shader* s) {
    return (s && !s->msl.empty()) ? s->msl.c_str() : nullptr;
}

GCN_API const char* gcn_shader_msl_error(const gcn_shader* s) {
    return (s && !s->msl_error.empty()) ? s->msl_error.c_str() : nullptr;
}

GCN_API char* gcn_spirv_to_msl(const uint32_t* spirv, size_t num_words, const gcn_msl_options* opt,
                               char* err, size_t err_size) {
    if (!spirv || !num_words) {
        CopyErr(err, err_size, "invalid arguments");
        return nullptr;
    }
    gcn_msl_options mo;
    if (opt) {
        mo = *opt;
    } else {
        gcn_msl_options_default(&mo);
    }
    std::vector<u32> words(spirv, spirv + num_words);
    std::string msl, e;
    const GcnShim::HostFpEnvScope fp_env; // SPIRV-Cross prints float literals through a double
    if (!GcnMsl::Convert(words, mo, msl, e, nullptr)) {
        CopyErr(err, err_size, e);
        return nullptr;
    }
    char* r = static_cast<char*>(std::malloc(msl.size() + 1));
    std::memcpy(r, msl.c_str(), msl.size() + 1);
    return r;
}

GCN_API void gcn_free(void* p) {
    std::free(p);
}

GCN_API void gcn_shader_get_info(const gcn_shader* s, gcn_shader_info* o) {
    if (!s || !o) {
        return;
    }
    std::memset(o, 0, sizeof(*o));
    const auto& info = s->info;
    o->hw_stage = static_cast<u32>(info.hw_stage);
    o->sw_stage = static_cast<u32>(info.sw_stage);
    o->pgm_hash = info.pgm_hash;
    o->code_addr = s->code_addr;
    o->code_size = s->code_size;
    o->num_buffers = info.buffers.size();
    o->num_images = info.images.size();
    o->num_samplers = info.samplers.size();
    o->num_fmasks = info.fmasks.size();
    o->num_vertex_attribs = s->spec.fetch_shader_data.attributes.size();
    o->flatbuf_size_dw = info.srt_info.flattened_bufsize_dw;
    o->has_fetch_shader = info.has_fetch_shader;
    o->fetch_shader_inlined = info.fetch_shader_inlined;
    o->fetch_inline_reason = info.fetch_inline_reason;
    o->fetch_inline_attrib = info.fetch_inline_attrib;
    o->fetch_shader_sgpr_base = info.fetch_shader_sgpr_base;
    o->vertex_offset_sgpr = s->spec.fetch_shader_data.vertex_offset_sgpr;
    o->instance_offset_sgpr = s->spec.fetch_shader_data.instance_offset_sgpr;
    o->mrt_mask = info.mrt_mask;
    o->has_discard = info.has_discard;
    o->uses_dma = info.uses_dma;
    o->uses_guest_memory = info.uses_guest_memory;
    o->null_image_reads = info.null_image_reads;
    o->num_image_tables = info.image_tables.size();
    o->zeroed_write_lanes = info.zeroed_write_lanes;
    o->unpredicated_wave_regions = info.unpredicated_wave_regions;
    o->per_subgroup_lane_ops = info.per_subgroup_lane_ops;
    o->per_lane_ends = info.per_lane_ends;
    o->translation_failed = info.translation_failed;
    if (info.hw_stage == HwStage::Compute) {
        o->shared_memory_size =
            s->runtime.hw.cs.shared_memory_size + info.shared_memory_scratch_size;
        for (u32 i = 0; i < 3; i++) {
            o->workgroup_size[i] = s->runtime.hw.cs.workgroup_size[i];
        }
    }
    o->msl_push_constant_buffer = s->msl_bindings.push_constant_buffer;
    o->msl_first_free_buffer = s->msl_bindings.first_free_buffer;
    o->msl_num_textures = s->msl_bindings.num_textures;
    o->msl_num_samplers = s->msl_bindings.num_samplers;
    o->msl_argument_buffer = s->msl_bindings.argument_buffer;
    o->msl_argument_buffer_entries = s->msl_bindings.num_arg_entries;
}

// Binding numbering per stage: buffers first, then images (each takes num_bindings), then
// samplers - identical to EmitContext::DefineBuffers/DefineImagesAndSamplers with a zero
// start binding.
GCN_API bool gcn_shader_get_buffer(const gcn_shader* s, uint32_t index, gcn_buffer_binding* o) {
    if (!s || !o || index >= s->info.buffers.size()) {
        return false;
    }
    const auto& b = s->info.buffers[index];
    std::memset(o, 0, sizeof(*o));
    o->spirv_binding = index;
    o->buffer_slot = index;
    o->type = static_cast<u32>(b.buffer_type);
    const auto t = b.used_types;
    o->used_types = (True(t & IR::Type::U8) ? GCN_USES_U8 : 0) |
                    (True(t & IR::Type::U16) ? GCN_USES_U16 : 0) |
                    (True(t & IR::Type::U32) ? GCN_USES_U32 : 0) |
                    (True(t & IR::Type::F32) ? GCN_USES_F32 : 0) |
                    (True(t & IR::Type::U64) ? GCN_USES_U64 : 0);
    o->is_written = b.is_written;
    o->is_formatted = b.is_formatted;
    o->post_op = static_cast<u32>(b.post_op);
    o->post_op_dw1_mask = b.post_op_dw1_mask;
    o->post_op_dw2_shift = b.post_op_dw2_shift;
    FromSharpFetch(o->sharp, b.sharp_fetch);
    if (auto it = s->msl_bindings.buffers.find(o->spirv_binding);
        it != s->msl_bindings.buffers.end()) {
        o->num_msl_buffers = std::min<u32>(it->second.size(), 5);
        for (u32 i = 0; i < o->num_msl_buffers; i++) {
            o->msl_buffers[i] = it->second[i];
        }
    }
    o->msl_arg_index = UINT32_MAX;
    if (auto it = s->msl_bindings.arg_entries.find(o->spirv_binding);
        it != s->msl_bindings.arg_entries.end()) {
        o->msl_arg_index = it->second;
    }
    return true;
}

GCN_API bool gcn_shader_get_image(const gcn_shader* s, uint32_t index, gcn_image_binding* o) {
    if (!s || !o || index >= s->info.images.size()) {
        return false;
    }
    std::memset(o, 0, sizeof(*o));
    u32 binding = s->info.buffers.size();
    for (u32 i = 0; i < index; i++) {
        binding += s->spec.images[i].num_bindings ? s->spec.images[i].num_bindings : 1;
    }
    const auto& im = s->info.images[index];
    const auto& sp = s->spec.images[index];
    o->spirv_binding = binding;
    o->num_bindings = sp.num_bindings ? sp.num_bindings : 1;
    o->is_storage = im.is_written;
    o->is_depth = im.is_depth;
    o->is_atomic = im.is_atomic;
    o->is_array = im.is_array;
    o->is_r128 = im.is_r128;
    o->is_integer = sp.is_integer;
    o->mip_fallback_mode = static_cast<u8>(im.mip_fallback_mode);
    o->constant_mip_index = im.constant_mip_index;
    o->view_type = static_cast<u32>(sp.type);
    o->post_op = static_cast<u32>(im.post_op);
    FromSharpFetch(o->sharp, im.sharp_fetch);
    o->msl_texture = UINT32_MAX;
    if (auto it = s->msl_bindings.textures.find(binding); it != s->msl_bindings.textures.end()) {
        o->msl_texture = it->second;
    }
    o->table = im.table == NoImageTable ? UINT32_MAX : im.table;
    o->table_entry = im.table == NoImageTable ? UINT32_MAX : im.table_entry;
    return true;
}

GCN_API uint32_t gcn_shader_image_table_reads(const gcn_shader* s, const uint32_t* flatbuf,
                                              uint64_t (*out)[2], uint32_t max) {
    if (!s || !flatbuf || s->info.image_tables.empty()) {
        return 0;
    }
    u32 n = 0;
    Shader::ForEachSrtTableRead(s->info.srt_info.program, flatbuf, [&](u64 addr, u64 size) {
        if (out && n < max) {
            out[n][0] = addr;
            out[n][1] = size;
        }
        ++n;
    });
    return n;
}

GCN_API bool gcn_shader_get_image_table(const gcn_shader* s, uint32_t index, gcn_image_table* o) {
    if (!s || !o || index >= s->info.image_tables.size()) {
        return false;
    }
    const auto& t = s->info.image_tables[index];
    std::memset(o, 0, sizeof(*o));
    o->kind = static_cast<u32>(t.kind);
    o->table_buffer = t.table_buffer;
    o->index_buffer = t.index_buffer == ~0u ? UINT32_MAX : t.index_buffer;
    o->first_dw = t.first_dw;
    o->stride_dw = t.stride_dw;
    o->num_dwords = t.num_dwords;
    o->num_entries = t.num_entries;
    o->reached = t.reached;
    o->num_sites = t.num_sites;
    return true;
}

GCN_API bool gcn_shader_get_sampler(const gcn_shader* s, uint32_t index, gcn_sampler_binding* o) {
    if (!s || !o || index >= s->info.samplers.size()) {
        return false;
    }
    std::memset(o, 0, sizeof(*o));
    u32 binding = s->info.buffers.size();
    for (const auto& sp : s->spec.images) {
        binding += sp.num_bindings ? sp.num_bindings : 1;
    }
    binding += index;
    const auto& sm = s->info.samplers[index];
    o->spirv_binding = binding;
    o->is_depth = sm.is_depth;
    o->post_op = static_cast<u32>(sm.post_op);
    o->post_op_tsharp_dw3_off = sm.post_op_tsharp_dw3_off;
    FromSharpFetch(o->sharp, sm.sharp_fetch);
    o->msl_sampler = UINT32_MAX;
    if (auto it = s->msl_bindings.samplers.find(binding); it != s->msl_bindings.samplers.end()) {
        o->msl_sampler = it->second;
    }
    return true;
}

GCN_API bool gcn_vertex_format_fetchable(uint32_t dfmt, uint32_t nfmt) {
    const auto d = AmdGpu::DataFormat(dfmt);
    return Gcn::IsVertexFormatFetchable(AmdGpu::RemapDataFormat(d),
                                        AmdGpu::RemapNumberFormat(AmdGpu::NumberFormat(nfmt), d));
}

GCN_API bool gcn_shader_get_vertex_attrib(const gcn_shader* s, uint32_t index,
                                          gcn_vertex_attrib* o) {
    if (!s || !o || index >= s->spec.fetch_shader_data.attributes.size()) {
        return false;
    }
    const auto& a = s->spec.fetch_shader_data.attributes[index];
    std::memset(o, 0, sizeof(*o));
    o->location = index;
    o->dest_vgpr = a.dest_vgpr;
    o->num_elements = a.num_elements;
    o->sgpr_base = a.sgpr_base;
    o->dword_offset = a.dword_offset;
    o->step_rate = a.instance_data;
    o->inst_offset = a.inst_offset;
    o->data_format = a.data_format;
    o->num_format = a.num_format;
    using IT = Gcn::VertexAttribute::InstanceIdType;
    switch (a.GetStepRate()) {
    case IT::None:
        o->divisor = 0;
        break;
    case IT::OverStepRate0:
        o->divisor = s->runtime.sw.vs.step_rate_0;
        break;
    case IT::OverStepRate1:
        o->divisor = s->runtime.sw.vs.step_rate_1;
        break;
    default:
        o->divisor = 1;
        break;
    }
    return true;
}

GCN_API bool gcn_shader_build_flatbuf(const gcn_shader* s, const uint32_t user_data[16],
                                      gcn_read_guest_fn read, void* ctx, uint32_t* out,
                                      size_t out_dw) {
    if (!s || !user_data || !out || out_dw < s->info.srt_info.flattened_bufsize_dw) {
        return false;
    }
    std::memset(out, 0, s->info.srt_info.flattened_bufsize_dw * 4);
    std::memcpy(out, user_data, NUM_USER_DATA_REGS * 4);
    if (s->info.srt_info.program.empty()) {
        return true;
    }
    return RunSrtProgram(s->info.srt_info.program, user_data, out, read, ctx);
}

GCN_API bool gcn_shader_fetch_buffer_sharp(const gcn_shader* s, uint32_t index,
                                           const uint32_t* flatbuf, uint64_t code_addr,
                                           uint32_t out[4]) {
    if (!s || !flatbuf || !out || index >= s->info.buffers.size()) {
        return false;
    }
    const FlatView v{{flatbuf, s->info.srt_info.flattened_bufsize_dw}, VAddr(code_addr)};
    const auto& b = s->info.buffers[index];
    const AmdGpu::Buffer sharp = b.GetSharp(v);
    std::memcpy(out, &sharp, 16);
    return b.IsSpecial() || (sharp.base_address != 0 && sharp.Valid());
}

GCN_API bool gcn_shader_fetch_image_sharp(const gcn_shader* s, uint32_t index,
                                          const uint32_t* flatbuf, uint32_t out[8]) {
    if (!s || !flatbuf || !out || index >= s->info.images.size()) {
        return false;
    }
    const FlatView v{{flatbuf, s->info.srt_info.flattened_bufsize_dw}, 0};
    const AmdGpu::Image sharp = s->info.images[index].GetSharp(v);
    std::memcpy(out, &sharp, 32);
    return sharp.Address() != 0 && sharp.GetDataFmt() != AmdGpu::DataFormat::FormatInvalid;
}

GCN_API bool gcn_shader_fetch_sampler_sharp(const gcn_shader* s, uint32_t index,
                                            const uint32_t* flatbuf, uint32_t out[4]) {
    if (!s || !flatbuf || !out || index >= s->info.samplers.size()) {
        return false;
    }
    if (s->info.samplers[index].sharp_fetch.summary == SharpFetch<AmdGpu::Sampler>::Summary::Invalid) {
        // PS4Recomp: an S# the shader loads at run time: there is no sharp to fetch
        std::memset(out, 0, 16);
        return false;
    }
    const FlatView v{{flatbuf, s->info.srt_info.flattened_bufsize_dw}, 0};
    const AmdGpu::Sampler sharp = s->info.samplers[index].GetSharp(v);
    std::memcpy(out, &sharp, 16);
    return sharp.Valid();
}

GCN_API bool gcn_shader_fetch_vertex_attrib_sharp(const gcn_shader* s, uint32_t index,
                                                  const uint32_t user_data[16],
                                                  gcn_read_guest_fn read, void* ctx,
                                                  uint32_t out[4]) {
    if (!s || !user_data || !out || index >= s->spec.fetch_shader_data.attributes.size()) {
        return false;
    }
    GcnShim::GuestMemoryScope scope{read, ctx};
    Info tmp = s->info;
    tmp.user_data = std::span<const u32>(user_data, 16);
    const auto sharp = s->spec.fetch_shader_data.attributes[index].GetSharp(tmp);
    std::memcpy(out, &sharp, 16);
    return sharp.base_address != 0;
}

GCN_API void gcn_set_direct_guest_memory(gcn_read_guest_fn read, void* ctx, const void* base, uint64_t size) {
    Shader::g_srt_direct = Shader::SrtDirectMemory{read, ctx, static_cast<const u8*>(base), read ? size : 0};
}

GCN_API void gcn_set_image_address_check(gcn_guest_mapped_fn mapped, void* ctx) {
    Shader::g_image_address_check = Shader::ImageAddressCheck{mapped, mapped ? ctx : nullptr};
}

GCN_API bool gcn_shader_is_compatible(const gcn_shader* s, const uint32_t user_data[16],
                                      const gcn_runtime_info* runtime, gcn_read_guest_fn read,
                                      void* ctx) {
    if (!s || !user_data || !runtime) {
        return false;
    }
    try {
        GcnShim::GuestMemoryScope scope{read, ctx};
        std::array<u32, 16> ud;
        std::memcpy(ud.data(), user_data, 64);
        RuntimeInfo rt{};
        if (!ToRuntimeInfo(*runtime, rt, nullptr)) {
            return false;
        }
        if (rt.hw_stage == HwStage::Geometry) {
            // The copy shader span is not compared (its hash is); reuse the compiled one.
            rt.hw.gs.vs_copy = s->runtime.hw.gs.vs_copy;
            if (!rt.hw.gs.vs_copy_hash) {
                rt.hw.gs.vs_copy_hash = s->runtime.hw.gs.vs_copy_hash;
            }
        }
        Info tmp = s->info;
        tmp.user_data = std::span<const u32>(ud);
        tmp.RefreshFlatBuf();
        const StageSpecialization spec(tmp, rt, s->profile, Backend::Bindings{});
        return s->spec == spec;
    } catch (...) {
        return false;
    }
}

namespace {
bool SameSrt(const Info& a, const Info& b) {
    return a.srt_info.flattened_bufsize_dw == b.srt_info.flattened_bufsize_dw &&
           a.srt_info.program == b.srt_info.program;
}

// Same resource lists (what StageSpecialization walks) and the same SRT program as another
// permutation of the program, so a specialization built from b's Info and flattened user data
// describes the draw for a too. Equal sizes alone are not enough: with another SRT program the
// flattened dwords sit elsewhere, the specialization read from them is another draw's, and a
// permutation that fits the draw never compares equal (a new permutation per draw, up to the
// renderer's cap, then skipped draws).
bool SameResourceLayout(const Info& a, const Info& b) {
    return a.sw_stage == b.sw_stage && a.buffers.size() == b.buffers.size() &&
           a.images.size() == b.images.size() && a.fmasks.size() == b.fmasks.size() &&
           a.samplers.size() == b.samplers.size() && a.flattened_ud_buf.size() == b.flattened_ud_buf.size() &&
           SameSrt(a, b);
}
} // namespace

GCN_API bool gcn_shader_explain_mismatch(const gcn_shader* s, const uint32_t user_data[16],
                                         const gcn_runtime_info* runtime, gcn_read_guest_fn read,
                                         void* ctx, char* out, size_t out_size) {
    auto put = [&](const std::string& m) {
        if (out && out_size) {
            std::snprintf(out, out_size, "%s", m.c_str());
        }
    };
    put("");
    if (!s || !user_data || !runtime) {
        put("bad arguments");
        return true;
    }
    try {
        GcnShim::GuestMemoryScope scope{read, ctx};
        std::array<u32, 16> ud;
        std::memcpy(ud.data(), user_data, 64);
        RuntimeInfo rt{};
        if (!ToRuntimeInfo(*runtime, rt, nullptr)) {
            put("runtime info not convertible");
            return true;
        }
        Info tmp = s->info;
        tmp.user_data = std::span<const u32>(ud);
        tmp.RefreshFlatBuf();
        const StageSpecialization spec(tmp, rt, s->profile, Backend::Bindings{});
        const StageSpecialization& have = s->spec;
        if (have == spec) {
            return false;
        }
        if (!(have.runtime_info == spec.runtime_info)) {
            put("runtime info");
        } else if (have.vs_attribs != spec.vs_attribs) {
            put("vertex attributes");
        } else if (have.fetch_shader_data != spec.fetch_shader_data) {
            put("fetch shader");
        } else if (have.inlined_fetch_hash != spec.inlined_fetch_hash) {
            put(fmt::format("inlined fetch shader code {:#x}/{:#x}", have.inlined_fetch_hash,
                            spec.inlined_fetch_hash));
        } else if (have.image_tables != spec.image_tables) {
            std::string m = "T# table entry buckets";
            for (size_t i = 0; i < std::max(have.image_tables.size(), spec.image_tables.size()); ++i) {
                m += fmt::format(" {}/{}", i < have.image_tables.size() ? int(have.image_tables[i]) : -1,
                                 i < spec.image_tables.size() ? int(spec.image_tables[i]) : -1);
            }
            put(m);
        } else if (have.buffers.size() != spec.buffers.size() || have.images.size() != spec.images.size() ||
                   have.samplers.size() != spec.samplers.size()) {
            put("resource counts");
        } else {
            for (size_t i = 0; i < spec.buffers.size(); ++i) {
                const auto& a = have.buffers[i];
                const auto& b = spec.buffers[i];
                if (!(a == b)) {
                    put(fmt::format("buffer {}: stride {}/{} misalign {}/{} formatted {}/{} fmt {}/{} {}/{} "
                                    "swizzle {}/{}",
                                    i, u32(a.stride), u32(b.stride), u32(a.misalign), u32(b.misalign),
                                    u32(a.is_formatted), u32(b.is_formatted), u32(a.data_format),
                                    u32(b.data_format), u32(a.num_format), u32(b.num_format),
                                    u32(a.swizzle_enable), u32(b.swizzle_enable)));
                    return true;
                }
            }
            for (size_t i = 0; i < spec.images.size(); ++i) {
                if (!(have.images[i] == spec.images[i])) {
                    put(fmt::format("image {}: type {}/{}", i, u32(have.images[i].type), u32(spec.images[i].type)));
                    return true;
                }
            }
            put("samplers, fmasks or bindings");
        }
        return true;
    } catch (...) {
        put("exception");
        return true;
    }
}

GCN_API bool gcn_shader_same_srt(const gcn_shader* a, const gcn_shader* b) {
    return a && b && (a == b || SameSrt(a->info, b->info));
}

GCN_API int gcn_shader_find_compatible(const gcn_shader* const* list, uint32_t count,
                                       const uint32_t user_data[16], const gcn_runtime_info* runtime,
                                       gcn_read_guest_fn read, void* ctx) {
    if (!list || !count || !user_data || !runtime) {
        return -1;
    }
    const gcn_shader* base = list[0];
    if (!base || base->runtime.hw_stage == HwStage::Geometry) {
        for (uint32_t k = 0; k < count; ++k) {
            if (gcn_shader_is_compatible(list[k], user_data, runtime, read, ctx)) return (int)k;
        }
        return -1;
    }
    try {
        GcnShim::GuestMemoryScope scope{read, ctx};
        std::array<u32, 16> ud;
        std::memcpy(ud.data(), user_data, 64);
        RuntimeInfo rt{};
        if (!ToRuntimeInfo(*runtime, rt, nullptr)) {
            return -1;
        }
        Info tmp = base->info;
        tmp.user_data = std::span<const u32>(ud);
        tmp.RefreshFlatBuf();
        const StageSpecialization spec(tmp, rt, base->profile, Backend::Bindings{});
        for (uint32_t k = 0; k < count; ++k) {
            const gcn_shader* s = list[k];
            if (!s) continue;
            if (SameResourceLayout(s->info, base->info)) {
                if (s->spec == spec) return (int)k;
            } else if (gcn_shader_is_compatible(s, user_data, runtime, read, ctx)) {
                return (int)k;
            }
        }
        return -1;
    } catch (...) {
        return -1;
    }
}

GCN_API int gcn_shader_find_compatible_flat(const gcn_shader* const* list, uint32_t count,
                                            const uint32_t user_data[16], const uint32_t* flatbuf,
                                            size_t flatbuf_dw, const gcn_runtime_info* runtime,
                                            gcn_read_guest_fn read, void* ctx) {
    if (!list || !count || !list[0] || !flatbuf ||
        flatbuf_dw < list[0]->info.srt_info.flattened_bufsize_dw ||
        list[0]->runtime.hw_stage == HwStage::Geometry) {
        return gcn_shader_find_compatible(list, count, user_data, runtime, read, ctx);
    }
    if (!user_data || !runtime) {
        return -1;
    }
    const gcn_shader* base = list[0];
    try {
        GcnShim::GuestMemoryScope scope{read, ctx};
        std::array<u32, 16> ud;
        std::memcpy(ud.data(), user_data, 64);
        RuntimeInfo rt{};
        if (!ToRuntimeInfo(*runtime, rt, nullptr)) {
            return -1;
        }
        Info tmp = base->info;
        tmp.user_data = std::span<const u32>(ud);
        tmp.flattened_ud_buf.assign(flatbuf, flatbuf + base->info.srt_info.flattened_bufsize_dw);
        const StageSpecialization spec(tmp, rt, base->profile, Backend::Bindings{});
        for (uint32_t k = 0; k < count; ++k) {
            const gcn_shader* s = list[k];
            if (!s) continue;
            if (SameResourceLayout(s->info, base->info)) {
                if (s->spec == spec) return (int)k;
            } else if (gcn_shader_is_compatible(s, user_data, runtime, read, ctx)) {
                return (int)k;
            }
        }
        return -1;
    } catch (...) {
        return -1;
    }
}

} // extern "C"

struct gcn_draw_spec {
    const gcn_shader* base = nullptr;
    Shader::Info info;            // the draw's view of base->info (spec.info points here)
    Shader::StageSpecialization spec;
};

namespace {
inline void HashMix(u64& h, u64 v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
}
inline void HashMap(u64& h, const AmdGpu::CompMapping& m) {
    u32 v;
    std::memcpy(&v, m.array.data(), 4);
    HashMix(h, v);
}
inline bool MaskBit(const u64 mask[2], u32 i) {
    return i < 128 && ((mask[i >> 6] >> (i & 63)) & 1);
}
// Hash of the StageSpecialization fields operator== compares (a subset: runtime info, fetch
// shader data and start bindings are left out, they are equal within one cache key in practice
// and are confirmed by the full comparison), buffers and images only in the slots of `mask`.
// Compatible specializations (operator== with other.bitset == mask) hash equal: every field
// mixed here is one operator== requires equal, under the same conditions.
u64 SpecHash(const Shader::StageSpecialization& sp, const u64 mask[2]) {
    u64 h = 0x6a09e667f3bcc909ull;
    HashMix(h, sp.vs_attribs.size());
    for (const auto& a : sp.vs_attribs) {
        HashMix(h, a.divisor);
        HashMix(h, static_cast<u64>(a.num_class));
        HashMap(h, a.dst_select);
    }
    HashMix(h, sp.fmasks.size());
    for (const auto& f : sp.fmasks) HashMix(h, (u64(f.width) << 32) | f.height);
    HashMix(h, sp.image_tables.size());
    for (const u8 b : sp.image_tables) HashMix(h, 3000 + b);
    if (sp.buffers.empty() && sp.images.empty() && sp.samplers.empty()) return h; // see operator==
    u32 binding = 0;
    for (const auto& b : sp.buffers) {
        if (MaskBit(mask, binding++)) {
            HashMix(h, 1000 + binding);
            HashMix(h, b.stride);
            HashMix(h, (u64(b.is_formatted) << 1) | b.swizzle_enable);
            if (b.is_formatted) {
                HashMix(h, (u64(b.data_format) << 8) | b.num_format);
                HashMap(h, b.dst_select);
                HashMix(h, static_cast<u64>(b.num_conversion));
            }
            if (b.swizzle_enable) HashMix(h, (u64(b.index_stride) << 8) | b.element_size);
        }
    }
    for (const auto& im : sp.images) {
        if (MaskBit(mask, binding++)) {
            HashMix(h, 2000 + binding);
            HashMix(h, static_cast<u64>(im.type));
            HashMix(h, (u64(im.is_1d_as_2d) << 3) | (u64(im.is_integer) << 2) | (u64(im.is_storage) << 1) |
                           im.is_srgb);
            HashMap(h, im.dst_select);
            HashMix(h, static_cast<u64>(im.num_conversion));
            HashMix(h, im.num_bindings);
        }
    }
    for (const auto& sa : sp.samplers) HashMix(h, (u64(sa.force_unnormalized) << 1) | sa.force_degamma);
    return h;
}
} // namespace

extern "C" {

GCN_API gcn_draw_spec* gcn_draw_spec_create(const gcn_shader* base, const uint32_t user_data[16],
                                            const gcn_runtime_info* runtime, gcn_read_guest_fn read,
                                            void* ctx) {
    if (!base || !user_data || !runtime || base->runtime.hw_stage == HwStage::Geometry) {
        return nullptr;
    }
    try {
        GcnShim::GuestMemoryScope scope{read, ctx};
        RuntimeInfo rt{};
        if (!ToRuntimeInfo(*runtime, rt, nullptr)) {
            return nullptr;
        }
        // as gcn_shader_find_compatible: the draw's specialization against the base permutation
        std::array<u32, 16> ud;
        std::memcpy(ud.data(), user_data, 64);
        auto d = std::make_unique<gcn_draw_spec>();
        d->base = base;
        d->info = base->info;
        d->info.user_data = std::span<const u32>(ud);
        d->info.RefreshFlatBuf();
        d->spec = Shader::StageSpecialization(d->info, rt, base->profile, Backend::Bindings{});
        d->spec.info = &d->info;
        d->info.user_data = {}; // ud is gone after this call; only Valid() reads info later
        return d.release();
    } catch (...) {
        return nullptr;
    }
}

GCN_API void gcn_draw_spec_destroy(gcn_draw_spec* spec) {
    delete spec;
}

GCN_API void gcn_draw_spec_mask(const gcn_draw_spec* spec, uint64_t mask[2]) {
    mask[0] = mask[1] = 0;
    if (!spec) return;
    for (u32 i = 0; i < Shader::StageSpecialization::MaxStageResources; ++i)
        if (spec->spec.bitset[i]) mask[i >> 6] |= 1ull << (i & 63);
}

GCN_API uint64_t gcn_draw_spec_hash(const gcn_draw_spec* spec, const uint64_t mask[2]) {
    return spec ? SpecHash(spec->spec, mask) : 0;
}

GCN_API uint64_t gcn_shader_spec_hash(const gcn_shader* s, const uint64_t mask[2]) {
    return s ? SpecHash(s->spec, mask) : 0;
}

GCN_API bool gcn_shader_same_resource_layout(const gcn_shader* a, const gcn_shader* b) {
    return a && b && a->runtime.hw_stage != HwStage::Geometry && b->runtime.hw_stage != HwStage::Geometry &&
           SameResourceLayout(a->info, b->info); // the one comparison gcn_shader_find_compatible uses
}

GCN_API bool gcn_shader_matches_draw_spec(const gcn_shader* s, const gcn_draw_spec* spec,
                                          const uint32_t user_data[16], const gcn_runtime_info* runtime,
                                          gcn_read_guest_fn read, void* ctx) {
    if (!s) return false;
    // as gcn_shader_find_compatible: the base's specialization serves permutations of the same
    // resource layout; others get the full check
    if (spec && gcn_shader_same_resource_layout(s, spec->base)) return s->spec == spec->spec;
    return gcn_shader_is_compatible(s, user_data, runtime, read, ctx);
}

GCN_API void gcn_decode_buffer(const uint32_t v[4], gcn_buffer_desc* o) {
    if (!v || !o) {
        return;
    }
    AmdGpu::Buffer b;
    std::memcpy(&b, v, 16);
    std::memset(o, 0, sizeof(*o));
    o->base_address = b.base_address;
    o->stride = b.GetStride();
    o->num_records = b.num_records;
    o->size_bytes = b.GetSize();
    o->dst_sel[0] = b.dst_sel_x;
    o->dst_sel[1] = b.dst_sel_y;
    o->dst_sel[2] = b.dst_sel_z;
    o->dst_sel[3] = b.dst_sel_w;
    o->num_format = static_cast<u8>(b.GetNumberFmt());
    o->data_format = static_cast<u8>(b.GetDataFmt());
    o->element_size = b.element_size;
    o->index_stride = b.index_stride;
    o->swizzle_enable = b.swizzle_enable;
    o->add_tid_enable = b.add_tid_enable;
    o->mtype = b.mtype;
    o->valid = b.Valid();
}

GCN_API void gcn_decode_image(const uint32_t t[8], gcn_image_desc* o) {
    if (!t || !o) {
        return;
    }
    AmdGpu::Image im;
    std::memcpy(&im, t, 32);
    std::memset(o, 0, sizeof(*o));
    o->base_address = im.Address();
    o->width = u32(im.width) + 1;
    o->height = u32(im.height) + 1;
    o->depth = u32(im.depth) + 1;
    o->pitch = im.Pitch();
    o->base_level = im.base_level;
    o->last_level = im.last_level;
    o->base_array = im.base_array;
    o->last_array = im.last_array;
    o->num_levels = im.NumLevels();
    o->num_layers = im.NumLayers();
    o->num_samples = im.NumSamples();
    o->type = static_cast<u32>(im.GetType());
    o->data_format = static_cast<u8>(im.GetDataFmt());
    o->num_format = static_cast<u8>(im.GetNumberFmt());
    o->dst_sel[0] = im.dst_sel_x;
    o->dst_sel[1] = im.dst_sel_y;
    o->dst_sel[2] = im.dst_sel_z;
    o->dst_sel[3] = im.dst_sel_w;
    o->tiling_index = im.tiling_index;
    o->pow2pad = im.pow2pad;
    o->valid = im.Valid();
    o->min_lod = float(im.min_lod) / 256.0f;
}

GCN_API void gcn_decode_sampler(const uint32_t sv[4], gcn_sampler_desc* o) {
    if (!sv || !o) {
        return;
    }
    AmdGpu::Sampler s;
    std::memcpy(&s, sv, 16);
    std::memset(o, 0, sizeof(*o));
    o->clamp_x = u8(s.clamp_x.Value());
    o->clamp_y = u8(s.clamp_y.Value());
    o->clamp_z = u8(s.clamp_z.Value());
    o->max_aniso = u8(s.max_aniso.Value());
    o->depth_compare_func = u8(s.depth_compare_func.Value());
    o->xy_mag_filter = u8(s.xy_mag_filter.Value());
    o->xy_min_filter = u8(s.xy_min_filter.Value());
    o->z_filter = u8(s.z_filter.Value());
    o->mip_filter = u8(s.mip_filter.Value());
    o->filter_mode = u8(s.filter_mode.Value());
    o->border_color_type = u8(s.border_color_type.Value());
    o->force_unnormalized = s.force_unnormalized.Value();
    o->force_degamma = s.force_degamma.Value();
    o->trunc_coord = s.trunc_coord.Value();
    o->disable_cube_wrap = s.disable_cube_wrap.Value();
    o->lod_bias = s.LodBias();
    o->min_lod = s.MinLod();
    o->max_lod = s.MaxLod();
    o->border_color_ptr = u32(s.border_color_ptr.Value());
}

GCN_API void gcn_set_log_level(int level) {
    GcnShim::SetLogLevel(level);
}

GCN_API void gcn_set_log_callback(gcn_log_fn fn, void* ctx) {
    GcnShim::SetLogCallback(fn, ctx);
}

GCN_API void gcn_set_dump_dir(const char* dir) {
    GcnShim::SetDumpDir(dir);
}

GCN_API const char* gcn_version(void) {
    return "gcn-recompiler 1 (shadPS4 32021e4)";
}

} // extern "C"

static_assert(GCN_MAX_BUFFERS == Shader::NUM_BUFFERS, "gcn_push_data.buf_sizes size");
static_assert(offsetof(gcn_push_data, buf_sizes) == 56, "push block layout (DefinePushDataBlock)");
