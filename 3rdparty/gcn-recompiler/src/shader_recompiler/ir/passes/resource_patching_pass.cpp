// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <vector>
#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/opcodes.h"
#include "shader_recompiler/ir/operand_helper.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/passes/resource_pass.h"
#include "shader_recompiler/ir/passes/srt.h"
#include "shader_recompiler/ir/reinterpret.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/resource.h"
#include "video_core/amdgpu/resource.h"

namespace Shader::Optimization {
namespace {

IR::Type BufferDataType(const IR::Inst& inst, const Profile& profile) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::LoadBufferU8:
    case IR::Opcode::StoreBufferU8:
        return IR::Type::U8;
    case IR::Opcode::LoadBufferU16:
    case IR::Opcode::StoreBufferU16:
        return IR::Type::U16;
    case IR::Opcode::LoadBufferU64:
    case IR::Opcode::StoreBufferU64:
    case IR::Opcode::BufferAtomicIAdd64:
    case IR::Opcode::BufferAtomicSMax64:
    case IR::Opcode::BufferAtomicSMin64:
    case IR::Opcode::BufferAtomicUMax64:
    case IR::Opcode::BufferAtomicUMin64:
        return IR::Type::U64;
    case IR::Opcode::BufferAtomicFMax32:
    case IR::Opcode::BufferAtomicFMin32:
        return profile.supports_buffer_fp32_atomic_min_max ? IR::Type::F32 : IR::Type::U32;
    case IR::Opcode::LoadBufferFormatF32:
    case IR::Opcode::StoreBufferFormatF32:
        // Formatted buffer loads can use a variety of types.
        return IR::Type::U32 | IR::Type::F32 | IR::Type::U16 | IR::Type::U8;
    default:
        return IR::Type::U32;
    }
}

u32 BufferAddressShift(const IR::Inst& inst, AmdGpu::DataFormat data_format) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::LoadBufferU8:
    case IR::Opcode::StoreBufferU8:
        return 0;
    case IR::Opcode::LoadBufferU16:
    case IR::Opcode::StoreBufferU16:
        return 1;
    case IR::Opcode::LoadBufferU64:
    case IR::Opcode::StoreBufferU64:
    case IR::Opcode::BufferAtomicIAdd64:
    case IR::Opcode::BufferAtomicSMax64:
    case IR::Opcode::BufferAtomicSMin64:
    case IR::Opcode::BufferAtomicUMax64:
    case IR::Opcode::BufferAtomicUMin64:
        return 3;
    case IR::Opcode::LoadBufferFormatF32:
    case IR::Opcode::StoreBufferFormatF32: {
        switch (data_format) {
        case AmdGpu::DataFormat::Format8:
            return 0;
        case AmdGpu::DataFormat::Format8_8:
        case AmdGpu::DataFormat::Format16:
            return 1;
        case AmdGpu::DataFormat::Format8_8_8_8:
        case AmdGpu::DataFormat::Format16_16:
        case AmdGpu::DataFormat::Format10_11_11:
        case AmdGpu::DataFormat::Format2_10_10_10:
        case AmdGpu::DataFormat::Format16_16_16_16:
        case AmdGpu::DataFormat::Format32:
        case AmdGpu::DataFormat::Format32_32:
        case AmdGpu::DataFormat::Format32_32_32:
        case AmdGpu::DataFormat::Format32_32_32_32:
            return 2;
        default:
            return 0;
        }
        break;
    }
    case IR::Opcode::ReadConstBuffer:
        // Provided address is already in dwords
        return 0;
    default:
        return 2;
    }
}

class Descriptors {
public:
    explicit Descriptors(Info& info_)
        : info{info_}, buffer_resources{info_.buffers}, image_resources{info_.images},
          sampler_resources{info_.samplers}, fmask_resources(info_.fmasks) {}

    u32 Add(const BufferResource& desc) {
        const u32 index{Add(buffer_resources, desc, [&desc](const auto& existing) {
            return desc.sharp_fetch == existing.sharp_fetch && desc.post_op == existing.post_op &&
                   desc.post_op_dw1_mask == existing.post_op_dw1_mask &&
                   desc.buffer_type == existing.buffer_type;
        })};
        auto& buffer = buffer_resources[index];
        buffer.used_types |= desc.used_types;
        buffer.is_written |= desc.is_written;
        buffer.is_formatted |= desc.is_formatted;
        return index;
    }

    u32 Add(const ImageResource& desc) {
        const u32 index{Add(image_resources, desc, [&desc](const auto& existing) {
            return desc.sharp_fetch == existing.sharp_fetch && desc.is_array == existing.is_array &&
                   desc.mip_fallback_mode == existing.mip_fallback_mode &&
                   desc.constant_mip_index == existing.constant_mip_index &&
                   desc.post_op == existing.post_op;
        })};
        auto& image = image_resources[index];
        image.is_atomic |= desc.is_atomic;
        image.is_written |= desc.is_written;
        return index;
    }

    u32 Add(const SamplerResource& desc) {
        const u32 index{Add(sampler_resources, desc, [this, &desc](const auto& existing) {
            return desc.sharp_fetch == existing.sharp_fetch && desc.post_op == existing.post_op &&
                   desc.post_op_tsharp_dw3_off == existing.post_op_tsharp_dw3_off &&
                   desc.is_depth == existing.is_depth;
        })};
        return index;
    }

    u32 Add(const FMaskResource& desc) {
        u32 index = Add(fmask_resources, desc, [&desc](const auto& existing) {
            return desc.sharp_idx == existing.sharp_idx;
        });
        return index;
    }

private:
    template <typename Descriptors, typename Descriptor, typename Func>
    static u32 Add(Descriptors& descriptors, const Descriptor& desc, Func&& pred) {
        const auto it{std::ranges::find_if(descriptors, pred)};
        if (it != descriptors.end()) {
            return static_cast<u32>(std::distance(descriptors.begin(), it));
        }
        descriptors.push_back(desc);
        return static_cast<u32>(descriptors.size()) - 1;
    }

    const Info& info;
    BufferResourceList& buffer_resources;
    ImageResourceList& image_resources;
    SamplerResourceList& sampler_resources;
    FMaskResourceList& fmask_resources;
};

} // Anonymous namespace

using SharpSources = boost::container::small_vector<const IR::Inst*, 4>;

SharpLocation SharpLocationFromSource(const IR::Inst* inst) {
    SharpLocation location{};
    if (inst->GetOpcode() != IR::Opcode::GetUserData && inst->GetOpcode() != IR::Opcode::ReadConst &&
        inst->GetOpcode() != IR::Opcode::ReadConstBuffer) {
        // PS4Recomp: a value the shader computes (its flags are not a flatbuf offset)
        return UNKNOWN_LOCATION;
    }
    if (inst->GetOpcode() == IR::Opcode::GetUserData) {
        return static_cast<SharpLocation>(inst->Arg(0).ScalarReg());
    } else if (inst->GetOpcode() == IR::Opcode::ReadConstBuffer) {
        location = inst->Flags<IR::BufferInstInfo>().flatbuf_off_dw;
    } else {
        location = inst->Flags<SharpLocation>();
    }
    if (location == 0) {
        LOG_WARNING(Render_Recompiler, "Sharp source was not flatenned");
        return UNKNOWN_LOCATION;
    }
    return location;
}

template <typename T>
SharpFetch<T> ConstructSharpFetch(const SharpReference& sharp) {
    using Summary = SharpFetch<T>::Summary;
    SharpFetch<T> sharp_fetch{};
    for (u32 i = 0; i < sharp.num_dwords; i++) {
        auto dword = sharp.dwords[i];
        if (dword.IsImmediate()) {
            sharp_fetch.immediates[i] = dword.U32();
        } else {
            sharp_fetch.offsets[i] = SharpLocationFromSource(dword.Inst());
            sharp_fetch.load_mask |= (1 << i);
            if (sharp_fetch.offsets[i] == UNKNOWN_LOCATION) {
                sharp_fetch.summary = Summary::Invalid;
            }
        }
    }
    if (sharp_fetch.summary != Summary::Invalid) {
        const u32 base = sharp_fetch.offsets[0];
        for (u32 i = 1; i < sharp.num_dwords; ++i) {
            if (sharp_fetch.offsets[i] - base != i) {
                return sharp_fetch;
            }
        }
        sharp_fetch.summary = Summary::SingleLoad;
    }
    return sharp_fetch;
}

// PS4Recomp: true if the V# dword is an immediate or is read from the flattened user data (the
// renderer then fetches the sharp before the draw); false for a value only the shader knows: a
// V# loaded at an offset computed in the shader (s_load_dwordx4 s[4:7], s[0:1], s2 in a loop),
// or computed from such a load.
static bool IsFlattenedSharpDword(const IR::Value& dword) {
    if (dword.IsImmediate()) {
        return true;
    }
    const IR::Inst* inst = dword.Inst();
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetUserData:
        return true;
    case IR::Opcode::ReadConst:
        return inst->Flags<u16>() != 0;
    case IR::Opcode::ReadConstBuffer: {
        const auto f = inst->Flags<IR::BufferInstInfo>();
        return f.sharp_source != 0 && f.flatbuf_off_dw != 0;
    }
    default:
        return false;
    }
}

// "dword i: producer" for the dwords of a sharp that are not flattened (error messages)
static std::string DescribeUnflattened(const SharpReference& sharp) {
    std::string out;
    for (u32 i = 0; i < sharp.num_dwords; ++i) {
        if (IsFlattenedSharpDword(sharp.dwords[i])) {
            continue;
        }
        const IR::Inst* inst = sharp.dwords[i].Inst();
        out += fmt::format("{}dword {}: {}", out.empty() ? "" : ", ", i, IR::NameOf(inst->GetOpcode()));
        if (inst->GetOpcode() == IR::Opcode::ReadConst || inst->GetOpcode() == IR::Opcode::Void) {
            out += fmt::format(" (offset {})", inst->GetOpcode() == IR::Opcode::ReadConst && inst->Arg(1).IsImmediate()
                                                   ? fmt::format("{}", inst->Arg(1).U32())
                                                   : std::string("computed"));
        }
    }
    return out;
}

static bool IsFlattenedSharp(const SharpReference& sharp) {
    for (u32 i = 0; i < sharp.num_dwords; ++i) {
        if (!IsFlattenedSharpDword(sharp.dwords[i])) {
            return false;
        }
    }
    return true;
}

// PS4Recomp: a buffer access through a V# the shader loads at run time (IsFlattenedSharp false),
// or any MUBUF/MTBUF access with ADDR64. No binding can stand for it: it becomes guest memory
// loads and stores (LoadGuestU32 / StoreGuestU32) addressed from the V# dwords as the shader
// holds them, with GCN's addressing and range check (address = base + index * stride + offset; a
// dword is accessed only when it lies below stride * num_records bytes, num_records bytes when
// stride is 0, and the V# type is 0). With ADDR64 the offset is the 64-bit VGPR address plus
// soffset and inst_offset, without the num_records check (see in_range below). Compute shaders
// load and store; draw stages only load (their stores fail the translation). Formatted accesses
// convert 32-bit data formats (no conversion: the dwords are moved as they are) with dst_sel
// applied to loads; a V# with another data or number format, a swizzled V#, an
// unaligned dword or an address past 40 bits is reported to the renderer through the fault
// buffer and that access is not made. Accesses that cannot be lowered (8/16/64-bit, atomics, a
// tbuffer format other than 32-bit) fail the translation.
static void LowerDynamicBufferAccess(IR::Inst& inst, Info& info) {
    namespace G = GuestMemory;
    const IR::Opcode op = inst.GetOpcode();
    const auto flags = inst.Flags<IR::BufferInstInfo>();
    const u32 pc = static_cast<u32>(flags.pc.Value());

    bool is_store = false;
    bool is_format = false;
    u32 num_dwords = 1;
    bool f32_data = false;
    switch (op) {
    case IR::Opcode::ReadConstBuffer:
    case IR::Opcode::LoadBufferU32:
        break;
    case IR::Opcode::LoadBufferU32x2:
        num_dwords = 2;
        break;
    case IR::Opcode::LoadBufferU32x3:
        num_dwords = 3;
        break;
    case IR::Opcode::LoadBufferU32x4:
        num_dwords = 4;
        break;
    case IR::Opcode::LoadBufferF32:
    case IR::Opcode::LoadBufferF32x2:
    case IR::Opcode::LoadBufferF32x3:
    case IR::Opcode::LoadBufferF32x4:
        num_dwords = 1 + u32(op) - u32(IR::Opcode::LoadBufferF32);
        f32_data = true;
        break;
    case IR::Opcode::StoreBufferU32:
    case IR::Opcode::StoreBufferU32x2:
    case IR::Opcode::StoreBufferU32x3:
    case IR::Opcode::StoreBufferU32x4:
        num_dwords = 1 + u32(op) - u32(IR::Opcode::StoreBufferU32);
        is_store = true;
        break;
    case IR::Opcode::StoreBufferF32:
    case IR::Opcode::StoreBufferF32x2:
    case IR::Opcode::StoreBufferF32x3:
    case IR::Opcode::StoreBufferF32x4:
        num_dwords = 1 + u32(op) - u32(IR::Opcode::StoreBufferF32);
        is_store = true;
        f32_data = true;
        break;
    case IR::Opcode::LoadBufferFormatF32:
        is_format = true;
        num_dwords = 4;
        break;
    case IR::Opcode::StoreBufferFormatF32:
        is_format = true;
        is_store = true;
        num_dwords = 4;
        break;
    default:
        UNREACHABLE_MSG("{} at pc {:#x}: buffer access through a V# loaded at run time is not "
                        "implemented for this operation",
                        IR::NameOf(op), pc);
    }
    // Draw stages reach guest memory for loads only (the renderer's draw path binds the page
    // table and a fault slot that never probes; see GmDrawSlot in ps4_gnm_draw.mm).
    if (is_store && info.sw_stage != SwStage::Compute) {
        UNREACHABLE_MSG("{} at pc {:#x}: buffer store to guest memory ({}) in a non-compute shader "
                        "(guest memory stores are only implemented for compute)",
                        IR::NameOf(op), pc, flags.addr64 ? "ADDR64" : "a V# loaded at run time");
    }
    static_assert(u32(IR::Opcode::LoadBufferF32x4) - u32(IR::Opcode::LoadBufferF32) == 3);
    static_assert(u32(IR::Opcode::StoreBufferU32x4) - u32(IR::Opcode::StoreBufferU32) == 3);
    static_assert(u32(IR::Opcode::StoreBufferF32x4) - u32(IR::Opcode::StoreBufferF32) == 3);

    const bool inst_typed = is_format && flags.inst_data_fmt != AmdGpu::DataFormat::FormatInvalid;
    u32 inst_components = 0;
    if (inst_typed) {
        const auto dfmt = flags.inst_data_fmt.Value();
        const auto nfmt = flags.inst_num_fmt.Value();
        const bool fmt_ok = dfmt == AmdGpu::DataFormat::Format32 || dfmt == AmdGpu::DataFormat::Format32_32 ||
                            dfmt == AmdGpu::DataFormat::Format32_32_32 ||
                            dfmt == AmdGpu::DataFormat::Format32_32_32_32;
        const bool nfmt_ok = nfmt == AmdGpu::NumberFormat::Uint || nfmt == AmdGpu::NumberFormat::Sint ||
                             nfmt == AmdGpu::NumberFormat::Float;
        if (!fmt_ok || !nfmt_ok) {
            UNREACHABLE_MSG("{} at pc {:#x}: tbuffer format {}/{} through a V# loaded at run time "
                            "(only 32-bit uint/sint/float formats are implemented)",
                            IR::NameOf(op), pc, dfmt, nfmt);
        }
        inst_components = AmdGpu::NumComponents(dfmt);
    }

    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
    IR::Inst* handle = inst.Arg(0).Inst();
    ASSERT_MSG(handle->NumArgs() == 4, "V# of {} with {} dwords", IR::NameOf(op), handle->NumArgs());
    const IR::U32 dw0{handle->Arg(0)};
    const IR::U32 dw1{handle->Arg(1)};
    const IR::U32 dw2{handle->Arg(2)};
    const IR::U32 dw3{handle->Arg(3)};
    const auto field = [&](const IR::U32& dw, u32 offset, u32 count) {
        return ir.BitFieldExtract(dw, ir.Imm32(offset), ir.Imm32(count));
    };
    // base bits 0..39: the GPU ignores bits 40..47 of the V# (games leave other values there;
    // Uncharted 4's async compute does), as AmdGpu::Buffer::base_address does
    const IR::U64 base = ir.PackUint2x32(ir.CompositeConstruct(dw0, ir.BitwiseAnd(dw1, ir.Imm32(0xFFu))));
    const IR::U32 stride = field(dw1, 16, 14);
    const IR::U1 swizzled = ir.INotEqual(field(dw1, 31, 1), ir.Imm32(0u));
    const IR::U32 num_records = dw2;
    const IR::U1 is_buffer = ir.IEqual(field(dw3, 30, 2), ir.Imm32(0u));
    const IR::U64 size = IR::U64{ir.Select(ir.IEqual(stride, ir.Imm32(0u)), ir.UConvert(64, num_records),
                                           ir.IMul(ir.UConvert(64, stride), ir.UConvert(64, num_records)))};

    // byte offset of the access in the buffer, in 64 bits (index * stride does not wrap)
    IR::U64 offset;
    if (flags.addr64) {
        // ADDR64 (CI ISA, MUBUF/MTBUF): address = V#.base + VGPR[vaddr:vaddr+1] (64 bits) +
        // soffset + inst_offset; idxen and offen are not used.
        // the address args hold (VGPR[vaddr], VGPR[vaddr + 1], soffset) (Translator::BufferAddress)
        const IR::U64 vaddr64 = ir.PackUint2x32(
            ir.CompositeConstruct(IR::GetBufferIndexArg(&inst), IR::GetBufferVOffsetArg(&inst)));
        const IR::U32 rest = ir.IAdd(ir.Imm32(u32(flags.inst_offset.Value())), IR::GetBufferSOffsetArg(&inst));
        offset = ir.IAdd(vaddr64, ir.UConvert(64, rest));
    } else if (op == IR::Opcode::ReadConstBuffer) {
        offset = ir.ShiftLeftLogical(ir.UConvert(64, IR::U32{inst.Arg(1)}), ir.Imm32(2u));
    } else {
        IR::U32 index = flags.index_enable ? IR::GetBufferIndexArg(&inst) : ir.Imm32(0u);
        // ADD_TID_ENABLE: thread_id[5:0] of the wave (compute only, see ComputeWaveLane)
        const IR::U1 add_tid = ir.INotEqual(field(dw3, 23, 1), ir.Imm32(0u));
        index = ir.IAdd(index, IR::U32{ir.Select(add_tid, ComputeWaveLane(ir), ir.Imm32(0u))});
        IR::U32 voffset = ir.IAdd(ir.Imm32(u32(flags.inst_offset.Value())), IR::GetBufferSOffsetArg(&inst));
        if (flags.voffset_enable) {
            voffset = ir.IAdd(voffset, IR::GetBufferVOffsetArg(&inst));
        }
        offset = ir.IAdd(ir.IMul(ir.UConvert(64, index), ir.UConvert(64, stride)), ir.UConvert(64, voffset));
    }
    // ADDR64 is not range checked against num_records: the 64-bit VGPR address is a pointer, not
    // an offset into the V#'s records (CI ISA: idxen/offen and the record index are not used with
    // it). TLOU's hair and foliage shaders read their shadow cascades through a V# of base 0,
    // stride 4, num_records 0xFFFFFFFF (16 GiB) at addresses near 0x11_2006_xxxx (68 GiB): a range
    // check would return 0, and the PS4 frame shows those surfaces shadowed (Task 7 of the TLOU
    // colour plan). The page table still checks the 40-bit address and the mapping.
    const auto in_range = [&](const IR::U64& byte) {
        if (flags.addr64) {
            return ir.Imm1(true);
        }
        return ir.ILessThanEqual(ir.IAdd(byte, ir.Imm64(u64(4))), size, false);
    };
    // Reports only for an access that reaches the buffer: through a null V# (num_records 0, as
    // in a zeroed copy table entry) or past its end an access is a no-op, as on GCN.
    const IR::U1 reaches = ir.LogicalAnd(is_buffer, in_range(offset));
    const IR::U1 unaligned =
        ir.INotEqual(ir.BitwiseAnd(IR::U32{ir.CompositeExtract(ir.UnpackUint2x32(offset), 0)}, ir.Imm32(3u)), ir.Imm32(0u));
    ir.GuestReportStatus(ir.LogicalAnd(reaches, swizzled), G::StatusSwizzle);
    ir.GuestReportStatus(ir.LogicalAnd(reaches, unaligned), G::StatusUnaligned);
    IR::U1 usable = ir.LogicalAnd(is_buffer, ir.LogicalNot(ir.LogicalOr(swizzled, unaligned)));

    // data and number format of a formatted access, dst_sel
    IR::U32 components = ir.Imm32(num_dwords); // dwords accessed
    IR::U1 int_format = ir.Imm1(false);
    std::array<IR::U32, 4> dst_sel{};
    if (is_format && !inst_typed) {
        const IR::U32 dfmt = field(dw3, 15, 4);
        const IR::U32 nfmt = field(dw3, 12, 3);
        const auto eq = [&](const IR::U32& v, u32 k) { return ir.IEqual(v, ir.Imm32(k)); };
        components = IR::U32{ir.Select(eq(dfmt, u32(AmdGpu::DataFormat::Format32)), ir.Imm32(1u),
            ir.Select(eq(dfmt, u32(AmdGpu::DataFormat::Format32_32)), ir.Imm32(2u),
            ir.Select(eq(dfmt, u32(AmdGpu::DataFormat::Format32_32_32)), ir.Imm32(3u),
            ir.Select(eq(dfmt, u32(AmdGpu::DataFormat::Format32_32_32_32)), ir.Imm32(4u), ir.Imm32(0u)))))};
        int_format = ir.LogicalOr(eq(nfmt, u32(AmdGpu::NumberFormat::Uint)), eq(nfmt, u32(AmdGpu::NumberFormat::Sint)));
        const IR::U1 fmt_ok = ir.LogicalAnd(ir.INotEqual(components, ir.Imm32(0u)),
                                            ir.LogicalOr(int_format, eq(nfmt, u32(AmdGpu::NumberFormat::Float))));
        for (u32 i = 0; i < 4; ++i) {
            dst_sel[i] = field(dw3, 3 * i, 3);
        }
        IR::U1 bad = ir.LogicalNot(fmt_ok);
        if (is_store) {
            // stores move the components as they are: dst_sel must be the identity for them
            for (u32 c = 0; c < 4; ++c) {
                const IR::U1 stored = ir.ILessThan(ir.Imm32(c), components, false);
                bad = ir.LogicalOr(bad, ir.LogicalAnd(stored, ir.INotEqual(dst_sel[c], ir.Imm32(4u + c))));
            }
        }
        ir.GuestReportStatus(ir.LogicalAnd(reaches, bad), G::StatusFormat);
        usable = ir.LogicalAnd(usable, ir.LogicalNot(bad));
    } else if (inst_typed) {
        components = ir.Imm32(inst_components);
        const auto nfmt = flags.inst_num_fmt.Value();
        int_format = ir.Imm1(nfmt == AmdGpu::NumberFormat::Uint || nfmt == AmdGpu::NumberFormat::Sint);
    }

    // dword c of the access: its guest address and whether it is accessed
    const auto dword_addr = [&](u32 c) {
        const IR::U64 byte = c == 0 ? offset : IR::U64{ir.IAdd(offset, ir.Imm64(u64(4 * c)))};
        const IR::U1 enable = ir.LogicalAnd(ir.LogicalAnd(usable, in_range(byte)),
                                            ir.ILessThan(ir.Imm32(c), components, false));
        return std::pair{IR::U64{ir.IAdd(base, byte)}, enable};
    };

    info.uses_guest_memory = true;
    if (is_store) {
        const IR::Value data = inst.Arg(2);
        for (u32 c = 0; c < num_dwords; ++c) {
            IR::Value v = num_dwords == 1 ? data : ir.CompositeExtract(data, c);
            const IR::U32 value = (f32_data || is_format) ? ir.BitCast<IR::U32>(IR::F32{v}) : IR::U32{v};
            const auto [addr, enable] = dword_addr(c);
            ir.StoreGuestU32(addr, enable, value);
        }
        inst.Invalidate();
        return;
    }
    std::array<IR::U32, 4> loaded{};
    for (u32 c = 0; c < num_dwords; ++c) {
        const auto [addr, enable] = dword_addr(c);
        loaded[c] = ir.LoadGuestU32(addr, enable);
    }
    IR::Value result;
    if (is_format) {
        std::array<IR::F32, 4> out;
        const IR::U32 one = IR::U32{ir.Select(int_format, ir.Imm32(1u), ir.Imm32(0x3F800000u))};
        for (u32 i = 0; i < 4; ++i) {
            IR::U32 v = loaded[i];
            if (!inst_typed) {
                // dst_sel: 0 zero, 1 one, 4..7 component x..w
                const IR::U32& sel = dst_sel[i];
                v = ir.Imm32(0u);
                v = IR::U32{ir.Select(ir.IEqual(sel, ir.Imm32(1u)), one, v)};
                for (u32 c = 0; c < 4; ++c) {
                    v = IR::U32{ir.Select(ir.IEqual(sel, ir.Imm32(4u + c)), loaded[c], v)};
                }
            }
            out[i] = ir.BitCast<IR::F32>(v);
        }
        result = ir.CompositeConstruct(out[0], out[1], out[2], out[3]);
    } else if (num_dwords == 1) {
        result = f32_data ? IR::Value{ir.BitCast<IR::F32>(loaded[0])} : IR::Value{loaded[0]};
    } else {
        boost::container::static_vector<IR::Value, 4> parts;
        for (u32 c = 0; c < num_dwords; ++c) {
            parts.push_back(f32_data ? IR::Value{ir.BitCast<IR::F32>(loaded[c])} : IR::Value{loaded[c]});
        }
        result = ir.CompositeConstruct(parts);
    }
    inst.ReplaceUsesWithAndRemove(result);
}

void PatchBufferSharp(const ResourceDiscovery& resource, Info& info, Descriptors& descriptors,
                      const Profile& profile) {
    IR::Inst& inst = *resource.user;

    const u32 buffer_binding = descriptors.Add(BufferResource{
        .sharp_fetch = ConstructSharpFetch<AmdGpu::Buffer>(resource.sharps[0]),
        .used_types = BufferDataType(inst, profile),
        .buffer_type = BufferType::Guest,
        .is_written = IsBufferStore(inst),
        .is_formatted = inst.GetOpcode() == IR::Opcode::LoadBufferFormatF32 ||
                        inst.GetOpcode() == IR::Opcode::StoreBufferFormatF32,
        .post_op = resource.sharps[0].post_op,
        .post_op_dw1_mask = resource.sharps[0].post_op_data.dw1_mask,
        .post_op_dw2_shift = resource.sharps[0].post_op_data.dw2_shift,
    });

    // Replace handle with binding index in buffer resource list.
    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
    inst.SetArg(0, ir.Imm32(buffer_binding));
}

// PS4Recomp: the image type that lays out the instruction's address VGPRs. It is the view type,
// except for an image read as 2D of height 1 (Image::Is1DAs2D): its instructions still carry 1D
// addresses (x [, slice]), which the patching widens to the 2D view's (x, y [, slice]).
AmdGpu::ImageType AddressType(const AmdGpu::Image& image, bool is_array) {
    if (image.Is1DAs2D()) {
        return image.GetType() == AmdGpu::ImageType::Color1DArray && is_array
                   ? AmdGpu::ImageType::Color1DArray
                   : AmdGpu::ImageType::Color1D;
    }
    return image.GetViewType(is_array);
}

// PS4Recomp: a read through a T# the shader loads at run time (Invalid fetch) is deferred
// (returns false, nothing patched) when `defer_dynamic` is set: LowerDynamicImageTable gets it
// once every buffer has its binding.
bool PatchImageSharp(const ResourceDiscovery& resource, Info& info, Descriptors& descriptors,
                     const Profile& profile, bool defer_dynamic = false) {
    IR::Inst& inst = *resource.user;
    if (defer_dynamic && !IsImageAtomicInstruction(inst) && inst.GetOpcode() != IR::Opcode::ImageWrite &&
        ConstructSharpFetch<AmdGpu::Image>(resource.sharps[0]).summary ==
            SharpFetch<AmdGpu::Image>::Summary::Invalid) {
        return false;
    }

    // Read image sharp.
    const auto inst_info = inst.Flags<IR::TextureInstInfo>();
    const bool is_atomic = IsImageAtomicInstruction(inst);
    const bool is_written = inst.GetOpcode() == IR::Opcode::ImageWrite || is_atomic;
    // ImageRead with !is_written gets emitted as OpImageFetch with LOD operand, doesn't
    // need fallback (TODO is this 100% true?)
    const bool needs_mip_storage_fallback =
        inst_info.has_lod && is_written && !profile.supports_image_load_store_lod;
    ImageResource image_res = {
        .sharp_fetch = ConstructSharpFetch<AmdGpu::Image>(resource.sharps[0]),
        .is_depth = bool(inst_info.is_depth),
        .is_atomic = is_atomic,
        .is_array = bool(inst_info.is_array),
        .is_written = is_written,
        .is_r128 = bool(inst_info.is_r128),
        .post_op = resource.sharps[0].post_op,
    };

    if (image_res.sharp_fetch.summary == SharpFetch<AmdGpu::Image>::Summary::Invalid) {
        // PS4Recomp: a T# the shader loads at run time has no binding. A write or atomic through
        // it would be dropped: the translation fails. A read of a T# loaded with s_buffer_load
        // through a V# the SRT walker cannot follow (or one loaded at run time) gets the null
        // image (reads 0), as before:
        // logged here and counted (Info::null_image_reads, logged by the renderer per shader).
        // Any other source fails the translation.
        const IR::Inst* handle = inst.Arg(0).Inst();
        bool from_buffer_loads = true;
        u32 k = 0;
        for (u32 part = 0; part < 2 && from_buffer_loads; ++part) {
            const IR::Inst* dwords = handle->Arg(part).TryInst();
            if (!dwords) {
                continue;
            }
            for (size_t i = 0; i < dwords->NumArgs(); ++i, ++k) {
                if (k < resource.sharps[0].num_dwords && IsFlattenedSharpDword(resource.sharps[0].dwords[k])) {
                    continue;
                }
                // s_buffer_load through a V# the walker cannot follow, kept as a buffer read or
                // lowered to guest memory loads (a V# loaded at run time)
                const IR::Value v = dwords->Arg(i);
                if (v.IsImmediate() || (v.Inst()->GetOpcode() != IR::Opcode::ReadConstBuffer &&
                                        v.Inst()->GetOpcode() != IR::Opcode::LoadGuestU32)) {
                    from_buffer_loads = false;
                    break;
                }
            }
        }
        if (is_written || !from_buffer_loads) {
            UNREACHABLE_MSG("{} at pc {:#x}: image {} through a T# loaded at run time is not "
                            "implemented ({})",
                            IR::NameOf(inst.GetOpcode()), u32(inst_info.pc.Value()),
                            is_written ? "store or atomic" : "read of a T# not loaded by s_buffer_load",
                            DescribeUnflattened(resource.sharps[0]));
        }
        LOG_ERROR(Render_Recompiler,
                  "{} at pc {:#x}: image read through a T# loaded at run time reads the null image ({})",
                  IR::NameOf(inst.GetOpcode()), u32(inst_info.pc.Value()), DescribeUnflattened(resource.sharps[0]));
        info.null_image_reads++;
    }
    auto image = image_res.GetSharp(info);
    ASSERT(image.GetType() != AmdGpu::ImageType::Invalid);

    if (needs_mip_storage_fallback) {
        // If the mip level to IMAGE_(LOAD/STORE)_MIP is a constant, set up ImageResource
        // so that we will only bind a single level.
        // If index is dynamic, we will bind levels as an array
        const auto view_type = AddressType(image, image_res.is_array);

        IR::Inst* body = inst.Arg(1).Inst();
        const auto lod_arg = [&] -> IR::Value {
            switch (view_type) {
            case AmdGpu::ImageType::Color1D: // x, [lod]
                return body->Arg(1);
            case AmdGpu::ImageType::Color1DArray: // x, slice, [lod]
            case AmdGpu::ImageType::Color2D:      // x, y, [lod]
                return body->Arg(2);
            case AmdGpu::ImageType::Color2DArray: // x, y, slice, [lod]
            case AmdGpu::ImageType::Cube:         // x, y, face, [lod]
            case AmdGpu::ImageType::Color3D:      // x, y, z, [lod]
                return body->Arg(3);
            case AmdGpu::ImageType::Color2DMsaa:
            case AmdGpu::ImageType::Color2DMsaaArray:
            default:
                UNREACHABLE_MSG("Invalid image type {}", view_type);
            }
        }();

        if (lod_arg.IsImmediate()) {
            image_res.mip_fallback_mode = MipStorageFallbackMode::ConstantIndex;
            image_res.constant_mip_index = lod_arg.U32();
        } else {
            image_res.mip_fallback_mode = MipStorageFallbackMode::DynamicIndex;
        }
    }

    // Patch image instruction if image is FMask.
    if (AmdGpu::IsFmask(image.GetDataFmt())) {
        ASSERT_MSG(!is_written, "FMask storage instructions are not supported");

        IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
        switch (inst.GetOpcode()) {
        case IR::Opcode::ImageRead:
        case IR::Opcode::ImageSampleRaw: {
            IR::F32 fmaskx = ir.BitCast<IR::F32>(ir.Imm32(0x76543210));
            IR::F32 fmasky = ir.BitCast<IR::F32>(ir.Imm32(0xfedcba98));
            inst.ReplaceUsesWith(ir.CompositeConstruct(fmaskx, fmasky));
            return true;
        }
        case IR::Opcode::ImageQueryLod:
            inst.ReplaceUsesWith(ir.Imm32(1));
            return true;
        case IR::Opcode::ImageQueryDimensions: {
            IR::Value dims = ir.CompositeConstruct(ir.Imm32(static_cast<u32>(image.width)),  // x
                                                   ir.Imm32(static_cast<u32>(image.height)), // y
                                                   ir.Imm32(1), ir.Imm32(1)); // depth, mip
            inst.ReplaceUsesWith(dims);

            // Track FMask resource to do specialization.
            descriptors.Add(FMaskResource{
                .sharp_idx = SharpLocationFromSource(resource.sharps[0].dwords[0].Inst()),
            });
            return true;
        }
        default:
            UNREACHABLE_MSG("Can't patch fmask instruction {}", inst.GetOpcode());
        }
    }

    u32 image_binding = descriptors.Add(image_res);

    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};

    if (inst.GetOpcode() == IR::Opcode::ImageSampleRaw) {
        auto& lod_prod = resource.sharps[1].post_op_data.lod_prod;
        const u32 sampler_binding = descriptors.Add(SamplerResource{
            .sharp_fetch = ConstructSharpFetch<AmdGpu::Sampler>(resource.sharps[1]),
            .post_op = resource.sharps[1].post_op,
            .post_op_tsharp_dw3_off =
                lod_prod.IsEmpty() ? UNKNOWN_LOCATION : SharpLocationFromSource(lod_prod.Inst()),
            .is_depth = bool(inst_info.is_depth), // true for the _C (compare) opcodes
        });
        inst.SetArg(0, ir.Imm32(image_binding | sampler_binding << 16));
    } else {
        inst.SetArg(0, ir.Imm32(image_binding));
    }
    return true;
}

// PS4Recomp: value = mul * leaf + add (mod 2^32) for one value `leaf` only the shader knows
// (leaf == nullptr: the constant add).
struct AffineU32 {
    const IR::Inst* leaf{};
    u32 mul{};
    u32 add{};
};

static std::optional<AffineU32> EvalAffine(const IR::Value& v, u32 depth = 0) {
    if (v.IsImmediate()) {
        if (v.Type() != IR::Type::U32) {
            return std::nullopt;
        }
        return AffineU32{nullptr, 0, v.U32()};
    }
    const IR::Inst* inst = v.Inst();
    const auto as_leaf = AffineU32{inst, 1, 0};
    if (depth >= 16) {
        return as_leaf;
    }
    const auto combine = [](AffineU32 a, AffineU32 b, bool sub) -> std::optional<AffineU32> {
        if (a.leaf && b.leaf && a.leaf != b.leaf) {
            return std::nullopt;
        }
        return AffineU32{a.leaf ? a.leaf : b.leaf, sub ? a.mul - b.mul : a.mul + b.mul,
                         sub ? a.add - b.add : a.add + b.add};
    };
    switch (inst->GetOpcode()) {
    case IR::Opcode::IAdd32:
    case IR::Opcode::ISub32: {
        const auto a = EvalAffine(inst->Arg(0), depth + 1);
        const auto b = EvalAffine(inst->Arg(1), depth + 1);
        if (!a || !b) {
            return as_leaf;
        }
        const auto r = combine(*a, *b, inst->GetOpcode() == IR::Opcode::ISub32);
        return r ? r : std::optional<AffineU32>{as_leaf};
    }
    case IR::Opcode::IMul32: {
        const auto a = EvalAffine(inst->Arg(0), depth + 1);
        const auto b = EvalAffine(inst->Arg(1), depth + 1);
        if (a && b && !b->leaf) {
            return AffineU32{a->leaf, a->mul * b->add, a->add * b->add};
        }
        if (a && b && !a->leaf) {
            return AffineU32{b->leaf, b->mul * a->add, b->add * a->add};
        }
        return as_leaf;
    }
    case IR::Opcode::ShiftLeftLogical32: {
        const auto a = EvalAffine(inst->Arg(0), depth + 1);
        if (a && inst->Arg(1).IsImmediate() && inst->Arg(1).U32() < 32) {
            const u32 n = inst->Arg(1).U32();
            return AffineU32{a->leaf, a->mul << n, a->add << n};
        }
        return as_leaf;
    }
    case IR::Opcode::ShiftRightLogical32: {
        // (mul * leaf + add) >> n == (mul >> n) * leaf + (add >> n) only when mul and add are
        // multiples of 2^n and the sum does not wrap: the T# offsets of a table are dword
        // offsets of byte offsets (byte offset >> 2) far below 2^32. A multiplier of 2^31 or more
        // is a negative one (first - s * index): the sum wraps for every index past 0
        const auto a = EvalAffine(inst->Arg(0), depth + 1);
        if (a && inst->Arg(1).IsImmediate() && inst->Arg(1).U32() < 32) {
            const u32 n = inst->Arg(1).U32();
            const u32 m = (1u << n) - 1;
            if ((a->mul & m) == 0 && (a->add & m) == 0 && a->mul < 0x80000000u) {
                return AffineU32{a->leaf, a->mul >> n, a->add >> n};
            }
        }
        return as_leaf;
    }
    default:
        return as_leaf;
    }
}

// The flattened V# (or SRT pointer) an SRT program reads a guest buffer through, as
// CopyBufferDwords takes it: V# at flat[vs] (SingleLoad), or the buffer of reads through an SRT
// pointer at an offset only the shader knows (LowerDynamicReadConst: dwords 0-1 the pointer,
// size_imm bytes).
struct TableBufferSource {
    SharpLocation vs{};
    u32 size_imm{};
};

static std::optional<TableBufferSource> BufferSource(const Info& info, u32 buffer) {
    if (buffer >= info.buffers.size()) {
        return std::nullopt;
    }
    const BufferResource& b = info.buffers[buffer];
    if (b.buffer_type != BufferType::Guest || b.post_op != SharpFetchPostOp::None) {
        return std::nullopt;
    }
    const auto& f = b.sharp_fetch;
    if (f.summary == SharpFetch<AmdGpu::Buffer>::Summary::SingleLoad) {
        return TableBufferSource{f.offsets[0], 0};
    }
    if (f.summary == SharpFetch<AmdGpu::Buffer>::Summary::MultiLoad && f.load_mask == 0b0011 &&
        f.offsets[1] == f.offsets[0] + 1 && f.immediates[2] != 0) {
        return TableBufferSource{f.offsets[0], f.immediates[2]};
    }
    return std::nullopt;
}

// A T# whose dword i the shader reads at dword offset first + i + stride * L of buffer
// `buffer` (s_buffer_load / s_load through a flattened V# or SRT pointer), L a value only the
// shader knows.
struct TableSite {
    u32 buffer{};
    u32 first{};
    u32 stride{};
    u32 num_dwords{};
    const IR::Inst* leaf{};
    IR::Value offset0; // the dword offset of T# dword 0 as the shader computes it
};

static std::optional<TableSite> MatchTableSite(const SharpReference& tsharp) {
    if (tsharp.num_dwords != 4 && tsharp.num_dwords != 8) {
        return std::nullopt;
    }
    TableSite site{};
    site.num_dwords = tsharp.num_dwords;
    for (u32 i = 0; i < tsharp.num_dwords; ++i) {
        const IR::Inst* rd = tsharp.dwords[i].TryInst();
        if (!rd || rd->GetOpcode() != IR::Opcode::ReadConstBuffer || !rd->Arg(0).IsImmediate()) {
            return std::nullopt;
        }
        const auto a = EvalAffine(rd->Arg(1));
        // a multiplier of 2^31 or more is a negative stride (first - s * index) mod 2^32: no
        // table form (the bound and the entry offsets assume a growing index)
        if (!a || !a->leaf || a->mul == 0 || a->mul >= 0x80000000u) {
            return std::nullopt;
        }
        if (i == 0) {
            site.buffer = rd->Arg(0).U32();
            site.leaf = a->leaf;
            site.stride = a->mul;
            site.first = a->add;
            site.offset0 = rd->Arg(1);
        } else if (rd->Arg(0).U32() != site.buffer || a->leaf != site.leaf ||
                   a->mul != site.stride || a->add != site.first + i) {
            return std::nullopt;
        }
    }
    return site;
}

// A flattened scalar: its flatbuf dword, or nullopt.
static std::optional<SharpLocation> FlattenedDword(const IR::Value& v) {
    const IR::Inst* inst = v.TryInst();
    if (!inst) {
        return std::nullopt;
    }
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetUserData:
        return static_cast<SharpLocation>(inst->Arg(0).ScalarReg());
    case IR::Opcode::ReadConst:
        if (const u16 off = inst->Flags<u16>(); off != 0) {
            return off;
        }
        return std::nullopt;
    case IR::Opcode::ReadConstBuffer: {
        const auto f = inst->Flags<IR::BufferInstInfo>();
        if (f.sharp_source != 0 && f.flatbuf_off_dw != 0) {
            return static_cast<SharpLocation>(f.flatbuf_off_dw.Value());
        }
        return std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

static const IR::Inst* StripConditionRef(const IR::Value& v) {
    const IR::Inst* inst = v.TryInst();
    while (inst && inst->GetOpcode() == IR::Opcode::ConditionRef) {
        inst = inst->Arg(0).TryInst();
    }
    return inst;
}

// The loop bound of a counter k = Phi(0, k + 1): the block feeding k + 1 back ends in a branch
// on a compare of k + 1 with a flattened N that takes the back edge while k + 1 < N (signed or
// unsigned) or k + 1 != N. Uncharted 4's T# tables indexed by a loop counter: a do-while over
// N entries, N loaded from the SRT.
static std::optional<ImageTableBound> MatchLoopCounter(const IR::Inst* phi) {
    if (phi->GetOpcode() != IR::Opcode::Phi) {
        return std::nullopt;
    }
    const IR::Inst* inc = nullptr;
    const IR::Block* latch = nullptr;
    bool init = false;
    for (size_t i = 0; i < phi->NumArgs(); ++i) {
        const IR::Value v = phi->Arg(i);
        if (v.IsImmediate()) {
            if (v.Type() != IR::Type::U32 || v.U32() != 0 || init) {
                return std::nullopt;
            }
            init = true;
            continue;
        }
        const IR::Inst* a = v.Inst();
        const bool is_inc = a->GetOpcode() == IR::Opcode::IAdd32 &&
                            ((a->Arg(0).TryInst() == phi && a->Arg(1).IsImmediate() &&
                              a->Arg(1).U32() == 1) ||
                             (a->Arg(1).TryInst() == phi && a->Arg(0).IsImmediate() &&
                              a->Arg(0).U32() == 1));
        if (!is_inc || (inc && inc != a) || (latch && latch != phi->PhiBlock(i))) {
            return std::nullopt;
        }
        inc = a;
        latch = phi->PhiBlock(i);
    }
    if (!init || !inc || !latch || latch->imm_successors.size() != 2) {
        return std::nullopt;
    }
    const IR::Block* header = phi->GetParent();
    const IR::Inst* cond = StripConditionRef(latch->branch_cond);
    bool back_when_true = true;
    while (cond && cond->GetOpcode() == IR::Opcode::LogicalNot) {
        back_when_true = !back_when_true;
        cond = StripConditionRef(cond->Arg(0));
    }
    if (!cond || cond->NumArgs() != 2) {
        return std::nullopt;
    }
    // the compare as "continue while": (opcode, counter operand, whether it means continue)
    const IR::Value lhs = cond->Arg(0), rhs = cond->Arg(1);
    const bool lhs_inc = lhs.TryInst() == inc, rhs_inc = rhs.TryInst() == inc;
    if (lhs_inc == rhs_inc) {
        return std::nullopt;
    }
    const IR::Value n = lhs_inc ? rhs : lhs;
    std::optional<LoopCompare> compare;
    bool continue_when_true = true;
    switch (cond->GetOpcode()) {
    case IR::Opcode::ULessThan32: // inc < N, or N < inc (exit when true... not a count)
        if (lhs_inc) compare = LoopCompare::ULess;
        break;
    case IR::Opcode::SLessThan32:
        if (lhs_inc) compare = LoopCompare::SLess;
        break;
    case IR::Opcode::UGreaterThan32: // N > inc
        if (rhs_inc) compare = LoopCompare::ULess;
        break;
    case IR::Opcode::SGreaterThan32:
        if (rhs_inc) compare = LoopCompare::SLess;
        break;
    case IR::Opcode::UGreaterThanEqual32: // inc >= N: exit
        if (lhs_inc) compare = LoopCompare::ULess, continue_when_true = false;
        break;
    case IR::Opcode::SGreaterThanEqual32:
        if (lhs_inc) compare = LoopCompare::SLess, continue_when_true = false;
        break;
    case IR::Opcode::ULessThanEqual32: // N <= inc: exit
        if (rhs_inc) compare = LoopCompare::ULess, continue_when_true = false;
        break;
    case IR::Opcode::SLessThanEqual32:
        if (rhs_inc) compare = LoopCompare::SLess, continue_when_true = false;
        break;
    case IR::Opcode::INotEqual32:
        compare = LoopCompare::NotEqual;
        break;
    case IR::Opcode::IEqual32:
        compare = LoopCompare::NotEqual, continue_when_true = false;
        break;
    default:
        break;
    }
    if (!compare) {
        return std::nullopt;
    }
    // imm_successors[0] is the block taken when the condition holds
    const bool back_on_true = continue_when_true == back_when_true;
    if (latch->imm_successors[back_on_true ? 0 : 1] != header ||
        latch->imm_successors[back_on_true ? 1 : 0] == header) {
        return std::nullopt;
    }
    const auto n_dw = FlattenedDword(n);
    if (!n_dw) {
        return std::nullopt;
    }
    ImageTableBound bound{};
    bound.kind = ImageTableKind::Loop;
    bound.count_dw = *n_dw;
    bound.compare = *compare;
    return bound;
}

// Tables already lowered in this shader, by their form.
struct TableKey {
    u32 buffer, first, stride, num_dwords;
    u32 index_buffer, index_first, index_stride; // Indexed (else ~0u)
    u32 count_dw, compare;                       // Loop (else ~0u)
    auto operator<=>(const TableKey&) const = default;
};
// One entry of a lowered table: its flatbuf dwords and the offset that selects it.
struct TableEntry {
    u32 dst{};      // flatbuf dword of T# dword 0
    u32 cmp_imm{};  // the T# dword offset that selects the entry, or
    u32 cmp_flat{}; // the flatbuf dword holding it (Indexed records; 0: cmp_imm)
};
struct LoweredTable {
    u32 index{}; // into Info::image_tables
    std::vector<TableEntry> entries;
};
struct DynamicImageTables {
    std::map<TableKey, LoweredTable> tables;
    // index buffer scans (buffer, first, stride) -> flatbuf dword of their CollectIndices result
    std::map<std::array<u32, 3>, u32> index_scans;
};

// Appends ops to the SRT program (before its End) and refreshes the compile-time flatbuf.
static void AppendSrtOps(Info& info, std::initializer_list<u32> ops, u32 new_flat_dwords) {
    auto& program = info.srt_info.program;
    if (!program.empty()) {
        ASSERT(program.back() == static_cast<u32>(SrtOp::End));
        program.pop_back();
    }
    program.insert(program.end(), ops);
    program.push_back(static_cast<u32>(SrtOp::End));
    info.srt_info.flattened_bufsize_dw += new_flat_dwords;
}

// PS4Recomp: an image read through a T# the shader loads from a table at run time: s_buffer_load
// / s_load of T# dword i at dword offset c + i + s * L of a buffer whose V# (or SRT pointer) is
// flattened, L a value only the shader knows (Uncharted 4: lights, records of 396 bytes indexed
// by the tile's light list; reflection probes, indexed by a probe record's texture index; a loop
// over N entries). The entries a draw can reach are copied into the flattened user data by the
// SRT program (SrtOp::CopyBufferDwords / CopyIndexedDwords), each an ordinary image binding: the
// renderer fetches, uploads, tracks and captures it like any other, and the shader is specialized
// to it. Which entries a draw reaches comes from a bound (ImageTableBound): the table buffer's V#
// (Records), the bounded buffer the index is read from (Indexed: one entry per distinct index its
// records hold, scanned by the SRT program, and the entry of index 0 that an index read past that
// buffer returns), or the count of the loop whose counter the index is (Loop). The bucket of every bound's entry count is part of the
// specialization (StageSpecialization::image_tables), so a draw reaching more entries gets
// another permutation. The shader runs the instruction once per entry and keeps the result of the
// entry whose offset equals the one it loaded the T# from: an exact address compare, so any other
// offset gives 0, the null image, as the all-zero T# an s_buffer_load past num_records returns.
// Returns false if the T# does not have that form, or no bound gives at most
// MaxImageTableEntries entries for this draw: the caller then binds the null image (logged and
// counted, Info::null_image_reads).
bool LowerDynamicImageTable(const ResourceDiscovery& resource, Info& info, Descriptors& descriptors,
                            DynamicImageTables& lowering, std::vector<IR::Inst*>& clones) {
    auto& tables = lowering.tables;
    IR::Inst& inst = *resource.user;
    const auto inst_info = inst.Flags<IR::TextureInstInfo>();
    const u32 pc = u32(inst_info.pc.Value());
    const IR::Opcode op = inst.GetOpcode();
    const bool is_sample = op == IR::Opcode::ImageSampleRaw;
    if (!is_sample && op != IR::Opcode::ImageRead) {
        return false;
    }
    const SharpReference& tsharp = resource.sharps[0];
    const auto site = MatchTableSite(tsharp);
    if (!site) {
        return false;
    }
    const auto table_src = BufferSource(info, site->buffer);
    if (!table_src) {
        return false;
    }
    // The S#: static (the same for every entry), or derived from T# dword 3 (aniso off for
    // single-level images): then one S# per entry, reading that entry's dword 3.
    const SharpReference& ssharp = resource.sharps[1];
    const bool sampler_per_entry =
        is_sample && ssharp.post_op == SharpFetchPostOp::DisableAnisoIfSingleLod;
    SamplerResource sampler_res{};
    if (is_sample) {
        sampler_res = SamplerResource{
            .sharp_fetch = ConstructSharpFetch<AmdGpu::Sampler>(ssharp),
            .post_op = ssharp.post_op,
            .post_op_tsharp_dw3_off = UNKNOWN_LOCATION,
            .is_depth = bool(inst_info.is_depth),
        };
        if (sampler_res.sharp_fetch.summary == SharpFetch<AmdGpu::Sampler>::Summary::Invalid) {
            return false;
        }
        if (sampler_per_entry && !(ssharp.post_op_data.lod_prod == tsharp.dwords[3])) {
            return false;
        }
    }

    // The bounds the table's form allows, in order of preference, until one fits this draw. The
    // table keeps only the bound it was made for (its bucket must stay the same for the shader to
    // be reused; the bounds before it do not matter once it fits), or every bound when none fits
    // (so that a draw one of them fits re-specializes).
    std::array<ImageTableBound, 3> bounds{};
    u32 num_bounds = 0;
    u32 chosen = ~0u, num_entries = 0, reached = 0;
    const auto try_bound = [&](const ImageTableBound& b) {
        bounds[num_bounds] = b;
        const u64 n = b.Entries(info.flattened_ud_buf.data());
        const u32 bucket_entries = ImageTableBucketEntries(ImageTableBucket(n));
        if (bucket_entries <= MaxImageTableEntries) {
            chosen = num_bounds;
            num_entries = bucket_entries;
            reached = static_cast<u32>(n);
        }
        ++num_bounds;
        return chosen != ~0u;
    };
    bool fits = false;
    if (table_src->size_imm == 0) {
        ImageTableBound b{};
        b.kind = ImageTableKind::Records;
        b.vs = table_src->vs;
        b.first_dw = site->first;
        b.stride_dw = site->stride;
        fits = try_bound(b);
    }
    std::optional<TableBufferSource> index_src;
    u32 index_buffer = ~0u, index_first = 0, index_stride = 0;
    if (const IR::Inst* rd = site->leaf; !fits && rd->GetOpcode() == IR::Opcode::ReadConstBuffer &&
                                         rd->Arg(0).IsImmediate()) {
        const auto a = EvalAffine(rd->Arg(1));
        if (a && a->leaf && a->mul != 0 && a->mul < 0x80000000u) {
            index_src = BufferSource(info, rd->Arg(0).U32());
        }
        if (index_src) {
            index_buffer = rd->Arg(0).U32();
            index_first = a->add;
            index_stride = a->mul;
            // the distinct indices the buffer's records hold, found by the SRT program
            const std::array<u32, 3> scan{index_buffer, index_first, index_stride};
            auto it = lowering.index_scans.find(scan);
            if (it == lowering.index_scans.end()) {
                const u32 dst = info.srt_info.flattened_bufsize_dw;
                AppendSrtOps(info,
                             {static_cast<u32>(SrtOp::CollectIndices), u32(index_src->vs),
                              index_src->size_imm, index_first * 4, index_stride * 4,
                              MaxImageTableIndexRecords, dst},
                             1 + SrtMaxTableIndices);
                info.RefreshFlatBuf();
                it = lowering.index_scans.emplace(scan, dst).first;
            }
            ImageTableBound b{};
            b.kind = ImageTableKind::Indexed;
            b.count_dw = static_cast<SharpLocation>(it->second);
            fits = try_bound(b);
        }
    }
    if (!fits) {
        if (const auto loop = MatchLoopCounter(site->leaf)) {
            fits = try_bound(*loop);
        }
    }
    if (num_bounds == 0) {
        LOG_ERROR(Render_Recompiler,
                  "{} at pc {:#x}: T# table in buffer {} (dword {} + {} * index): no bound for its "
                  "index ({})",
                  IR::NameOf(op), pc, site->buffer, site->first, site->stride,
                  IR::NameOf(site->leaf->GetOpcode()));
        return false;
    }
    if (fits) {
        // only the bound the entries are made for
        bounds[0] = bounds[chosen];
        num_bounds = 1;
        chosen = 0;
    }
    const ImageTableKind kind = fits ? bounds[chosen].kind : ImageTableKind::None;
    TableKey key{site->buffer, site->first, site->stride, site->num_dwords, ~0u, ~0u, ~0u, ~0u, ~0u};
    if (kind == ImageTableKind::Indexed) {
        key.index_buffer = index_buffer;
        key.index_first = index_first;
        key.index_stride = index_stride;
    } else if (kind == ImageTableKind::Loop) {
        key.count_dw = bounds[chosen].count_dw;
        key.compare = static_cast<u32>(bounds[chosen].compare);
    }

    // The table: found (another site reads it) or added, its entries copied by the SRT program.
    const u32 nd = site->num_dwords;
    auto it = tables.find(key);
    if (it != tables.end() && kind == ImageTableKind::None) {
        // a gap table: every bound of its sites, so that a draw one of them fits re-specializes
        ImageTableResource& t = info.image_tables[it->second.index];
        const auto same = [](const ImageTableBound& x, const ImageTableBound& y) {
            return x.kind == y.kind && x.vs == y.vs && x.size_imm == y.size_imm &&
                   x.first_dw == y.first_dw && x.stride_dw == y.stride_dw &&
                   x.count_dw == y.count_dw && x.compare == y.compare;
        };
        for (u32 i = 0; i < num_bounds; ++i) {
            bool have = false;
            for (u32 j = 0; j < t.num_bounds; ++j) {
                have |= same(t.bounds[j], bounds[i]);
            }
            if (!have && t.num_bounds < t.bounds.size()) {
                t.bounds[t.num_bounds++] = bounds[i];
            }
        }
    }
    if (it == tables.end()) {
        if (info.image_tables.size() == info.image_tables.capacity()) {
            LOG_ERROR(Render_Recompiler, "{} at pc {:#x}: too many T# tables", IR::NameOf(op), pc);
            return false;
        }
        ImageTableResource t{};
        t.kind = kind;
        t.table_buffer = site->buffer;
        t.first_dw = site->first;
        t.stride_dw = site->stride;
        t.num_dwords = nd;
        t.index_buffer = kind == ImageTableKind::Indexed ? index_buffer : ~0u;
        t.reached = reached;
        t.num_bounds = num_bounds;
        t.bounds = bounds;
        t.num_entries = num_entries;
        if (kind != ImageTableKind::None && info.images.size() + t.num_entries > NUM_IMAGES) {
            LOG_ERROR(Render_Recompiler, "{} at pc {:#x}: no image slots left for a T# table",
                      IR::NameOf(op), pc);
            t.kind = ImageTableKind::None;
            t.num_entries = 0;
        }
        if (t.kind != ImageTableKind::None && t.kind != ImageTableKind::Indexed &&
            (u64(site->first) + u64(site->stride) * (t.num_entries - 1) + nd) * 4 > 0xFFFFFFFFull) {
            // the byte offset of a record the entries copy does not fit the 32 bits the SRT
            // program (and s_buffer_load) offsets have
            LOG_ERROR(Render_Recompiler,
                      "{} at pc {:#x}: T# table in buffer {} (dword {} + {} * index): entry {} lies "
                      "past 4 GiB",
                      IR::NameOf(op), pc, site->buffer, site->first, site->stride, t.num_entries - 1);
            t.kind = ImageTableKind::None;
            t.num_entries = 0;
        }
        LoweredTable lowered{static_cast<u32>(info.image_tables.size()), {}};
        if (t.kind != ImageTableKind::None) {
            const u32 dst0 = info.srt_info.flattened_bufsize_dw;
            u32 next_cmp = dst0 + t.num_entries * nd;
            for (u32 k = 0; k < t.num_entries; ++k) {
                const u32 dst = dst0 + k * nd;
                if (kind == ImageTableKind::Indexed) {
                    // distinct index k (index 0 first; past the count: index 0 again)
                    const u32 idx = bounds[chosen].count_dw + 1 + k;
                    AppendSrtOps(info,
                                 {static_cast<u32>(SrtOp::CopyIndexedDwords), u32(table_src->vs),
                                  table_src->size_imm, site->first, site->stride, idx, nd, dst,
                                  next_cmp},
                                 0);
                    lowered.entries.push_back(TableEntry{dst, 0, next_cmp++});
                    continue;
                }
                const u32 guard = kind == ImageTableKind::Loop
                                      ? SrtTableGuard(bounds[chosen].count_dw, bounds[chosen].compare)
                                      : 0;
                AppendSrtOps(info,
                             {static_cast<u32>(SrtOp::CopyBufferDwords), u32(table_src->vs),
                              table_src->size_imm, (site->first + site->stride * k) * 4, nd, dst,
                              guard, k},
                             0);
                lowered.entries.push_back(TableEntry{dst, site->first + site->stride * k, 0});
            }
            info.srt_info.flattened_bufsize_dw = next_cmp;
            info.RefreshFlatBuf();
        }
        info.image_tables.push_back(t);
        it = tables.emplace(key, std::move(lowered)).first;
    }
    const u32 table_index = it->second.index;
    ImageTableResource& table = info.image_tables[table_index];
    table.num_sites++;
    if (table.kind == ImageTableKind::None) {
        LOG_ERROR(Render_Recompiler,
                  "{} at pc {:#x}: T# table in buffer {} (dword {} + {} * index): no bound gives at "
                  "most {} entries for this draw ({} by its first bound), not bound",
                  IR::NameOf(op), pc, site->buffer, site->first, site->stride, MaxImageTableEntries,
                  bounds[0].Entries(info.flattened_ud_buf.data()));
        return false;
    }
    const auto entry_image = [&](u32 k) {
        const TableEntry& e = it->second.entries[k];
        SharpFetch<AmdGpu::Image> fetch{};
        fetch.summary = SharpFetch<AmdGpu::Image>::Summary::SingleLoad;
        for (u32 i = 0; i < nd; ++i) {
            fetch.offsets[i] = static_cast<SharpLocation>(e.dst + i);
            fetch.load_mask |= u8(1u << i);
        }
        return ImageResource{
            .sharp_fetch = fetch,
            .is_depth = bool(inst_info.is_depth),
            .is_array = bool(inst_info.is_array),
            .is_r128 = bool(inst_info.is_r128),
            .post_op = tsharp.post_op,
            .table = static_cast<u8>(table_index),
            .table_entry = static_cast<u8>(k),
        };
    };
    const u32 n = static_cast<u32>(it->second.entries.size());

    // One instruction per entry, then the result of the entry the shader's offset selects.
    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
    const u32 shared_sampler = is_sample && !sampler_per_entry ? descriptors.Add(sampler_res) : 0;
    const IR::F32 zero = ir.Imm32(0.0f);
    std::array<IR::F32, 4> result{zero, zero, zero, zero};
    for (u32 k = 0; k < n; ++k) {
        const TableEntry& e = it->second.entries[k];
        const u32 image_binding = descriptors.Add(entry_image(k));
        u32 sampler_binding = shared_sampler;
        if (sampler_per_entry) {
            SamplerResource s = sampler_res;
            s.post_op_tsharp_dw3_off = static_cast<SharpLocation>(e.dst + 3);
            sampler_binding = descriptors.Add(s);
        }
        IR::Inst* clone = &*inst.GetParent()->PrependNewInst(
            IR::Block::InstructionList::s_iterator_to(inst), inst);
        clone->SetArg(0, ir.Imm32(is_sample ? (image_binding | sampler_binding << 16) : image_binding));
        clones.push_back(clone);
        IR::U32 cmp = ir.Imm32(e.cmp_imm);
        if (e.cmp_flat != 0) {
            IR::U32 flat = ir.ReadConst(ir.CompositeConstruct(ir.Imm32(0u), ir.Imm32(0u)), ir.Imm32(0u));
            flat.Inst()->SetFlags<u16>(static_cast<u16>(e.cmp_flat));
            cmp = flat;
        }
        const IR::U1 hit = ir.IEqual(IR::U32{site->offset0}, cmp);
        for (u32 c = 0; c < 4; ++c) {
            result[c] = IR::F32{ir.Select(hit, ir.CompositeExtract(IR::Value{clone}, c), result[c])};
        }
    }
    LOG_INFO(Render_Recompiler,
             "{} at pc {:#x}: T# table {} in buffer {} (dword {} + {} * index), {} entries bound ({}, "
             "{} reached by this draw)",
             IR::NameOf(op), pc, table_index, site->buffer, site->first, site->stride, n,
             table.kind == ImageTableKind::Records   ? "records"
             : table.kind == ImageTableKind::Indexed ? "indexed"
                                                     : "loop",
             table.reached);
    inst.ReplaceUsesWith(ir.CompositeConstruct(result[0], result[1], result[2], result[3]));
    return true;
}

static IR::Value IsMbcntWithExec(IR::Value value, bool is_hi, IR::U1 exec) {
    if (value.IsImmediate()) {
        return {};
    }
    const IR::Inst* inst = value.Inst();
    if (inst->GetOpcode() != IR::Opcode::MaskedBitCount32 || inst->Arg(0).IsImmediate() ||
        inst->Arg(2).U1() != is_hi) {
        return {};
    }
    IR::Inst* prod = inst->Arg(0).Inst();
    if (prod->GetOpcode() != IR::Opcode::CompositeExtractU32x2 || prod->Arg(0).IsImmediate() ||
        prod->Arg(1).U32() != is_hi) {
        return {};
    }
    prod = prod->Arg(0).Inst();
    if (prod->GetOpcode() != IR::Opcode::UnpackUint2x32 || prod->Arg(0).IsImmediate()) {
        return {};
    }
    prod = prod->Arg(0).Inst();
    if (prod->GetOpcode() != IR::Opcode::Ballot || (prod->Arg(0) != exec && !is_hi)) {
        return {};
    }
    return inst->Arg(1);
}

static IR::Use FindUniqueUser(IR::Inst* inst, auto&& pred) {
    IR::Use picked{};
    for (auto& use : inst->Uses()) {
        if (pred(use.user)) {
            ASSERT(!picked.user);
            picked = use;
        }
    }
    ASSERT(picked.user);
    return picked;
}

static void RemoveAppendBufferLaneOffset(IR::Inst& vx) {
    // Attempt to detect either of the following patterns and
    // remove the lane id addition/subtraction to ds instruction.
    //
    // ds_append       vX gds
    // v_mbcnt_hi_u32_b32 vY, exec_hi, vX
    // v_mbcnt_lo_u32_b32 idx, exec_lo, vY
    //
    // v_mbcnt_hi_u32_b32 vY, exec_hi, 0
    // v_mbcnt_lo_u32_b32 vZ, exec_lo, vY
    // ds_append       vX gds
    // v_add_i32       idx, vcc, vX, vZ
    //
    // ds_consume      vX gds
    // v_mbcnt_hi_u32_b32 vY, exec_hi, 0
    // v_mbcnt_lo_u32_b32 vZ, exec_lo, vY
    // v_sub_i32       vK, vcc, vX, vZ
    // v_subrev_i32    idx, vcc, 1, vK / v_add_i32    idx, vcc, -1, vK
    //
    // ds_consume      vX gds
    // v_mbcnt_hi_u32_b32 vY, exec_hi, 0
    // v_mbcnt_lo_u32_b32 vZ, exec_lo, vY
    // v_add_i32       vK, vcc, -1, vX
    // v_sub_i32       idx, vcc, vK, vZ

    IR::U1 exec{vx.Arg(1)};
    const auto uses = vx.Uses();
    for (auto use : uses) {
        const auto& [user, operand] = use;
        if (user->GetOpcode() == IR::Opcode::MaskedBitCount32) {
            // First pattern
            ASSERT(vx.GetOpcode() == IR::Opcode::DataAppend);
            IR::Inst* vy = user;
            auto vx_2 = IsMbcntWithExec(IR::Value{vy}, true, exec);
            ASSERT(!vx_2.IsEmpty() && vx_2 == IR::Value{&vx});
            const auto [idx, operand] = FindUniqueUser(vy, [](const IR::Inst* inst) {
                return inst->GetOpcode() == IR::Opcode::MaskedBitCount32;
            });
            auto vy_2 = IsMbcntWithExec(IR::Value{idx}, false, exec);
            ASSERT(!vy_2.IsEmpty() && vy_2 == IR::Value{vy});
            idx->ReplaceUsesWithAndRemove(IR::Value{&vx});
            continue;
        }
        if (user->GetOpcode() != IR::Opcode::IAdd32 && user->GetOpcode() != IR::Opcode::ISub32) {
            continue;
        }
        const auto other = user->Arg(1 - operand);
        if (other.IsImmediate()) {
            ASSERT_MSG(other.U32() == 1u || other.U32() == std::numeric_limits<u32>::max() &&
                                                vx.GetOpcode() == IR::Opcode::DataConsume,
                       "Unexpected constant offset {} to DataConsume result", other.U32());
            use = FindUniqueUser(user, [](const IR::Inst* inst) {
                return inst->GetOpcode() == IR::Opcode::IAdd32 ||
                       inst->GetOpcode() == IR::Opcode::ISub32;
            });
        }

        // Other patterns
        auto vz = user->Arg(1 - operand);
        auto vy = IsMbcntWithExec(vz, false, exec);
        ASSERT(!vy.IsEmpty() && !vy.IsImmediate());
        auto zero_const = IsMbcntWithExec(vy, true, exec);
        ASSERT(!zero_const.IsEmpty() && zero_const.IsImmediate() && zero_const.U32() == 0u);
        user->SetArg(1 - operand, IR::Value{u32{0u}});
    }
}

void PatchGlobalDataShareAccess(IR::Inst& inst, Info& info, Descriptors& descriptors,
                                const Profile& profile) {
    const u32 binding = descriptors.Add(BufferResource{
        .used_types = IR::Type::U32,
        .buffer_type = BufferType::GdsBuffer,
        .is_written = true,
    });

    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};

    if (inst.GetOpcode() == IR::Opcode::DataAppend || inst.GetOpcode() == IR::Opcode::DataConsume) {
        RemoveAppendBufferLaneOffset(inst);
        const IR::Value replacement =
            inst.GetOpcode() == IR::Opcode::DataAppend
                ? ir.BufferAtomicIAdd(ir.Imm32(binding), inst.Arg(0), ir.Imm32(1u), {})
                : ir.BufferAtomicISub(ir.Imm32(binding), inst.Arg(0), ir.Imm32(1u), {});
        inst.ReplaceUsesWithAndRemove(replacement);
        return;
    }

    // Convert shared memory opcode to storage buffer atomic to GDS buffer.
    auto& buffer = info.buffers[binding];
    const IR::U32 offset = IR::U32{inst.Arg(0)};
    const IR::U32 address_words = ir.ShiftRightLogical(offset, ir.Imm32(1));
    const IR::U32 address_dwords = ir.ShiftRightLogical(offset, ir.Imm32(2));
    const IR::U32 address_qwords = ir.ShiftRightLogical(offset, ir.Imm32(3));
    const IR::U32 handle = ir.Imm32(binding);
    switch (inst.GetOpcode()) {
    case IR::Opcode::SharedAtomicIAdd32:
        inst.ReplaceUsesWith(ir.BufferAtomicIAdd(handle, address_dwords, inst.Arg(1), {}));
        break;
    case IR::Opcode::SharedAtomicIAdd64:
        inst.ReplaceUsesWith(ir.BufferAtomicIAdd(handle, address_qwords, IR::U64{inst.Arg(1)}, {}));
        break;
    case IR::Opcode::SharedAtomicISub32:
        inst.ReplaceUsesWith(ir.BufferAtomicISub(handle, address_dwords, inst.Arg(1), {}));
        break;
    case IR::Opcode::SharedAtomicSMin32:
    case IR::Opcode::SharedAtomicUMin32: {
        const bool is_signed = inst.GetOpcode() == IR::Opcode::SharedAtomicSMin32;
        inst.ReplaceUsesWith(
            ir.BufferAtomicIMin(handle, address_dwords, inst.Arg(1), is_signed, {}));
        break;
    }
    case IR::Opcode::SharedAtomicSMax32:
    case IR::Opcode::SharedAtomicUMax32: {
        const bool is_signed = inst.GetOpcode() == IR::Opcode::SharedAtomicSMax32;
        inst.ReplaceUsesWith(
            ir.BufferAtomicIMax(handle, address_dwords, inst.Arg(1), is_signed, {}));
        break;
    }
    case IR::Opcode::SharedAtomicInc32:
        inst.ReplaceUsesWith(ir.BufferAtomicInc(handle, address_dwords, {}));
        break;
    case IR::Opcode::SharedAtomicDec32:
        inst.ReplaceUsesWith(ir.BufferAtomicDec(handle, address_dwords, {}));
        break;
    case IR::Opcode::SharedAtomicAnd32:
        inst.ReplaceUsesWith(ir.BufferAtomicAnd(handle, address_dwords, inst.Arg(1), {}));
        break;
    case IR::Opcode::SharedAtomicOr32:
        inst.ReplaceUsesWith(ir.BufferAtomicOr(handle, address_dwords, inst.Arg(1), {}));
        break;
    case IR::Opcode::SharedAtomicXor32:
        inst.ReplaceUsesWith(ir.BufferAtomicXor(handle, address_dwords, inst.Arg(1), {}));
        break;
    case IR::Opcode::SharedAtomicCmpSwap32:
        // Args are (address, value, cmp_value)
        inst.ReplaceUsesWith(
            ir.BufferAtomicCmpSwap(handle, address_dwords, inst.Arg(1), inst.Arg(2), {}));
        break;
    case IR::Opcode::LoadSharedU16: {
        inst.ReplaceUsesWith(ir.LoadBufferU16(handle, address_words, {}));
        buffer.used_types |= IR::Type::U16;
        break;
    }
    case IR::Opcode::LoadSharedU32:
        inst.ReplaceUsesWith(ir.LoadBufferU32(1, handle, address_dwords, {}));
        break;
    case IR::Opcode::LoadSharedU64: {
        inst.ReplaceUsesWith(ir.LoadBufferU64(handle, address_qwords, {}));
        buffer.used_types |= IR::Type::U64;
        break;
    }
    case IR::Opcode::WriteSharedU16: {
        ir.StoreBufferU16(handle, address_words, IR::U16{inst.Arg(1)}, {});
        inst.Invalidate();
        buffer.used_types |= IR::Type::U16;
        break;
    }
    case IR::Opcode::WriteSharedU32:
        ir.StoreBufferU32(1, handle, address_dwords, inst.Arg(1), {});
        inst.Invalidate();
        break;
    case IR::Opcode::WriteSharedU64: {
        ir.StoreBufferU64(handle, address_qwords, IR::U64{inst.Arg(1)}, {});
        inst.Invalidate();
        buffer.used_types |= IR::Type::U64;
        break;
    }
    default:
        UNREACHABLE_MSG("Unexpected opcode {}", inst.GetOpcode());
    }
}

IR::U32 CalculateBufferAddress(IR::IREmitter& ir, const IR::Inst& inst, const Info& info,
                               const AmdGpu::Buffer& buffer, u32 stride) {
    const auto inst_info = inst.Flags<IR::BufferInstInfo>();
    const u32 inst_offset = inst_info.inst_offset.Value();
    const auto is_inst_typed = inst_info.inst_data_fmt != AmdGpu::DataFormat::FormatInvalid;
    const auto data_format = is_inst_typed
                                 ? AmdGpu::RemapDataFormat(inst_info.inst_data_fmt.Value())
                                 : buffer.GetDataFmt();
    const u32 shift = BufferAddressShift(inst, data_format);
    const u32 mask = (1 << shift) - 1;
    const IR::U32 soffset = IR::GetBufferSOffsetArg(&inst);

    // If address calculation is of the form "index * const_stride + offset" with offset constant
    // and both const_stride and offset are divisible with the element size, apply shift directly.
    if (inst_info.index_enable && !inst_info.voffset_enable && soffset.IsImmediate() &&
        !buffer.swizzle_enable && !buffer.add_tid_enable && (stride & mask) == 0) {
        const u32 total_offset = soffset.U32() + inst_offset;
        if ((total_offset & mask) == 0) {
            // buffer_offset = index * (const_stride >> shift) + (offset >> shift)
            const IR::U32 index = IR::GetBufferIndexArg(&inst);
            return ir.IAdd(ir.IMul(index, ir.Imm32(stride >> shift)),
                           ir.Imm32(total_offset >> shift));
        }
    }

    // index = (inst_idxen ? vgpr_index : 0) + (const_add_tid_enable ? thread_id[5:0] : 0)
    IR::U32 index = ir.Imm32(0U);
    if (inst_info.index_enable) {
        const IR::U32 vgpr_index = IR::GetBufferIndexArg(&inst);
        index = ir.IAdd(index, vgpr_index);
    }
    if (buffer.add_tid_enable) {
        ASSERT_MSG(info.sw_stage == SwStage::Compute,
                   "Thread ID buffer addressing is not supported outside of compute (V# base {:#x} "
                   "stride {} num_records {:#x} dword3 {:#010x}).",
                   u64(buffer.base_address), buffer.GetStride(), buffer.num_records,
                   reinterpret_cast<const u32*>(&buffer)[3]);
        const IR::U32 thread_id{ComputeWaveLane(ir)};
        index = ir.IAdd(index, thread_id);
    }
    // offset = (inst_offen ? vgpr_offset : 0) + inst_offset
    IR::U32 offset = ir.Imm32(inst_offset);
    offset = ir.IAdd(offset, soffset);
    if (inst_info.voffset_enable) {
        const IR::U32 voffset = IR::GetBufferVOffsetArg(&inst);
        offset = ir.IAdd(offset, voffset);
    }
    const IR::U32 const_stride = ir.Imm32(stride);
    IR::U32 buffer_offset;
    if (buffer.swizzle_enable) {
        const IR::U32 const_index_stride = ir.Imm32(buffer.GetIndexStride());
        const IR::U32 const_element_size = ir.Imm32(buffer.GetElementSize());
        // index_msb = index / const_index_stride
        const IR::U32 index_msb{ir.IDiv(index, const_index_stride)};
        // index_lsb = index % const_index_stride
        const IR::U32 index_lsb{ir.IMod(index, const_index_stride)};
        // offset_msb = offset / const_element_size
        const IR::U32 offset_msb{ir.IDiv(offset, const_element_size)};
        // offset_lsb = offset % const_element_size
        const IR::U32 offset_lsb{ir.IMod(offset, const_element_size)};
        // buffer_offset =
        //     (index_msb * const_stride + offset_msb * const_element_size) * const_index_stride
        //     + index_lsb * const_element_size + offset_lsb
        const IR::U32 buffer_offset_msb = ir.IMul(
            ir.IAdd(ir.IMul(index_msb, const_stride), ir.IMul(offset_msb, const_element_size)),
            const_index_stride);
        const IR::U32 buffer_offset_lsb =
            ir.IAdd(ir.IMul(index_lsb, const_element_size), offset_lsb);
        buffer_offset = ir.IAdd(buffer_offset_msb, buffer_offset_lsb);
    } else {
        // buffer_offset = index * const_stride + offset
        buffer_offset = ir.IAdd(ir.IMul(index, const_stride), offset);
    }
    if (shift != 0) {
        buffer_offset = ir.ShiftRightLogical(buffer_offset, ir.Imm32(shift));
    }
    return buffer_offset;
}

void PatchBufferArgs(IR::Inst& inst, Info& info) {
    const auto handle = inst.Arg(0);
    const auto buffer_res = info.buffers[handle.U32()];
    const auto buffer = buffer_res.GetSharp(info);

    // Address of constant buffer reads can be calculated at IR emission time.
    if (inst.GetOpcode() == IR::Opcode::ReadConstBuffer) {
        return;
    }

    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
    inst.SetArg(IR::LoadBufferArgs::Address,
                CalculateBufferAddress(ir, inst, info, buffer, buffer.stride));
}

void PatchImageSampleArgs(IR::Inst& inst, Info& info, const ImageResource& image_res,
                          const AmdGpu::Image& image) {
    const auto handle = inst.Arg(0);
    const auto& sampler_res = info.samplers[(handle.U32() >> 16) & 0xFFFF];
    const auto sampler = sampler_res.GetSharp(info);

    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
    const auto inst_info = inst.Flags<IR::TextureInstInfo>();
    const auto view_type = image.GetViewType(image_res.is_array);
    // PS4Recomp: the address VGPRs follow the instruction's type; a 1D image read as 2D of height
    // 1 gets y = 0.5 (the centre of its one row), a zero y offset and zero y derivatives
    const auto addr_type = AddressType(image, image_res.is_array);
    const bool as_2d = image.Is1DAs2D();

    IR::Inst* body1 = inst.Arg(2).Inst();
    IR::Inst* body2 = inst.Arg(3).Inst();
    IR::Inst* body3 = inst.Arg(4).Inst();
    IR::F32 body4 = IR::F32{inst.Arg(5)};
    const auto get_addr_reg = [&](u32 index) -> IR::F32 {
        if (index <= 3) {
            return IR::F32{body1->Arg(index)};
        }
        if (index >= 4 && index <= 7) {
            return IR::F32{body2->Arg(index - 4)};
        }
        if (index >= 8 && index <= 11) {
            return IR::F32{body3->Arg(index - 8)};
        }
        if (index == 12) {
            return body4;
        }
        UNREACHABLE();
    };
    u32 addr_reg = 0;

    // Load first address components as denoted in 8.2.4 VGPR Usage Sea Islands Series Instruction
    // Set Architecture
    const IR::Value offset = [&] -> IR::Value {
        if (!inst_info.has_offset) {
            return IR::U32{};
        }

        // The offsets are six-bit signed integers: X=[5:0], Y=[13:8], and Z=[21:16].
        IR::Value arg = get_addr_reg(addr_reg++);
        if (const IR::Inst* offset_inst = arg.TryInst()) {
            ASSERT(offset_inst->GetOpcode() == IR::Opcode::BitCastF32U32);
            arg = offset_inst->Arg(0);
        }

        const auto read = [&](u32 off) -> IR::U32 {
            if (arg.IsImmediate()) {
                const u32 imm =
                    arg.Type() == IR::Type::F32 ? std::bit_cast<u32>(arg.F32()) : arg.U32();
                const u16 comp = (imm >> off) & 0x3F;
                return ir.Imm32(s32(comp << 26) >> 26);
            }
            return ir.BitFieldExtract(IR::U32{arg}, ir.Imm32(off), ir.Imm32(6), true);
        };

        switch (addr_type) {
        case AmdGpu::ImageType::Color1D:
        case AmdGpu::ImageType::Color1DArray:
            return as_2d ? ir.CompositeConstruct(read(0), ir.Imm32(0u)) : IR::Value{read(0)};
        case AmdGpu::ImageType::Color2D:
        case AmdGpu::ImageType::Color2DMsaa:
        case AmdGpu::ImageType::Color2DArray:
        case AmdGpu::ImageType::Cube:
            return ir.CompositeConstruct(read(0), read(8));
        case AmdGpu::ImageType::Color3D:
            return ir.CompositeConstruct(read(0), read(8), read(16));
        default:
            UNREACHABLE();
        }
    }();
    const IR::F32 bias = inst_info.has_bias ? get_addr_reg(addr_reg++) : IR::F32{};
    const IR::F32 dref = inst_info.is_depth ? get_addr_reg(addr_reg++) : IR::F32{};
    const auto [derivatives_dx, derivatives_dy] = [&] -> std::pair<IR::Value, IR::Value> {
        if (!inst_info.has_derivatives) {
            return {};
        }
        switch (addr_type) {
        case AmdGpu::ImageType::Color1D:
        case AmdGpu::ImageType::Color1DArray:
            // du/dx, du/dy
            addr_reg = addr_reg + 2;
            if (as_2d) {
                return {ir.CompositeConstruct(get_addr_reg(addr_reg - 2), ir.Imm32(0.f)),
                        ir.CompositeConstruct(get_addr_reg(addr_reg - 1), ir.Imm32(0.f))};
            }
            return {get_addr_reg(addr_reg - 2), get_addr_reg(addr_reg - 1)};
        case AmdGpu::ImageType::Color2D:
        case AmdGpu::ImageType::Color2DMsaa:
        case AmdGpu::ImageType::Color2DArray:
        case AmdGpu::ImageType::Cube:
            // (du/dx, dv/dx), (du/dy, dv/dy)
            addr_reg = addr_reg + 4;
            return {ir.CompositeConstruct(get_addr_reg(addr_reg - 4), get_addr_reg(addr_reg - 3)),
                    ir.CompositeConstruct(get_addr_reg(addr_reg - 2), get_addr_reg(addr_reg - 1))};
        case AmdGpu::ImageType::Color3D:
            // (du/dx, dv/dx, dw/dx), (du/dy, dv/dy, dw/dy)
            addr_reg = addr_reg + 6;
            return {ir.CompositeConstruct(get_addr_reg(addr_reg - 6), get_addr_reg(addr_reg - 5),
                                          get_addr_reg(addr_reg - 4)),
                    ir.CompositeConstruct(get_addr_reg(addr_reg - 3), get_addr_reg(addr_reg - 2),
                                          get_addr_reg(addr_reg - 1))};
        default:
            UNREACHABLE();
        }
    }();

    const bool is_msaa = view_type == AmdGpu::ImageType::Color2DMsaa ||
                         view_type == AmdGpu::ImageType::Color2DMsaaArray;
    const bool unnormalized = sampler.force_unnormalized || inst_info.is_unnormalized;
    const bool needs_dimentions = (!is_msaa && unnormalized) || (is_msaa && !unnormalized);
    const auto dimensions =
        needs_dimentions ? ir.ImageQueryDimension(handle, ir.Imm32(0u), ir.Imm1(false), inst_info)
                         : IR::Value{};
    const auto get_coord = [&](u32 coord_idx, u32 dim_idx) -> IR::Value {
        const auto coord = get_addr_reg(coord_idx);
        if (is_msaa) {
            // For MSAA images preserve the unnormalized coord or manually unnormalize it
            if (unnormalized) {
                return ir.ConvertFToU(32, coord);
            } else {
                const auto dim =
                    ir.ConvertUToF(32, 32, IR::U32{ir.CompositeExtract(dimensions, dim_idx)});
                return ir.ConvertFToU(32, ir.FPMul(coord, dim));
            }
        }
        if (unnormalized) {
            // Normalize the coordinate for sampling, dividing by its corresponding dimension.
            const auto dim =
                ir.ConvertUToF(32, 32, IR::U32{ir.CompositeExtract(dimensions, dim_idx)});
            return ir.FPDiv(coord, dim);
        }
        return coord;
    };

    // Now we can load body components as noted in Table 8.9 Image Opcodes with Sampler
    const IR::Value coords = [&] -> IR::Value {
        switch (addr_type) {
        case AmdGpu::ImageType::Color1D: // x
            addr_reg = addr_reg + 1;
            if (as_2d) {
                return ir.CompositeConstruct(get_coord(addr_reg - 1, 0), ir.Imm32(0.5f));
            }
            return get_coord(addr_reg - 1, 0);
        case AmdGpu::ImageType::Color1DArray: // x, slice
            addr_reg = addr_reg + 2;
            if (as_2d) {
                // the slice is the 2D array's third coordinate (and dimension)
                return ir.CompositeConstruct(get_coord(addr_reg - 2, 0), ir.Imm32(0.5f),
                                             get_coord(addr_reg - 1, 2));
            }
            return ir.CompositeConstruct(get_coord(addr_reg - 2, 0), get_coord(addr_reg - 1, 1));
        case AmdGpu::ImageType::Color2D:      // x, y
        case AmdGpu::ImageType::Color2DMsaa:  // x, y
            addr_reg = addr_reg + 2;
            return ir.CompositeConstruct(get_coord(addr_reg - 2, 0), get_coord(addr_reg - 1, 1));
        case AmdGpu::ImageType::Color2DArray: // x, y, slice
        case AmdGpu::ImageType::Cube:         // x, y, face
        case AmdGpu::ImageType::Color3D:      // x, y, z
            addr_reg = addr_reg + 3;
            return ir.CompositeConstruct(get_coord(addr_reg - 3, 0), get_coord(addr_reg - 2, 1),
                                         get_coord(addr_reg - 1, 2));
        default:
            UNREACHABLE();
        }
    }();

    ASSERT(!inst_info.has_lod || !inst_info.has_lod_clamp);
    const bool explicit_lod = inst_info.has_lod || inst_info.force_level0;
    const IR::F32 lod = inst_info.has_lod        ? get_addr_reg(addr_reg++)
                        : inst_info.force_level0 ? ir.Imm32(0.0f)
                                                 : IR::F32{};
    const IR::F32 lod_clamp = inst_info.has_lod_clamp ? get_addr_reg(addr_reg++) : IR::F32{};

    auto texel = [&] -> IR::Value {
        if (is_msaa) {
            return ir.ImageRead(handle, coords, {}, ir.Imm32(0U), inst_info);
        }
        if (inst_info.is_gather) {
            if (inst_info.is_depth) {
                return ir.ImageGatherDref(handle, coords, offset, dref, inst_info);
            }
            return ir.ImageGather(handle, coords, offset, inst_info);
        }
        if (inst_info.has_derivatives) {
            return ir.ImageGradient(handle, coords, derivatives_dx, derivatives_dy, offset,
                                    lod_clamp, inst_info);
        }
        if (inst_info.is_depth) {
            if (explicit_lod) {
                return ir.ImageSampleDrefExplicitLod(handle, coords, dref, lod, offset, inst_info);
            }
            return ir.ImageSampleDrefImplicitLod(handle, coords, dref, bias, offset, inst_info);
        }
        if (explicit_lod) {
            return ir.ImageSampleExplicitLod(handle, coords, lod, offset, inst_info);
        }
        return ir.ImageSampleImplicitLod(handle, coords, bias, offset, inst_info);
    }();

    auto converted = ApplyReadNumberConversionVec4(ir, texel, image.GetNumberConversion());
    if (sampler.force_degamma && image.GetNumberFmt() != AmdGpu::NumberFormat::Srgb) {
        converted = ApplyForceDegamma(ir, texel);
    }
    inst.ReplaceUsesWith(converted);
}

void PatchImageArgs(IR::Inst& inst, Info& info) {
    const auto image_handle = inst.Arg(0);
    const auto binding_index = image_handle.U32() & 0xFFFF;
    const auto& image_res = info.images[binding_index];
    auto image = image_res.GetSharp(info);

    if (inst.GetOpcode() == IR::Opcode::ImageQueryDimensions) {
        // PS4Recomp: a 1D image read as 2D of height 1 answers as a 1D image: (width, 0, 0,
        // levels), (width, layers, 0, levels) for an array, where the 2D query gives (width, 1,
        // 0, levels) and (width, 1, layers, levels). Nothing to patch for the others.
        if (!image.Is1DAs2D()) {
            return;
        }
        IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
        const IR::Value dims = ir.ImageQueryDimension(image_handle, IR::U32{inst.Arg(1)},
                                                      IR::U1{inst.Arg(2)},
                                                      inst.Flags<IR::TextureInstInfo>());
        const bool array = image.GetViewType(image_res.is_array) == AmdGpu::ImageType::Color2DArray;
        const IR::Value zero{ir.Imm32(0u)};
        inst.ReplaceUsesWith(ir.CompositeConstruct(
            ir.CompositeExtract(dims, 0), array ? ir.CompositeExtract(dims, 2) : zero,
            zero, ir.CompositeExtract(dims, 3)));
        return;
    }

    // Sample instructions must be handled separately using address register data.
    if (inst.GetOpcode() == IR::Opcode::ImageSampleRaw) {
        return PatchImageSampleArgs(inst, info, image_res, image);
    }

    IR::IREmitter ir{*inst.GetParent(), IR::Block::InstructionList::s_iterator_to(inst)};
    auto inst_info = inst.Flags<IR::TextureInstInfo>();
    const auto view_type = image.GetViewType(image_res.is_array);
    // PS4Recomp: the address VGPRs follow the instruction's type; a 1D image read as 2D of height
    // 1 gets y = 0 (its one row; 0.5, the row's centre, for the float coordinates of a LOD query)
    const auto addr_type = AddressType(image, image_res.is_array);
    const bool as_2d = image.Is1DAs2D();
    const IR::Value row = inst.GetOpcode() == IR::Opcode::ImageQueryLod ? IR::Value{ir.Imm32(0.5f)}
                                                                        : IR::Value{ir.Imm32(0u)};

    // Now that we know the image type, adjust texture coordinate vector.
    IR::Inst* body = inst.Arg(1).Inst();
    const auto [coords, arg] = [&] -> std::pair<IR::Value, IR::Value> {
        switch (addr_type) {
        case AmdGpu::ImageType::Color1D: // x, [lod]
            if (as_2d) {
                return {ir.CompositeConstruct(body->Arg(0), row), body->Arg(1)};
            }
            return {body->Arg(0), body->Arg(1)};
        case AmdGpu::ImageType::Color1DArray: // x, slice, [lod]
            if (as_2d) {
                return {ir.CompositeConstruct(body->Arg(0), row, body->Arg(1)), body->Arg(2)};
            }
            return {ir.CompositeConstruct(body->Arg(0), body->Arg(1)), body->Arg(2)};
        case AmdGpu::ImageType::Color2D:      // x, y, [lod]
            return {ir.CompositeConstruct(body->Arg(0), body->Arg(1)), body->Arg(2)};
        case AmdGpu::ImageType::Color2DMsaa: // x, y. (sample is passed on different argument)
        {
            auto skip_lod = false;
            if (inst_info.has_lod) {
                const auto mipid = body->Arg(2);
                if (mipid.IsImmediate() && mipid.U32() == 0) {
                    // if image_x_mip refers to a MSAA image, and mipid is 0, it is safe to be
                    // skipped and fragid is taken from the next arg
                    LOG_WARNING(Render_Recompiler, "Encountered a _mip instruction with MSAA "
                                                   "image, and mipid is 0, skipping LoD");
                    inst_info.has_lod.Assign(false);
                    skip_lod = true;
                } else {
                    UNREACHABLE_MSG(
                        "Encountered a _mip instruction with MSAA image, and mipid is non-zero");
                }
            }
            return {ir.CompositeConstruct(body->Arg(0), body->Arg(1)), body->Arg(skip_lod ? 3 : 2)};
        }
        case AmdGpu::ImageType::Color2DArray:     // x, y, slice, [lod]
        case AmdGpu::ImageType::Color2DMsaaArray: // x, y, slice. (sample is passed on different
                                                  // argument)
        case AmdGpu::ImageType::Cube:             // x, y, face, [lod]
        case AmdGpu::ImageType::Color3D:          // x, y, z, [lod]
            return {ir.CompositeConstruct(body->Arg(0), body->Arg(1), body->Arg(2)), body->Arg(3)};
        default:
            UNREACHABLE_MSG("Unknown image type {}", view_type);
        }
    }();

    const auto has_ms = view_type == AmdGpu::ImageType::Color2DMsaa ||
                        view_type == AmdGpu::ImageType::Color2DMsaaArray;
    ASSERT(!inst_info.has_lod || !has_ms);
    // If we are binding a single mip level as fallback, drop the argument
    const auto lod =
        (inst_info.has_lod && image_res.mip_fallback_mode != MipStorageFallbackMode::ConstantIndex)
            ? IR::U32{arg}
            : IR::U32{};
    const auto ms = has_ms ? IR::U32{arg} : IR::U32{};

    const auto is_storage = image_res.is_written;
    if (inst.GetOpcode() == IR::Opcode::ImageRead) {
        auto texel = ir.ImageRead(image_handle, coords, lod, ms, inst_info);
        if (is_storage) {
            // Storage image requires shader swizzle.
            texel = ApplySwizzle(ir, texel, image.DstSelect());
        }
        auto converted = ApplyReadNumberConversionVec4(ir, texel, image.GetNumberConversion());
        if (inst_info.is_d16 && !AmdGpu::IsInteger(image.GetNumberFmt())) {
            // D16 loads of float data return half floats: hand the translator the half bits
            // (it packs the low 16 bits of two components into one VGPR).
            std::array<IR::Value, 4> halves;
            for (u32 i = 0; i < 4; ++i) {
                const IR::F32 c{ir.CompositeExtract(converted, i)};
                halves[i] = ir.BitCast<IR::F32>(
                    IR::U32{ir.UConvert(32, ir.BitCast<IR::U16>(IR::F16{ir.FPConvert(16, c)}))});
            }
            converted = ir.CompositeConstruct(halves[0], halves[1], halves[2], halves[3]);
        }
        inst.ReplaceUsesWith(converted);
    } else {
        inst.SetArg(1, coords);
        if (inst.GetOpcode() == IR::Opcode::ImageWrite) {
            inst.SetArg(2, lod);
            inst.SetArg(3, ms);

            auto texel = inst.Arg(4);
            if (is_storage) {
                // Storage image requires shader swizzle.
                texel = ApplySwizzle(ir, texel, image.DstSelect().Inverse());
            }
            const auto converted =
                ApplyWriteNumberConversionVec4(ir, texel, image.GetNumberConversion());
            inst.SetArg(4, converted);
        }
    }
}

void ResourcePatchingPass(Shader::Info& info, const ResourceDiscoveryList& resources,
                          const Profile& profile) {
    // Iterate over discovered resources and patch them after finding the sharp.
    // Pass 1: Track resource sharps
    Descriptors descriptors{info};
    // PS4Recomp: accesses through V#s loaded at run time become guest memory accesses
    boost::container::small_vector<bool, 32> lowered(resources.size(), false);
    boost::container::small_vector<size_t, 8> deferred;
    for (size_t i = 0; i < resources.size(); ++i) {
        const auto& usage = resources[i];
        IR::Inst& inst = *usage.user;
        if (IsBufferInstruction(inst)) {
            // ADDR64 accesses address guest memory through a 64-bit VGPR pointer: no binding
            // stands for them either
            const bool addr64 = inst.GetOpcode() != IR::Opcode::ReadConstBuffer &&
                                inst.Flags<IR::BufferInstInfo>().addr64;
            if (addr64 || !IsFlattenedSharp(usage.sharps[0])) {
                LowerDynamicBufferAccess(inst, info);
                lowered[i] = true;
                continue;
            }
            PatchBufferSharp(usage, info, descriptors, profile);
        } else if (IsImageInstruction(inst)) {
            if (!PatchImageSharp(usage, info, descriptors, profile, true)) {
                deferred.push_back(i);
            }
        }
    }
    // PS4Recomp: reads through T#s the shader loads from a table at run time, once every buffer
    // has its binding (the table's); the others read the null image (PatchImageSharp)
    std::vector<IR::Inst*> table_clones;
    DynamicImageTables tables;
    for (const size_t i : deferred) {
        if (LowerDynamicImageTable(resources[i], info, descriptors, tables, table_clones)) {
            lowered[i] = true;
        } else {
            PatchImageSharp(resources[i], info, descriptors, profile);
        }
    }

    // Pass 2: Patch instruction args
    for (size_t i = 0; i < resources.size(); ++i) {
        const auto& usage = resources[i];
        IR::Inst& inst = *usage.user;
        if (lowered[i]) {
            continue;
        }
        if (IsBufferInstruction(inst)) {
            PatchBufferArgs(inst, info);
        } else if (IsImageInstruction(inst)) {
            PatchImageArgs(inst, info);
        } else if (IsDataRingInstruction(inst)) {
            PatchGlobalDataShareAccess(inst, info, descriptors, profile);
        }
    }
    for (IR::Inst* clone : table_clones) {
        PatchImageArgs(*clone, info);
    }
}

} // namespace Shader::Optimization
