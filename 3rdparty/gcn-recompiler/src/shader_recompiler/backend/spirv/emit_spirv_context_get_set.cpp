// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "core/emulator_settings.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/ir/attribute.h"
#include "shader_recompiler/ir/microinstruction.h"
#include "shader_recompiler/ir/patch.h"
#include "shader_recompiler/runtime_info.h"

#include <magic_enum/magic_enum.hpp>

namespace Shader::Backend::SPIRV {

using PointerType = EmitContext::PointerType;
using PointerSize = EmitContext::PointerSize;

static std::pair<Id, bool> OutputAttrComponentType(EmitContext& ctx, IR::Attribute attr) {
    if (IR::IsParam(attr)) {
        const u32 index{u32(attr) - u32(IR::Attribute::Param0)};
        const auto& info{ctx.output_params.at(index)};
        return {info.component_type, info.is_integer};
    }
    if (IR::IsMrt(attr)) {
        const u32 index{u32(attr) - u32(IR::Attribute::RenderTarget0)};
        const auto& info{ctx.frag_outputs.at(index)};
        return {info.component_type, info.is_integer};
    }
    switch (attr) {
    case IR::Attribute::Position0:
    case IR::Attribute::ClipDistance:
    case IR::Attribute::CullDistance:
    case IR::Attribute::Depth:
    case IR::Attribute::PointSize:
        return {ctx.F32[1], false};
    case IR::Attribute::RenderTargetIndex:
    case IR::Attribute::ViewportIndex:
    case IR::Attribute::SampleMask:
        return {ctx.U32[1], true};
    case IR::Attribute::StencilRef:
        return {ctx.S32[1], true};
    default:
        UNREACHABLE_MSG("Write attribute {}", attr);
    }
}

Id EmitGetUserData(EmitContext& ctx, IR::ScalarReg reg) {
    const Id ud_reg{ctx.EmitFlatbufferLoad(ctx.ConstU32(static_cast<u32>(reg)))};
    ctx.Name(ud_reg, fmt::format("ud_{}", u32(reg)));
    return ud_reg;
}

Id EmitReadConst(EmitContext& ctx, IR::Inst* inst, Id addr, Id offset) {
    const u32 flatbuf_off_dw = inst->Flags<u32>();
    if (!EmulatorSettings.IsDirectMemoryAccessEnabled()) {
        return ctx.EmitFlatbufferLoad(ctx.ConstU32(flatbuf_off_dw));
    }
    if (flatbuf_off_dw == 0) {
        return ctx.OpFunctionCall(ctx.U32[1], ctx.read_const_dynamic, addr, offset);
    } else {
        return ctx.EmitFlatbufferLoad(ctx.ConstU32(flatbuf_off_dw));
    }
}

Id EmitReadConstBuffer(EmitContext& ctx, u32 handle, Id index) {
    const auto& buffer = ctx.buffers[handle];
    if (const Id offset = buffer.Offset(PointerSize::B32); Sirit::ValidId(offset)) {
        index = ctx.OpIAdd(ctx.U32[1], index, offset);
    }
    const auto [id, pointer_type] = buffer.Alias(PointerType::U32);
    // PS4Recomp: s_buffer_load past num_records reads 0 on GCN, like the vector buffer loads
    // (EmitLoadBufferB32xN). Without the check a Metal shader reads whatever follows the buffer:
    // Uncharted 4's tiled lighting takes its per-tile light count this way, and a count read
    // past the end of the count array made its loop run for seconds (GPU watchdog).
    const Id size = buffer.Size(PointerSize::B32);
    const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value, ctx.ClampIndex(index, size))};
    return ctx.ZeroIfOutOfBounds(ctx.U32[1], ctx.u32_zero_value, ctx.OpLoad(ctx.U32[1], ptr), index, size);
}

Id EmitGetAttribute(EmitContext& ctx, IR::Attribute attr, u32 comp, u32 index) {
    if (IR::IsParam(attr)) {
        const u32 param_index{u32(attr) - u32(IR::Attribute::Param0)};
        const auto& param{ctx.input_params.at(param_index)};
        const Id value = [&] {
            if (param.is_array) {
                ASSERT(param.num_components > 1);
                if (param.is_loaded) {
                    return ctx.OpCompositeExtract(param.component_type, param.id_array[index],
                                                  comp);
                } else {
                    return ctx.OpLoad(param.component_type,
                                      ctx.OpAccessChain(param.pointer_type, param.id,
                                                        ctx.ConstU32(index), ctx.ConstU32(comp)));
                }
            } else {
                ASSERT(!param.is_loaded);
                if (param.num_components > 1) {
                    return ctx.OpLoad(
                        param.component_type,
                        ctx.OpAccessChain(param.pointer_type, param.id, ctx.ConstU32(comp)));
                } else {
                    return ctx.OpLoad(param.component_type, param.id);
                }
            }
        }();
        return param.is_integer ? ctx.OpBitcast(ctx.F32[1], value) : value;
    }
    if (IR::IsBarycentricCoord(attr) && attr != IR::Attribute::BaryCoordPullModel &&
        ctx.profile.supports_fragment_shader_barycentric) {
        ++comp;
    }
    switch (attr) {
    case IR::Attribute::Position0:
        ASSERT(ctx.sw_stage == SwStage::Geometry);
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.gl_in, ctx.ConstU32(index),
                                            ctx.ConstU32(0U), ctx.ConstU32(comp)));
    case IR::Attribute::FragCoord:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.frag_coord, ctx.ConstU32(comp)));
    case IR::Attribute::TessellationEvaluationPointU:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.tess_coord, ctx.u32_zero_value));
    case IR::Attribute::TessellationEvaluationPointV:
        return ctx.OpLoad(ctx.F32[1],
                          ctx.OpAccessChain(ctx.input_f32, ctx.tess_coord, ctx.ConstU32(1U)));
    case IR::Attribute::BaryCoordSmooth:
        if (ctx.profile.supports_amd_shader_explicit_vertex_parameter) {
            return ctx.OpLoad(ctx.F32[1], ctx.OpAccessChain(ctx.input_f32, ctx.bary_coord_smooth,
                                                            ctx.ConstU32(comp)));
        } else {
            return ctx.OpCompositeExtract(ctx.F32[1], ctx.OpLoad(ctx.F32[3], ctx.bary_coord), comp);
        }
    case IR::Attribute::BaryCoordSmoothCentroid:
        if (ctx.profile.supports_amd_shader_explicit_vertex_parameter) {
            return ctx.OpLoad(ctx.F32[1],
                              ctx.OpAccessChain(ctx.input_f32, ctx.bary_coord_smooth_centroid,
                                                ctx.ConstU32(comp)));
        } else {
            return ctx.OpCompositeExtract(
                ctx.F32[1], ctx.OpInterpolateAtCentroid(ctx.F32[3], ctx.bary_coord), comp);
        }
    case IR::Attribute::BaryCoordSmoothSample:
        if (ctx.profile.supports_amd_shader_explicit_vertex_parameter) {
            return ctx.OpLoad(
                ctx.F32[1],
                ctx.OpAccessChain(ctx.input_f32, ctx.bary_coord_smooth_sample, ctx.ConstU32(comp)));
        } else {
            return ctx.OpCompositeExtract(
                ctx.F32[1],
                ctx.OpInterpolateAtSample(ctx.F32[3], ctx.bary_coord,
                                          ctx.OpLoad(ctx.U32[1], ctx.sample_index)),
                comp);
        }
    case IR::Attribute::BaryCoordNoPersp:
        return ctx.OpLoad(ctx.F32[1], ctx.OpAccessChain(ctx.input_f32, ctx.bary_coord_nopersp,
                                                        ctx.ConstU32(comp)));
    case IR::Attribute::BaryCoordNoPerspSample:
        return ctx.OpLoad(
            ctx.F32[1],
            ctx.OpAccessChain(ctx.input_f32, ctx.bary_coord_nopersp_sample, ctx.ConstU32(comp)));
    case IR::Attribute::BaryCoordPullModel: {
        ASSERT_MSG(comp < 3, "Invalid BaryCoordPullModel component {}", comp);

        if (ctx.profile.supports_amd_shader_explicit_vertex_parameter) {
            return ctx.OpLoad(
                ctx.F32[1],
                ctx.OpAccessChain(ctx.input_f32, ctx.bary_coord_pull_model, ctx.ConstU32(comp)));
        }

        // FragCoord.w is reciprocal clip W at the fragment center. Perspective barycentrics
        // are (I/W)/(1/W) and (J/W)/(1/W), so multiplying by FragCoord.w recovers the
        // pull-model values expected by the guest.
        const Id inv_w = ctx.OpLoad(
            ctx.F32[1], ctx.OpAccessChain(ctx.input_f32, ctx.frag_coord, ctx.ConstU32(3U)));
        if (comp == 2) {
            return inv_w;
        }
        const Id ij =
            ctx.OpCompositeExtract(ctx.F32[1], ctx.OpLoad(ctx.F32[3], ctx.bary_coord), comp + 1);
        return ctx.OpFMul(ctx.F32[1], ij, inv_w);
    }
    default:
        UNREACHABLE_MSG("Read attribute {}", attr);
    }
}

Id EmitGetAttributeU1(EmitContext& ctx, IR::Attribute attr, u32 comp) {
    ASSERT(comp == 0);
    switch (attr) {
    case IR::Attribute::IsFrontFace:
        return ctx.OpLoad(ctx.U1[1], ctx.front_facing);
    case IR::Attribute::IsHelperInvocation:
        return ctx.OpLoad(ctx.U1[1], ctx.helper_invocation);
    default:
        UNREACHABLE_MSG("Unsupported U1 attribute {}", attr);
    }
}

Id EmitGetAttributeU32(EmitContext& ctx, IR::Attribute attr, u32 comp) {
    switch (attr) {
    case IR::Attribute::VertexId:
        return ctx.OpLoad(ctx.U32[1], ctx.vertex_index);
    case IR::Attribute::InstanceId:
        return ctx.OpLoad(ctx.U32[1], ctx.instance_id);
    case IR::Attribute::BaseVertex:
        return ctx.OpLoad(ctx.U32[1], ctx.base_vertex);
    case IR::Attribute::BaseInstance:
        return ctx.OpLoad(ctx.U32[1], ctx.base_instance);
    case IR::Attribute::WorkgroupIndex:
        return ctx.workgroup_index_id;
    case IR::Attribute::WorkgroupId:
        return ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.workgroup_id), comp);
    case IR::Attribute::LocalInvocationId:
        return ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.local_invocation_id),
                                      comp);
    case IR::Attribute::LocalInvocationIndex:
        return ctx.OpLoad(ctx.U32[1], ctx.local_invocation_index);
    case IR::Attribute::SampleIndex:
        return ctx.OpLoad(ctx.U32[1], ctx.sample_index);
    case IR::Attribute::SampleMask:
        return ctx.OpLoad(ctx.U32[1],
                          ctx.OpAccessChain(ctx.input_u32, ctx.sample_mask_in, ctx.u32_zero_value));
    case IR::Attribute::RenderTargetIndex:
        return ctx.OpLoad(ctx.U32[1], ctx.output_layer);
    case IR::Attribute::PrimitiveId:
        return ctx.OpLoad(ctx.U32[1], ctx.primitive_id);
    case IR::Attribute::InvocationId:
        ASSERT(ctx.info.sw_stage == SwStage::Geometry ||
               ctx.info.sw_stage == SwStage::TessellationControl);
        return ctx.OpLoad(ctx.U32[1], ctx.invocation_id);
    case IR::Attribute::SubgroupLtMask:
        return ctx.OpLoad(
            ctx.U32[1], ctx.OpAccessChain(ctx.input_u32, ctx.subgroup_lt_mask, ctx.ConstU32(comp)));
    case IR::Attribute::PatchVertices:
        ASSERT(ctx.info.sw_stage == SwStage::TessellationControl);
        return ctx.OpLoad(ctx.U32[1], ctx.patch_vertices);
    case IR::Attribute::PackedHullInvocationInfo: {
        ASSERT(ctx.info.sw_stage == SwStage::TessellationControl);
        // [0:8]: patch id within VGT
        // [8:12]: output control point id
        // But 0:8 should be treated as 0 for attribute addressing purposes
        if (ctx.runtime_info.sw.tcs.IsPassthrough()) {
            // Gcn shader would run with 1 thread, but we need to run a thread for
            // each output control point.
            // If Gcn shader uses this value, we should make sure all threads in the
            // Vulkan shader use 0
            return ctx.u32_zero_value;
        } else {
            const Id invocation_id = ctx.OpLoad(ctx.U32[1], ctx.invocation_id);
            return ctx.OpShiftLeftLogical(ctx.U32[1], invocation_id, ctx.ConstU32(8u));
        }
    }
    default:
        UNREACHABLE_MSG("Read U32 attribute {}", attr);
    }
}

void EmitSetAttribute(EmitContext& ctx, IR::Attribute attr, Id value, u32 element) {
    const auto op_store = [&](Id pointer) {
        const auto [component_type, is_integer] = OutputAttrComponentType(ctx, attr);
        if (is_integer) {
            ctx.OpStore(pointer, ctx.OpBitcast(component_type, value));
        } else {
            ctx.OpStore(pointer, value);
        }
    };
    if (IR::IsParam(attr)) {
        const u32 attr_index{u32(attr) - u32(IR::Attribute::Param0)};
        if (ctx.hw_stage == HwStage::Local) {
            const auto component_ptr = ctx.TypePointer(spv::StorageClass::Output, ctx.F32[1]);
            return op_store(ctx.OpAccessChain(component_ptr, ctx.output_attr_array,
                                              ctx.ConstU32(attr_index), ctx.ConstU32(element)));
        } else {
            const auto& info{ctx.output_params.at(attr_index)};
            ASSERT(info.num_components > 0);
            if (info.num_components == 1) {
                return op_store(info.id);
            } else {
                return op_store(
                    ctx.OpAccessChain(info.pointer_type, info.id, ctx.ConstU32(element)));
            }
        }
    }
    if (IR::IsMrt(attr)) {
        const u32 index{u32(attr) - u32(IR::Attribute::RenderTarget0)};
        const auto& info{ctx.frag_outputs.at(index)};
        if (element < 3 && ctx.runtime_info.hw.fs.color_buffers[index].blend_self_scale) {
            // Emulates GCN's factor-scaled min/max blend: min/max(src*src, dst*dst).
            value = ctx.OpFMul(ctx.F32[1], value, value);
        }
        if (info.num_components == 1) {
            return op_store(info.id);
        } else {
            return op_store(ctx.OpAccessChain(info.pointer_type, info.id, ctx.ConstU32(element)));
        }
    }
    switch (attr) {
    case IR::Attribute::Position0:
        return op_store(
            ctx.OpAccessChain(ctx.output_f32, ctx.output_position, ctx.ConstU32(element)));
    case IR::Attribute::ClipDistance:
        return op_store(
            ctx.OpAccessChain(ctx.output_f32, ctx.clip_distances, ctx.ConstU32(element)));
    case IR::Attribute::CullDistance:
        return op_store(
            ctx.OpAccessChain(ctx.output_f32, ctx.cull_distances, ctx.ConstU32(element)));
    case IR::Attribute::PointSize:
        return op_store(ctx.output_point_size);
    case IR::Attribute::RenderTargetIndex:
        return op_store(ctx.output_layer);
    case IR::Attribute::ViewportIndex:
        return op_store(ctx.output_viewport_index);
    case IR::Attribute::Depth:
        return op_store(ctx.frag_depth);
    case IR::Attribute::SampleMask:
        return op_store(ctx.OpAccessChain(ctx.output_u32, ctx.sample_mask, ctx.u32_zero_value));
    case IR::Attribute::StencilRef:
        if (ctx.profile.supports_shader_stencil_export) {
            return op_store(ctx.stencil_ref);
        }
        return;
    default:
        UNREACHABLE_MSG("Write attribute {}", attr);
    }
}

Id EmitGetTessGenericAttribute(EmitContext& ctx, Id vertex_index, Id attr_index, Id comp_index) {
    const auto attr_comp_ptr = ctx.TypePointer(spv::StorageClass::Input, ctx.F32[1]);
    return ctx.OpLoad(ctx.F32[1], ctx.OpAccessChain(attr_comp_ptr, ctx.input_attr_array,
                                                    vertex_index, attr_index, comp_index));
}

Id EmitReadTcsGenericOuputAttribute(EmitContext& ctx, Id vertex_index, Id attr_index,
                                    Id comp_index) {
    const auto attr_comp_ptr = ctx.TypePointer(spv::StorageClass::Output, ctx.F32[1]);
    return ctx.OpLoad(ctx.F32[1], ctx.OpAccessChain(attr_comp_ptr, ctx.output_attr_array,
                                                    vertex_index, attr_index, comp_index));
}

void EmitSetTcsGenericAttribute(EmitContext& ctx, Id value, Id attr_index, Id comp_index) {
    // Implied vertex index is invocation_id
    const auto component_ptr = ctx.TypePointer(spv::StorageClass::Output, ctx.F32[1]);
    Id pointer =
        ctx.OpAccessChain(component_ptr, ctx.output_attr_array,
                          ctx.OpLoad(ctx.U32[1], ctx.invocation_id), attr_index, comp_index);
    ctx.OpStore(pointer, value);
}

Id EmitGetPatch(EmitContext& ctx, IR::Patch patch) {
    const u32 index{IR::GenericPatchIndex(patch)};
    const Id element{ctx.ConstU32(IR::GenericPatchElement(patch))};
    const Id type{ctx.sw_stage == SwStage::TessellationControl ? ctx.output_f32 : ctx.input_f32};
    const Id pointer{ctx.OpAccessChain(type, ctx.patches.at(index), element)};
    return ctx.OpLoad(ctx.F32[1], pointer);
}

void EmitSetPatch(EmitContext& ctx, IR::Patch patch, Id value) {
    const Id pointer{[&] {
        if (IR::IsGeneric(patch)) {
            const u32 index{IR::GenericPatchIndex(patch)};
            const Id element{ctx.ConstU32(IR::GenericPatchElement(patch))};
            return ctx.OpAccessChain(ctx.output_f32, ctx.patches.at(index), element);
        }
        switch (patch) {
        case IR::Patch::TessellationLodLeft:
        case IR::Patch::TessellationLodRight:
        case IR::Patch::TessellationLodTop:
        case IR::Patch::TessellationLodBottom: {
            const u32 index{static_cast<u32>(patch) - u32(IR::Patch::TessellationLodLeft)};
            const Id index_id{ctx.ConstU32(index)};
            return ctx.OpAccessChain(ctx.output_f32, ctx.output_tess_level_outer, index_id);
        }
        case IR::Patch::TessellationLodInteriorU:
            return ctx.OpAccessChain(ctx.output_f32, ctx.output_tess_level_inner,
                                     ctx.u32_zero_value);
        case IR::Patch::TessellationLodInteriorV:
            return ctx.OpAccessChain(ctx.output_f32, ctx.output_tess_level_inner, ctx.ConstU32(1u));
        default:
            UNREACHABLE_MSG("Patch {}", u32(patch));
        }
    }()};
    ctx.OpStore(pointer, value);
}

template <u32 N, PointerType alias>
static Id EmitLoadBufferB32xN(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    const auto& spv_buffer = ctx.buffers[handle];
    if (const Id offset = spv_buffer.Offset(PointerSize::B32); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto& data_types = alias == PointerType::U32 ? ctx.U32 : ctx.F32;
    const auto [id, pointer_type] = spv_buffer.Alias(alias);

    const Id size = spv_buffer.Size(PointerSize::B32);
    const Id zero = alias == PointerType::U32 ? ctx.u32_zero_value : ctx.ConstF32(0.0f);
    const auto load_dword = [&](Id index) {
        const Id ptr = ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value, ctx.ClampIndex(index, size));
        return ctx.ZeroIfOutOfBounds(data_types[1], zero, ctx.OpLoad(data_types[1], ptr), index, size);
    };
    boost::container::static_vector<Id, N> ids;
    if (spv_buffer.misalign != 0) {
        // PS4Recomp: the buffer is bound at the dword below its base address; dword i of the
        // guest buffer spans bound dwords i and i+1 (GCN reads it unaligned). Guest dword i
        // exists iff it lies wholly inside the guest buffer (i < guest_dwords; GCN returns 0
        // for a dword crossing the end).
        const u32 shift = spv_buffer.misalign * 8;
        const Id guest_size = spv_buffer.guest_dwords;
        const auto as_u32 = [&](Id v) { return alias == PointerType::U32 ? v : ctx.OpBitcast(ctx.U32[1], v); };
        Id lo = as_u32(load_dword(address));
        for (u32 i = 0; i < N; i++) {
            const Id hi = as_u32(load_dword(ctx.OpIAdd(ctx.U32[1], address, ctx.ConstU32(i + 1))));
            const Id gi = i == 0 ? address : ctx.OpIAdd(ctx.U32[1], address, ctx.ConstU32(i));
            const Id v = ctx.ZeroIfOutOfBounds(
                ctx.U32[1], ctx.u32_zero_value,
                ctx.OpBitwiseOr(ctx.U32[1], ctx.OpShiftRightLogical(ctx.U32[1], lo, ctx.ConstU32(shift)),
                                ctx.OpShiftLeftLogical(ctx.U32[1], hi, ctx.ConstU32(32 - shift))),
                gi, guest_size);
            ids.push_back(alias == PointerType::U32 ? v : ctx.OpBitcast(ctx.F32[1], v));
            lo = hi;
        }
    } else {
        for (u32 i = 0; i < N; i++) {
            ids.push_back(load_dword(i == 0 ? address : ctx.OpIAdd(ctx.U32[1], address, ctx.ConstU32(i))));
        }
    }

    const Id result = N == 1 ? ids[0] : ctx.OpCompositeConstruct(data_types[N], ids);
    return result;
}

Id EmitLoadBufferU8(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    const auto& spv_buffer = ctx.buffers[handle];
    if (const Id offset = spv_buffer.Offset(PointerSize::B8); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto [id, pointer_type] = spv_buffer.Alias(PointerType::U8);
    const Id size = spv_buffer.Size(PointerSize::B8);
    const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value,
                                   ctx.ClampIndex(address, size))};
    const Id result{ctx.OpLoad(ctx.U8, ptr)};
    return ctx.ZeroIfOutOfBounds(ctx.U8, ctx.u8_zero_value, result, address, size);
}

Id EmitLoadBufferU16(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    const auto& spv_buffer = ctx.buffers[handle];
    if ((spv_buffer.misalign & 1) != 0) {
        // PS4Recomp: the buffer starts at an odd byte of the bound dword (a vertex V# at an odd
        // address, 8_8 data): halfword i of the guest buffer is bound bytes 2i + misalign and
        // 2i + misalign + 1 (read through the U8 alias). It exists iff both are inside the guest
        // buffer (GCN returns 0 for an element crossing the end; the pushed size is the guest
        // size plus misalign).
        const auto [id8, pointer8] = spv_buffer.Alias(PointerType::U8);
        const Id size8 = spv_buffer.Size(PointerSize::B8);
        const Id lo_at = ctx.OpIAdd(ctx.U32[1], ctx.OpShiftLeftLogical(ctx.U32[1], address, ctx.ConstU32(1U)),
                                    ctx.ConstU32(spv_buffer.misalign));
        const Id hi_at = ctx.OpIAdd(ctx.U32[1], lo_at, ctx.ConstU32(1U));
        const auto load_byte = [&](Id at) {
            const Id ptr{ctx.OpAccessChain(pointer8, id8, ctx.u32_zero_value, ctx.ClampIndex(at, size8))};
            return ctx.OpUConvert(ctx.U32[1], ctx.OpLoad(ctx.U8, ptr));
        };
        const Id value = ctx.OpBitwiseOr(ctx.U32[1], load_byte(lo_at),
                                         ctx.OpShiftLeftLogical(ctx.U32[1], load_byte(hi_at), ctx.ConstU32(8U)));
        return ctx.ZeroIfOutOfBounds(ctx.U16, ctx.u16_zero_value, ctx.OpUConvert(ctx.U16, value), hi_at, size8);
    }
    if (const Id offset = spv_buffer.Offset(PointerSize::B16); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto [id, pointer_type] = spv_buffer.Alias(PointerType::U16);
    const Id size = spv_buffer.Size(PointerSize::B16);
    const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value,
                                   ctx.ClampIndex(address, size))};
    const Id result{ctx.OpLoad(ctx.U16, ptr)};
    return ctx.ZeroIfOutOfBounds(ctx.U16, ctx.u16_zero_value, result, address, size);
}

Id EmitLoadBufferU32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<1, PointerType::U32>(ctx, inst, handle, address);
}

Id EmitLoadBufferU32x2(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<2, PointerType::U32>(ctx, inst, handle, address);
}

Id EmitLoadBufferU32x3(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<3, PointerType::U32>(ctx, inst, handle, address);
}

Id EmitLoadBufferU32x4(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<4, PointerType::U32>(ctx, inst, handle, address);
}

Id EmitLoadBufferU64(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    const auto& spv_buffer = ctx.buffers[handle];
    if (spv_buffer.misalign != 0)
        UNREACHABLE_MSG("64-bit load of a buffer whose base is not dword aligned (+{})", spv_buffer.misalign);
    if (const Id offset = spv_buffer.Offset(PointerSize::B64); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto [id, pointer_type] = spv_buffer.Alias(PointerType::U64);
    const Id size = spv_buffer.Size(PointerSize::B64);
    const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u64_zero_value,
                                   ctx.ClampIndex(address, size))};
    const Id result{ctx.OpLoad(ctx.U64, ptr)};
    return ctx.ZeroIfOutOfBounds(ctx.U64, ctx.u64_zero_value, result, address, size);
}

Id EmitLoadBufferF32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<1, PointerType::F32>(ctx, inst, handle, address);
}

Id EmitLoadBufferF32x2(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<2, PointerType::F32>(ctx, inst, handle, address);
}

Id EmitLoadBufferF32x3(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<3, PointerType::F32>(ctx, inst, handle, address);
}

Id EmitLoadBufferF32x4(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferB32xN<4, PointerType::F32>(ctx, inst, handle, address);
}

Id EmitLoadBufferFormatF32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    UNREACHABLE_MSG("SPIR-V instruction");
}

template <u32 N, PointerType alias>
static void EmitStoreBufferB32xN(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address,
                                 Id value) {
    const auto& spv_buffer = ctx.buffers[handle];
    if (const Id offset = spv_buffer.Offset(PointerSize::B32); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto& data_types = alias == PointerType::U32 ? ctx.U32 : ctx.F32;
    const auto [id, pointer_type] = spv_buffer.Alias(alias);

    const Id size = spv_buffer.Size(PointerSize::B32);
    if (spv_buffer.misalign != 0) {
        // PS4Recomp: bound at the dword below the base. Guest dword i covers the top 4 - m bytes
        // of bound dword i and the low m bytes of dword i + 1; each part is written with an
        // atomic AND (clear its bytes) and OR (set them), so the neighbouring bytes, which
        // belong to other data, are never overwritten.
        const u32 shift = spv_buffer.misalign * 8;
        const u32 low_mask = (1u << shift) - 1u;
        const auto [scope, semantics] = std::pair{ctx.ConstU32(static_cast<u32>(spv::Scope::Device)),
                                                  ctx.u32_zero_value};
        const auto [uid, u32_ptr] = spv_buffer.Alias(PointerType::U32);
        const Id guest_size = spv_buffer.guest_dwords;
        for (u32 i = 0; i < N; i++) {
            const Id index_i = i == 0 ? address : ctx.OpIAdd(ctx.U32[1], address, ctx.ConstU32(i));
            Id value_i = N == 1 ? value : ctx.OpCompositeExtract(data_types[1], value, i);
            if (alias != PointerType::U32) value_i = ctx.OpBitcast(ctx.U32[1], value_i);
            ctx.EmitBoundsCheckedStore(spv_buffer.buffer_type, index_i, guest_size, [&] {
                const Id p0 = ctx.OpAccessChain(u32_ptr, uid, ctx.u32_zero_value, index_i);
                const Id p1 = ctx.OpAccessChain(u32_ptr, uid, ctx.u32_zero_value,
                                                ctx.OpIAdd(ctx.U32[1], index_i, ctx.ConstU32(1u)));
                ctx.OpAtomicAnd(ctx.U32[1], p0, scope, semantics, ctx.ConstU32(low_mask));
                ctx.OpAtomicOr(ctx.U32[1], p0, scope, semantics,
                               ctx.OpShiftLeftLogical(ctx.U32[1], value_i, ctx.ConstU32(shift)));
                ctx.OpAtomicAnd(ctx.U32[1], p1, scope, semantics, ctx.ConstU32(~low_mask));
                ctx.OpAtomicOr(ctx.U32[1], p1, scope, semantics,
                               ctx.OpShiftRightLogical(ctx.U32[1], value_i, ctx.ConstU32(32 - shift)));
            });
        }
        return;
    }
    for (u32 i = 0; i < N; i++) {
        const Id index_i = i == 0 ? address : ctx.OpIAdd(ctx.U32[1], address, ctx.ConstU32(i));
        const Id value_i = N == 1 ? value : ctx.OpCompositeExtract(data_types[1], value, i);
        ctx.EmitBoundsCheckedStore(spv_buffer.buffer_type, index_i, size, [&] {
            const Id ptr_i = ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value, index_i);
            ctx.OpStore(ptr_i, value_i);
        });
    }
}

void EmitStoreBufferU8(EmitContext& ctx, IR::Inst*, u32 handle, Id address, Id value) {
    const auto& spv_buffer = ctx.buffers[handle];
    if (const Id offset = spv_buffer.Offset(PointerSize::B8); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto [id, pointer_type] = spv_buffer.Alias(PointerType::U8);
    ctx.EmitBoundsCheckedStore(spv_buffer.buffer_type, address, spv_buffer.Size(PointerSize::B8), [&] {
        const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value, address)};
        ctx.OpStore(ptr, value);
    });
}

void EmitStoreBufferU16(EmitContext& ctx, IR::Inst*, u32 handle, Id address, Id value) {
    const auto& spv_buffer = ctx.buffers[handle];
    if ((spv_buffer.misalign & 1) != 0)
        UNREACHABLE_MSG("16-bit store at an odd offset of a buffer whose base is not dword aligned (+{})", spv_buffer.misalign);
    if (const Id offset = spv_buffer.Offset(PointerSize::B16); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto [id, pointer_type] = spv_buffer.Alias(PointerType::U16);
    ctx.EmitBoundsCheckedStore(spv_buffer.buffer_type, address, spv_buffer.Size(PointerSize::B16), [&] {
        const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u32_zero_value, address)};
        ctx.OpStore(ptr, value);
    });
}

void EmitStoreBufferU32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<1, PointerType::U32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferU32x2(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<2, PointerType::U32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferU32x3(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<3, PointerType::U32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferU32x4(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<4, PointerType::U32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferU64(EmitContext& ctx, IR::Inst*, u32 handle, Id address, Id value) {
    const auto& spv_buffer = ctx.buffers[handle];
    if (spv_buffer.misalign != 0)
        UNREACHABLE_MSG("64-bit store of a buffer whose base is not dword aligned (+{})", spv_buffer.misalign);
    if (const Id offset = spv_buffer.Offset(PointerSize::B64); Sirit::ValidId(offset)) {
        address = ctx.OpIAdd(ctx.U32[1], address, offset);
    }
    const auto [id, pointer_type] = spv_buffer.Alias(PointerType::U64);
    ctx.EmitBoundsCheckedStore(spv_buffer.buffer_type, address, spv_buffer.Size(PointerSize::B64), [&] {
        const Id ptr{ctx.OpAccessChain(pointer_type, id, ctx.u64_zero_value, address)};
        ctx.OpStore(ptr, value);
    });
}

void EmitStoreBufferF32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<1, PointerType::F32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferF32x2(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<2, PointerType::F32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferF32x3(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<3, PointerType::F32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferF32x4(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferB32xN<4, PointerType::F32>(ctx, inst, handle, address, value);
}

void EmitStoreBufferFormatF32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    UNREACHABLE_MSG("SPIR-V instruction");
}

// PS4Recomp: guest memory through the page table (EmitContext::DefineGuestMemoryFunctions).
Id EmitLoadGuestU32(EmitContext& ctx, Id address, Id enable) {
    const Id load_label = ctx.OpLabel();
    const Id merge_label = ctx.OpLabel();
    const Id entry_label = ctx.last_label;
    ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(enable, load_label, merge_label);
    ctx.AddLabel(load_label);
    const Id value = ctx.OpFunctionCall(ctx.U32[1], ctx.guest_load, address);
    ctx.OpBranch(merge_label);
    ctx.AddLabel(merge_label);
    return ctx.OpPhi(ctx.U32[1], value, load_label, ctx.u32_zero_value, entry_label);
}

void EmitStoreGuestU32(EmitContext& ctx, Id address, Id enable, Id value) {
    const Id store_label = ctx.OpLabel();
    const Id merge_label = ctx.OpLabel();
    ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(enable, store_label, merge_label);
    ctx.AddLabel(store_label);
    ctx.OpFunctionCall(ctx.void_id, ctx.guest_store, address, value);
    ctx.OpBranch(merge_label);
    ctx.AddLabel(merge_label);
}

void EmitGuestReportStatus(EmitContext& ctx, Id condition, Id bits) {
    const Id report_label = ctx.OpLabel();
    const Id merge_label = ctx.OpLabel();
    ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(condition, report_label, merge_label);
    ctx.AddLabel(report_label);
    ctx.OpFunctionCall(ctx.void_id, ctx.guest_report, bits);
    ctx.OpBranch(merge_label);
    ctx.AddLabel(merge_label);
}

void EmitGetScalarRegister(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetScalarRegister(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitGetVectorRegister(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetVectorRegister(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetVirtualRegister(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitGetVirtualRegister(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetGotoVariable(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitGetGotoVariable(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

} // namespace Shader::Backend::SPIRV
