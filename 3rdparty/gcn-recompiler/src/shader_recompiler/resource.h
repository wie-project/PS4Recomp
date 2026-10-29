// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bit>
#include "common/types.h"
#include "shader_recompiler/ir/type.h"
#include "video_core/amdgpu/resource.h"

#include <boost/container/static_vector.hpp>

namespace Shader {

static constexpr u32 NUM_USER_DATA_REGS = 16;
static constexpr u32 NUM_IMAGES = 64;
static constexpr u32 NUM_BUFFERS = 56;
static constexpr u32 NUM_SAMPLERS = 16;
static constexpr u32 NUM_FMASKS = 8;

// PS4Recomp: guest memory reached by buffer accesses through V#s the shader loads at run time
// (Info::uses_guest_memory). Mirrored as GCN_GUEST_* in include/gcn_recompiler.h.
namespace GuestMemory {
// BufferType::BdaPagetable: one u64 per 64 MiB page of the 40-bit guest address space, the GPU
// address of host memory holding that page (0: not mapped)
constexpr u32 PageShift = 26;
constexpr u32 NumPages = 1u << (40 - PageShift);
// read / written pages are tracked at 1 MiB
// Guest addresses below this are never mapped (the host's NULL guard area); page 0's table
// entry is the GPU address its host memory would have at guest address 0.
constexpr u64 FirstMappedAddress = 0x10000;
constexpr u32 TrackShift = 20;
constexpr u32 NumTrackPages = 1u << (40 - TrackShift);
// BufferType::FaultBuffer dwords
constexpr u32 ModeDword = 0;     // 1: probe (nothing is written), 0: the real dispatch
constexpr u32 StatusDword = 1;   // Status* bits
constexpr u32 UnmappedDword = 4; // bit p: page p was accessed while its page table entry was 0
constexpr u32 ReadDword = UnmappedDword + NumPages / 32;       // bit t: 1 MiB page t was read
constexpr u32 WrittenDword = ReadDword + NumTrackPages / 32;   // bit t: ... written (or would be)
constexpr u32 FaultDwords = WrittenDword + NumTrackPages / 32;
constexpr u32 StatusUnmapped = 1u << 0; // an access outside a probe hit an unmapped page (skipped)
constexpr u32 StatusFormat = 1u << 1;   // a V# with a data/number format the shader cannot convert
constexpr u32 StatusSwizzle = 1u << 2;  // a swizzled V# (swizzle_enable)
constexpr u32 StatusRange = 1u << 3;    // an address at or above 2^40, or below FirstMappedAddress
constexpr u32 StatusUnaligned = 1u << 4; // a dword access at an address that is not 4-aligned
} // namespace GuestMemory

using SharpLocation = u16;

constexpr SharpLocation UNKNOWN_LOCATION = std::numeric_limits<u16>::max();

template <typename T>
struct SharpFetch {
    static constexpr std::size_t N = sizeof(T) / sizeof(u32);
    static_assert(N <= 8);

    enum class Summary : u8 {
        SingleLoad,
        MultiLoad,
        Invalid,
    };

    Summary summary{Summary::MultiLoad};
    std::array<u32, N> immediates;
    std::array<SharpLocation, N> offsets;
    u8 load_mask;

    bool operator==(const SharpFetch&) const = default;

    template <u32 num_dwords = N>
        requires(num_dwords <= N)
    constexpr bool Fetch(const u32* flatbuf, T* out) const {
        if (summary == Summary::SingleLoad) [[likely]] {
            std::memcpy(out, flatbuf + offsets[0], num_dwords * sizeof(u32));
            return true;
        } else if (summary == Summary::Invalid) [[unlikely]] {
            return false;
        }
        u8 mask = load_mask;
        std::array<u32, num_dwords> out_dw;
        for (u32 i = 0; i < num_dwords; i++) {
            out_dw[i] = (mask & 1) ? flatbuf[offsets[i]] : immediates[i];
            mask >>= 1;
        }
        std::memcpy(out, out_dw.data(), sizeof(out_dw));
        return true;
    }
};

enum class SharpFetchPostOp : u8 {
    None,
    // For buffers
    BitwiseOrDw1WithImm,
    OffsetByProgramBase,
    ClearStrideAndBitshiftDw2WithImm,
    // For images,
    ConvertCubeTo2DArray,
    // For samplers
    DisableAnisoIfSingleLod,
    ForceRepeatXyzClamp,
    ForceLastTexelXyClamp,
    ClearAnisoRatioAndThreshold,
};

enum class BufferType : u8 {
    Guest,
    Flatbuf,
    BdaPagetable,
    FaultBuffer,
    GdsBuffer,
    SharedMemory,
    ClipPlanes,
};

struct BufferResource {
    SharpFetch<AmdGpu::Buffer> sharp_fetch{};
    IR::Type used_types{};
    BufferType buffer_type{};
    bool is_written{};
    bool is_formatted{};
    SharpFetchPostOp post_op{};
    u32 post_op_dw1_mask{};
    u32 post_op_dw2_shift{};

    bool IsSpecial() const {
        return buffer_type != BufferType::Guest;
    }

    constexpr AmdGpu::Buffer GetSharp(const auto& info) const {
        AmdGpu::Buffer buffer;
        if (!sharp_fetch.Fetch(info.flattened_ud_buf.data(), &buffer)) {
            return AmdGpu::Buffer::Null();
        }
        if (post_op == SharpFetchPostOp::BitwiseOrDw1WithImm) {
            reinterpret_cast<u32*>(&buffer)[1] |= post_op_dw1_mask;
        } else if (post_op == SharpFetchPostOp::ClearStrideAndBitshiftDw2WithImm) {
            buffer.stride = 0;
            buffer.num_records <<= post_op_dw2_shift;
        } else if (post_op == SharpFetchPostOp::OffsetByProgramBase) {
            buffer.base_address += info.pgm_base;
        }
        if (!buffer.Valid()) {
            return AmdGpu::Buffer::Null();
        }
        return buffer;
    }
};
using BufferResourceList = boost::container::static_vector<BufferResource, NUM_BUFFERS>;

enum class MipStorageFallbackMode : u16 {
    None,
    DynamicIndex,
    ConstantIndex,
};

// PS4Recomp: false for a T# no Metal texture can be made of (MTLTextureDescriptor validation
// aborts the process): multisampled with more than 8 samples, 3D past 2048 texels, a cube that
// is not square, more layers than an array texture holds (2048 for a 2D array, 341 cubes = 2046
// faces for a cube array; the layers are depth + 1, 6 per cube: POW2PAD pads only the guest
// layout's slice count, the renderer's texture holds the real ones). (A 1D image, also with mips
// or a block-compressed format, is made as a 2D image of height 1: Image::Is1DAs2D.) The unused
// entries of T# tables hold data that is no T# (Uncharted 4's lights: floats) and decode to such
// T#s.
// Mirrors the renderer's check (ps4_gnm_resources.mm DecodeImageUncached); a table entry failing
// it counts as unbound (GetSharp: the null image).
inline bool IsCreatableImage(const AmdGpu::Image& image) {
    using AmdGpu::ImageType;
    const ImageType type = image.GetType();
    const u32 width = u32(image.width) + 1, height = u32(image.height) + 1;
    const u32 depth_field = u32(image.depth) + 1;
    const bool msaa = type == ImageType::Color2DMsaa || type == ImageType::Color2DMsaaArray;
    const bool cube = type == ImageType::Cube;
    const u32 layers = type == ImageType::Color3D ? 1 : depth_field * (cube ? 6 : 1);
    if (msaa && image.last_level > 3) {
        return false;
    }
    if (type == ImageType::Color3D && (width > 2048 || height > 2048 || depth_field > 2048)) {
        return false;
    }
    if (cube && width != height) {
        return false;
    }
    return layers <= (cube ? 2046u : 2048u);
}

// PS4Recomp: no table (ImageResource::table)
constexpr u8 NoImageTable = 0xFF;

// PS4Recomp: the host's test whether a guest address is mapped (gcn_set_image_address_check).
// A table entry whose T# starts at an unmapped address holds no T# a draw could read (the GPU
// faults on it): unbound, like an entry failing IsCreatableImage. Without a test (fn null)
// every address counts as mapped.
struct ImageAddressCheck {
    bool (*fn)(void* ctx, u64 addr){};
    void* ctx{};
};
extern ImageAddressCheck g_image_address_check;

inline bool IsMappedImage(const AmdGpu::Image& image) {
    const ImageAddressCheck& c = g_image_address_check;
    return !c.fn || c.fn(c.ctx, image.Address());
}

struct ImageResource {
    SharpFetch<AmdGpu::Image> sharp_fetch{};
    bool is_depth{};
    bool is_atomic{};
    bool is_array{};
    bool is_written{};
    bool is_r128{};
    u8 constant_mip_index{};
    MipStorageFallbackMode mip_fallback_mode{};
    SharpFetchPostOp post_op{};
    // PS4Recomp: entry `table_entry` of T# table `table` (Info::image_tables), a T# the SRT
    // program copies from a table the shader indexes at run time; NoImageTable otherwise
    u8 table = NoImageTable;
    u8 table_entry{};

    constexpr AmdGpu::Image GetSharp(const auto& info) const {
        AmdGpu::Image image{};
        if (!Fetch(info.flattened_ud_buf.data(), &image)) {
            return AmdGpu::Image::Null(is_depth);
        }
        if (post_op == SharpFetchPostOp::ConvertCubeTo2DArray) {
            image.type = u64(AmdGpu::ImageType::Color2DArray);
            image.depth = (image.depth + 1) * 6 - 1;
        }
        if (table != NoImageTable && image.Valid() &&
            (!IsCreatableImage(image) || AmdGpu::IsFmask(image.GetDataFmt()) ||
             !IsMappedImage(image))) {
            // an entry that holds no T# the instruction can read: unbound (the null image). An
            // FMask surface is only read through the FMask path of a T# known at translation.
            return AmdGpu::Image::Null(is_depth);
        }
        if (!image.Valid()) {
            image = AmdGpu::Image::Null(is_depth);
        } else if (is_depth) {
            const auto data_fmt = image.GetDataFmt();
            if (data_fmt != AmdGpu::DataFormat::Format16 &&
                data_fmt != AmdGpu::DataFormat::Format32) {
                image = AmdGpu::Image::Null(true);
            }
        }
        return image;
    }

    constexpr bool Fetch(const u32* flatbuf, AmdGpu::Image* out) const {
        if (!is_r128) {
            // Fetch full 8 byte T#
            return sharp_fetch.Fetch(flatbuf, out);
        }
        // Fetch r128 T# and fix pitch
        if (!sharp_fetch.Fetch<4>(flatbuf, out)) {
            return false;
        }
        out->pitch = out->width;
        return true;
    }

    u32 NumBindings(const auto& info) const {
        const AmdGpu::Image tsharp = GetSharp(info);
        return (mip_fallback_mode == MipStorageFallbackMode::DynamicIndex)
                   ? (tsharp.last_level - tsharp.base_level + 1)
                   : 1;
    }
};
using ImageResourceList = boost::container::static_vector<ImageResource, NUM_IMAGES>;

struct SamplerResource {
    SharpFetch<AmdGpu::Sampler> sharp_fetch{};
    SharpFetchPostOp post_op{};
    SharpLocation post_op_tsharp_dw3_off{};
    bool is_depth{};

    constexpr AmdGpu::Sampler GetSharp(const auto& info) const {
        AmdGpu::Sampler sampler{};
        sharp_fetch.Fetch(info.flattened_ud_buf.data(), &sampler);
        if (post_op == SharpFetchPostOp::DisableAnisoIfSingleLod) {
            const u32 tsharp_dw3 = info.flattened_ud_buf[post_op_tsharp_dw3_off];
            if (((tsharp_dw3 >> 12) & 0xff) == 0) {
                sampler.max_aniso.Assign(AmdGpu::AnisoRatio::One);
            }
        } else if (post_op == SharpFetchPostOp::ForceRepeatXyzClamp) {
            sampler.clamp_x.Assign(AmdGpu::ClampMode::Wrap);
            sampler.clamp_y.Assign(AmdGpu::ClampMode::Wrap);
            sampler.clamp_z.Assign(AmdGpu::ClampMode::Wrap);
        } else if (post_op == SharpFetchPostOp::ForceLastTexelXyClamp) {
            sampler.clamp_x.Assign(AmdGpu::ClampMode::ClampLastTexel);
            sampler.clamp_y.Assign(AmdGpu::ClampMode::ClampLastTexel);
            sampler.clamp_z.Assign(AmdGpu::ClampMode::Wrap);
        } else if (post_op == SharpFetchPostOp::ClearAnisoRatioAndThreshold) {
            sampler.max_aniso.Assign(AmdGpu::AnisoRatio::One);
            sampler.aniso_threshold.Assign(0);
        }
        return sampler;
    }
};
using SamplerResourceList = boost::container::static_vector<SamplerResource, NUM_SAMPLERS>;

struct FMaskResource {
    SharpLocation sharp_idx;

    constexpr AmdGpu::Image GetSharp(const auto& info) const {
        return info.template ReadUdSharp<AmdGpu::Image>(sharp_idx);
    }
};
using FMaskResourceList = boost::container::static_vector<FMaskResource, NUM_FMASKS>;

struct PushData {
    static constexpr u32 XOffsetIndex = 0;
    static constexpr u32 YOffsetIndex = 1;
    static constexpr u32 XScaleIndex = 2;
    static constexpr u32 YScaleIndex = 3;
    static constexpr u32 BufOffsetIndex = 4;
    // PS4Recomp: member 7 = u32 buf_sizes[NUM_BUFFERS] (bytes, at offset 56), used for buffer
    // bounds checks when profile.needs_buffer_bounds_checks.
    static constexpr u32 BufSizeIndex = 7;

    float xoffset;
    float yoffset;
    float xscale;
    float yscale;
    std::array<u8, NUM_BUFFERS> buf_offsets;

    void AddOffset(u32 binding, u32 offset) {
        ASSERT(offset < 256 && binding < buf_offsets.size());
        buf_offsets[binding] = offset;
    }
};
static_assert(sizeof(PushData) <= 128,
              "PushData size is greater than minimum size guaranteed by Vulkan spec");

} // namespace Shader
