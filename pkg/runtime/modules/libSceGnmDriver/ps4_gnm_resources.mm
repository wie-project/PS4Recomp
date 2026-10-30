// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - GNM resource helpers for the native Metal backend (see ps4_gnm_resources.h).
 *
 * Ported in large part from shadPS4 (GPL-2.0-or-later):
 * SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *   pixel_format.{h,cpp}, resource.h, tiling.{h,cpp}, regs_*.h, liverpool.h (src/video_core/amdgpu)
 *   tile.h, image_info.cpp, sampler.cpp (src/video_core/texture_cache)
 *   tiling.comp (src/video_core/host_shaders) - CPU port of the (de)tiling compute shader
 *   liverpool_to_vk.cpp, vk_rasterizer.cpp, vk_graphics_pipeline.cpp, vk_pipeline_cache.cpp
 *   (src/video_core/renderer_vulkan) - translated to Metal terms
 *
 * Compiled as ObjC++ (clang -fobjc-arc), C++17.
 */

#include <dispatch/dispatch.h>
#include "ps4_gnm_resources.h"
#include "ps4_hitch.h"

#include <math.h>
#include <string.h>

#include <algorithm>

namespace ps4gnm {

namespace {
// The texture cache lock; time spent waiting for it goes to the long-frame breakdown (texlock).
struct TimedLock {
    std::mutex& m;
    explicit TimedLock(std::mutex& mu) : m(mu) {
        if (m.try_lock()) return;
        const uint64_t t0 = ps4_hitch_now_ns();
        m.lock();
        ps4_hitch_add_ns(PS4_HITCH_TEXLOCK, ps4_hitch_now_ns() - t0);
    }
    ~TimedLock() { m.unlock(); }
    TimedLock(const TimedLock&) = delete;
    TimedLock& operator=(const TimedLock&) = delete;
};
} // namespace

// -------------------------------------------------------------------------------------
// small helpers (C++17: no <bit>)
// -------------------------------------------------------------------------------------
static inline uint32_t BitWidth(uint32_t v) {
    return v == 0 ? 0u : 32u - (uint32_t)__builtin_clz(v);
}
static inline uint32_t BitCeil(uint32_t v) {
    return v <= 1 ? 1u : 1u << BitWidth(v - 1);
}
static inline bool HasSingleBit(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}
static inline uint32_t Bits(uint32_t v, uint32_t lo, uint32_t n) {
    return (v >> lo) & (n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u));
}
static inline uint64_t Bits64(uint64_t v, uint32_t lo, uint32_t n) {
    return (v >> lo) & (n >= 64 ? ~0ull : ((1ull << n) - 1ull));
}
static inline float AsFloat(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// =====================================================================================
// 1. Formats
// =====================================================================================

uint32_t CompMapping::ApplyMask(uint32_t mask) const {
    // port of shadPS4 CompMapping::ApplyMask / Map
    uint32_t out = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t sw = (uint32_t)(*this)[i];
        const uint32_t mapped = sw >= (uint32_t)CompSwizzle::Red ? sw - (uint32_t)CompSwizzle::Red : i;
        out |= ((mask >> i) & 1u) << mapped;
    }
    return out;
}

CompMapping CompMapping::Inverse() const {
    CompMapping res;
    const CompSwizzle targets[4] = {CompSwizzle::Red, CompSwizzle::Green, CompSwizzle::Blue,
                                    CompSwizzle::Alpha};
    for (uint32_t t = 0; t < 4; ++t) {
        CompSwizzle d = CompSwizzle::Zero;
        for (uint32_t i = 0; i < 4; ++i) {
            if ((*this)[i] == targets[t]) {
                d = targets[i];
                break;
            }
        }
        res[t] = d;
    }
    return res;
}

DataFormat RemapDataFormat(DataFormat format) {
    switch (format) {
    case DataFormat::Format11_11_10:
        return DataFormat::Format10_11_11;
    case DataFormat::Format10_10_10_2:
        return DataFormat::Format2_10_10_10;
    case DataFormat::Format5_5_5_1:
        return DataFormat::Format1_5_5_5;
    default:
        return format;
    }
}

NumberFormat RemapNumberFormat(NumberFormat format, DataFormat data_format) {
    switch (format) {
    case NumberFormat::Unorm:
        switch (data_format) {
        case DataFormat::Format32:
        case DataFormat::Format32_32:
        case DataFormat::Format32_32_32:
        case DataFormat::Format32_32_32_32:
            return NumberFormat::Uint;
        default:
            return format;
        }
    case NumberFormat::Srgb:
        switch (data_format) {
        case DataFormat::FormatBc4:
        case DataFormat::FormatBc5:
        case DataFormat::FormatBc6:
            return NumberFormat::Unorm;
        default:
            return format;
        }
    case NumberFormat::Uscaled:
        return NumberFormat::Uint;
    case NumberFormat::Sscaled:
    case NumberFormat::SnormNz:
        return NumberFormat::Sint;
    case NumberFormat::Ubnorm:
        return NumberFormat::Unorm;
    case NumberFormat::Float:
        if (data_format == DataFormat::Format8) {
            return NumberFormat::Uint;
        }
        return format;
    default:
        return format;
    }
}

CompMapping RemapSwizzle(DataFormat format, CompMapping swizzle) {
    CompMapping result = swizzle;
    switch (format) {
    case DataFormat::Format1_5_5_5:
    case DataFormat::Format11_11_10:
        result.r = swizzle.b;
        result.g = swizzle.g;
        result.b = swizzle.r;
        result.a = swizzle.a;
        return result;
    case DataFormat::Format10_10_10_2:
        result.r = swizzle.a;
        result.g = swizzle.b;
        result.b = swizzle.g;
        result.a = swizzle.r;
        return result;
    case DataFormat::Format4_4_4_4:
        result.r = swizzle.g;
        result.g = swizzle.b;
        result.b = swizzle.a;
        result.a = swizzle.r;
        return result;
    case DataFormat::Format5_6_5:
        result.r = swizzle.b;
        result.g = swizzle.g;
        result.b = swizzle.r;
        result.a = swizzle.a;
        return result;
    case DataFormat::Format8:
    case DataFormat::Format16:
    case DataFormat::Format32:
        for (uint32_t i = 0; i < 4; ++i) {
            if (result[i] == CompSwizzle::Alpha) {
                result[i] = CompSwizzle::Red;
            }
        }
        return result;
    default:
        return swizzle;
    }
}

NumberConversion MapNumberConversion(NumberFormat num_fmt, DataFormat data_fmt) {
    switch (num_fmt) {
    case NumberFormat::Unorm:
        switch (data_fmt) {
        case DataFormat::Format32:
        case DataFormat::Format32_32:
        case DataFormat::Format32_32_32:
        case DataFormat::Format32_32_32_32:
            return NumberConversion::Uint32ToUnorm;
        default:
            return NumberConversion::None;
        }
    case NumberFormat::Srgb:
        return data_fmt == DataFormat::FormatBc6 ? NumberConversion::SrgbToNorm
                                                 : NumberConversion::None;
    case NumberFormat::Uscaled:
        return NumberConversion::UintToUscaled;
    case NumberFormat::Sscaled:
        return NumberConversion::SintToSscaled;
    case NumberFormat::Ubnorm:
        return NumberConversion::UnormToUbnorm;
    case NumberFormat::SnormNz:
        switch (data_fmt) {
        case DataFormat::Format8:
        case DataFormat::Format8_8:
        case DataFormat::Format8_8_8_8:
            return NumberConversion::Sint8ToSnormNz;
        case DataFormat::Format16:
        case DataFormat::Format16_16:
        case DataFormat::Format16_16_16_16:
            return NumberConversion::Sint16ToSnormNz;
        default:
            return NumberConversion::None; // shadPS4 asserts here
        }
    default:
        return NumberConversion::None;
    }
}

// shadPS4 pixel_format.cpp tables (index = DataFormat, 0..41)
static const int8_t kNumComponents[42] = {
    0, 1, 1, 2, 1, 2, 3, 3, 4, 4, 4, 2, 4, 3, 4, 0, 3, 4, 4, 4, 2,
    2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 4, 4, 4, 4, 1, 2, 3, 4,
};
static const int16_t kBitsPerBlock[42] = {
    0,  8,  16, 16, 32, 32, 32, 32, 32, 32, 32, 64, 64, 96, 128, -1, 16, 16, 16, 16, 32,
    32, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, 16, 16, 32, 64, 128, 128, 64, 128, 128, 128,
};
static const int16_t kBitsPerElement[42] = {
    0,  8,  16, 16, 32, 32, 32, 32, 32, 32, 32, 64, 64, 96, 128, -1, 16, 16, 16, 16, 32,
    32, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, 16, 16, 32, 4, 8, 8, 4, 8, 8, 8,
};

uint32_t NumComponents(DataFormat f) {
    const uint32_t i = (uint32_t)f;
    return i < 42 ? (uint32_t)std::max<int>(0, kNumComponents[i]) : 0u;
}
uint32_t NumBitsPerBlock(DataFormat f) {
    const uint32_t i = (uint32_t)f;
    return i < 42 ? (uint32_t)std::max<int>(0, kBitsPerBlock[i]) : 0u;
}
uint32_t NumBitsPerElement(DataFormat f) {
    const uint32_t i = (uint32_t)f;
    return i < 42 ? (uint32_t)std::max<int>(0, kBitsPerElement[i]) : 0u;
}

const char* NameOf(DataFormat f) {
    switch (f) {
#define N(x) case DataFormat::x: return #x;
        N(FormatInvalid) N(Format8) N(Format16) N(Format8_8) N(Format32) N(Format16_16)
        N(Format10_11_11) N(Format11_11_10) N(Format10_10_10_2) N(Format2_10_10_10)
        N(Format8_8_8_8) N(Format32_32) N(Format16_16_16_16) N(Format32_32_32)
        N(Format32_32_32_32) N(Format5_6_5) N(Format1_5_5_5) N(Format5_5_5_1) N(Format4_4_4_4)
        N(Format8_24) N(Format24_8) N(FormatX24_8_32) N(FormatGB_GR) N(FormatBG_RG)
        N(Format5_9_9_9) N(FormatBc1) N(FormatBc2) N(FormatBc3) N(FormatBc4) N(FormatBc5)
        N(FormatBc6) N(FormatBc7)
#undef N
    default:
        return "Format?";
    }
}

const char* NameOf(NumberFormat f) {
    switch (f) {
#define N(x) case NumberFormat::x: return #x;
        N(Unorm) N(Snorm) N(Uscaled) N(Sscaled) N(Uint) N(Sint) N(SnormNz) N(Float) N(Srgb)
        N(Ubnorm) N(UbnormNz) N(Ubint) N(Ubscaled)
#undef N
    default:
        return "Num?";
    }
}

PixelFormatInfo GetPixelFormat(DataFormat dfmt, NumberFormat nfmt) {
    // Metal format chosen to have the same memory layout as the Vulkan format shadPS4's
    // liverpool_to_vk.cpp SurfaceFormat table picks, so shadPS4's RemapSwizzle() stays valid.
    PixelFormatInfo info;
    const uint32_t bits = NumBitsPerBlock(dfmt);
    info.bytes_per_block = bits / 8;
    info.is_integer = IsInteger(nfmt);
    info.is_srgb = nfmt == NumberFormat::Srgb;
    if (IsBlockCoded(dfmt)) {
        info.compressed = true;
        info.block_width = info.block_height = 4;
    }
    MTLPixelFormat f = MTLPixelFormatInvalid;
    using D = DataFormat;
    using N = NumberFormat;
#define CASE5(u, s, ui, si, fl_or_srgb)                                                            \
    switch (nfmt) {                                                                                \
    case N::Unorm: f = u; break;                                                                   \
    case N::Snorm: f = s; break;                                                                   \
    case N::Uint: f = ui; break;                                                                   \
    case N::Sint: f = si; break;                                                                   \
    default: f = fl_or_srgb; break;                                                                \
    }
    switch (dfmt) {
    case D::Format8:
        CASE5(MTLPixelFormatR8Unorm, MTLPixelFormatR8Snorm, MTLPixelFormatR8Uint, MTLPixelFormatR8Sint,
              nfmt == N::Srgb ? MTLPixelFormatR8Unorm_sRGB : MTLPixelFormatInvalid);
        break;
    case D::Format16:
        CASE5(MTLPixelFormatR16Unorm, MTLPixelFormatR16Snorm, MTLPixelFormatR16Uint,
              MTLPixelFormatR16Sint, nfmt == N::Float ? MTLPixelFormatR16Float : MTLPixelFormatInvalid);
        break;
    case D::Format8_8:
        CASE5(MTLPixelFormatRG8Unorm, MTLPixelFormatRG8Snorm, MTLPixelFormatRG8Uint,
              MTLPixelFormatRG8Sint, nfmt == N::Srgb ? MTLPixelFormatRG8Unorm_sRGB : MTLPixelFormatInvalid);
        break;
    case D::Format32:
        f = nfmt == N::Uint ? MTLPixelFormatR32Uint
            : nfmt == N::Sint ? MTLPixelFormatR32Sint
            : nfmt == N::Float ? MTLPixelFormatR32Float : MTLPixelFormatInvalid;
        break;
    case D::Format16_16:
        CASE5(MTLPixelFormatRG16Unorm, MTLPixelFormatRG16Snorm, MTLPixelFormatRG16Uint,
              MTLPixelFormatRG16Sint, nfmt == N::Float ? MTLPixelFormatRG16Float : MTLPixelFormatInvalid);
        break;
    case D::Format10_11_11:
        f = nfmt == N::Float ? MTLPixelFormatRG11B10Float : MTLPixelFormatInvalid;
        break;
    case D::Format2_10_10_10:
        switch (nfmt) {
        case N::Unorm: f = MTLPixelFormatRGB10A2Unorm; break;
        case N::Uint: f = MTLPixelFormatRGB10A2Uint; break;
        case N::Snorm: f = MTLPixelFormatRGB10A2Unorm; info.exact = false; break; // no snorm 1010102
        case N::Sint: f = MTLPixelFormatRGB10A2Uint; info.exact = false; break;
        default: break;
        }
        break;
    case D::Format8_8_8_8:
        CASE5(MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA8Snorm, MTLPixelFormatRGBA8Uint,
              MTLPixelFormatRGBA8Sint,
              nfmt == N::Srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatInvalid);
        break;
    case D::Format32_32:
        f = nfmt == N::Uint ? MTLPixelFormatRG32Uint
            : nfmt == N::Sint ? MTLPixelFormatRG32Sint
            : nfmt == N::Float ? MTLPixelFormatRG32Float : MTLPixelFormatInvalid;
        break;
    case D::Format16_16_16_16:
        CASE5(MTLPixelFormatRGBA16Unorm, MTLPixelFormatRGBA16Snorm, MTLPixelFormatRGBA16Uint,
              MTLPixelFormatRGBA16Sint,
              nfmt == N::Float ? MTLPixelFormatRGBA16Float
              : nfmt == N::SnormNz ? MTLPixelFormatRGBA16Snorm : MTLPixelFormatInvalid);
        break;
    case D::Format32_32_32:
        // No 96-bit Metal pixel format: caller must expand to RGBA32 (not done here).
        info.exact = false;
        break;
    case D::Format32_32_32_32:
        f = nfmt == N::Uint ? MTLPixelFormatRGBA32Uint
            : nfmt == N::Sint ? MTLPixelFormatRGBA32Sint
            : nfmt == N::Float ? MTLPixelFormatRGBA32Float : MTLPixelFormatInvalid;
        break;
    case D::Format5_6_5:
        // == VK R5G6B5_UNORM_PACK16 (R in bits 11..15)
        f = nfmt == N::Unorm ? MTLPixelFormatB5G6R5Unorm : MTLPixelFormatInvalid;
        break;
    case D::Format1_5_5_5:
    case D::Format5_5_5_1:
        // == VK A1R5G5B5_UNORM_PACK16 (A bit 15, R 10..14)
        f = nfmt == N::Unorm ? MTLPixelFormatBGR5A1Unorm : MTLPixelFormatInvalid;
        info.exact = dfmt == D::Format1_5_5_5;
        break;
    case D::Format4_4_4_4:
        // VK B4G4R4A4 has R in bits 4..7 / B in 12..15; Metal ABGR4 has them swapped.
        f = nfmt == N::Unorm ? MTLPixelFormatABGR4Unorm : MTLPixelFormatInvalid;
        info.swap_rb = true;
        break;
    case D::Format5_9_9_9:
        f = nfmt == N::Float ? MTLPixelFormatRGB9E5Float : MTLPixelFormatInvalid;
        break;
    case D::FormatBc1:
        f = nfmt == N::Srgb ? MTLPixelFormatBC1_RGBA_sRGB : MTLPixelFormatBC1_RGBA;
        info.exact = nfmt == N::Srgb || nfmt == N::Unorm;
        break;
    case D::FormatBc2:
        f = nfmt == N::Srgb ? MTLPixelFormatBC2_RGBA_sRGB : MTLPixelFormatBC2_RGBA;
        info.exact = nfmt == N::Srgb || nfmt == N::Unorm;
        break;
    case D::FormatBc3:
        f = nfmt == N::Srgb ? MTLPixelFormatBC3_RGBA_sRGB : MTLPixelFormatBC3_RGBA;
        info.exact = nfmt == N::Srgb || nfmt == N::Unorm;
        break;
    case D::FormatBc4:
        f = nfmt == N::Snorm ? MTLPixelFormatBC4_RSnorm : MTLPixelFormatBC4_RUnorm;
        info.exact = nfmt == N::Snorm || nfmt == N::Unorm;
        break;
    case D::FormatBc5:
        f = nfmt == N::Snorm ? MTLPixelFormatBC5_RGSnorm : MTLPixelFormatBC5_RGUnorm;
        info.exact = nfmt == N::Snorm || nfmt == N::Unorm;
        break;
    case D::FormatBc6:
        f = nfmt == N::Snorm ? MTLPixelFormatBC6H_RGBFloat : MTLPixelFormatBC6H_RGBUfloat;
        info.exact = nfmt == N::Snorm || nfmt == N::Unorm;
        break;
    case D::FormatBc7:
        f = nfmt == N::Srgb ? MTLPixelFormatBC7_RGBAUnorm_sRGB : MTLPixelFormatBC7_RGBAUnorm;
        info.exact = nfmt == N::Srgb || nfmt == N::Unorm;
        break;
    default:
        break;
    }
#undef CASE5
    info.mtl_format = (uint32_t)f;
    if (f == MTLPixelFormatInvalid) {
        info.exact = false;
    }
    return info;
}

PixelFormatInfo GetPixelFormatRaw(uint32_t raw_dfmt, uint32_t raw_nfmt) {
    const DataFormat d = (DataFormat)raw_dfmt;
    return GetPixelFormat(RemapDataFormat(d), RemapNumberFormat((NumberFormat)raw_nfmt, d));
}

static inline CompSwizzle SwapRB(CompSwizzle c) {
    return c == CompSwizzle::Red ? CompSwizzle::Blue : c == CompSwizzle::Blue ? CompSwizzle::Red : c;
}

CompMapping ComposeViewSwizzle(const PixelFormatInfo& info, CompMapping sw) {
    if (!info.swap_rb) {
        return sw;
    }
    // Values name channels of the VK format; the same bits are the other channel in Metal.
    CompMapping out;
    for (uint32_t i = 0; i < 4; ++i) {
        out[i] = SwapRB(sw[i]);
    }
    return out;
}

uint32_t GetColorTargetPixelFormat(DataFormat dfmt, NumberFormat nfmt, CompMapping cb_swizzle,
                                   CompMapping* export_swizzle, bool* exact) {
    PixelFormatInfo pf = GetPixelFormat(dfmt, nfmt);
    CompMapping ex = cb_swizzle;
    const CompMapping bgra{CompSwizzle::Blue, CompSwizzle::Green, CompSwizzle::Red, CompSwizzle::Alpha};
    if (dfmt == DataFormat::Format8_8_8_8 && cb_swizzle == bgra &&
        (nfmt == NumberFormat::Unorm || nfmt == NumberFormat::Srgb)) {
        pf.mtl_format = nfmt == NumberFormat::Srgb ? (uint32_t)MTLPixelFormatBGRA8Unorm_sRGB
                                                   : (uint32_t)MTLPixelFormatBGRA8Unorm;
        ex = CompMapping{};
    } else if (pf.swap_rb) {
        // out_metal[i] = out_vk[perm(i)]: swap positions 0 and 2
        std::swap(ex.r, ex.b);
    }
    if (export_swizzle) *export_swizzle = ex;
    if (exact) *exact = pf.exact;
    return pf.mtl_format;
}

uint32_t GetDepthPixelFormat(ZFormat z, StencilFormat s, bool* exact) {
    bool ex = true;
    MTLPixelFormat f = MTLPixelFormatInvalid;
    const bool st = s == StencilFormat::Stencil8;
    switch (z) {
    case ZFormat::Invalid:
        f = st ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid; // shadPS4: D32S8
        break;
    case ZFormat::Z16:
        if (st) {
            f = MTLPixelFormatDepth32Float_Stencil8; // no D16S8 on Metal
            ex = false;
        } else {
            f = MTLPixelFormatDepth16Unorm;
        }
        break;
    case ZFormat::Z32Float:
        f = st ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatDepth32Float;
        break;
    default:
        ex = false;
        break;
    }
    if (exact) *exact = ex;
    return (uint32_t)f;
}

VertexFormatInfo GetVertexFormat(uint32_t raw_dfmt, uint32_t raw_nfmt) {
    VertexFormatInfo v;
    const DataFormat rd = (DataFormat)raw_dfmt;
    const DataFormat d = RemapDataFormat(rd);
    const NumberFormat n = RemapNumberFormat((NumberFormat)raw_nfmt, rd);
    v.components = NumComponents(d);
    v.size_bytes = NumBitsPerBlock(d) / 8;
    v.shader_conversion = MapNumberConversion((NumberFormat)raw_nfmt, rd) != NumberConversion::None ||
                          RemapSwizzle(rd, CompMapping{}) != CompMapping{};
    MTLVertexFormat f = MTLVertexFormatInvalid;
    using D = DataFormat;
    using N = NumberFormat;
    switch (d) {
    case D::Format8:
        f = n == N::Unorm ? MTLVertexFormatUCharNormalized : n == N::Snorm ? MTLVertexFormatCharNormalized
            : n == N::Uint ? MTLVertexFormatUChar : n == N::Sint ? MTLVertexFormatChar : MTLVertexFormatInvalid;
        break;
    case D::Format8_8:
        f = n == N::Unorm ? MTLVertexFormatUChar2Normalized : n == N::Snorm ? MTLVertexFormatChar2Normalized
            : n == N::Uint ? MTLVertexFormatUChar2 : n == N::Sint ? MTLVertexFormatChar2 : MTLVertexFormatInvalid;
        break;
    case D::Format8_8_8_8:
        f = n == N::Unorm ? MTLVertexFormatUChar4Normalized : n == N::Snorm ? MTLVertexFormatChar4Normalized
            : n == N::Uint ? MTLVertexFormatUChar4 : n == N::Sint ? MTLVertexFormatChar4 : MTLVertexFormatInvalid;
        break;
    case D::Format16:
        f = n == N::Unorm ? MTLVertexFormatUShortNormalized : n == N::Snorm ? MTLVertexFormatShortNormalized
            : n == N::Uint ? MTLVertexFormatUShort : n == N::Sint ? MTLVertexFormatShort
            : n == N::Float ? MTLVertexFormatHalf : MTLVertexFormatInvalid;
        break;
    case D::Format16_16:
        f = n == N::Unorm ? MTLVertexFormatUShort2Normalized : n == N::Snorm ? MTLVertexFormatShort2Normalized
            : n == N::Uint ? MTLVertexFormatUShort2 : n == N::Sint ? MTLVertexFormatShort2
            : n == N::Float ? MTLVertexFormatHalf2 : MTLVertexFormatInvalid;
        break;
    case D::Format16_16_16_16:
        f = n == N::Unorm ? MTLVertexFormatUShort4Normalized : n == N::Snorm ? MTLVertexFormatShort4Normalized
            : n == N::Uint ? MTLVertexFormatUShort4 : n == N::Sint ? MTLVertexFormatShort4
            : n == N::Float ? MTLVertexFormatHalf4 : MTLVertexFormatInvalid;
        break;
    case D::Format32:
        f = n == N::Uint ? MTLVertexFormatUInt : n == N::Sint ? MTLVertexFormatInt
            : n == N::Float ? MTLVertexFormatFloat : MTLVertexFormatInvalid;
        break;
    case D::Format32_32:
        f = n == N::Uint ? MTLVertexFormatUInt2 : n == N::Sint ? MTLVertexFormatInt2
            : n == N::Float ? MTLVertexFormatFloat2 : MTLVertexFormatInvalid;
        break;
    case D::Format32_32_32:
        f = n == N::Uint ? MTLVertexFormatUInt3 : n == N::Sint ? MTLVertexFormatInt3
            : n == N::Float ? MTLVertexFormatFloat3 : MTLVertexFormatInvalid;
        break;
    case D::Format32_32_32_32:
        f = n == N::Uint ? MTLVertexFormatUInt4 : n == N::Sint ? MTLVertexFormatInt4
            : n == N::Float ? MTLVertexFormatFloat4 : MTLVertexFormatInvalid;
        break;
    case D::Format2_10_10_10:
        if (n == N::Unorm) f = MTLVertexFormatUInt1010102Normalized;
        else if (n == N::Snorm) f = MTLVertexFormatInt1010102Normalized;
        else { f = MTLVertexFormatUInt; v.shader_conversion = true; } // unpack in shader
        break;
    case D::Format10_11_11:
        if (@available(macOS 14.0, *)) {
            f = n == N::Float ? MTLVertexFormatFloatRG11B10 : MTLVertexFormatInvalid;
        } else {
            f = MTLVertexFormatUInt;
            v.shader_conversion = true;
        }
        break;
    case D::Format5_9_9_9:
        if (@available(macOS 14.0, *)) {
            f = n == N::Float ? MTLVertexFormatFloatRGB9E5 : MTLVertexFormatInvalid;
        } else {
            f = MTLVertexFormatUInt;
            v.shader_conversion = true;
        }
        break;
    default:
        break;
    }
    v.mtl_vertex_format = (uint32_t)f;
    return v;
}

// =====================================================================================
// 4. Tiling (port of shadPS4 tiling.h/.cpp, tile.h and host_shaders/tiling.comp)
// =====================================================================================

static const uint32_t kMicroTileSize = 8;
static const uint32_t kDramRowSize = 1024;
static const uint32_t kPipeInterleaveBits = 8;

bool IsValidTileMode(uint32_t t) {
    return t <= 26 || t == 31;
}

const char* NameOf(TileMode t) {
    static const char* names[] = {
        "Depth2DThin64", "Depth2DThin128", "Depth2DThin256", "Depth2DThin512", "Depth2DThin1K",
        "Depth1DThin", "Depth2DThinPrt256", "Depth2DThinPrt1K", "DisplayLinearAligned",
        "Display1DThin", "Display2DThin", "DisplayThinPrt", "Display2DThinPrt", "Thin1DThin",
        "Thin2DThin", "Thin3DThin", "ThinThinPrt", "Thin2DThinPrt", "Thin3DThinPrt",
        "Thick1DThick", "Thick2DThick", "Thick3DThick", "ThickThickPrt", "Thick2DThickPrt",
        "Thick3DThickPrt", "Thick2DXThick", "Thick3DXThick"};
    const uint32_t i = (uint32_t)t;
    if (i <= 26) return names[i];
    if (i == 31) return "DisplayLinearGeneral";
    return "TileMode?";
}

bool IsMacroTiled(ArrayMode a) {
    switch (a) {
    case ArrayMode::ArrayLinearGeneral:
    case ArrayMode::ArrayLinearAligned:
    case ArrayMode::Array1DTiledThin1:
    case ArrayMode::Array1DTiledThick:
        return false;
    default:
        return true;
    }
}

uint32_t GetMicroTileThickness(ArrayMode a) {
    switch (a) {
    case ArrayMode::Array1DTiledThick:
    case ArrayMode::Array2DTiledThick:
    case ArrayMode::Array3DTiledThick:
    case ArrayMode::ArrayPrtTiledThick:
    case ArrayMode::ArrayPrt2DTiledThick:
    case ArrayMode::ArrayPrt3DTiledThick:
        return 4;
    case ArrayMode::Array2DTiledXThick:
    case ArrayMode::Array3DTiledXThick:
        return 8;
    default:
        return 1;
    }
}

uint32_t GetAltNumBanks(MacroTileMode m) {
    switch (m) {
    case MacroTileMode::Mode_1x1_2_Dup:
    case MacroTileMode::Mode_1x1_2_Dup2:
    case MacroTileMode::Mode_1x1_2_Dup3:
        return 2;
    case MacroTileMode::Mode_1x1_2:
    case MacroTileMode::Mode_1x1_8_Dup:
    case MacroTileMode::Mode_1x1_4_Dup:
        return 4;
    case MacroTileMode::Mode_1x8_16:
        return 16;
    default:
        return 8;
    }
}

ArrayMode GetArrayMode(TileMode t) {
    switch (t) {
    case TileMode::Depth1DThin:
    case TileMode::Display1DThin:
    case TileMode::Thin1DThin:
        return ArrayMode::Array1DTiledThin1;
    case TileMode::Depth2DThin64:
    case TileMode::Depth2DThin128:
    case TileMode::Depth2DThin256:
    case TileMode::Depth2DThin512:
    case TileMode::Depth2DThin1K:
    case TileMode::Display2DThin:
    case TileMode::Thin2DThin:
        return ArrayMode::Array2DTiledThin1;
    case TileMode::DisplayThinPrt:
    case TileMode::ThinThinPrt:
        return ArrayMode::ArrayPrtTiledThin1;
    case TileMode::Depth2DThinPrt256:
    case TileMode::Depth2DThinPrt1K:
    case TileMode::Display2DThinPrt:
    case TileMode::Thin2DThinPrt:
        return ArrayMode::ArrayPrt2DTiledThin1;
    case TileMode::Thin3DThin:
        return ArrayMode::Array3DTiledThin1;
    case TileMode::Thin3DThinPrt:
        return ArrayMode::ArrayPrt3DTiledThin1;
    case TileMode::Thick1DThick:
        return ArrayMode::Array1DTiledThick;
    case TileMode::Thick2DThick:
        return ArrayMode::Array2DTiledThick;
    case TileMode::Thick3DThick:
        return ArrayMode::Array3DTiledThick;
    case TileMode::ThickThickPrt:
        return ArrayMode::ArrayPrtTiledThick;
    case TileMode::Thick2DThickPrt:
        return ArrayMode::ArrayPrt2DTiledThick;
    case TileMode::Thick3DThickPrt:
        return ArrayMode::ArrayPrt3DTiledThick;
    case TileMode::Thick2DXThick:
        return ArrayMode::Array2DTiledXThick;
    case TileMode::Thick3DXThick:
        return ArrayMode::Array3DTiledXThick;
    case TileMode::DisplayLinearAligned:
        return ArrayMode::ArrayLinearAligned;
    case TileMode::DisplayLinearGeneral:
    default:
        return ArrayMode::ArrayLinearGeneral;
    }
}

MicroTileMode GetMicroTileMode(TileMode t) {
    const uint32_t i = (uint32_t)t;
    if (i <= 7) return MicroTileMode::Depth;
    if (i == 8 || i == 9 || i == 10 || i == 11 || i == 12 || i == 31) return MicroTileMode::Display;
    if (i >= 13 && i <= 18) return MicroTileMode::Thin;
    return MicroTileMode::Thick;
}

PipeConfig GetPipeConfig(TileMode t) {
    switch (t) {
    case TileMode::DisplayThinPrt:
    case TileMode::Thin3DThin:
    case TileMode::ThinThinPrt:
    case TileMode::Thick3DThick:
    case TileMode::ThickThickPrt:
    case TileMode::Thick3DThickPrt:
    case TileMode::Thick3DXThick:
        return PipeConfig::P8_32x32_8x16;
    case TileMode::DisplayLinearGeneral:
        return PipeConfig::P2;
    default:
        return PipeConfig::P8_32x32_16x16;
    }
}

PipeConfig GetAltPipeConfig(TileMode t) {
    return t == TileMode::DisplayLinearGeneral ? PipeConfig::P2 : PipeConfig::P16_32x32_8x16;
}

uint32_t GetSampleSplit(TileMode t) {
    switch (t) {
    case TileMode::Display2DThin:
    case TileMode::DisplayThinPrt:
    case TileMode::Display2DThinPrt:
    case TileMode::Thin2DThin:
    case TileMode::Thin3DThin:
    case TileMode::ThinThinPrt:
    case TileMode::Thin2DThinPrt:
    case TileMode::Thin3DThinPrt:
        return 2;
    default:
        return 1;
    }
}

uint32_t GetTileSplitHw(TileMode t) {
    switch (t) {
    case TileMode::Depth2DThin128:
        return 128;
    case TileMode::Depth2DThin256:
    case TileMode::Depth2DThinPrt256:
        return 256;
    case TileMode::Depth2DThin512:
        return 512;
    case TileMode::Depth2DThin1K:
    case TileMode::Depth2DThinPrt1K:
        return 1024;
    default:
        return 64;
    }
}

uint32_t GetBankWidth(MacroTileMode) {
    return 1;
}

uint32_t GetBankHeight(MacroTileMode m) {
    switch (m) {
    case MacroTileMode::Mode_1x2_16:
    case MacroTileMode::Mode_1x2_16_Dup:
        return 2;
    case MacroTileMode::Mode_1x4_16:
    case MacroTileMode::Mode_1x4_16_Dup:
        return 4;
    case MacroTileMode::Mode_1x8_16:
        return 8;
    default:
        return 1;
    }
}

uint32_t GetNumBanks(MacroTileMode m) {
    switch (m) {
    case MacroTileMode::Mode_1x1_2:
    case MacroTileMode::Mode_1x1_2_Dup:
    case MacroTileMode::Mode_1x1_2_Dup2:
    case MacroTileMode::Mode_1x1_2_Dup3:
        return 2;
    case MacroTileMode::Mode_1x1_4:
    case MacroTileMode::Mode_1x1_4_Dup:
        return 4;
    case MacroTileMode::Mode_1x1_8:
    case MacroTileMode::Mode_1x1_8_Dup:
        return 8;
    default:
        return 16;
    }
}

uint32_t GetMacrotileAspect(MacroTileMode m) {
    switch (m) {
    case MacroTileMode::Mode_1x2_16:
    case MacroTileMode::Mode_1x1_16:
    case MacroTileMode::Mode_1x1_16_Dup:
    case MacroTileMode::Mode_1x2_16_Dup:
    case MacroTileMode::Mode_1x1_16_Dup2:
        return 2;
    case MacroTileMode::Mode_1x4_16:
    case MacroTileMode::Mode_1x8_16:
    case MacroTileMode::Mode_1x4_16_Dup:
        return 4;
    default:
        return 1;
    }
}

uint32_t GetAltBankHeight(MacroTileMode m) {
    switch (m) {
    case MacroTileMode::Mode_1x1_16:
    case MacroTileMode::Mode_1x1_16_Dup:
    case MacroTileMode::Mode_1x2_16_Dup:
        return 2;
    case MacroTileMode::Mode_1x4_16:
    case MacroTileMode::Mode_1x2_16:
    case MacroTileMode::Mode_1x8_16:
    case MacroTileMode::Mode_1x4_16_Dup:
        return 4;
    default:
        return 1;
    }
}

uint32_t GetAltMacrotileAspect(MacroTileMode m) {
    switch (m) {
    case MacroTileMode::Mode_1x4_16:
    case MacroTileMode::Mode_1x2_16:
    case MacroTileMode::Mode_1x8_16:
    case MacroTileMode::Mode_1x4_16_Dup:
        return 2;
    default:
        return 1;
    }
}

bool IsPrt(ArrayMode a) {
    switch (a) {
    case ArrayMode::ArrayPrtTiledThin1:
    case ArrayMode::ArrayPrtTiledThick:
    case ArrayMode::ArrayPrt2DTiledThin1:
    case ArrayMode::ArrayPrt2DTiledThick:
    case ArrayMode::ArrayPrt3DTiledThin1:
    case ArrayMode::ArrayPrt3DTiledThick:
        return true;
    default:
        return false;
    }
}

uint32_t GetPipeCount(PipeConfig p) {
    switch (p) {
    case PipeConfig::P2:
        return 2;
    case PipeConfig::P16_32x32_8x16:
        return 16;
    default:
        return 8;
    }
}

uint32_t CalculateTileSplit(TileMode t, ArrayMode a, MicroTileMode mt, uint32_t bpp) {
    const uint32_t sample_split = GetSampleSplit(t);
    const uint32_t tile_split_hw = GetTileSplitHw(t);
    const uint32_t thickness = GetMicroTileThickness(a);
    const uint32_t tile_bytes_1x = (bpp * kMicroTileSize * kMicroTileSize * thickness + 7) / 8;
    const uint32_t color_tile_split = std::max(256u, sample_split * tile_bytes_1x);
    const uint32_t tile_split = mt == MicroTileMode::Depth ? tile_split_hw : color_tile_split;
    return std::min(kDramRowSize, tile_split);
}

MacroTileMode CalculateMacrotileMode(TileMode t, uint32_t bpp, uint32_t num_samples) {
    const ArrayMode a = GetArrayMode(t);
    const MicroTileMode mt = GetMicroTileMode(t);
    const uint32_t thickness = GetMicroTileThickness(a);
    const uint32_t tile_bytes_1x = bpp * kMicroTileSize * kMicroTileSize * thickness / 8;
    const uint32_t split = CalculateTileSplit(t, a, mt, bpp);
    const uint32_t tile_bytes = std::min(split, num_samples * tile_bytes_1x);
    const uint32_t idx = BitWidth(tile_bytes / 64) - 1;
    return IsPrt(a) ? (MacroTileMode)(idx + 8) : (MacroTileMode)idx;
}

void GetMacroTileExtents(TileMode t, uint32_t bpp, uint32_t num_samples, bool alt, uint32_t* w,
                         uint32_t* h) {
    // Formula from the comment above shadPS4's tile.h macro_tile_extents tables. The table
    // columns are bpp 8,16,32,64,128 (bit_width(bpp)-4), so 96 bpp uses the 64 bpp column.
    *w = *h = 0;
    if (!IsMacroTiled(GetArrayMode(t))) return;
    const uint32_t col = BitWidth(bpp) - 4;
    const uint32_t table_bpp = 8u << col;
    const MacroTileMode m = CalculateMacrotileMode(t, table_bpp, num_samples);
    const uint32_t pipes = GetPipeCount(alt ? GetAltPipeConfig(t) : GetPipeConfig(t));
    const uint32_t aspect = alt ? GetAltMacrotileAspect(m) : GetMacrotileAspect(m);
    const uint32_t banks = alt ? GetAltNumBanks(m) : GetNumBanks(m);
    const uint32_t bank_h = alt ? GetAltBankHeight(m) : GetBankHeight(m);
    *w = pipes * GetBankWidth(m) * kMicroTileSize * aspect;
    *h = banks * bank_h * kMicroTileSize / aspect;
}

// ---- image size (tile.h) ----

static void SizeLinearAligned(uint32_t pitch, uint32_t height, uint32_t bpp, uint32_t samples,
                              uint32_t* op, uint32_t* oh, uint64_t* osz) {
    const uint32_t pitch_align = std::max(8u, 64u / ((bpp + 7) / 8));
    uint32_t pa = (pitch + pitch_align - 1) & ~(pitch_align - 1);
    const uint32_t ha = height;
    uint64_t log_sz = (uint64_t)pa * ha * samples;
    const uint32_t slice_align = std::max(64u, 256u / ((bpp + 7) / 8));
    while (log_sz % slice_align) {
        pa += pitch_align;
        log_sz = (uint64_t)pa * ha * samples;
    }
    *op = pa;
    *oh = ha;
    *osz = (log_sz * bpp + 7) / 8;
}

static void SizeMicroTiled(uint32_t pitch, uint32_t height, uint32_t thickness, uint32_t bpp,
                           uint32_t samples, uint32_t* op, uint32_t* oh, uint64_t* osz) {
    uint32_t pa = (pitch + 7) & ~7u;
    const uint32_t ha = (height + 7) & ~7u;
    uint64_t log_sz = ((uint64_t)pa * ha * bpp * samples + 7) / 8;
    while ((log_sz * thickness) % 256) {
        pa += 8;
        log_sz = ((uint64_t)pa * ha * bpp * samples + 7) / 8;
    }
    *op = pa;
    *oh = ha;
    *osz = log_sz;
}

static bool SizeMacroTiled(uint32_t pitch, uint32_t height, uint32_t bpp, uint32_t samples,
                           TileMode t, bool alt, uint32_t* op, uint32_t* oh, uint64_t* osz) {
    uint32_t pw, ph;
    GetMacroTileExtents(t, bpp, samples, alt, &pw, &ph);
    if (pw == 0 || ph == 0) return false;
    const uint32_t pa = (pitch + pw - 1) & ~(pw - 1);
    const uint32_t ha = (height + ph - 1) & ~(ph - 1);
    *op = pa;
    *oh = ha;
    *osz = ((uint64_t)pa * ha * samples * bpp + 7) / 8;
    return true;
}

static bool IsMacroTiledMip(uint32_t pitch, uint32_t height, uint32_t bpp, uint32_t samples,
                            TileMode t, uint32_t mip, bool alt) {
    if (mip == 0) return true;
    uint32_t pw, ph;
    GetMacroTileExtents(t, bpp, samples, alt, &pw, &ph);
    return pitch >= pw && height >= ph;
}

bool ComputeSurfaceLayout(const SurfaceDesc& d, SurfaceLayout* out) {
    *out = SurfaceLayout{};
    if (!IsValidTileMode((uint32_t)d.tile_mode) || d.levels == 0 || d.levels > kMaxMips ||
        d.bpp == 0 || d.bpp > 128 || (d.bpp & 7) != 0 || !HasSingleBit(d.num_samples) ||
        d.num_samples > 8) {
        return false;
    }
    const ArrayMode am = GetArrayMode(d.tile_mode);
    out->array_mode = am;
    const uint32_t thickness = GetMicroTileThickness(am);
    const bool macro = IsMacroTiled(am);
    uint64_t total = 0;
    for (uint32_t mip = 0; mip < d.levels; ++mip) {
        uint32_t mip_w = d.pitch >> mip;
        uint32_t mip_h = d.height >> mip;
        if (d.is_block) {
            mip_w = (mip_w + 3) / 4;
            mip_h = (mip_h + 3) / 4;
        }
        mip_w = std::max(mip_w, 1u);
        mip_h = std::max(mip_h, 1u);
        uint32_t mip_d = std::max(d.depth >> mip, 1u);
        if (d.is_pow2) {
            mip_w = BitCeil(mip_w);
            mip_h = BitCeil(mip_h);
            mip_d = BitCeil(mip_d);
        }
        MipLayout& m = out->mips[mip];
        uint32_t mip_thickness = 1;
        uint64_t sz = 0;
        if (am == ArrayMode::ArrayLinearGeneral) {
            // shadPS4 does not handle LinearGeneral; treat as unpadded linear.
            m.pitch = mip_w;
            m.height = mip_h;
            sz = ((uint64_t)mip_w * mip_h * d.bpp * d.num_samples + 7) / 8;
        } else if (am == ArrayMode::ArrayLinearAligned) {
            SizeLinearAligned(mip_w, mip_h, d.bpp, d.num_samples, &m.pitch, &m.height, &sz);
        } else if (macro &&
                   IsMacroTiledMip(mip_w, mip_h, d.bpp, d.num_samples, d.tile_mode, mip, d.alt_tile)) {
            mip_thickness = thickness;
            if (!SizeMacroTiled(mip_w, mip_h, d.bpp, d.num_samples, d.tile_mode, d.alt_tile,
                                &m.pitch, &m.height, &sz)) {
                return false;
            }
        } else {
            mip_thickness = std::min(thickness, 4u);
            SizeMicroTiled(mip_w, mip_h, mip_thickness, d.bpp, d.num_samples, &m.pitch, &m.height, &sz);
            if (macro) {
                out->micro_tiled_mips |= 1u << mip;
                m.micro_tiled = true;
            }
        }
        uint32_t num_slices = mip_d * d.layers;
        num_slices += (0u - num_slices) & (mip_thickness - 1);
        m.slices = num_slices;
        m.size = (uint32_t)(sz * num_slices);
        m.offset = (uint32_t)total;
        total += (uint64_t)m.size;
    }
    out->num_mips = d.levels;
    out->total_size = (uint32_t)std::min<uint64_t>(total, 0xFFFFFFFFull);
    return total <= 0xFFFFFFFFull;
}

void MakeSingleLevelLayout(const SurfaceDesc& d, uint32_t padded_pitch, uint32_t padded_height,
                           uint32_t slice_bytes, SurfaceLayout* out) {
    // shadPS4 ImageInfo(ColorBuffer/DepthBuffer): mips_layout[0] = {slice*layers, pitch, height, 0}
    *out = SurfaceLayout{};
    out->array_mode = GetArrayMode(d.tile_mode);
    out->num_mips = 1;
    MipLayout& m = out->mips[0];
    m.offset = 0;
    m.pitch = padded_pitch;
    m.height = padded_height;
    m.slices = std::max(1u, d.layers);
    m.size = slice_bytes * m.slices;
    out->total_size = m.size;
}

// ---- address computation (tiling.comp) ----

struct TileParams {
    uint32_t bpp = 32;           // BITS_PER_PIXEL
    uint32_t samples = 1;        // NUM_SAMPLES
    MicroTileMode micro = MicroTileMode::Thin;
    uint32_t thickness = 1;      // MICRO_TILE_THICKNESS
    ArrayMode array = ArrayMode::ArrayLinearAligned;
    bool macro = false;
    bool tiled = false;
    uint32_t num_pipes = 8, pipe_bits = 3;
    PipeConfig pipe_cfg = PipeConfig::P8_32x32_16x16;
    uint32_t bank_width = 1, bank_height = 1, num_banks = 16, bank_bits = 4;
    uint32_t tile_split = 1024, aspect = 1;
    uint32_t bank_swizzle = 0;
};

static bool MakeTileParams(const SurfaceDesc& d, TileParams* p) {
    *p = TileParams{};
    p->bpp = d.bpp;
    p->samples = d.num_samples;
    p->array = GetArrayMode(d.tile_mode);
    p->micro = GetMicroTileMode(d.tile_mode);
    p->thickness = GetMicroTileThickness(p->array);
    p->macro = IsMacroTiled(p->array);
    p->tiled = d.tile_mode != TileMode::DisplayLinearAligned && d.tile_mode != TileMode::DisplayLinearGeneral;
    p->bank_swizzle = d.bank_swizzle;
    if (p->macro) {
        const MacroTileMode m = CalculateMacrotileMode(d.tile_mode, d.bpp, d.num_samples);
        p->pipe_cfg = GetPipeConfig(d.tile_mode);
        // tiling.comp: NUM_PIPES = PIPE_CONFIG == P2 ? 2 : 8
        p->num_pipes = p->pipe_cfg == PipeConfig::P2 ? 2 : 8;
        p->pipe_bits = p->pipe_cfg == PipeConfig::P2 ? 1 : 3;
        p->bank_width = GetBankWidth(m);
        p->bank_height = GetBankHeight(m);
        p->num_banks = GetNumBanks(m);
        p->bank_bits = BitWidth(p->num_banks) - 1;
        p->tile_split = CalculateTileSplit(d.tile_mode, p->array, p->micro, d.bpp);
        p->aspect = GetMacrotileAspect(m);
    }
    return true;
}

static inline uint32_t B1(uint32_t v, uint32_t b) {
    return (v >> b) & 1u;
}

static uint32_t PixelIndexWithinMicroTile(const TileParams& P, uint32_t x, uint32_t y, uint32_t z,
                                          uint32_t thickness) {
    uint32_t p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, p7 = 0, p8 = 0;
    const uint32_t x0 = B1(x, 0), x1 = B1(x, 1), x2 = B1(x, 2);
    const uint32_t y0 = B1(y, 0), y1 = B1(y, 1), y2 = B1(y, 2);
    const uint32_t z0 = B1(z, 0), z1 = B1(z, 1), z2 = B1(z, 2);
    if (P.micro == MicroTileMode::Display) {
        switch (P.bpp) {
        case 8: p0 = x0; p1 = x1; p2 = x2; p3 = y1; p4 = y0; p5 = y2; break;
        case 16: p0 = x0; p1 = x1; p2 = x2; p3 = y0; p4 = y1; p5 = y2; break;
        case 32: p0 = x0; p1 = x1; p2 = y0; p3 = x2; p4 = y1; p5 = y2; break;
        case 64: p0 = x0; p1 = y0; p2 = x1; p3 = x2; p4 = y1; p5 = y2; break;
        case 128: p0 = x0; p1 = y0; p2 = x1; p3 = y1; p4 = x2; p5 = y2; break;
        default: break; // 96 bpp: tiling.comp leaves all bits 0
        }
    } else if (P.micro == MicroTileMode::Thin || P.micro == MicroTileMode::Depth) {
        p0 = x0; p1 = y0; p2 = x1; p3 = y1; p4 = x2; p5 = y2;
    } else {
        switch (P.bpp) {
        case 8:
        case 16: p0 = x0; p1 = y0; p2 = x1; p3 = y1; p4 = z0; p5 = z1; break;
        case 32: p0 = x0; p1 = y0; p2 = x1; p3 = z0; p4 = y1; p5 = z1; break;
        case 64:
        case 128: p0 = x0; p1 = y0; p2 = z0; p3 = x1; p4 = y1; p5 = z1; break;
        default: break;
        }
        p6 = x2;
        p7 = y2;
        if (thickness == 8) p8 = z2;
    }
    return p0 | (p1 << 1) | (p2 << 2) | (p3 << 3) | (p4 << 4) | (p5 << 5) | (p6 << 6) | (p7 << 7) |
           (p8 << 8);
}

// element offset in bytes within a micro tile (sample 0)
static inline uint32_t MicroElementOffset(const TileParams& P, uint32_t pixel_index) {
    // depth: pixel_offset = idx*bpp*samples; others idx*bpp (sample_offset = 0)
    return P.micro == MicroTileMode::Depth ? pixel_index * P.bpp * P.samples / 8
                                           : pixel_index * P.bpp / 8;
}

static uint64_t AddrMicroTiled(const TileParams& P, uint32_t x, uint32_t y, uint32_t slice,
                               uint32_t pitch, uint32_t height, uint32_t thickness) {
    const uint64_t micro_tile_bytes = 64ull * thickness * P.bpp * P.samples / 8;
    const uint64_t slice_bytes = ((uint64_t)pitch * height * thickness * P.bpp * P.samples + 7) / 8;
    const uint32_t tiles_per_row = pitch / kMicroTileSize;
    const uint64_t slice_offset = (uint64_t)(slice / thickness) * slice_bytes;
    const uint64_t tile_offset =
        ((uint64_t)(y / kMicroTileSize) * tiles_per_row + x / kMicroTileSize) * micro_tile_bytes;
    const uint32_t pi = PixelIndexWithinMicroTile(P, x, y, slice, thickness);
    return slice_offset + tile_offset + MicroElementOffset(P, pi);
}

static uint32_t PipeFromCoord(const TileParams& P, uint32_t x, uint32_t y, uint32_t slice) {
    const uint32_t tx = x / kMicroTileSize, ty = y / kMicroTileSize;
    const uint32_t x3 = B1(tx, 0), x4 = B1(tx, 1), x5 = B1(tx, 2);
    const uint32_t y3 = B1(ty, 0), y4 = B1(ty, 1), y5 = B1(ty, 2);
    uint32_t p0 = 0, p1 = 0, p2 = 0;
    if (P.pipe_cfg == PipeConfig::P2) {
        p0 = x3 ^ y3;
    } else if (P.pipe_cfg == PipeConfig::P8_32x32_8x16) {
        p0 = x4 ^ y3 ^ x5;
        p1 = x3 ^ y4;
        p2 = x5 ^ y5;
    } else if (P.pipe_cfg == PipeConfig::P8_32x32_16x16) {
        p0 = x3 ^ y3 ^ x4;
        p1 = x4 ^ y4;
        p2 = x5 ^ y5;
    }
    uint32_t pipe = p0 | (p1 << 1) | (p2 << 2);
    uint32_t pipe_swizzle = 0;
    if (P.array == ArrayMode::Array3DTiledThin1 || P.array == ArrayMode::Array3DTiledThick ||
        P.array == ArrayMode::Array3DTiledXThick) {
        pipe_swizzle += std::max(1u, P.num_pipes / 2 - 1) * (slice / P.thickness);
    }
    pipe_swizzle &= (P.num_pipes - 1);
    return pipe ^ pipe_swizzle;
}

static uint32_t BankFromCoord(const TileParams& P, uint32_t x, uint32_t y, uint32_t slice,
                              uint32_t tile_split_slice) {
    const uint32_t tx = x / kMicroTileSize / (P.bank_width * P.num_pipes);
    const uint32_t ty = y / kMicroTileSize / P.bank_height;
    const uint32_t x3 = B1(tx, 0), x4 = B1(tx, 1), x5 = B1(tx, 2), x6 = B1(tx, 3);
    const uint32_t y3 = B1(ty, 0), y4 = B1(ty, 1), y5 = B1(ty, 2), y6 = B1(ty, 3);
    uint32_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (P.num_banks == 16) {
        b0 = x3 ^ y6;
        b1 = x4 ^ y5 ^ y6;
        b2 = x5 ^ y4;
        b3 = x6 ^ y3;
    } else if (P.num_banks == 8) {
        b0 = x3 ^ y5;
        b1 = x4 ^ y4 ^ y5;
        b2 = x5 ^ y3;
    } else if (P.num_banks == 4) {
        b0 = x3 ^ y4;
        b1 = x4 ^ y3;
    } else if (P.num_banks == 2) {
        b0 = x3 ^ y3;
    }
    uint32_t bank = b0 | (b1 << 1) | (b2 << 2) | (b3 << 3);
    uint32_t slice_rotation = 0, tile_split_rotation = 0;
    const ArrayMode a = P.array;
    if (a == ArrayMode::Array2DTiledThin1 || a == ArrayMode::Array2DTiledThick ||
        a == ArrayMode::Array2DTiledXThick) {
        slice_rotation = ((P.num_banks / 2) - 1) * (slice / P.thickness);
    } else if (a == ArrayMode::Array3DTiledThin1 || a == ArrayMode::Array3DTiledThick ||
               a == ArrayMode::Array3DTiledXThick) {
        slice_rotation = std::max(1u, (P.num_pipes / 2) - 1) * (slice / P.thickness) / P.num_pipes;
    }
    if (a == ArrayMode::Array2DTiledThin1 || a == ArrayMode::Array3DTiledThin1 ||
        a == ArrayMode::ArrayPrt2DTiledThin1 || a == ArrayMode::ArrayPrt3DTiledThin1) {
        tile_split_rotation = ((P.num_banks / 2) + 1) * tile_split_slice;
    }
    bank ^= P.bank_swizzle + slice_rotation;
    bank ^= tile_split_rotation;
    bank &= (P.num_banks - 1);
    return bank;
}

struct MacroGeom {
    uint32_t micro_tile_bytes_full; // MICRO_TILE_BYTES
    uint32_t micro_tile_bytes;      // after tile split
    uint32_t slices_per_tile;
    bool split;
    uint32_t macro_tile_pitch, macro_tile_height;
    uint64_t macro_tile_bytes;
    uint32_t macro_tiles_per_row;
    uint64_t slice_bytes;
    uint32_t addr_shift;            // NUM_PIPE_INTERLEAVE_BITS + pipe bits + bank bits
};

static void MakeMacroGeom(const TileParams& P, uint32_t pitch, uint32_t height, MacroGeom* g) {
    g->micro_tile_bytes_full = 64u * P.thickness * P.bpp * P.samples / 8;
    g->micro_tile_bytes = g->micro_tile_bytes_full;
    g->slices_per_tile = 1;
    g->split = g->micro_tile_bytes_full > P.tile_split && P.thickness == 1;
    if (g->split) {
        g->slices_per_tile = g->micro_tile_bytes_full / P.tile_split;
        g->micro_tile_bytes = P.tile_split;
    }
    g->macro_tile_pitch = (kMicroTileSize * P.bank_width * P.num_pipes) * P.aspect;
    g->macro_tile_height = (kMicroTileSize * P.bank_height * P.num_banks) / P.aspect;
    g->macro_tile_bytes = (uint64_t)g->micro_tile_bytes * (g->macro_tile_pitch / kMicroTileSize) *
                          (g->macro_tile_height / kMicroTileSize) / (P.num_pipes * P.num_banks);
    g->macro_tiles_per_row = pitch / g->macro_tile_pitch;
    const uint64_t per_slice = (uint64_t)g->macro_tiles_per_row * (height / g->macro_tile_height);
    g->slice_bytes = per_slice * g->macro_tile_bytes;
    g->addr_shift = kPipeInterleaveBits + P.pipe_bits + P.bank_bits;
}

// base offset (element_offset == 0) for a micro tile, plus the pipe/bank bits
static inline void MacroTileBase(const TileParams& P, const MacroGeom& g, uint32_t x, uint32_t y,
                                 uint32_t slice, uint32_t tile_split_slice, uint64_t* base,
                                 uint64_t* pb_bits) {
    const uint32_t mtx = x / g.macro_tile_pitch, mty = y / g.macro_tile_height;
    const uint64_t macro_tile_offset = ((uint64_t)mty * g.macro_tiles_per_row + mtx) * g.macro_tile_bytes;
    const uint64_t slice_offset =
        g.slice_bytes * (tile_split_slice + (uint64_t)g.slices_per_tile * (slice / P.thickness));
    const uint32_t tile_row = (y / kMicroTileSize) % P.bank_height;
    const uint32_t tile_col = ((x / kMicroTileSize) / P.num_pipes) % P.bank_width;
    const uint64_t tile_offset = (uint64_t)(tile_row * P.bank_width + tile_col) * g.micro_tile_bytes;
    *base = slice_offset + macro_tile_offset + tile_offset;
    if (IsPrt(P.array)) {
        x %= g.macro_tile_pitch;
        y %= g.macro_tile_height;
    }
    const uint32_t pipe = PipeFromCoord(P, x, y, slice);
    const uint32_t bank = BankFromCoord(P, x, y, slice, tile_split_slice);
    *pb_bits = ((uint64_t)pipe << kPipeInterleaveBits) |
               ((uint64_t)bank << (kPipeInterleaveBits + P.pipe_bits));
}

static inline uint64_t MacroCombine(const MacroGeom& g, uint64_t total, uint64_t pb_bits) {
    return (total & ((1u << kPipeInterleaveBits) - 1)) | pb_bits |
           ((total >> kPipeInterleaveBits) << g.addr_shift);
}

static uint64_t AddrMacroTiled(const TileParams& P, uint32_t x, uint32_t y, uint32_t slice,
                               uint32_t pitch, uint32_t height) {
    MacroGeom g;
    MakeMacroGeom(P, pitch, height, &g);
    const uint32_t pi = PixelIndexWithinMicroTile(P, x, y, slice, P.thickness);
    uint32_t e = MicroElementOffset(P, pi);
    uint32_t split_slice = 0;
    if (g.split) {
        split_slice = e / P.tile_split;
        e %= P.tile_split;
    }
    uint64_t base, pb;
    MacroTileBase(P, g, x, y, slice, split_slice, &base, &pb);
    return MacroCombine(g, base + e, pb);
}

uint64_t TiledElementOffset(const SurfaceDesc& d, const SurfaceLayout& l, uint32_t mip, uint32_t x,
                            uint32_t y, uint32_t slice) {
    TileParams P;
    MakeTileParams(d, &P);
    const MipLayout& m = l.mips[mip];
    if (!P.tiled) {
        return m.offset + (((uint64_t)slice * m.height + y) * m.pitch + x) * (d.bpp / 8);
    }
    if (P.macro) {
        if (l.micro_tiled_mips & (1u << mip)) {
            return m.offset + AddrMicroTiled(P, x, y, slice, m.pitch, m.height, std::min(P.thickness, 4u));
        }
        return m.offset + AddrMacroTiled(P, x, y, slice, m.pitch, m.height);
    }
    return m.offset + AddrMicroTiled(P, x, y, slice, m.pitch, m.height, P.thickness);
}

// ---- fast (de)tiler ----

template <bool kTile>
static inline void CopyElem(const uint8_t* tiled_src, uint8_t* tiled_dst, size_t tiled_size,
                            uint64_t taddr, uint8_t* lin_dst, const uint8_t* lin_src, uint32_t bpe) {
    if (taddr + bpe > tiled_size) {
        if (!kTile) memset(lin_dst, 0, bpe);
        return;
    }
    if (kTile) {
        memcpy(tiled_dst + taddr, lin_src, bpe);
    } else {
        // common sizes get fixed-size copies
        switch (bpe) {
        case 4: memcpy(lin_dst, tiled_src + taddr, 4); break;
        case 8: memcpy(lin_dst, tiled_src + taddr, 8); break;
        case 16: memcpy(lin_dst, tiled_src + taddr, 16); break;
        case 2: memcpy(lin_dst, tiled_src + taddr, 2); break;
        case 1: lin_dst[0] = tiled_src[taddr]; break;
        default: memcpy(lin_dst, tiled_src + taddr, bpe); break;
        }
    }
}

// Runs f(0..n-1), spread over the CPU cores when the work is large enough to pay for it
// (tiling large surfaces, e.g. 4K render targets, takes tens of milliseconds on one core).
template <class F>
static void ParallelFor(uint32_t n, bool parallel, F&& f) {
    if (!parallel || n < 2) {
        for (uint32_t i = 0; i < n; ++i) f(i);
        return;
    }
    using Fn = std::remove_reference_t<F>;
    dispatch_apply_f(n, DISPATCH_APPLY_AUTO, (void*)&f,
                     [](void* ctx, size_t i) { (*static_cast<Fn*>(ctx))((uint32_t)i); });
}

// kMode: 0 detile (tiled_src -> lin_dst), 1 tile (lin_src -> tiled_dst), 2 map: for every
// linear element store its tiled byte offset in map[linear offset / bpe] (~0u past tiled_size).
template <int kMode>
static bool ProcessSurface(const SurfaceDesc& d, const SurfaceLayout& l, const uint8_t* tiled_src,
                           uint8_t* tiled_dst, size_t tiled_size, uint8_t* lin_dst,
                           const uint8_t* lin_src, size_t linear_size, uint32_t first_mip,
                           uint32_t num_mips, bool visible_only, uint32_t* map = nullptr) {
    constexpr bool kTile = kMode == 1;
    TileParams P;
    if (!MakeTileParams(d, &P)) return false;
    const uint32_t bpe = d.bpp / 8;
    const uint32_t last = (uint32_t)std::min<uint64_t>((uint64_t)first_mip + num_mips, l.num_mips);
    for (uint32_t mip = first_mip; mip < last; ++mip) {
        const MipLayout& m = l.mips[mip];
        const uint32_t pitch = m.pitch, height = m.height;
        if (pitch == 0 || height == 0) continue;
        uint32_t w = pitch, h = height, slices = m.slices;
        if (visible_only) {
            uint32_t vw = std::max(1u, d.width >> mip), vh = std::max(1u, d.height >> mip);
            if (d.is_block) {
                vw = (vw + 3) / 4;
                vh = (vh + 3) / 4;
            }
            w = std::min(w, vw);
            h = std::min(h, vh);
            slices = std::min(slices, std::max(1u, d.depth >> mip) * d.layers);
        }
        const uint64_t lin_slice = (uint64_t)pitch * height * bpe;
        if ((uint64_t)m.offset + lin_slice * slices > linear_size) {
            return false;
        }
        if (!P.tiled) {
            // linear: rows are already laid out like the linear buffer
            const uint64_t row = (uint64_t)pitch * bpe;
            for (uint32_t s = 0; s < slices; ++s) {
                for (uint32_t y = 0; y < h; ++y) {
                    const uint64_t off = m.offset + s * lin_slice + y * row;
                    const uint64_t n = (uint64_t)w * bpe;
                    if constexpr (kMode == 2) {
                        for (uint32_t x = 0; x < w; ++x) {
                            const uint64_t o = off + (uint64_t)x * bpe;
                            map[o / bpe] = o + bpe <= tiled_size ? (uint32_t)o : ~0u;
                        }
                        continue;
                    }
                    if (off + n > tiled_size) {
                        if (!kTile) memset(lin_dst + off, 0, n);
                        continue;
                    }
                    if (kTile) memcpy(tiled_dst + off, lin_src + off, n);
                    else memcpy(lin_dst + off, tiled_src + off, n);
                }
            }
            continue;
        }
        const bool micro_path = !P.macro || (l.micro_tiled_mips & (1u << mip));
        const uint32_t thick = P.macro ? std::min(P.thickness, 4u) : P.thickness;
        const uint32_t call_thick = micro_path ? thick : P.thickness;
        // element-offset LUT for (x&7, y&7, z&7)
        uint32_t lut[512];
        for (uint32_t z = 0; z < 8; ++z)
            for (uint32_t y = 0; y < 8; ++y)
                for (uint32_t x = 0; x < 8; ++x)
                    lut[(z << 6) | (y << 3) | x] =
                        MicroElementOffset(P, PixelIndexWithinMicroTile(P, x, y, z, call_thick));
        const uint32_t tiles_x = (w + 7) / 8, tiles_y = (h + 7) / 8;
        // every tile row writes its own linear rows / tiles: rows run in parallel on big surfaces
        const bool parallel = (uint64_t)tiles_x * tiles_y >= 4096;
        if (micro_path) {
            const uint64_t mtb = 64ull * thick * P.bpp * P.samples / 8;
            const uint64_t slice_bytes = ((uint64_t)pitch * height * thick * P.bpp * P.samples + 7) / 8;
            const uint32_t tiles_per_row = pitch / 8;
            for (uint32_t s = 0; s < slices; ++s) {
                const uint64_t soff = m.offset + (uint64_t)(s / thick) * slice_bytes;
                const uint32_t zl = (s & 7) << 6;
                ParallelFor(tiles_y, parallel, [&](uint32_t ty) {
                    for (uint32_t tx = 0; tx < tiles_x; ++tx) {
                        const uint64_t tbase = soff + ((uint64_t)ty * tiles_per_row + tx) * mtb;
                        const uint32_t y0 = ty * 8, x0 = tx * 8;
                        const uint32_t ye = std::min(h, y0 + 8), xe = std::min(w, x0 + 8);
                        for (uint32_t y = y0; y < ye; ++y) {
                            const uint64_t lrow = m.offset + s * lin_slice + ((uint64_t)y * pitch) * bpe;
                            const uint32_t* lrowlut = &lut[zl | ((y & 7) << 3)];
                            for (uint32_t x = x0; x < xe; ++x) {
                                const uint64_t ta = tbase + lrowlut[x & 7];
                                const uint64_t lo = lrow + (uint64_t)x * bpe;
                                if constexpr (kMode == 2)
                                    map[lo / bpe] = ta + bpe <= tiled_size ? (uint32_t)ta : ~0u;
                                else
                                    CopyElem<kTile>(tiled_src, tiled_dst, tiled_size, ta, lin_dst + lo,
                                                    lin_src + lo, bpe);
                            }
                        }
                    }
                });
            }
        } else {
            MacroGeom g;
            MakeMacroGeom(P, pitch, height, &g);
            const uint32_t nsplit = g.split ? g.slices_per_tile : 1;
            for (uint32_t s = 0; s < slices; ++s) {
                const uint32_t zl = (s & 7) << 6;
                ParallelFor(tiles_y, parallel, [&](uint32_t ty) {
                    uint64_t bases[16], pbs[16];
                    for (uint32_t tx = 0; tx < tiles_x; ++tx) {
                        const uint32_t y0 = ty * 8, x0 = tx * 8;
                        for (uint32_t sp = 0; sp < nsplit && sp < 16; ++sp) {
                            MacroTileBase(P, g, x0, y0, s, sp, &bases[sp], &pbs[sp]);
                        }
                        const uint32_t ye = std::min(h, y0 + 8), xe = std::min(w, x0 + 8);
                        for (uint32_t y = y0; y < ye; ++y) {
                            const uint64_t lrow = m.offset + s * lin_slice + ((uint64_t)y * pitch) * bpe;
                            const uint32_t* lrowlut = &lut[zl | ((y & 7) << 3)];
                            for (uint32_t x = x0; x < xe; ++x) {
                                uint32_t e = lrowlut[x & 7];
                                uint32_t sp = 0;
                                if (g.split) {
                                    sp = e / P.tile_split;
                                    e %= P.tile_split;
                                }
                                const uint64_t ta = m.offset + MacroCombine(g, bases[sp] + e, pbs[sp]);
                                const uint64_t lo = lrow + (uint64_t)x * bpe;
                                if constexpr (kMode == 2)
                                    map[lo / bpe] = ta + bpe <= tiled_size ? (uint32_t)ta : ~0u;
                                else
                                    CopyElem<kTile>(tiled_src, tiled_dst, tiled_size, ta, lin_dst + lo,
                                                    lin_src + lo, bpe);
                            }
                        }
                    }
                });
            }
        }
    }
    return true;
}

bool DetileSurface(const SurfaceDesc& d, const SurfaceLayout& l, const uint8_t* tiled,
                   size_t tiled_size, uint8_t* linear, size_t linear_size, uint32_t first_mip,
                   uint32_t num_mips, bool visible_only) {
    if (!tiled || !linear) return false;
    return ProcessSurface<0>(d, l, tiled, nullptr, tiled_size, linear, nullptr, linear_size,
                                 first_mip, num_mips, visible_only);
}

bool TileSurface(const SurfaceDesc& d, const SurfaceLayout& l, const uint8_t* linear,
                 size_t linear_size, uint8_t* tiled, size_t tiled_size, uint32_t first_mip,
                 uint32_t num_mips, bool visible_only) {
    if (!tiled || !linear) return false;
    return ProcessSurface<1>(d, l, nullptr, tiled, tiled_size, nullptr, linear, linear_size,
                                first_mip, num_mips, visible_only);
}

bool BuildDetileMap(const SurfaceDesc& d, const SurfaceLayout& l, size_t tiled_size, uint32_t first_mip,
                    uint32_t num_mips, std::vector<uint32_t>* map) {
    const uint32_t bpe = d.bpp / 8;
    if (!map || bpe == 0 || l.total_size == 0 || tiled_size > 0xFFFFFFFFull) return false;
    map->assign((size_t)(l.total_size / bpe), ~0u);
    return ProcessSurface<2>(d, l, nullptr, nullptr, tiled_size, nullptr, nullptr, l.total_size, first_mip,
                             num_mips, true, map->data());
}

// =====================================================================================
// 2. Descriptors
// =====================================================================================

const char* NameOf(ImageType t) {
    switch (t) {
    case ImageType::Invalid: return "Invalid";
    case ImageType::Color1D: return "Color1D";
    case ImageType::Color2D: return "Color2D";
    case ImageType::Color3D: return "Color3D";
    case ImageType::Cube: return "Cube";
    case ImageType::Color1DArray: return "Color1DArray";
    case ImageType::Color2DArray: return "Color2DArray";
    case ImageType::Color2DMsaa: return "Color2DMsaa";
    case ImageType::Color2DMsaaArray: return "Color2DMsaaArray";
    default: return "Unknown";
    }
}

static inline uint64_t Q(const uint32_t* dw, uint32_t i) {
    return (uint64_t)dw[2 * i] | ((uint64_t)dw[2 * i + 1] << 32);
}

static bool DecodeImageUncached(const uint32_t* dwords, uint32_t num_dwords, ImageDesc* out);
static std::atomic<uint64_t> g_uncreatable_images{0};

// A draw decodes the same T# several times (hazard check, depth companions, binding):
// remember recent results per thread (the decode is a pure function of the 8 dwords).
bool DecodeImage(const uint32_t* dwords, uint32_t num_dwords, ImageDesc* out) {
    struct Slot {
        uint32_t raw[8];
        uint32_t n;
        bool used, ok;
        ImageDesc desc;
    };
    static thread_local Slot cache[512];
    uint32_t raw[8];
    for (uint32_t i = 0; i < 8; ++i) raw[i] = i < num_dwords ? dwords[i] : 0;
    uint64_t h = 1469598103934665603ull ^ num_dwords;
    for (uint32_t i = 0; i < 8; ++i) h = (h ^ raw[i]) * 1099511628211ull;
    Slot& sl = cache[(h ^ (h >> 29)) & 511];
    if (sl.used && sl.n == num_dwords && memcmp(sl.raw, raw, sizeof(raw)) == 0) {
        *out = sl.desc;
        return sl.ok;
    }
    const bool ok = DecodeImageUncached(raw, num_dwords < 8 ? num_dwords : 8, out);
    memcpy(sl.raw, raw, sizeof(raw));
    sl.n = num_dwords;
    sl.desc = *out;
    sl.ok = ok;
    sl.used = true;
    return ok;
}

static bool DecodeImageUncached(const uint32_t* dwords, uint32_t num_dwords, ImageDesc* out) {
    *out = ImageDesc{};
    for (uint32_t i = 0; i < 8; ++i) out->raw[i] = i < num_dwords ? dwords[i] : 0;
    const uint32_t* dw = out->raw;
    const uint64_t q0 = Q(dw, 0), q1 = Q(dw, 1), q2 = Q(dw, 2), q3 = Q(dw, 3);
    out->address = Bits64(q0, 0, 38) << 8;
    out->min_lod = (float)Bits64(q0, 40, 12) / 256.f;
    out->raw_dfmt = (uint32_t)Bits64(q0, 52, 6);
    out->raw_nfmt = (uint32_t)Bits64(q0, 58, 4);
    out->width = (uint32_t)Bits64(q1, 0, 14) + 1;
    out->height = (uint32_t)Bits64(q1, 14, 14) + 1;
    out->dst_sel_raw.r = (CompSwizzle)Bits64(q1, 32, 3);
    out->dst_sel_raw.g = (CompSwizzle)Bits64(q1, 35, 3);
    out->dst_sel_raw.b = (CompSwizzle)Bits64(q1, 38, 3);
    out->dst_sel_raw.a = (CompSwizzle)Bits64(q1, 41, 3);
    out->base_level = (uint32_t)Bits64(q1, 44, 4);
    out->last_level = (uint32_t)Bits64(q1, 48, 4);
    out->tile_mode = (TileMode)Bits64(q1, 52, 5);
    out->pow2pad = Bits64(q1, 57, 1) != 0;
    const uint32_t type = (uint32_t)Bits64(q1, 60, 4);
    out->type = (ImageType)type;
    const uint32_t depth_field = (uint32_t)Bits64(q2, 0, 13);
    out->pitch = (uint32_t)Bits64(q2, 13, 14) + 1;
    out->base_array = (uint32_t)Bits64(q2, 32, 13);
    out->last_array = (uint32_t)Bits64(q2, 45, 13);
    out->alt_tile_mode = Bits64(q3, 24, 1) != 0;
    out->valid = (type & 0x8u) != 0;

    const DataFormat rd = (DataFormat)out->raw_dfmt;
    out->dfmt = RemapDataFormat(rd);
    out->nfmt = RemapNumberFormat((NumberFormat)out->raw_nfmt, rd);
    out->conversion = MapNumberConversion((NumberFormat)out->raw_nfmt, rd);
    out->swizzle = RemapSwizzle(rd, out->dst_sel_raw);
    out->tiled = out->tile_mode != TileMode::DisplayLinearAligned &&
                 out->tile_mode != TileMode::DisplayLinearGeneral;

    const bool msaa = out->type == ImageType::Color2DMsaa || out->type == ImageType::Color2DMsaaArray;
    out->num_levels = msaa ? 1 : out->last_level + 1;
    out->num_samples = msaa ? 1u << out->last_level : 1;
    out->depth = out->type == ImageType::Color3D ? depth_field + 1 : 1;
    uint32_t slices = depth_field + 1;
    if (out->type == ImageType::Color3D) slices = 1;
    else if (out->type == ImageType::Cube) slices *= 6;
    // Non-array types: shadPS4 keeps depth+1 layers (games usually set depth 0 there).
    out->num_layers = slices;
    // POW2PAD pads the slice count of the guest layout only: the padded slices hold no image data
    // a view can name (last_array stays below depth + 1), so the texture is made of the real ones.
    // (inFAMOUS Second Son: 256 cubes, 1536 faces, laid out as 2048.)
    out->layout_layers = out->pow2pad ? BitCeil(slices) : slices;

    // Neo bank swizzle (shadPS4 Image::GetBankSwizzle)
    if (out->alt_tile_mode && out->dfmt != DataFormat::FormatInvalid &&
        IsValidTileMode((uint32_t)out->tile_mode) && IsMacroTiled(GetArrayMode(out->tile_mode))) {
        const uint32_t bpp = NumBitsPerElement(out->dfmt);
        if (bpp >= 8) {
            const MacroTileMode m = CalculateMacrotileMode(out->tile_mode, bpp, out->num_samples);
            const uint32_t banks = GetAltNumBanks(m);
            out->bank_swizzle = (uint8_t)((((banks - 1) << 4) & Bits64(q0, 0, 38)) >> 4);
        }
    }

    out->pixel = GetPixelFormat(out->dfmt, out->nfmt);
    out->view_swizzle = ComposeViewSwizzle(out->pixel, out->swizzle);
    // A 1D image has one row (GCN ignores the height field of a 1D T#; the guest layout is sized
    // with height 1). Every 1D T# is made as a 2D texture of height 1 (a 2D array for
    // Color1DArray), which holds the same texels and reads the same way, since GCN samples a 1D
    // image along x: the translator declares every 1D image as a 2D one and reads it as one
    // (libgcn_recompiler Image::Is1DAs2D): coordinates (x) become (x, 0) for loads and (u, 0.5)
    // for samples. Metal could not make a 1D texture with a block-compressed format or with mips
    // anyway. Uploads and detiling take the 2D path; a block row of a level shorter than a block
    // is padded, as for any level smaller than a block.
    const bool is1d = out->type == ImageType::Color1D || out->type == ImageType::Color1DArray;
    if (is1d) out->height = 1;
    switch (out->type) {
    case ImageType::Color1D: out->mtl_texture_type = MTLTextureType2D; break;
    case ImageType::Color1DArray: out->mtl_texture_type = MTLTextureType2DArray; break;
    case ImageType::Color2D: out->mtl_texture_type = MTLTextureType2D; break;
    case ImageType::Color2DArray: out->mtl_texture_type = MTLTextureType2DArray; break;
    case ImageType::Color2DMsaa: out->mtl_texture_type = MTLTextureType2DMultisample; break;
    case ImageType::Color2DMsaaArray: out->mtl_texture_type = MTLTextureType2DMultisampleArray; break;
    case ImageType::Cube:
        out->mtl_texture_type = out->num_layers > 6 ? MTLTextureTypeCubeArray : MTLTextureTypeCube;
        break;
    case ImageType::Color3D: out->mtl_texture_type = MTLTextureType3D; break;
    default: out->mtl_texture_type = MTLTextureType2D; break;
    }
    // A T# no Metal texture can be made of (MTLTextureDescriptor validation would abort the
    // process): multisampled with more than 8 samples, 3D past 2048 texels, a cube that is not
    // square, more layers than an array texture holds (Apple GPUs: arrayLength 2048 for a 2D
    // array, 341 cubes = 2046 faces for a cube array; validation: "arrayLength (342) is set to zero
    // or greater than the max allowed size (341)"). It decodes as invalid (a null binding),
    // counted and logged. The unused entries of T# tables, which hold no T# (Uncharted 4's lights:
    // floats), decode to such T#s. The same check is libgcn_recompiler's IsCreatableImage
    // (resource.h), which makes such a table entry count as unbound in the shader's specialization.
    if (out->valid && IsValidTileMode((uint32_t)out->tile_mode)) {
        const bool cube = out->type == ImageType::Cube;
        const bool is3d = out->type == ImageType::Color3D;
        if ((msaa && out->last_level > 3) ||
            (is3d && (out->width > 2048 || out->height > 2048 || out->depth > 2048)) ||
            (cube && out->width != out->height) || out->num_layers > (cube ? 2046u : 2048u)) {
            const uint64_t n = g_uncreatable_images.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n & (n - 1)) == 0)
                fprintf(stderr,
                        "[gnm-tex] T# %08x %08x %08x %08x %08x %08x %08x %08x: type %u %ux%ux%u, %u levels, %u "
                        "layers, dfmt %u: not a texture Metal can create, bound as null (x%llu)\n",
                        dw[0], dw[1], dw[2], dw[3], dw[4], dw[5], dw[6], dw[7], type, out->width, out->height,
                        out->depth, out->num_levels, out->num_layers, out->raw_dfmt, (unsigned long long)n);
            out->valid = false;
        }
    }
    return out->valid && IsValidTileMode((uint32_t)out->tile_mode);
}

uint64_t UncreatableImageCount() {
    return g_uncreatable_images.load(std::memory_order_relaxed);
}

extern "C" int32_t sceKernelIsNeoMode(void);
SurfaceDesc ImageDesc::ToSurfaceDesc() const {
    return ToSurfaceDesc(sceKernelIsNeoMode() != 0);
}

SurfaceDesc ImageDesc::ToSurfaceDesc(bool neo_mode) const {
    // shadPS4 ImageInfo(const AmdGpu::Image&, ...)
    SurfaceDesc s;
    s.tile_mode = tile_mode;
    s.bpp = NumBitsPerBlock(dfmt);
    s.num_samples = num_samples;
    s.width = width;
    s.height = height;
    s.depth = type == ImageType::Color3D ? depth : 1;
    s.pitch = pitch;
    s.levels = std::min<uint32_t>(num_levels, kMaxMips);
    s.layers = layout_layers;
    s.is_block = IsBlockCoded(dfmt);
    s.is_pow2 = pow2pad;
    s.alt_tile = alt_tile_mode && neo_mode; // shadPS4 ImageInfo: alt_tile = IsNeoMode()
    s.bank_swizzle = alt_tile_mode ? bank_swizzle : 0;
    return s;
}

uint32_t ImageDesc::NumViewLevels() const {
    if (type == ImageType::Color2DMsaa || type == ImageType::Color2DMsaaArray) return 1;
    const uint32_t max_level = std::min(last_level + 1, num_levels);
    return max_level > base_level ? max_level - base_level : 1;
}

uint32_t ImageDesc::NumViewLayers(bool is_array) const {
    ImageType vt = type;
    if (!is_array) {
        if (vt == ImageType::Color1DArray) vt = ImageType::Color1D;
        if (vt == ImageType::Color2DArray) vt = ImageType::Color2D;
        if (vt == ImageType::Color2DMsaaArray) vt = ImageType::Color2DMsaa;
    }
    switch (vt) {
    case ImageType::Color1D:
    case ImageType::Color2D:
    case ImageType::Color2DMsaa:
    case ImageType::Color3D:
        return 1;
    default: {
        const uint32_t max_array = std::min(last_array + 1, num_layers);
        return max_array > base_array ? max_array - base_array : 1;
    }
    }
}

bool DecodeBuffer(const uint32_t dwords[4], BufferDesc* out) {
    *out = BufferDesc{};
    memcpy(out->raw, dwords, 16);
    const uint64_t q0 = Q(dwords, 0);
    out->address = Bits64(q0, 0, 40);
    out->stride = (uint32_t)Bits64(q0, 48, 14);
    out->cache_swizzle = Bits64(q0, 62, 1) != 0;
    out->swizzle_enable = Bits64(q0, 63, 1) != 0;
    out->num_records = dwords[2];
    const uint32_t d3 = dwords[3];
    out->dst_sel_raw.r = (CompSwizzle)Bits(d3, 0, 3);
    out->dst_sel_raw.g = (CompSwizzle)Bits(d3, 3, 3);
    out->dst_sel_raw.b = (CompSwizzle)Bits(d3, 6, 3);
    out->dst_sel_raw.a = (CompSwizzle)Bits(d3, 9, 3);
    out->raw_nfmt = Bits(d3, 12, 3);
    out->raw_dfmt = Bits(d3, 15, 4);
    out->element_size = 2u << Bits(d3, 19, 2);
    out->index_stride = 8u << Bits(d3, 21, 2);
    out->add_tid_enable = Bits(d3, 23, 1) != 0;
    out->valid = Bits(d3, 30, 2) == 0;
    out->size = out->stride == 0 ? out->num_records : out->stride * out->num_records;
    const DataFormat rd = (DataFormat)out->raw_dfmt;
    out->dfmt = RemapDataFormat(rd);
    out->nfmt = RemapNumberFormat((NumberFormat)out->raw_nfmt, rd);
    out->swizzle = RemapSwizzle(rd, out->dst_sel_raw);
    out->vertex = GetVertexFormat(out->raw_dfmt, out->raw_nfmt);
    return out->valid;
}

static uint32_t MtlCompare(uint32_t gcn) {
    switch (gcn & 7) {
    case 0: return MTLCompareFunctionNever;
    case 1: return MTLCompareFunctionLess;
    case 2: return MTLCompareFunctionEqual;
    case 3: return MTLCompareFunctionLessEqual;
    case 4: return MTLCompareFunctionGreater;
    case 5: return MTLCompareFunctionNotEqual;
    case 6: return MTLCompareFunctionGreaterEqual;
    default: return MTLCompareFunctionAlways;
    }
}

static uint32_t MtlAddressMode(ClampMode m, bool* exact) {
    // shadPS4 LiverpoolToVK::ClampMode (radv mapping)
    switch (m) {
    case ClampMode::Wrap: return MTLSamplerAddressModeRepeat;
    case ClampMode::Mirror: return MTLSamplerAddressModeMirrorRepeat;
    case ClampMode::ClampLastTexel: return MTLSamplerAddressModeClampToEdge;
    case ClampMode::MirrorOnceHalfBorder:
    case ClampMode::MirrorOnceBorder:
        *exact = false;
        return MTLSamplerAddressModeMirrorClampToEdge;
    case ClampMode::MirrorOnceLastTexel: return MTLSamplerAddressModeMirrorClampToEdge;
    case ClampMode::ClampHalfBorder:
        *exact = false;
        return MTLSamplerAddressModeClampToBorderColor;
    case ClampMode::ClampBorder: return MTLSamplerAddressModeClampToBorderColor;
    default: *exact = false; return MTLSamplerAddressModeRepeat;
    }
}

bool DecodeSampler(const uint32_t dwords[4], SamplerDesc* s) {
    *s = SamplerDesc{};
    memcpy(s->raw, dwords, 16);
    const uint64_t r0 = Q(dwords, 0), r1 = Q(dwords, 1);
    s->clamp_x = (ClampMode)Bits64(r0, 0, 3);
    s->clamp_y = (ClampMode)Bits64(r0, 3, 3);
    s->clamp_z = (ClampMode)Bits64(r0, 6, 3);
    s->max_aniso_ratio = (uint32_t)Bits64(r0, 9, 3);
    s->depth_compare = (uint32_t)Bits64(r0, 12, 3);
    s->force_unnormalized = Bits64(r0, 15, 1) != 0;
    s->force_degamma = Bits64(r0, 20, 1) != 0;
    s->trunc_coord = Bits64(r0, 27, 1) != 0;
    s->disable_cube_wrap = Bits64(r0, 28, 1) != 0;
    s->filter_mode = (FilterMode)Bits64(r0, 29, 2);
    s->min_lod = (float)Bits64(r0, 32, 12) / 256.f;
    s->max_lod = (float)Bits64(r0, 44, 12) / 256.f;
    const uint32_t bias = (uint32_t)Bits64(r1, 0, 14);
    s->lod_bias = (float)(int16_t)((bias ^ 0x2000u) - 0x2000u) / 256.f;
    s->mag_filter = (TexFilter)Bits64(r1, 20, 2);
    s->min_filter = (TexFilter)Bits64(r1, 22, 2);
    s->z_filter = (uint32_t)Bits64(r1, 24, 2);
    s->mip_filter = (MipFilter)Bits64(r1, 26, 2);
    s->border_color_ptr = (uint32_t)Bits64(r1, 32, 12);
    s->border_color_type = (BorderColor)Bits64(r1, 62, 2);

    bool exact = true;
    const auto filt = [](TexFilter f) -> uint32_t {
        return (f == TexFilter::Bilinear || f == TexFilter::AnisoLinear) ? MTLSamplerMinMagFilterLinear
                                                                          : MTLSamplerMinMagFilterNearest;
    };
    s->mtl_mag_filter = filt(s->mag_filter);
    s->mtl_min_filter = filt(s->min_filter);
    switch (s->mip_filter) {
    case MipFilter::None: s->mtl_mip_filter = MTLSamplerMipFilterNotMipmapped; break;
    case MipFilter::Point: s->mtl_mip_filter = MTLSamplerMipFilterNearest; break;
    case MipFilter::Linear: s->mtl_mip_filter = MTLSamplerMipFilterLinear; break;
    default: s->mtl_mip_filter = MTLSamplerMipFilterNearest; exact = false; break;
    }
    s->mtl_address_s = MtlAddressMode(s->clamp_x, &exact);
    s->mtl_address_t = MtlAddressMode(s->clamp_y, &exact);
    s->mtl_address_r = MtlAddressMode(s->clamp_z, &exact);
    const bool aniso = s->mag_filter == TexFilter::AnisoPoint || s->mag_filter == TexFilter::AnisoLinear ||
                       s->min_filter == TexFilter::AnisoPoint || s->min_filter == TexFilter::AnisoLinear;
    static const float kAniso[5] = {1.f, 2.f, 4.f, 8.f, 16.f};
    s->max_anisotropy = aniso ? kAniso[std::min(s->max_aniso_ratio, 4u)] : 1.f;
    switch (s->border_color_type) {
    case BorderColor::TransparentBlack: s->mtl_border_color = MTLSamplerBorderColorTransparentBlack; break;
    case BorderColor::OpaqueBlack: s->mtl_border_color = MTLSamplerBorderColorOpaqueBlack; break;
    case BorderColor::White: s->mtl_border_color = MTLSamplerBorderColorOpaqueWhite; break;
    default:
        // Custom border colors (TA_BC_BASE_ADDR table) are not expressible in Metal.
        s->mtl_border_color = MTLSamplerBorderColorOpaqueBlack;
        exact = false;
        break;
    }
    if (s->filter_mode != FilterMode::Blend) exact = false; // no min/max reduction in Metal
    s->mtl_compare = MtlCompare(s->depth_compare);
    s->normalized_coords = true; // like shadPS4, unnormalized coordinates are handled in the shader
    s->exact = exact;
    return s->max_aniso_ratio <= 4 && (uint32_t)s->filter_mode <= 2 && (uint32_t)s->mip_filter <= 2;
}

static MTLTextureSwizzle ToMTLSwz(CompSwizzle c) {
    switch (c) {
    case CompSwizzle::Zero: return MTLTextureSwizzleZero;
    case CompSwizzle::One: return MTLTextureSwizzleOne;
    case CompSwizzle::Red: return MTLTextureSwizzleRed;
    case CompSwizzle::Green: return MTLTextureSwizzleGreen;
    case CompSwizzle::Blue: return MTLTextureSwizzleBlue;
    case CompSwizzle::Alpha: return MTLTextureSwizzleAlpha;
    default: return MTLTextureSwizzleZero;
    }
}

MTLTextureSwizzleChannels ToMTLSwizzle(CompMapping m) {
    return MTLTextureSwizzleChannelsMake(ToMTLSwz(m.r), ToMTLSwz(m.g), ToMTLSwz(m.b), ToMTLSwz(m.a));
}

MTLSamplerDescriptor* MakeSamplerDescriptor(const SamplerDesc& s, bool compare) {
    MTLSamplerDescriptor* d = [MTLSamplerDescriptor new];
    d.minFilter = (MTLSamplerMinMagFilter)s.mtl_min_filter;
    d.magFilter = (MTLSamplerMinMagFilter)s.mtl_mag_filter;
    d.mipFilter = (MTLSamplerMipFilter)s.mtl_mip_filter;
    d.sAddressMode = (MTLSamplerAddressMode)s.mtl_address_s;
    d.tAddressMode = (MTLSamplerAddressMode)s.mtl_address_t;
    d.rAddressMode = (MTLSamplerAddressMode)s.mtl_address_r;
    d.maxAnisotropy = (NSUInteger)std::max(1.f, std::min(16.f, s.max_anisotropy));
    d.borderColor = (MTLSamplerBorderColor)s.mtl_border_color;
    d.lodMinClamp = s.min_lod;
    d.lodMaxClamp = std::max(s.min_lod, s.max_lod);
    d.normalizedCoordinates = YES;
    if (compare) d.compareFunction = (MTLCompareFunction)s.mtl_compare;
    d.supportArgumentBuffers = YES;
    return d;
}

// =====================================================================================
// 3. Registers
// =====================================================================================

const char* NameOf(PrimitiveType p) {
    switch (p) {
    case PrimitiveType::None: return "None";
    case PrimitiveType::PointList: return "PointList";
    case PrimitiveType::LineList: return "LineList";
    case PrimitiveType::LineStrip: return "LineStrip";
    case PrimitiveType::TriangleList: return "TriangleList";
    case PrimitiveType::TriangleFan: return "TriangleFan";
    case PrimitiveType::TriangleStrip: return "TriangleStrip";
    case PrimitiveType::PatchPrimitive: return "Patch";
    case PrimitiveType::AdjLineList: return "AdjLineList";
    case PrimitiveType::AdjLineStrip: return "AdjLineStrip";
    case PrimitiveType::AdjTriangleList: return "AdjTriangleList";
    case PrimitiveType::AdjTriangleStrip: return "AdjTriangleStrip";
    case PrimitiveType::RectList: return "RectList";
    case PrimitiveType::LineLoop: return "LineLoop";
    case PrimitiveType::QuadList: return "QuadList";
    case PrimitiveType::QuadStrip: return "QuadStrip";
    case PrimitiveType::Polygon: return "Polygon";
    default: return "Prim?";
    }
}

static CompMapping ColorBufferSwizzle(uint32_t raw_format, uint32_t comp_swap) {
    // shadPS4 ColorBuffer::Swizzle()
    using C = CompSwizzle;
    static const C tbl[4][4][4] = {
        {{C::Red, C::Zero, C::Zero, C::Zero}, {C::Red, C::Green, C::Zero, C::Zero},
         {C::Red, C::Green, C::Blue, C::Zero}, {C::Red, C::Green, C::Blue, C::Alpha}},
        {{C::Green, C::Zero, C::Zero, C::Zero}, {C::Red, C::Alpha, C::Zero, C::Zero},
         {C::Red, C::Green, C::Alpha, C::Zero}, {C::Blue, C::Green, C::Red, C::Alpha}},
        {{C::Blue, C::Zero, C::Zero, C::Zero}, {C::Green, C::Red, C::Zero, C::Zero},
         {C::Blue, C::Green, C::Red, C::Zero}, {C::Alpha, C::Blue, C::Green, C::Red}},
        {{C::Alpha, C::Zero, C::Zero, C::Zero}, {C::Alpha, C::Red, C::Zero, C::Zero},
         {C::Alpha, C::Green, C::Red, C::Zero}, {C::Alpha, C::Red, C::Green, C::Blue}},
    };
    const uint32_t nc = NumComponents((DataFormat)raw_format);
    if (nc == 0) return CompMapping{};
    const C* e = tbl[comp_swap & 3][nc - 1];
    CompMapping m{e[0], e[1], e[2], e[3]};
    return RemapSwizzle((DataFormat)raw_format, m);
}

static inline uint32_t SwapMaskRB(uint32_t m) {
    return (m & 0xA) | ((m & 1) << 2) | ((m >> 2) & 1);
}
static inline uint32_t ToMTLWriteMask(uint32_t rgba) {
    uint32_t m = 0;
    if (rgba & 1) m |= MTLColorWriteMaskRed;
    if (rgba & 2) m |= MTLColorWriteMaskGreen;
    if (rgba & 4) m |= MTLColorWriteMaskBlue;
    if (rgba & 8) m |= MTLColorWriteMaskAlpha;
    return m;
}

void DecodeColorTarget(const uint32_t* regs, uint32_t index, ColorTarget* c) {
    *c = ColorTarget{};
    c->index = index;
    const uint32_t b = reg::CB_COLOR0_BASE + index * reg::CB_COLOR_STRIDE;
    for (uint32_t i = 0; i < 13; ++i) c->regs[i] = regs[b + i];
    const uint32_t base = c->regs[0], pitch = c->regs[reg::CB_COLOR_PITCH],
                   slice = c->regs[reg::CB_COLOR_SLICE], view = c->regs[reg::CB_COLOR_VIEW],
                   info = c->regs[reg::CB_COLOR_INFO], attrib = c->regs[reg::CB_COLOR_ATTRIB];
    c->endian = Bits(info, 0, 2);
    c->raw_format = Bits(info, 2, 5);
    c->linear_general = Bits(info, 7, 1) != 0;
    c->raw_number_type = Bits(info, 8, 3);
    c->comp_swap = Bits(info, 11, 2);
    c->fast_clear = Bits(info, 13, 1) != 0;
    c->compression = Bits(info, 14, 1) != 0;
    c->blend_clamp = Bits(info, 15, 1) != 0;
    c->blend_bypass = Bits(info, 16, 1) != 0;
    c->round_truncate = Bits(info, 18, 1) != 0;
    c->dcc = Bits(info, 28, 1) != 0;
    c->alt_tile_mode = Bits(info, 31, 1) != 0;
    c->tile_mode = (TileMode)Bits(attrib, 0, 5);
    c->fmask_tile_mode = Bits(attrib, 5, 5);
    c->num_samples = 1u << Bits(attrib, 15, 2); // NUM_FRAGMENTS (shadPS4 NumSamples())
    c->num_fragments = c->num_samples;
    c->force_dst_alpha_1 = Bits(attrib, 17, 1) != 0;
    if (c->linear_general) c->tile_mode = TileMode::DisplayLinearGeneral;
    c->tiled = c->tile_mode != TileMode::DisplayLinearAligned &&
               c->tile_mode != TileMode::DisplayLinearGeneral;

    c->pitch = (Bits(pitch, 0, 11) + 1) << 3;
    c->height = (Bits(slice, 0, 22) + 1) * 64 / c->pitch;
    c->base_slice = c->linear_general ? 0 : Bits(view, 0, 11);
    c->num_slices = Bits(view, 13, 11) + 1;
    c->address = ((uint64_t)base << 8) | (c->linear_general ? (Bits(view, 0, 11) & 0xff) : 0);
    c->cmask_address = (uint64_t)c->regs[reg::CB_COLOR_CMASK] << 8;
    c->fmask_address = (uint64_t)c->regs[reg::CB_COLOR_FMASK] << 8;
    c->clear_word0 = c->regs[reg::CB_COLOR_CLEAR_WORD0];
    c->clear_word1 = c->regs[reg::CB_COLOR_CLEAR_WORD1];
    c->bpp = NumBitsPerBlock((DataFormat)c->raw_format);
    c->slice_bytes = c->bpp / 8 * (Bits(slice, 0, 22) + 1) * 64u * c->num_samples;

    // number type: CB uses slot 6 (SNORM_NZ in T# terms) for SRGB
    NumberFormat fixed = (NumberFormat)c->raw_number_type;
    if (fixed == NumberFormat::SnormNz) fixed = NumberFormat::Srgb;
    c->dfmt = RemapDataFormat((DataFormat)c->raw_format);
    c->nfmt = RemapNumberFormat(fixed, (DataFormat)c->raw_format);
    c->conversion = MapNumberConversion(fixed, (DataFormat)c->raw_format);
    c->swizzle = ColorBufferSwizzle(c->raw_format, c->comp_swap);

    c->bound = base != 0 && c->raw_format != 0;
    c->target_mask = (regs[reg::CB_TARGET_MASK] >> (index * 4)) & 0xF;
    c->shader_mask = (regs[reg::CB_SHADER_MASK] >> (index * 4)) & 0xF;
    c->export_format = (regs[reg::SPI_SHADER_COL_FORMAT] >> (index * 4)) & 0xF;
    const uint32_t mode = Bits(regs[reg::CB_COLOR_CONTROL], 4, 3);
    c->enabled = c->bound && c->target_mask != 0 && mode != 0;

    bool exact = true;
    c->mtl_format = GetColorTargetPixelFormat(c->dfmt, c->nfmt, c->swizzle, &c->export_swizzle, &exact);
    c->format_exact = exact;
    // write mask: shadPS4 swizzle.ApplyMask(target_mask) gives VK-format channel bits; convert to
    // the logical channels of the Metal format.
    uint32_t vk_mask = c->swizzle.ApplyMask(c->target_mask);
    const bool bgra = c->mtl_format == MTLPixelFormatBGRA8Unorm || c->mtl_format == MTLPixelFormatBGRA8Unorm_sRGB;
    const PixelFormatInfo pf = GetPixelFormat(c->dfmt, c->nfmt);
    if (bgra || pf.swap_rb) vk_mask = SwapMaskRB(vk_mask);
    c->mtl_write_mask = ToMTLWriteMask(vk_mask);
}

SurfaceDesc ColorTarget::ToSurfaceDesc() const {
    SurfaceDesc s;
    s.tile_mode = tile_mode;
    s.bpp = bpp;
    s.num_samples = num_samples;
    s.width = pitch;
    s.height = height;
    s.pitch = pitch;
    s.levels = 1;
    s.layers = num_slices;
    s.bank_swizzle = 0;
    return s;
}

void ColorTarget::GetLayout(SurfaceLayout* out) const {
    const SurfaceDesc s = ToSurfaceDesc();
    if (tiled) {
        MakeSingleLevelLayout(s, pitch, height, slice_bytes, out);
    } else {
        uint32_t p, h;
        uint64_t sz;
        SizeLinearAligned(pitch, height, bpp, num_samples, &p, &h, &sz);
        MakeSingleLevelLayout(s, p, h, (uint32_t)sz, out);
    }
}

void DecodeDepthTarget(const uint32_t* regs, DepthTarget* d) {
    *d = DepthTarget{};
    for (uint32_t i = 0; i < 8; ++i) d->regs[i] = regs[reg::DB_Z_INFO + i];
    const uint32_t zi = regs[reg::DB_Z_INFO], si = regs[reg::DB_STENCIL_INFO];
    d->z_format = (ZFormat)Bits(zi, 0, 2);
    d->num_samples = 1u << Bits(zi, 2, 2);
    d->tile_split = Bits(zi, 13, 3);
    d->tile_mode = (TileMode)Bits(zi, 20, 3);
    d->allow_expclear = Bits(zi, 27, 1) != 0;
    d->htile_enable = Bits(zi, 29, 1) != 0;
    d->htile_linear = (regs[0xA2AF] & 1) != 0; // DB_HTILE_SURFACE
    d->stencil_format = (StencilFormat)Bits(si, 0, 1);
    d->stencil_tile_split = Bits(si, 13, 3);
    d->tiled = d->tile_mode != TileMode::DisplayLinearAligned && d->tile_mode != TileMode::DisplayLinearGeneral;
    d->z_read_address = (uint64_t)regs[reg::DB_Z_READ_BASE] << 8;
    d->stencil_read_address = (uint64_t)regs[reg::DB_STENCIL_READ_BASE] << 8;
    d->z_write_address = (uint64_t)regs[reg::DB_Z_WRITE_BASE] << 8;
    d->stencil_write_address = (uint64_t)regs[reg::DB_STENCIL_WRITE_BASE] << 8;
    d->htile_address = (uint64_t)regs[reg::DB_HTILE_DATA_BASE] << 8;
    const uint32_t size = regs[reg::DB_DEPTH_SIZE];
    d->pitch = (Bits(size, 0, 11) + 1) << 3;
    d->height = (Bits(size, 11, 11) + 1) << 3;
    d->bpp = d->z_format == ZFormat::Z32Float ? 32 : 16;
    d->slice_bytes = (Bits(regs[reg::DB_DEPTH_SLICE], 0, 22) + 1) * 64 * (d->bpp / 8) * d->num_samples;
    const uint32_t view = regs[reg::DB_DEPTH_VIEW];
    d->base_slice = Bits(view, 0, 11);
    d->num_slices = Bits(view, 13, 11) + 1;
    d->z_read_only = Bits(view, 24, 1) != 0;
    d->stencil_read_only = Bits(view, 25, 1) != 0;
    const uint32_t rc = regs[reg::DB_RENDER_CONTROL];
    d->depth_clear_enable = Bits(rc, 0, 1) != 0;
    d->stencil_clear_enable = Bits(rc, 1, 1) != 0;
    d->depth_clear = AsFloat(regs[reg::DB_DEPTH_CLEAR]);
    d->stencil_clear = regs[reg::DB_STENCIL_CLEAR] & 0xFF;
    d->bound = d->DepthValid() || d->StencilValid();
    bool exact = true;
    d->mtl_format = GetDepthPixelFormat(d->z_format, d->stencil_format, &exact);
    d->format_exact = exact;
}

uint64_t HtileSliceBytes(uint32_t pitch, uint32_t height, uint32_t pipes, bool linear) {
    if (pipes != 2 && pipes != 8 && pipes != 16) return 0;
    constexpr uint32_t kHtileBpp = 32, kHtileCacheBits = 16384;
    uint32_t mw, mh;
    if (linear) {
        mw = 8 * 512 / kHtileBpp; // SiLib::HwlComputeTileDataWidthAndHeightLinear
        mh = 8 * pipes;
    } else {
        uint32_t w = kHtileCacheBits / kHtileBpp, h = 1; // Lib::ComputeTileDataWidthAndHeight
        while (w > h * 2 * pipes && !(w & 1)) w /= 2, h *= 2;
        mw = 8 * w;
        mh = 8 * h * pipes;
    }
    const uint64_t p = (pitch + mw - 1) / mw * mw, hh = (height + mh - 1) / mh * mh;
    return p * hh / 64 * kHtileBpp / 8;
}

uint64_t DepthHtileSliceBytes(const DepthTarget& d, bool neo_mode) {
    if (neo_mode) return 0;
    const PipeConfig pc = GetPipeConfig(d.tile_mode);
    if (pc != PipeConfig::P8_32x32_16x16 && pc != PipeConfig::P8_32x32_8x16 && pc != PipeConfig::P2) return 0;
    return HtileSliceBytes(d.pitch, d.height, GetPipeCount(pc), d.htile_linear);
}

SurfaceDesc DepthTarget::ToSurfaceDesc() const {
    SurfaceDesc s;
    s.tile_mode = tile_mode;
    s.bpp = bpp;
    s.num_samples = num_samples;
    s.width = pitch;
    s.height = height;
    s.pitch = pitch;
    s.levels = 1;
    s.layers = num_slices;
    return s;
}

void DepthTarget::GetLayout(SurfaceLayout* out) const {
    MakeSingleLevelLayout(ToSurfaceDesc(), pitch, height, slice_bytes, out);
}

static uint32_t MtlBlendFactor(uint32_t f, bool* ok) {
    switch (f) {
    case 0: return MTLBlendFactorZero;
    case 1: return MTLBlendFactorOne;
    case 2: return MTLBlendFactorSourceColor;
    case 3: return MTLBlendFactorOneMinusSourceColor;
    case 4: return MTLBlendFactorSourceAlpha;
    case 5: return MTLBlendFactorOneMinusSourceAlpha;
    case 6: return MTLBlendFactorDestinationAlpha;
    case 7: return MTLBlendFactorOneMinusDestinationAlpha;
    case 8: return MTLBlendFactorDestinationColor;
    case 9: return MTLBlendFactorOneMinusDestinationColor;
    case 10: return MTLBlendFactorSourceAlphaSaturated;
    case 13: return MTLBlendFactorBlendColor;
    case 14: return MTLBlendFactorOneMinusBlendColor;
    case 15: return MTLBlendFactorSource1Color;
    case 16: return MTLBlendFactorOneMinusSource1Color;
    case 17: return MTLBlendFactorSource1Alpha;
    case 18: return MTLBlendFactorOneMinusSource1Alpha;
    case 19: return MTLBlendFactorBlendAlpha;
    case 20: return MTLBlendFactorOneMinusBlendAlpha;
    default: *ok = false; return MTLBlendFactorOne;
    }
}

static uint32_t MtlBlendOp(uint32_t f, bool* ok) {
    switch (f) {
    case 0: return MTLBlendOperationAdd;
    case 1: return MTLBlendOperationSubtract;
    case 2: return MTLBlendOperationMin;
    case 3: return MTLBlendOperationMax;
    case 4: return MTLBlendOperationReverseSubtract;
    default: *ok = false; return MTLBlendOperationAdd;
    }
}

static void NormalizeMinMaxBlend(uint32_t* op, uint32_t* src, uint32_t* dst, NumberFormat fmt) {
    // shadPS4 LiverpoolToVK::NormalizeMinMaxBlend
    if (fmt != NumberFormat::Unorm && fmt != NumberFormat::Srgb) return;
    if (*op != MTLBlendOperationMin && *op != MTLBlendOperationMax) return;
    const uint32_t Z = MTLBlendFactorZero, O = MTLBlendFactorOne;
    if ((*src != Z && *src != O) || (*dst != Z && *dst != O) || (*src != Z && *dst != Z)) return;
    if (*op == MTLBlendOperationMin) {
        *src = Z;
        *dst = Z;
    }
    *op = MTLBlendOperationAdd;
}

static bool IsDualSrc(uint32_t f) {
    return f >= 15 && f <= 18;
}

static uint32_t MtlStencilOp(uint32_t op, bool* ok) {
    switch (op) {
    case 0: return MTLStencilOperationKeep;
    case 1: return MTLStencilOperationZero;
    case 2: *ok = false; return MTLStencilOperationReplace; // Ones (shadPS4: Replace)
    case 3: return MTLStencilOperationReplace;              // ReplaceTest
    case 4: return MTLStencilOperationReplace;              // ReplaceOp (ref = op_val)
    case 5: return MTLStencilOperationIncrementClamp;
    case 6: return MTLStencilOperationDecrementClamp;
    case 7: return MTLStencilOperationInvert;
    case 8: return MTLStencilOperationIncrementWrap;
    case 9: return MTLStencilOperationDecrementWrap;
    default: *ok = false; return MTLStencilOperationKeep;   // bitwise ops
    }
}

static bool IsPrimitiveCulled(PrimitiveType t) {
    switch (t) {
    case PrimitiveType::TriangleList:
    case PrimitiveType::TriangleFan:
    case PrimitiveType::TriangleStrip:
    case PrimitiveType::PatchPrimitive:
    case PrimitiveType::AdjTriangleList:
    case PrimitiveType::AdjTriangleStrip:
    case PrimitiveType::QuadList:
    case PrimitiveType::QuadStrip:
    case PrimitiveType::Polygon:
        return true;
    default:
        return false;
    }
}

static inline int16_t S16(uint32_t v) {
    return (int16_t)(v & 0xFFFF);
}

void DecodeRenderState(const uint32_t* regs, RenderState* rs) {
    *rs = RenderState{};
    const uint32_t cc = regs[reg::CB_COLOR_CONTROL];
    rs->color_mode = Bits(cc, 4, 3);
    rs->rop3 = Bits(cc, 16, 8);
    for (uint32_t i = 0; i < 4; ++i) rs->blend_constants[i] = AsFloat(regs[reg::CB_BLEND_RED + i]);

    // ---- color targets + blend
    uint32_t min_w = 0xFFFFFFFF, min_h = 0xFFFFFFFF;
    for (uint32_t i = 0; i < kNumColorBuffers; ++i) {
        ColorTarget& c = rs->color[i];
        DecodeColorTarget(regs, i, &c);
        BlendState& b = rs->blend[i];
        const uint32_t bc = regs[reg::CB_BLEND0_CONTROL + i];
        b.raw = bc;
        b.write_mask = c.mtl_write_mask;
        if (c.enabled) {
            min_w = std::min(min_w, c.pitch);
            min_h = std::min(min_h, c.height);
        }
        const bool enable = Bits(bc, 30, 1) && !c.blend_bypass;
        b.enable = enable;
        if (!enable) continue;
        bool ok = true;
        const uint32_t csrc = Bits(bc, 0, 5), cfunc = Bits(bc, 5, 3), cdst = Bits(bc, 8, 5);
        const uint32_t asrc = Bits(bc, 16, 5), afunc = Bits(bc, 21, 3), adst = Bits(bc, 24, 5);
        const bool separate = Bits(bc, 29, 1) != 0;
        b.src_rgb = MtlBlendFactor(csrc, &ok);
        b.dst_rgb = MtlBlendFactor(cdst, &ok);
        b.op_rgb = MtlBlendOp(cfunc, &ok);
        b.src_alpha = separate ? MtlBlendFactor(asrc, &ok) : b.src_rgb;
        b.dst_alpha = separate ? MtlBlendFactor(adst, &ok) : b.dst_rgb;
        b.op_alpha = separate ? MtlBlendOp(afunc, &ok) : b.op_rgb;
        b.dual_source = IsDualSrc(csrc) || IsDualSrc(cdst) || (separate && (IsDualSrc(asrc) || IsDualSrc(adst)));
        // GCN defaults alpha to 1 when CB_SHADER_MASK masks the alpha export (vk_graphics_pipeline.cpp)
        if ((c.shader_mask & 8) == 0) {
            if (b.src_rgb == MTLBlendFactorSourceAlpha) b.src_rgb = MTLBlendFactorOne;
            else if (b.src_rgb == MTLBlendFactorOneMinusSourceAlpha) b.src_rgb = MTLBlendFactorZero;
            if (b.dst_rgb == MTLBlendFactorSourceAlpha) b.dst_rgb = MTLBlendFactorOne;
            else if (b.dst_rgb == MTLBlendFactorOneMinusSourceAlpha) b.dst_rgb = MTLBlendFactorZero;
        }
        // GCN applies the factors to MIN/MAX; Metal ignores them.
        if ((cfunc == 2 || cfunc == 3) && csrc == 2 && cdst == 8) ok = false; // needs squared export
        NormalizeMinMaxBlend(&b.op_rgb, &b.src_rgb, &b.dst_rgb, c.nfmt);
        NormalizeMinMaxBlend(&b.op_alpha, &b.src_alpha, &b.dst_alpha, c.nfmt);
        if ((b.op_rgb == MTLBlendOperationMin || b.op_rgb == MTLBlendOperationMax) &&
            (b.src_rgb != MTLBlendFactorOne || b.dst_rgb != MTLBlendFactorOne)) ok = false;
        b.unsupported = !ok;
    }
    if (rs->rop3 != 0xCC) {
        for (auto& b : rs->blend) b.unsupported = true; // Metal has no logic ops
    }

    // ---- depth target + depth/stencil state
    DepthTarget& db = rs->depth;
    DecodeDepthTarget(regs, &db);
    if (db.bound) {
        min_w = std::min(min_w, db.pitch);
        min_h = std::min(min_h, db.height);
    }
    const uint32_t dc = regs[reg::DB_DEPTH_CONTROL];
    DepthStencilState& ds = rs->ds;
    const bool stencil_enable = Bits(dc, 0, 1), depth_enable = Bits(dc, 1, 1),
               depth_write = Bits(dc, 2, 1), bounds = Bits(dc, 3, 1), backface = Bits(dc, 7, 1);
    ds.depth_test = depth_enable && db.DepthValid();
    if (ds.depth_test) {
        ds.depth_write = depth_write && !db.depth_clear_enable;
        ds.depth_compare = MtlCompare(Bits(dc, 4, 3));
    }
    ds.depth_bounds = bounds;
    ds.depth_bounds_min = AsFloat(regs[reg::DB_DEPTH_BOUNDS_MIN]);
    ds.depth_bounds_max = AsFloat(regs[reg::DB_DEPTH_BOUNDS_MAX]);
    ds.stencil_test = stencil_enable && db.StencilValid();
    if (ds.stencil_test) {
        const uint32_t sc = regs[reg::DB_STENCIL_CONTROL];
        const uint32_t rf = regs[reg::DB_STENCILREFMASK];
        const uint32_t rb = backface ? regs[reg::DB_STENCILREFMASK_BF] : rf;
        bool ok = true;
        auto fill = [&](StencilFace& f, uint32_t fail, uint32_t zpass, uint32_t zfail, uint32_t func,
                        uint32_t ref) {
            f.fail = MtlStencilOp(fail, &ok);
            f.pass = MtlStencilOp(zpass, &ok);
            f.depth_fail = MtlStencilOp(zfail, &ok);
            f.compare = MtlCompare(func);
            const bool uses_op = fail == 4 || zpass == 4 || zfail == 4; // ReplaceOp -> op_val
            f.reference = (uint8_t)(uses_op ? Bits(ref, 24, 8) : Bits(ref, 0, 8));
            f.read_mask = (uint8_t)Bits(ref, 8, 8);
            f.write_mask = db.stencil_clear_enable ? 0 : (uint8_t)Bits(ref, 16, 8);
        };
        fill(ds.front, Bits(sc, 0, 4), Bits(sc, 4, 4), Bits(sc, 8, 4), Bits(dc, 8, 3), rf);
        if (backface) {
            fill(ds.back, Bits(sc, 12, 4), Bits(sc, 16, 4), Bits(sc, 20, 4), Bits(dc, 20, 3), rb);
        } else {
            ds.back = ds.front;
        }
        ds.unsupported_stencil_op = !ok;
    }

    // ---- raster / primitive
    RasterState& r = rs->raster;
    r.prim = (PrimitiveType)regs[reg::VGT_PRIMITIVE_TYPE];
    switch (r.prim) {
    case PrimitiveType::PointList: r.mtl_primitive = MTLPrimitiveTypePoint; break;
    case PrimitiveType::LineList: r.mtl_primitive = MTLPrimitiveTypeLine; break;
    case PrimitiveType::LineStrip: r.mtl_primitive = MTLPrimitiveTypeLineStrip; break;
    case PrimitiveType::TriangleList: r.mtl_primitive = MTLPrimitiveTypeTriangle; break;
    case PrimitiveType::TriangleStrip: r.mtl_primitive = MTLPrimitiveTypeTriangleStrip; break;
    case PrimitiveType::AdjLineList:
    case PrimitiveType::LineLoop:
        r.mtl_primitive = MTLPrimitiveTypeLine;
        r.needs_emulation = true;
        break;
    case PrimitiveType::AdjLineStrip:
        r.mtl_primitive = MTLPrimitiveTypeLineStrip;
        r.needs_emulation = true;
        break;
    default:
        // TriangleFan / Polygon / QuadList / QuadStrip / RectList / Adj tris / Patch
        r.mtl_primitive = MTLPrimitiveTypeTriangle;
        r.needs_emulation = r.prim != PrimitiveType::None;
        break;
    }
    const uint32_t pc = regs[reg::PA_SU_SC_MODE_CNTL];
    const uint32_t cull = Bits(pc, 0, 2); // cull_front | cull_back << 1
    const uint32_t clip = regs[reg::PA_CL_CLIP_CNTL];
    r.clip_disabled = Bits(clip, 16, 1) || r.prim == PrimitiveType::RectList;
    r.clip_space_minus_w = Bits(clip, 19, 1) == 0;
    r.rasterizer_discard = Bits(clip, 22, 1) != 0;
    const bool zn_dis = Bits(clip, 26, 1), zf_dis = Bits(clip, 27, 1);
    r.depth_clip = (zn_dis == zf_dis) ? !zn_dis : false; // ClipperControl::ZclipEnable
    if (IsPrimitiveCulled(r.prim)) {
        r.cull_mode = cull == 1 ? MTLCullModeFront : cull == 2 ? MTLCullModeBack : MTLCullModeNone;
        r.cull_front_and_back = cull == 3;
    }
    // FACE: 0 = CCW front, 1 = CW front, in screen space (y down) - same convention as Metal
    // window coordinates with the (unflipped) viewport computed below.
    r.winding = Bits(pc, 2, 1) ? MTLWindingClockwise : MTLWindingCounterClockwise;
    const bool poly_mode_en = Bits(pc, 3, 2) != 0;
    const uint32_t poly_front = Bits(pc, 5, 3);
    r.fill_mode = (poly_mode_en && poly_front != 2) ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    const bool off_front = Bits(pc, 11, 1), off_back = Bits(pc, 12, 1), off_para = Bits(pc, 13, 1);
    r.depth_bias = off_front || off_back || off_para;
    if (r.depth_bias) {
        // vk_rasterizer.cpp: constant = front/back offset, clamp = POLY_OFFSET_CLAMP, slope = scale/16
        r.bias_constant = AsFloat(regs[off_front ? reg::PA_SU_POLY_OFFSET_FRONT_OFFSET : reg::PA_SU_POLY_OFFSET_BACK_OFFSET]);
        r.bias_slope = AsFloat(regs[off_front ? reg::PA_SU_POLY_OFFSET_FRONT_SCALE : reg::PA_SU_POLY_OFFSET_BACK_SCALE]) / 16.f;
        r.bias_clamp = AsFloat(regs[reg::PA_SU_POLY_OFFSET_CLAMP]);
    }
    r.window_offset_on_vertices = Bits(pc, 16, 1) != 0;
    r.provoking_last = Bits(pc, 19, 1) != 0;
    r.primitive_restart = (regs[reg::VGT_MULTI_PRIM_IB_RESET_EN] & 1) != 0;
    r.restart_index = regs[reg::VGT_MULTI_PRIM_IB_RESET_INDX];
    r.line_width = (float)(regs[reg::PA_SU_LINE_CNTL] & 0xFFFF) / 8.0f;

    // ---- scissor (vk_rasterizer.cpp UpdateViewportScissorState)
    const uint32_t wo = regs[reg::PA_SC_WINDOW_OFFSET];
    const uint32_t wtl = regs[reg::PA_SC_WINDOW_SCISSOR_TL], wbr = regs[reg::PA_SC_WINDOW_SCISSOR_BR];
    const uint32_t gtl = regs[reg::PA_SC_GENERIC_SCISSOR_TL], gbr = regs[reg::PA_SC_GENERIC_SCISSOR_BR];
    const uint32_t stl = regs[reg::PA_SC_SCREEN_SCISSOR_TL], sbr = regs[reg::PA_SC_SCREEN_SCISSOR_BR];
    const bool enable_offset = !Bits(wtl, 31, 1);
    const int16_t wox = enable_offset ? S16(wo) : 0, woy = enable_offset ? S16(wo >> 16) : 0;
    auto tl = [](int16_t scr, int16_t win, int16_t gen, int16_t off) {
        return std::max({scr, (int16_t)(win + off), (int16_t)(gen + off)});
    };
    auto br = [](int16_t scr, int16_t win, int16_t gen, int16_t off) {
        return std::min({scr, (int16_t)(win + off), (int16_t)(gen + off)});
    };
    int16_t sx0 = tl(S16(stl), (int16_t)Bits(wtl, 0, 15), (int16_t)Bits(gtl, 0, 15), wox);
    int16_t sy0 = tl(S16(stl >> 16), (int16_t)Bits(wtl, 16, 15), (int16_t)Bits(gtl, 16, 15), woy);
    int16_t sx1 = br(S16(sbr), (int16_t)Bits(wbr, 0, 15), (int16_t)Bits(gbr, 0, 15), wox);
    int16_t sy1 = br(S16(sbr >> 16), (int16_t)Bits(wbr, 16, 15), (int16_t)Bits(gbr, 16, 15), woy);
    auto mk = [](int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
        ScissorRect s;
        const int cx0 = std::max<int>(0, x0), cy0 = std::max<int>(0, y0);
        const int cx1 = std::max<int>(0, x1), cy1 = std::max<int>(0, y1);
        s.x = (uint32_t)cx0;
        s.y = (uint32_t)cy0;
        s.width = cx1 > cx0 ? (uint32_t)(cx1 - cx0) : 0;
        s.height = cy1 > cy0 ? (uint32_t)(cy1 - cy0) : 0;
        return s;
    };
    rs->scissor = mk(sx0, sy0, sx1, sy1);

    // ---- viewports
    const uint32_t vte = regs[reg::PA_CL_VTE_CNTL];
    const bool xs_en = Bits(vte, 0, 1), xo_en = Bits(vte, 1, 1), ys_en = Bits(vte, 2, 1),
               yo_en = Bits(vte, 3, 1), zs_en = Bits(vte, 4, 1), zo_en = Bits(vte, 5, 1);
    const bool vport_scissor = Bits(regs[reg::PA_SC_MODE_CNTL_0], 1, 1) != 0;
    for (uint32_t i = 0; i < kNumViewports; ++i) {
        const uint32_t vb = reg::PA_CL_VPORT_XSCALE + i * 6;
        const float xscale_raw = AsFloat(regs[vb + 0]);
        if (xscale_raw == 0.f) continue;
        Viewport v;
        const float zoffset = zo_en ? AsFloat(regs[vb + 5]) : 0.f;
        const float zscale = zs_en ? AsFloat(regs[vb + 4]) : 1.f;
        if (r.clip_space_minus_w) {
            v.znear = zoffset - zscale;
            v.zfar = zoffset + zscale;
        } else {
            v.znear = zoffset;
            v.zfar = zoffset + zscale;
        }
        if (r.clip_disabled) {
            // Screen-space positions: shadPS4 patches the VS to map [0,16384) to NDC with y
            // pointing down (Vulkan's NDC). Metal's NDC y points up, so the viewport flips y back
            // with a negative height; window coordinates, and the winding seen in them, are the
            // guest's screen space (tests/host/gnm_rectlist_test.mm).
            v.origin_x = 0;
            v.origin_y = 16384;
            v.width = 16384;
            v.height = -16384;
        } else {
            const float xscale = xs_en ? xscale_raw : 1.f;
            const float xoffset = xo_en ? AsFloat(regs[vb + 1]) : 0.f;
            const float yscale = ys_en ? AsFloat(regs[vb + 2]) : 1.f;
            const float yoffset = yo_en ? AsFloat(regs[vb + 3]) : 0.f;
            // GCN: x_w = xscale*x_ndc + xoffset, y_w = yscale*y_ndc + yoffset.
            // Metal: x_w = (x_ndc+1)*w/2 + ox, y_w = (1-y_ndc)*h/2 + oy.
            v.origin_x = xoffset - xscale;
            v.width = 2.0 * xscale;
            if (yscale <= 0.f) {
                v.height = -2.0 * yscale; // usual D3D-style negative yscale -> positive height
                v.origin_y = yoffset + yscale;
                v.flip_y = false;
            } else {
                // would need a negative height; instead negate y in the VS (flip_y) which also
                // flips the facing (handled by the caller: swap winding when flip_y).
                v.height = 2.0 * yscale;
                v.origin_y = yoffset - yscale;
                v.flip_y = true;
            }
        }
        ScissorRect s = rs->scissor;
        if (vport_scissor) {
            const uint32_t vtl = regs[reg::PA_SC_VPORT_SCISSOR_0_TL + 2 * i];
            const uint32_t vbr = regs[reg::PA_SC_VPORT_SCISSOR_0_TL + 2 * i + 1];
            const int16_t x0 = std::max<int16_t>(sx0, (int16_t)Bits(vtl, 0, 15));
            const int16_t y0 = std::max<int16_t>(sy0, (int16_t)Bits(vtl, 16, 15));
            const int16_t x1 = (int16_t)std::min<int>(std::max<int16_t>(0, sx1), (int)Bits(vbr, 0, 15));
            const int16_t y1 = (int16_t)std::min<int>(std::max<int16_t>(0, sy1), (int)Bits(vbr, 16, 15));
            s = mk(x0, y0, x1, y1);
        }
        rs->viewports[rs->num_viewports] = v;
        rs->scissors[rs->num_viewports] = s;
        rs->num_viewports++;
    }

    rs->scissor_empty = rs->scissor.width == 0 || rs->scissor.height == 0;
    for (uint32_t i = 0; i < rs->num_viewports; ++i)
        rs->scissor_empty |= rs->scissors[i].width == 0 || rs->scissors[i].height == 0;
    rs->msaa_samples = 1u << Bits(regs[reg::PA_SC_AA_CONFIG], 0, 3);
    rs->render_width = min_w == 0xFFFFFFFF ? 0 : min_w;
    rs->render_height = min_h == 0xFFFFFFFF ? 0 : min_h;
}

static const char* MtlFmtName(uint32_t f) {
    switch (f) {
    case MTLPixelFormatInvalid: return "Invalid";
    case MTLPixelFormatRGBA8Unorm: return "RGBA8Unorm";
    case MTLPixelFormatRGBA8Unorm_sRGB: return "RGBA8Unorm_sRGB";
    case MTLPixelFormatBGRA8Unorm: return "BGRA8Unorm";
    case MTLPixelFormatBGRA8Unorm_sRGB: return "BGRA8Unorm_sRGB";
    case MTLPixelFormatRGBA16Float: return "RGBA16Float";
    case MTLPixelFormatRGBA16Unorm: return "RGBA16Unorm";
    case MTLPixelFormatRGB10A2Unorm: return "RGB10A2Unorm";
    case MTLPixelFormatRG11B10Float: return "RG11B10Float";
    case MTLPixelFormatRGBA32Float: return "RGBA32Float";
    case MTLPixelFormatR32Float: return "R32Float";
    case MTLPixelFormatR32Uint: return "R32Uint";
    case MTLPixelFormatRG16Float: return "RG16Float";
    case MTLPixelFormatR16Float: return "R16Float";
    case MTLPixelFormatRG16Unorm: return "RG16Unorm";
    case MTLPixelFormatR8Unorm: return "R8Unorm";
    case MTLPixelFormatRG8Unorm: return "RG8Unorm";
    case MTLPixelFormatR16Unorm: return "R16Unorm";
    case MTLPixelFormatRG32Float: return "RG32Float";
    case MTLPixelFormatR16Uint: return "R16Uint";
    case MTLPixelFormatR8Uint: return "R8Uint";
    case MTLPixelFormatRG16Uint: return "RG16Uint";
    case MTLPixelFormatRGBA16Uint: return "RGBA16Uint";
    case MTLPixelFormatRGBA32Uint: return "RGBA32Uint";
    case MTLPixelFormatRG32Uint: return "RG32Uint";
    case MTLPixelFormatB5G6R5Unorm: return "B5G6R5Unorm";
    case MTLPixelFormatDepth16Unorm: return "Depth16Unorm";
    case MTLPixelFormatDepth32Float: return "Depth32Float";
    case MTLPixelFormatDepth32Float_Stencil8: return "Depth32Float_Stencil8";
    default: return "MTLPixelFormat(other)";
    }
}

void PrintRenderState(const RenderState& rs, FILE* f, bool verbose) {
    fprintf(f, "  CB_COLOR_CONTROL mode=%u rop3=0x%02x  msaa=%u  render=%ux%u\n", rs.color_mode,
            rs.rop3, rs.msaa_samples, rs.render_width, rs.render_height);
    for (uint32_t i = 0; i < kNumColorBuffers; ++i) {
        const ColorTarget& c = rs.color[i];
        if (!c.bound && !verbose) continue;
        fprintf(f,
                "  CB%u %s addr=0x%llx %ux%u slices=%u+%u %s/%s swap=%u tile=%s(%u) samples=%u "
                "mask=0x%x shmask=0x%x expfmt=%u fastclear=%d alt_tile=%d cmask=0x%llx fmask=0x%llx "
                "slice_bytes=%u -> %s%s write=0x%x\n",
                i, c.enabled ? "ON " : "off", (unsigned long long)c.address, c.pitch, c.height,
                c.base_slice, c.num_slices, NameOf(c.dfmt), NameOf(c.nfmt), c.comp_swap,
                NameOf(c.tile_mode), (uint32_t)c.tile_mode, c.num_samples, c.target_mask,
                c.shader_mask, c.export_format, c.fast_clear, c.alt_tile_mode, (unsigned long long)c.cmask_address,
                (unsigned long long)c.fmask_address, c.slice_bytes, MtlFmtName(c.mtl_format),
                c.format_exact ? "" : "(approx)", c.mtl_write_mask);
        const BlendState& b = rs.blend[i];
        if (b.enable)
            fprintf(f, "      blend raw=0x%08x rgb: src=%u dst=%u op=%u  a: src=%u dst=%u op=%u%s%s\n",
                    b.raw, b.src_rgb, b.dst_rgb, b.op_rgb, b.src_alpha, b.dst_alpha, b.op_alpha,
                    b.dual_source ? " dual-src" : "", b.unsupported ? " UNSUPPORTED" : "");
    }
    const DepthTarget& d = rs.depth;
    if (d.bound || verbose) {
        fprintf(f,
                "  DB z=%u s=%u zread=0x%llx zwrite=0x%llx sread=0x%llx %ux%u slices=%u+%u tile=%s(%u) "
                "samples=%u htile=%d(0x%llx) clear(d=%d %.3f s=%d %u) -> %s%s\n",
                (uint32_t)d.z_format, (uint32_t)d.stencil_format, (unsigned long long)d.z_read_address,
                (unsigned long long)d.z_write_address, (unsigned long long)d.stencil_read_address,
                d.pitch, d.height, d.base_slice, d.num_slices, NameOf(d.tile_mode),
                (uint32_t)d.tile_mode, d.num_samples, d.htile_enable,
                (unsigned long long)d.htile_address, d.depth_clear_enable, d.depth_clear,
                d.stencil_clear_enable, d.stencil_clear, MtlFmtName(d.mtl_format),
                d.format_exact ? "" : "(approx)");
    }
    const DepthStencilState& ds = rs.ds;
    fprintf(f, "  DS depth_test=%d write=%d cmp=%u stencil=%d bounds=%d\n", ds.depth_test,
            ds.depth_write, ds.depth_compare, ds.stencil_test, ds.depth_bounds);
    if (ds.stencil_test)
        fprintf(f, "     front cmp=%u fail=%u zfail=%u pass=%u ref=%u rm=%02x wm=%02x\n", ds.front.compare,
                ds.front.fail, ds.front.depth_fail, ds.front.pass, ds.front.reference,
                ds.front.read_mask, ds.front.write_mask);
    const RasterState& r = rs.raster;
    fprintf(f,
            "  RS prim=%s(%u)->mtl %u%s cull=%u%s winding=%s fill=%u bias=%d clipdis=%d minusW=%d "
            "discard=%d restart=%d line=%.2f\n",
            NameOf(r.prim), (uint32_t)r.prim, r.mtl_primitive, r.needs_emulation ? "(emulate)" : "",
            r.cull_mode, r.cull_front_and_back ? "(F&B)" : "",
            r.winding == MTLWindingClockwise ? "CW" : "CCW", r.fill_mode, r.depth_bias,
            r.clip_disabled, r.clip_space_minus_w, r.rasterizer_discard, r.primitive_restart, r.line_width);
    fprintf(f, "  SC scissor=(%u,%u %ux%u) viewports=%u%s\n", rs.scissor.x, rs.scissor.y,
            rs.scissor.width, rs.scissor.height, rs.num_viewports, rs.scissor_empty ? " (EMPTY scissor)" : "");
    for (uint32_t i = 0; i < rs.num_viewports && (i < 1 || verbose); ++i) {
        const Viewport& v = rs.viewports[i];
        fprintf(f, "  VP%u origin=(%.1f,%.1f) size=%.1fx%.1f z=[%.3f,%.3f]%s scissor=(%u,%u %ux%u)\n",
                i, v.origin_x, v.origin_y, v.width, v.height, v.znear, v.zfar,
                v.flip_y ? " flipY" : "", rs.scissors[i].x, rs.scissors[i].y,
                rs.scissors[i].width, rs.scissors[i].height);
    }
}

void ApplyBlend(const BlendState& b, uint32_t pixel_format, MTLRenderPipelineColorAttachmentDescriptor* d) {
    d.pixelFormat = (MTLPixelFormat)pixel_format;
    d.writeMask = (MTLColorWriteMask)b.write_mask;
    d.blendingEnabled = b.enable;
    if (b.enable) {
        d.sourceRGBBlendFactor = (MTLBlendFactor)b.src_rgb;
        d.destinationRGBBlendFactor = (MTLBlendFactor)b.dst_rgb;
        d.rgbBlendOperation = (MTLBlendOperation)b.op_rgb;
        d.sourceAlphaBlendFactor = (MTLBlendFactor)b.src_alpha;
        d.destinationAlphaBlendFactor = (MTLBlendFactor)b.dst_alpha;
        d.alphaBlendOperation = (MTLBlendOperation)b.op_alpha;
    }
}

MTLDepthStencilDescriptor* MakeDepthStencilDescriptor(const DepthStencilState& ds) {
    MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
    d.depthCompareFunction = ds.depth_test ? (MTLCompareFunction)ds.depth_compare : MTLCompareFunctionAlways;
    d.depthWriteEnabled = ds.depth_test && ds.depth_write;
    if (ds.stencil_test) {
        auto mk = [](const StencilFace& f) {
            MTLStencilDescriptor* s = [MTLStencilDescriptor new];
            s.stencilCompareFunction = (MTLCompareFunction)f.compare;
            s.stencilFailureOperation = (MTLStencilOperation)f.fail;
            s.depthFailureOperation = (MTLStencilOperation)f.depth_fail;
            s.depthStencilPassOperation = (MTLStencilOperation)f.pass;
            s.readMask = f.read_mask;
            s.writeMask = f.write_mask;
            return s;
        };
        d.frontFaceStencil = mk(ds.front);
        d.backFaceStencil = mk(ds.back);
    }
    return d;
}

MTLViewport ToMTLViewport(const Viewport& v) {
    MTLViewport m;
    m.originX = v.origin_x;
    m.originY = v.origin_y;
    m.width = v.width;
    m.height = v.height;
    m.znear = v.znear;
    m.zfar = v.zfar;
    return m;
}

MTLScissorRect ToMTLScissor(const ScissorRect& s, uint32_t rt_w, uint32_t rt_h) {
    MTLScissorRect m;
    m.x = std::min(s.x, rt_w);
    m.y = std::min(s.y, rt_h);
    m.width = std::min<NSUInteger>(s.width, rt_w - m.x);
    m.height = std::min<NSUInteger>(s.height, rt_h - m.y);
    return m;
}

// =====================================================================================
// 5. Readback conversion + texture cache
// =====================================================================================

enum class RowFmt {
    Unknown, RGBA8, BGRA8, RGB10A2, BGR10A2, RGBA16F, BGRA16F, RGBA16U, RG11B10F, RGBA32F, R8, RG8,
    R16F, R16U, R32F, B5G6R5, BGR5A1, ABGR4,
};

static inline uint8_t U8F(float v) {
    if (!(v > 0.f)) return 0;
    if (v >= 1.f) return 255;
    return (uint8_t)(v * 255.f + 0.5f);
}
static inline float HalfToFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v;
    if (e == 0) v = ldexpf((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = ldexpf((float)(m | 0x400), (int)e - 25);
    return s ? -v : v;
}
static inline float UF11(uint32_t v) { // 6e5
    const uint32_t e = (v >> 6) & 0x1F, m = v & 0x3F;
    if (e == 0) return ldexpf((float)m, -20);
    if (e == 31) return 65504.f;
    return ldexpf((float)(m | 0x40), (int)e - 21);
}
static inline float UF10(uint32_t v) { // 5e5
    const uint32_t e = (v >> 5) & 0x1F, m = v & 0x1F;
    if (e == 0) return ldexpf((float)m, -19);
    if (e == 31) return 65504.f;
    return ldexpf((float)(m | 0x20), (int)e - 20);
}

// decode one pixel to logical RGBA (memory/format channel order)
static bool DecodePixel(RowFmt f, const uint8_t* p, float c[4], uint32_t* nch) {
    *nch = 4;
    c[3] = 1.f;
    switch (f) {
    case RowFmt::RGBA8:
        for (int i = 0; i < 4; ++i) c[i] = p[i] / 255.f;
        return true;
    case RowFmt::BGRA8:
        c[0] = p[2] / 255.f; c[1] = p[1] / 255.f; c[2] = p[0] / 255.f; c[3] = p[3] / 255.f;
        return true;
    case RowFmt::RGB10A2:
    case RowFmt::BGR10A2: {
        uint32_t v;
        memcpy(&v, p, 4);
        const float lo = (v & 0x3FF) / 1023.f, mid = ((v >> 10) & 0x3FF) / 1023.f,
                    hi = ((v >> 20) & 0x3FF) / 1023.f;
        c[1] = mid;
        c[3] = (v >> 30) / 3.f;
        if (f == RowFmt::RGB10A2) { c[0] = lo; c[2] = hi; } else { c[2] = lo; c[0] = hi; }
        return true;
    }
    case RowFmt::RGBA16F:
    case RowFmt::BGRA16F: {
        uint16_t h[4];
        memcpy(h, p, 8);
        for (int i = 0; i < 4; ++i) c[i] = HalfToFloat(h[i]);
        if (f == RowFmt::BGRA16F) std::swap(c[0], c[2]);
        return true;
    }
    case RowFmt::RGBA16U: {
        uint16_t h[4];
        memcpy(h, p, 8);
        for (int i = 0; i < 4; ++i) c[i] = h[i] / 65535.f;
        return true;
    }
    case RowFmt::RG11B10F: {
        uint32_t v;
        memcpy(&v, p, 4);
        c[0] = UF11(v & 0x7FF); c[1] = UF11((v >> 11) & 0x7FF); c[2] = UF10(v >> 22);
        *nch = 3;
        return true;
    }
    case RowFmt::RGBA32F:
        memcpy(c, p, 16);
        return true;
    case RowFmt::R8: c[0] = p[0] / 255.f; c[1] = c[2] = 0; *nch = 1; return true;
    case RowFmt::RG8: c[0] = p[0] / 255.f; c[1] = p[1] / 255.f; c[2] = 0; *nch = 2; return true;
    case RowFmt::R16F: {
        uint16_t h; memcpy(&h, p, 2); c[0] = HalfToFloat(h); c[1] = c[2] = 0; *nch = 1; return true;
    }
    case RowFmt::R16U: {
        uint16_t h; memcpy(&h, p, 2); c[0] = h / 65535.f; c[1] = c[2] = 0; *nch = 1; return true;
    }
    case RowFmt::R32F: memcpy(&c[0], p, 4); c[1] = c[2] = 0; *nch = 1; return true;
    case RowFmt::B5G6R5: {
        uint16_t v; memcpy(&v, p, 2);
        c[2] = (v & 31) / 31.f; c[1] = ((v >> 5) & 63) / 63.f; c[0] = (v >> 11) / 31.f; *nch = 3;
        return true;
    }
    case RowFmt::BGR5A1: {
        uint16_t v; memcpy(&v, p, 2);
        c[2] = (v & 31) / 31.f; c[1] = ((v >> 5) & 31) / 31.f; c[0] = ((v >> 10) & 31) / 31.f;
        c[3] = (float)(v >> 15);
        return true;
    }
    case RowFmt::ABGR4: {
        uint16_t v; memcpy(&v, p, 2);
        c[3] = (v & 15) / 15.f; c[2] = ((v >> 4) & 15) / 15.f; c[1] = ((v >> 8) & 15) / 15.f;
        c[0] = (v >> 12) / 15.f;
        return true;
    }
    default:
        return false;
    }
}

static uint32_t RowFmtBytes(RowFmt f) {
    switch (f) {
    case RowFmt::RGBA16F: case RowFmt::BGRA16F: case RowFmt::RGBA16U: return 8;
    case RowFmt::RGBA32F: return 16;
    case RowFmt::R8: return 1;
    case RowFmt::RG8: case RowFmt::R16F: case RowFmt::R16U: case RowFmt::B5G6R5:
    case RowFmt::BGR5A1: case RowFmt::ABGR4: return 2;
    case RowFmt::Unknown: return 0;
    default: return 4;
    }
}

static RowFmt RowFmtFromMtl(uint32_t f) {
    switch (f) {
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB: return RowFmt::RGBA8;
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB: return RowFmt::BGRA8;
    case MTLPixelFormatRGB10A2Unorm: return RowFmt::RGB10A2;
    case MTLPixelFormatBGR10A2Unorm: return RowFmt::BGR10A2;
    case MTLPixelFormatRGBA16Float: return RowFmt::RGBA16F;
    case MTLPixelFormatRGBA16Unorm: return RowFmt::RGBA16U;
    case MTLPixelFormatRG11B10Float: return RowFmt::RG11B10F;
    case MTLPixelFormatRGBA32Float: return RowFmt::RGBA32F;
    case MTLPixelFormatR8Unorm: case MTLPixelFormatR8Unorm_sRGB: return RowFmt::R8;
    case MTLPixelFormatRG8Unorm: case MTLPixelFormatRG8Unorm_sRGB: return RowFmt::RG8;
    case MTLPixelFormatR16Float: return RowFmt::R16F;
    case MTLPixelFormatR16Unorm: return RowFmt::R16U;
    case MTLPixelFormatR32Float: return RowFmt::R32F;
    case MTLPixelFormatB5G6R5Unorm: return RowFmt::B5G6R5;
    case MTLPixelFormatBGR5A1Unorm: return RowFmt::BGR5A1;
    case MTLPixelFormatABGR4Unorm: return RowFmt::ABGR4;
    default: return RowFmt::Unknown;
    }
}

static void ConvertRow(RowFmt f, const uint8_t* src, uint8_t* dst, uint32_t width) {
    const uint32_t bpe = RowFmtBytes(f);
    if (f == RowFmt::BGRA8) {
        memcpy(dst, src, (size_t)width * 4);
        return;
    }
    if (f == RowFmt::RGBA8) {
        for (uint32_t x = 0; x < width; ++x) {
            dst[4 * x + 0] = src[4 * x + 2];
            dst[4 * x + 1] = src[4 * x + 1];
            dst[4 * x + 2] = src[4 * x + 0];
            dst[4 * x + 3] = src[4 * x + 3];
        }
        return;
    }
    for (uint32_t x = 0; x < width; ++x) {
        float c[4];
        uint32_t n;
        if (!DecodePixel(f, src + (size_t)x * bpe, c, &n)) {
            memset(dst + 4 * x, 0, 4);
            continue;
        }
        dst[4 * x + 0] = U8F(c[2]);
        dst[4 * x + 1] = U8F(c[1]);
        dst[4 * x + 2] = U8F(c[0]);
        dst[4 * x + 3] = U8F(c[3]);
    }
}

bool ConvertRowToBGRA8(DataFormat dfmt, NumberFormat nfmt, CompMapping swizzle, const uint8_t* src,
                       uint8_t* dst, uint32_t width) {
    CompMapping ex;
    bool exact;
    const uint32_t mtl = GetColorTargetPixelFormat(dfmt, nfmt, swizzle, &ex, &exact);
    const RowFmt rf = RowFmtFromMtl(mtl);
    if (rf == RowFmt::Unknown) return false;
    ConvertRow(rf, src, dst, width);
    if (!ex.IsIdentity()) {
        // memory channel i holds export component Map(i): undo it (logical[Map(i)] = mem[i])
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* p = dst + 4 * x;
            const uint8_t mem[4] = {p[2], p[1], p[0], p[3]};
            uint8_t lg[4] = {mem[0], mem[1], mem[2], mem[3]};
            for (uint32_t i = 0; i < 4; ++i) {
                const uint32_t sw = (uint32_t)ex[i];
                if (sw >= (uint32_t)CompSwizzle::Red) lg[sw - (uint32_t)CompSwizzle::Red] = mem[i];
            }
            p[0] = lg[2]; p[1] = lg[1]; p[2] = lg[0]; p[3] = lg[3];
        }
    }
    return true;
}

static RowFmt RowFmtFromVideoOut(uint32_t fmt) {
    switch (fmt) {
    case kVideoOutA8R8G8B8Srgb: return RowFmt::BGRA8;
    case kVideoOutA8B8G8R8Srgb: return RowFmt::RGBA8;
    case kVideoOutA2R10G10B10:
    case kVideoOutA2R10G10B10Srgb:
    case kVideoOutA2R10G10B10Bt2020Pq: return RowFmt::BGR10A2;
    case kVideoOutA16R16G16B16Float: return RowFmt::BGRA16F;
    default: return RowFmt::BGRA8;
    }
}

bool ReadVideoOutBufferBGRA8(const uint8_t* mem_base, uint64_t mem_size, uint64_t address,
                             uint32_t width, uint32_t height, uint32_t pitch, bool tiled,
                             uint32_t videoout_format, uint8_t* out, uint32_t out_row_bytes) {
    if (!mem_base || !out || address == 0 || address >= mem_size || width == 0 || height == 0) return false;
    const RowFmt rf = RowFmtFromVideoOut(videoout_format);
    const uint32_t bpe = RowFmtBytes(rf);
    SurfaceDesc d;
    d.tile_mode = tiled ? TileMode::Display2DThin : TileMode::DisplayLinearAligned;
    d.bpp = bpe * 8;
    d.width = width;
    d.height = height;
    // shadPS4 ImageInfo(BufferAttributeGroup): tiled pitch = align(width, 128), linear = width
    d.pitch = tiled ? ((std::max(pitch, width) + 127) & ~127u) : std::max(pitch, width);
    SurfaceLayout l;
    if (!ComputeSurfaceLayout(d, &l)) return false;
    const uint64_t avail = mem_size - address;
    std::vector<uint8_t> lin((size_t)l.total_size);
    if (!DetileSurface(d, l, mem_base + address, (size_t)std::min<uint64_t>(avail, l.total_size),
                       lin.data(), lin.size(), 0, 1, true)) {
        return false;
    }
    const uint32_t row = l.LinearRowBytes(0, d.bpp);
    for (uint32_t y = 0; y < height; ++y) {
        ConvertRow(rf, lin.data() + (size_t)y * row, out + (size_t)y * out_row_bytes, width);
    }
    return true;
}

// ---- TextureCache ----

struct TextureCache::Entry {
    enum Kind { Sampled = 0, Color = 1, Depth = 2 };
    Kind kind = Sampled;
    uint64_t address = 0;
    uint64_t size = 0;          // guest bytes covered
    // identity
    uint32_t mtl_format = 0, tex_type = 0, width = 0, height = 0, depth = 0, levels = 0, layers = 0;
    uint32_t samples = 1;
    CompMapping swizzle;
    SurfaceDesc surf;
    SurfaceLayout layout;
    DataFormat dfmt = DataFormat::FormatInvalid;
    NumberFormat nfmt = NumberFormat::Unorm;
    CompMapping cb_swizzle;     // color targets: CB component swap (for readback)
    id<MTLTexture> tex = nil;
    bool dirty = true;
    uint64_t generation = 0;    // bumped each time a render target is (re)bound
    // alias copies for sampling a render target with different dims/format
    id<MTLTexture> alias = nil;
    uint64_t alias_generation = ~0ull;
    uint32_t alias_key = 0;
    uint64_t gpu_upload_epoch = 0; // sampled: contents copied on the GPU after writes < this epoch
    uint64_t alloc_bytes = 0;      // tex.allocatedSize, as counted in MemStats
    mutable uint64_t last_seq = 0, last_ns = 0; // last use (SetUse values at the last lookup)
    // GPU-owned: the last write whose guest byte range is known exactly (NoteExactWrite, or a
    // compute copy's destination): its command buffer and the range. Other writes (draws, storage
    // images) only count as uses: which bytes they touch is not known.
    mutable uint64_t last_write_seq = 0, write_lo = 0, write_hi = 0;
    // Color: buffer writes over the surface (NoteBufferWrite). newer_epoch != 0: marked guest newer
    // (the write epoch of the write that completed the cover); newer_cover: the exactly known
    // written ranges since its last GPU write (sorted, merged, clipped to the surface).
    uint64_t newer_epoch = 0;
    mutable std::vector<std::pair<uint64_t, uint64_t>> newer_cover;
    bool newer_partial_logged = false;
};

void TextureCache::TouchLocked(const Entry* e) const {
    e->last_seq = use_seq_.load(std::memory_order_relaxed);
    e->last_ns = use_ns_.load(std::memory_order_relaxed);
}

void TextureCache::GpuWroteLocked(const Entry* e) const {
    e->newer_cover.clear();
}

void TextureCache::UnmarkLocked(const Entry* e) {
    if (!e->newer_epoch) return;
    const_cast<Entry*>(e)->newer_epoch = 0;
    guest_newer_n_.fetch_sub(1, std::memory_order_relaxed);
}

void TextureCache::ExactWriteLocked(const Entry* e, uint64_t lo, uint64_t hi, uint64_t seq) const {
    TouchLocked(e);
    GpuWroteLocked(e);
    lo = std::max(lo, e->address);
    hi = std::min(hi, e->address + e->size);
    if (lo >= hi) return;
    e->last_write_seq = seq ? seq : e->last_seq;
    e->write_lo = lo;
    e->write_hi = hi;
}

bool TextureCache::NoteExactWrite(uint64_t address, id<MTLTexture> tex, uint64_t lo, uint64_t hi, uint64_t seq) {
    if (!tex) return false;
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second->kind == Entry::Sampled || it->second->tex != tex) continue;
        ExactWriteLocked(it->second, lo, hi, seq);
        return true;
    }
    return false;
}

// A GPU-owned surface A is superseded when its whole guest range is covered by exactly known writes
// (write_lo..write_hi) of other GPU-owned surfaces, made by command buffers after the last one that
// used A: on the PS4 every byte of A was then overwritten, so A's GPU-only contents are dead. A
// newer surface that was only created, read, drawn to (which pixels a draw writes is unknown) or
// written no later than A's last use proves nothing (ties keep A), and partial cover keeps A,
// because the uncovered part may still hold contents that exist only in A's texture.
void TextureCache::SupersededLocked(std::vector<Entry*>* v, std::vector<char>* sup) const {
    v->clear();
    for (const auto& kv : entries_)
        if (kv.second->kind != Entry::Sampled) v->push_back(kv.second);
    std::sort(v->begin(), v->end(), [](const Entry* a, const Entry* b) { return a->address < b->address; });
    auto end_of = [](const Entry* e) { return e->address + std::max<uint64_t>(e->size, 1); };
    std::vector<uint64_t> max_end(v->size()); // max end over v[0..i]
    for (size_t i = 0; i < v->size(); ++i) max_end[i] = std::max(i ? max_end[i - 1] : 0, end_of((*v)[i]));
    sup->assign(v->size(), 0);
    std::vector<std::pair<uint64_t, uint64_t>> cover;
    for (size_t i = 0; i < v->size(); ++i) {
        const Entry* e = (*v)[i];
        const uint64_t a = e->address, b = end_of(e);
        cover.clear();
        auto take = [&](const Entry* o) {
            if (o != e && o->last_write_seq > e->last_seq && o->write_lo < b && o->write_hi > a)
                cover.push_back({std::max(o->write_lo, a), std::min(o->write_hi, b)});
        };
        for (size_t j = i + 1; j < v->size() && (*v)[j]->address < b; ++j) take((*v)[j]);
        for (size_t j = i; j-- > 0 && max_end[j] > a;) take((*v)[j]); // earlier starts reaching into A
        if (cover.empty()) continue;
        std::sort(cover.begin(), cover.end());
        uint64_t reach = a;
        for (const auto& c : cover) {
            if (c.first > reach) break;
            reach = std::max(reach, c.second);
        }
        (*sup)[i] = reach >= b;
    }
}

void TextureCache::RetireLocked(id<MTLTexture> t) {
    if (!t) return;
    retired_.push_back(t);
    retired_pending_.store(true, std::memory_order_release);
}

void TextureCache::TakeRetired(std::vector<id<MTLTexture>>* out) {
    if (!retired_pending_.load(std::memory_order_acquire)) return;
    TimedLock lk(mutex_);
    for (auto& t : retired_) out->push_back(t);
    retired_.clear();
    retired_pending_.store(false, std::memory_order_relaxed);
}

TextureCache::GpuOwnedStats TextureCache::GetGpuOwnedStats(uint64_t idle_ns) const {
    GpuOwnedStats g;
    TimedLock lk(mutex_);
    std::vector<Entry*> v;
    std::vector<char> sup;
    SupersededLocked(&v, &sup);
    const uint64_t now = use_ns_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < v.size(); ++i) {
        if (sup[i]) {
            g.superseded_count++;
            g.superseded_bytes += v[i]->alloc_bytes;
        }
        if (now > v[i]->last_ns && now - v[i]->last_ns >= idle_ns) {
            g.idle_count++;
            g.idle_bytes += v[i]->alloc_bytes;
        }
    }
    return g;
}

TextureCache::EvictResult TextureCache::EvictSuperseded(uint64_t idle_ns, uint64_t now_ns, uint64_t completed_seq) {
    EvictResult r;
    TimedLock lk(mutex_);
    std::vector<Entry*> v;
    std::vector<char> sup;
    SupersededLocked(&v, &sup);
    for (size_t i = 0; i < v.size(); ++i) {
        Entry* e = v[i];
        if (!sup[i] || e->last_seq > completed_seq || now_ns < e->last_ns || now_ns - e->last_ns < idle_ns) continue;
        auto range = entries_.equal_range(e->address);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second != e) continue;
            RetireLocked(e->tex);
            r.count++;
            r.bytes += e->alloc_bytes;
            TrackLocked(e, -1); // also marks the GPU range index stale
            UnmarkLocked(e);
            DropEntryLocked(e);
            entries_.erase(it);
            break;
        }
    }
    if (r.count) {
        evicted_gpu_count_.fetch_add(r.count, std::memory_order_relaxed);
        evicted_gpu_bytes_.fetch_add(r.bytes, std::memory_order_relaxed);
    }
    return r;
}

id<MTLTexture> TextureCache::NewTextureLocked(MTLTextureDescriptor* td) {
    id<MTLTexture> t = [device_ newTextureWithDescriptor:td];
    if (t && hooks_.created) hooks_.created(hooks_.ctx, t);
    return t;
}

void TextureCache::ReleaseTextureLocked(id<MTLTexture> t) {
    if (t && hooks_.released) hooks_.released(hooks_.ctx, t);
}

// An alias that is a view of a copy (GetTextureImpl: different dimensions) holds that copy as its parent.
id<MTLTexture> TextureCache::AliasCopy(const Entry* e) {
    return e->alias && e->alias.parentTexture && e->alias.parentTexture != e->tex ? e->alias.parentTexture : nil;
}

void TextureCache::DropEntryLocked(Entry* e) {
    ReleaseTextureLocked(e->tex);
    ReleaseTextureLocked(AliasCopy(e));
    delete e;
}

TextureCache::TextureCache(id<MTLDevice> device, id<MTLCommandQueue> queue, uint8_t* mem_base,
                           uint64_t mem_size)
    : device_(device), queue_(queue), mem_base_(mem_base), mem_size_(mem_size) {}

TextureCache::~TextureCache() {
    Clear();
}

void TextureCache::Clear() {
    TimedLock lk(mutex_);
    for (auto& kv : entries_) {
        TrackLocked(kv.second, -1);
        DropEntryLocked(kv.second);
    }
    entries_.clear();
    guest_newer_n_.store(0, std::memory_order_relaxed);
}

void TextureCache::HintAddLocked(uint64_t addr, uint64_t size) const {
    const uint64_t g0 = addr >> kHintShift, g1 = (addr + std::max<uint64_t>(size, 1) - 1) >> kHintShift;
    for (uint64_t g = g0; g <= g1 && g < kHintWords * 64; ++g)
        gpu_hint_[g >> 6].fetch_or(1ull << (g & 63), std::memory_order_release);
}

bool TextureCache::HintNear(uint64_t addr, uint64_t size) const {
    const uint64_t g0 = addr >> kHintShift, g1 = (addr + std::max<uint64_t>(size, 1) - 1) >> kHintShift;
    if (g1 >= kHintWords * 64) return true; // beyond the hint: take the lock
    for (uint64_t g = g0; g <= g1; ++g)
        if (gpu_hint_[g >> 6].load(std::memory_order_acquire) & (1ull << (g & 63))) return true;
    return false;
}

void TextureCache::TrackLocked(const Entry* e, int sign) {
    const bool sampled = e->kind == Entry::Sampled;
    if (!sampled) ++gpu_version_;
    if (!sampled && sign > 0) HintAddLocked(e->address, e->size);
    std::atomic<uint64_t>& count = sampled ? sampled_count_ : gpu_count_;
    std::atomic<uint64_t>& bytes = sampled ? sampled_bytes_ : gpu_bytes_;
    if (sign > 0) {
        count.fetch_add(1, std::memory_order_relaxed);
        bytes.fetch_add(e->alloc_bytes, std::memory_order_relaxed);
    } else {
        count.fetch_sub(1, std::memory_order_relaxed);
        bytes.fetch_sub(e->alloc_bytes, std::memory_order_relaxed);
    }
}

void TextureCache::SetUse(uint64_t seq, uint64_t now_ns) {
    use_seq_.store(seq, std::memory_order_relaxed);
    use_ns_.store(now_ns, std::memory_order_relaxed);
}

TextureCache::EvictResult TextureCache::EvictSampled(const ps4_texcache_policy& p, bool gpu_over, uint64_t now_ns,
                                                     uint64_t completed_seq) {
    EvictResult r;
    if (!ps4_texcache_evict_due(&p, sampled_bytes_.load(std::memory_order_relaxed), gpu_over)) return r;
    TimedLock lk(mutex_);
    evict_items_.clear();
    evict_its_.clear();
    uint64_t total = 0;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        const Entry* e = it->second;
        if (e->kind != Entry::Sampled) continue;
        evict_items_.push_back({e->last_ns, e->last_seq, e->alloc_bytes, (uint64_t)evict_its_.size()});
        evict_its_.push_back(it);
        total += e->alloc_bytes;
    }
    uint64_t freed = 0;
    const uint64_t n = ps4_texcache_select(evict_items_.data(), evict_items_.size(), total, &p, gpu_over ? 1 : 0,
                                           now_ns, completed_seq, &freed);
    for (uint64_t i = 0; i < n; ++i) {
        auto it = evict_its_[evict_items_[i].id];
        TrackLocked(it->second, -1);
        // work still using the texture keeps it alive: on Metal 3 its command buffer; on Metal 4 the
        // residency set until the removal's seq completed, and the part of each encoder that used it
        DropEntryLocked(it->second);
        entries_.erase(it);  // other iterators of the multimap stay valid
    }
    r.count = n;
    r.bytes = freed;
    if (n) {
        evicted_count_.fetch_add(n, std::memory_order_relaxed);
        evicted_bytes_.fetch_add(freed, std::memory_order_relaxed);
    }
    return r;
}

TextureCache::MemStats TextureCache::GetMemStats() const {
    MemStats m;
    m.sampled_count = sampled_count_.load(std::memory_order_relaxed);
    m.sampled_bytes = sampled_bytes_.load(std::memory_order_relaxed);
    m.gpu_count = gpu_count_.load(std::memory_order_relaxed);
    m.gpu_bytes = gpu_bytes_.load(std::memory_order_relaxed);
    return m;
}

void TextureCache::DrainQueueLocked() {
    if (!queue_) return;
    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    [cb commit];
    [cb waitUntilCompleted];
}

static MTLStorageMode SharedOrManaged(id<MTLDevice>) {
    // Apple silicon (UMA): shared textures are CPU-writable without staging blits.
    return MTLStorageModeShared;
}

extern "C" void ps4_gnm_hitch_note_upload(double ms);

bool TextureCache::UploadLocked(Entry* e) {
    const double t0 = CFAbsoluteTimeGetCurrent();
    const bool ok = UploadLockedImpl(e);
    ps4_gnm_hitch_note_upload((CFAbsoluteTimeGetCurrent() - t0) * 1000.0);
    return ok;
}

bool TextureCache::UploadLockedImpl(Entry* e) {
    if (!e->tex || !mem_base_) return false;
    // MSAA / private textures cannot be written by the CPU (their contents are GPU-produced)
    if (e->tex.sampleCount > 1 || e->tex.storageMode == MTLStorageModePrivate) {
        e->dirty = false;
        return false;
    }
    if (e->address >= mem_size_) return false;
    const PixelFormatInfo pf = GetPixelFormat(e->dfmt, e->nfmt);
    const uint32_t bpe = e->surf.bpp / 8;
    if (bpe == 0) return false;
    std::vector<uint8_t> lin((size_t)e->layout.total_size);
    const uint64_t avail = mem_size_ - e->address;
    if (!DetileSurface(e->surf, e->layout, mem_base_ + e->address,
                       (size_t)std::min<uint64_t>(avail, e->layout.total_size), lin.data(),
                       lin.size(), 0, e->levels, true)) {
        return false;
    }
    const bool is3d = e->tex_type == MTLTextureType3D;
    for (uint32_t m = 0; m < e->levels; ++m) {
        const MipLayout& ml = e->layout.mips[m];
        const uint32_t w = std::max(1u, e->width >> m), h = std::max(1u, e->height >> m);
        const uint32_t row = ml.pitch * bpe;               // elements -> bytes (blocks for BCn)
        const uint32_t img = ml.pitch * ml.height * bpe;
        (void)pf;
        // bounds check: rows actually read by replaceRegion (in blocks for BCn)
        const uint32_t bh = pf.block_height ? pf.block_height : 1;
        const uint32_t bw = pf.block_width ? pf.block_width : 1;
        const uint64_t rows = (uint64_t)(h + bh - 1) / bh;
        const uint64_t rowbytes = (uint64_t)((w + bw - 1) / bw) * pf.bytes_per_block;
        const uint64_t slices_read = is3d ? std::max(1u, e->depth >> m) : std::min(e->layers, ml.slices);
        if (rowbytes > row || (slices_read ? (uint64_t)ml.offset + (slices_read - 1) * (uint64_t)img + rows * row : 0) > lin.size()) {
            static int warned = 0;
            if (warned++ < 16)
                fprintf(stderr, "[gnm-tex] upload of 0x%llx mip %u out of bounds (fmt %u %ux%u pitch %u height %u bpe %u "
                                "rows %llu slices %llu offset %u total %zu); skipped\n",
                        (unsigned long long)e->address, m, e->mtl_format, w, h, ml.pitch, ml.height, bpe,
                        (unsigned long long)rows, (unsigned long long)slices_read, ml.offset, lin.size());
            break;
        }
        static int dbg = getenv("PS4_GNM_TEXDBG") ? 1 : 0;
        if (dbg)
            fprintf(stderr, "[gnm-tex] upload 0x%llx type %u fmt %u mip %u %ux%u d%u layers %u pitch %u height %u bpe %u off %u total %zu tex %lux%lux%lu arr %lu\n",
                    (unsigned long long)e->address, e->tex_type, e->mtl_format, m, w, h, e->depth, e->layers, ml.pitch,
                    ml.height, bpe, ml.offset, lin.size(), (unsigned long)e->tex.width, (unsigned long)e->tex.height,
                    (unsigned long)e->tex.depth, (unsigned long)e->tex.arrayLength);
        if (is3d) {
            const uint32_t d = std::max(1u, e->depth >> m);
            [e->tex replaceRegion:MTLRegionMake3D(0, 0, 0, w, h, d)
                      mipmapLevel:m
                            slice:0
                        withBytes:lin.data() + ml.offset
                      bytesPerRow:row
                    bytesPerImage:img];
        } else {
            uint32_t layers = std::min(e->layers, ml.slices);
            const NSUInteger tex_slices = (e->tex.textureType == MTLTextureTypeCube || e->tex.textureType == MTLTextureTypeCubeArray)
                                              ? e->tex.arrayLength * 6 : e->tex.arrayLength;
            layers = std::min<uint32_t>(layers, (uint32_t)tex_slices);
            for (uint32_t s = 0; s < layers; ++s) {
                [e->tex replaceRegion:MTLRegionMake2D(0, 0, w, e->tex_type == MTLTextureType1D ||
                                                                e->tex_type == MTLTextureType1DArray ? 1 : h)
                          mipmapLevel:m
                                slice:s
                            withBytes:lin.data() + ml.offset + (size_t)s * img
                          bytesPerRow:row
                        bytesPerImage:0];
            }
        }
    }
    stats.uploads++;
    stats.upload_bytes += e->layout.total_size;
    e->dirty = false;
    return true;
}

static uint32_t MaxMips(uint32_t w, uint32_t h, uint32_t d) {
    return BitWidth(std::max(w, std::max(h, d)));
}

id<MTLTexture> TextureCache::GetTexture(const ImageDesc& img) {
    return GetTextureImpl(img, nullptr, nullptr);
}

id<MTLTexture> TextureCache::GetTextureForGpuUpload(const ImageDesc& img, uint64_t write_epoch,
                                                    uint64_t upload_epoch, GpuUpload* up) {
    const uint64_t epochs[2] = {write_epoch, upload_epoch};
    *up = GpuUpload{};
    return GetTextureImpl(img, epochs, up);
}

id<MTLTexture> TextureCache::GetTextureForGpuRefresh(const ImageDesc& img, uint64_t upload_epoch, uint64_t min_bytes,
                                                     GpuUpload* up) {
    const uint64_t epochs[2] = {0, upload_epoch};
    *up = GpuUpload{};
    return GetTextureImpl(img, epochs, up, std::max<uint64_t>(1, min_bytes));
}

void TextureCache::MarkStale(uint64_t address, id<MTLTexture> tex) {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second->tex == tex) {
            it->second->dirty = true;
            it->second->gpu_upload_epoch = 0;
            return;
        }
}

bool TextureCache::DebugCpuUploadInto(const ImageDesc& img, id<MTLTexture> dst) {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(img.address);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind != Entry::Sampled || !e->tex || e->tex.pixelFormat != dst.pixelFormat ||
            e->tex.width != dst.width || e->tex.height != dst.height || e->tex.mipmapLevelCount != dst.mipmapLevelCount)
            continue;
        Entry tmp = *e; // same surface and layout, other texture
        tmp.tex = dst;
        const bool ok = UploadLockedImpl(&tmp);
        tmp.tex = nil;
        return ok;
    }
    return false;
}

// gpu_epochs: {write_epoch, upload_epoch} for GetTextureForGpuUpload / GetTextureForGpuRefresh,
// nullptr for GetTexture. refresh_min_bytes > 0: refresh mode (GetTextureForGpuRefresh).
id<MTLTexture> TextureCache::GetTextureImpl(const ImageDesc& img, const uint64_t* gpu_epochs, GpuUpload* up,
                                            uint64_t refresh_min_bytes) {
    if (!img.valid || img.address == 0 || img.pixel.mtl_format == 0 || !device_) return nil;
    const SurfaceDesc sd = img.ToSurfaceDesc();
    if (sd.bpp == 0) return nil;
    TimedLock lk(mutex_);

    const bool is3d = img.mtl_texture_type == MTLTextureType3D;
    const uint32_t width = img.width;
    const uint32_t height = (img.mtl_texture_type == MTLTextureType1D || img.mtl_texture_type == MTLTextureType1DArray) ? 1 : img.height;
    const uint32_t depth = is3d ? img.depth : 1;
    const uint32_t levels = std::max(1u, std::min(sd.levels, MaxMips(width, height, depth)));
    const bool msaa = img.mtl_texture_type == MTLTextureType2DMultisample ||
                      img.mtl_texture_type == MTLTextureType2DMultisampleArray;
    uint32_t layers = is3d ? 1 : img.num_layers;
    if (img.mtl_texture_type == MTLTextureType1D || img.mtl_texture_type == MTLTextureType2D ||
        img.mtl_texture_type == MTLTextureType2DMultisample) {
        layers = 1;
    }

    // 1) GPU-written render target at this address?
    auto range = entries_.equal_range(img.address);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind != Entry::Color || !e->tex) continue;
        const PixelFormatInfo rtpf = GetPixelFormat(e->dfmt, e->nfmt);
        if (rtpf.bytes_per_block != img.pixel.bytes_per_block || img.pixel.compressed) continue;
        stats.rt_aliased++;
        const MTLTextureSwizzleChannels swz = ToMTLSwizzle(img.view_swizzle);
        if (e->width == width && e->height == height && img.mtl_texture_type == MTLTextureType2D) {
            const uint32_t key = img.pixel.mtl_format ^ ((uint32_t)img.view_swizzle.r << 16) ^
                                 ((uint32_t)img.view_swizzle.g << 20) ^ ((uint32_t)img.view_swizzle.b << 24) ^
                                 ((uint32_t)img.view_swizzle.a << 28);
            if (!e->alias || e->alias_key != key) {
                ReleaseTextureLocked(AliasCopy(e)); // a copy this view replaces
                e->alias = [e->tex newTextureViewWithPixelFormat:(MTLPixelFormat)img.pixel.mtl_format
                                                     textureType:MTLTextureType2D
                                                          levels:NSMakeRange(0, 1)
                                                          slices:NSMakeRange(0, 1)
                                                         swizzle:swz];
                e->alias_key = key;
            }
            return TouchLocked(e), e->alias;
        }
        // different dimensions: copy the overlapping region into a sampled texture
        if (!e->alias || e->alias_generation != e->generation || e->alias.width != width ||
            e->alias.height != height || e->alias.pixelFormat != (MTLPixelFormat)img.pixel.mtl_format) {
            MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(MTLPixelFormat)e->mtl_format
                                                                                          width:width
                                                                                         height:height
                                                                                      mipmapped:NO];
            td.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
            td.storageMode = MTLStorageModePrivate;
            id<MTLTexture> copy = NewTextureLocked(td);
            const MTLSize sz = MTLSizeMake(std::min(width, e->width), std::min(height, e->height), 1);
            if (copy_hook_.copy) {
                copy_hook_.copy(copy_hook_.ctx, e->tex, copy, sz);
            } else {
                id<MTLCommandBuffer> cb = [queue_ commandBuffer];
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
                [bl copyFromTexture:e->tex
                        sourceSlice:0
                        sourceLevel:0
                       sourceOrigin:MTLOriginMake(0, 0, 0)
                         sourceSize:sz
                          toTexture:copy
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
                [bl endEncoding];
                [cb commit];
            }
            ReleaseTextureLocked(AliasCopy(e)); // the copy this one replaces
            e->alias = [copy newTextureViewWithPixelFormat:(MTLPixelFormat)img.pixel.mtl_format
                                               textureType:MTLTextureType2D
                                                    levels:NSMakeRange(0, 1)
                                                    slices:NSMakeRange(0, 1)
                                                   swizzle:swz];
            e->alias_generation = e->generation;
            e->alias_key = 0;
        }
        return TouchLocked(e), e->alias;
    }

    // GPU upload request: hand the entry to the caller instead of reading guest memory
    auto gpu_upload = [&](Entry* e) {
        const PixelFormatInfo pf = GetPixelFormat(e->dfmt, e->nfmt);
        up->tex = e->tex;
        up->surf = e->surf;
        up->layout = e->layout;
        up->width = e->width;
        up->height = e->height;
        up->depth = e->depth;
        up->levels = e->levels;
        up->layers = e->layers;
        up->tex_type = e->tex_type;
        up->bytes_per_block = pf.bytes_per_block;
        up->block_width = pf.block_width ? pf.block_width : 1;
        up->block_height = pf.block_height ? pf.block_height : 1;
        e->dirty = false;
        e->gpu_upload_epoch = gpu_epochs[1];
        stats.uploads++;
        stats.upload_bytes += e->layout.total_size;
    };

    // 2) cached sampled texture with identical identity
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind != Entry::Sampled) continue;
        if (e->mtl_format == img.pixel.mtl_format && e->tex_type == img.mtl_texture_type &&
            e->width == width && e->height == height && e->depth == depth && e->levels == levels &&
            e->layers == layers && e->swizzle == img.view_swizzle && e->surf.pitch == sd.pitch &&
            e->surf.tile_mode == sd.tile_mode && e->surf.is_pow2 == sd.is_pow2) {
            // CPU-written linear textures (e.g. Scaleform glyph cache) change dynamically without GPU barriers
            if (e->surf.tile_mode == TileMode::DisplayLinearAligned ||
                e->surf.tile_mode == TileMode::DisplayLinearGeneral) {
                e->dirty = true;
            }
            if (refresh_min_bytes) {
                // stale contents: big surfaces are copied on the GPU, small ones by the CPU
                // (MSAA entries take the CPU path, which only clears their dirty flag)
                if (e->dirty) {
                    if (e->samples <= 1 && e->layout.total_size >= refresh_min_bytes) gpu_upload(e);
                    else UploadLocked(e);
                }
            } else if (gpu_epochs) {
                // stale unless already copied on the GPU after the pending writes
                if ((e->dirty || e->gpu_upload_epoch <= gpu_epochs[0]) && e->samples <= 1) gpu_upload(e);
            } else if (e->dirty) {
                UploadLocked(e);
            }
            TouchLocked(e);
            stats.hits++;
            return e->tex;
        }
    }

    // 3) create
    stats.misses++;
    Entry* e = new Entry();
    e->kind = Entry::Sampled;
    e->address = img.address;
    e->mtl_format = img.pixel.mtl_format;
    e->tex_type = img.mtl_texture_type;
    e->width = width;
    e->height = height;
    e->depth = depth;
    e->levels = levels;
    e->layers = layers;
    e->samples = img.num_samples;
    e->swizzle = img.view_swizzle;
    e->surf = sd;
    e->dfmt = img.dfmt;
    e->nfmt = img.nfmt;
    if (!ComputeSurfaceLayout(sd, &e->layout)) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    e->size = e->layout.total_size;

    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = (MTLTextureType)img.mtl_texture_type;
    td.pixelFormat = (MTLPixelFormat)img.pixel.mtl_format;
    td.width = width;
    td.height = height;
    td.depth = depth;
    td.mipmapLevelCount = msaa ? 1 : levels;
    td.sampleCount = msaa ? img.num_samples : 1;
    if (img.mtl_texture_type == MTLTextureTypeCube) td.arrayLength = 1;
    else if (img.mtl_texture_type == MTLTextureTypeCubeArray) td.arrayLength = std::max(1u, layers / 6);
    else td.arrayLength = layers;
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = msaa ? MTLStorageModePrivate : SharedOrManaged(device_);
    td.swizzle = ToMTLSwizzle(img.view_swizzle);
    e->tex = NewTextureLocked(td);
    if (!e->tex) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    if (!msaa) {
        if (gpu_epochs && (!refresh_min_bytes || e->layout.total_size >= refresh_min_bytes)) gpu_upload(e);
        else UploadLocked(e);
    }
    TouchLocked(e);
    e->alloc_bytes = (uint64_t)e->tex.allocatedSize;
    TrackLocked(e, +1);
    entries_.emplace(e->address, e);
    return e->tex;
}

id<MTLTexture> TextureCache::GetColorTarget(const ColorTarget& cb, bool load_from_guest) {
    if (!cb.bound || cb.mtl_format == 0 || !device_) return nil;
    TimedLock lk(mutex_);
    const uint32_t w = (cb.extent_width && cb.extent_width <= cb.pitch) ? cb.extent_width : cb.pitch;
    const uint32_t h = (cb.extent_height && cb.extent_height <= cb.height + 64) ? cb.extent_height : cb.height;
    const uint32_t layers = cb.num_slices;
    auto range = entries_.equal_range(cb.address);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind == Entry::Color && e->mtl_format == cb.mtl_format && e->width == w &&
            e->height == h && e->layers == layers && e->samples == cb.num_samples) {
            e->generation++;
            stats.hits++;
            GpuWroteLocked(e); // drawn into: earlier buffer writes no longer describe it
            return TouchLocked(e), e->tex;
        }
    }
    Entry* e = new Entry();
    e->kind = Entry::Color;
    e->address = cb.address;
    e->mtl_format = cb.mtl_format;
    e->width = w;
    e->height = h;
    e->depth = 1;
    e->levels = 1;
    e->layers = layers;
    e->samples = cb.num_samples;
    e->surf = cb.ToSurfaceDesc();
    e->surf.width = w;
    e->surf.height = h;
    cb.GetLayout(&e->layout);
    e->size = e->layout.total_size;
    e->dfmt = cb.dfmt;
    e->nfmt = cb.nfmt;
    e->cb_swizzle = cb.swizzle;
    e->tex_type = cb.num_samples > 1 ? (layers > 1 ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample)
                                     : (layers > 1 ? MTLTextureType2DArray : MTLTextureType2D);
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = (MTLTextureType)e->tex_type;
    td.pixelFormat = (MTLPixelFormat)cb.mtl_format;
    td.width = w;
    td.height = h;
    td.arrayLength = layers;
    td.sampleCount = cb.num_samples;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
               MTLTextureUsagePixelFormatView;
    td.storageMode = cb.num_samples > 1 ? MTLStorageModePrivate : SharedOrManaged(device_);
    e->tex = NewTextureLocked(td);
    if (!e->tex) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    if (load_from_guest && cb.num_samples == 1) {
        // one-time initial contents; afterwards the GPU owns the texture. The Metal format has
        // the same memory layout as the guest surface (BGRA chosen for swapped 8888).
        e->levels = 1;
        UploadLocked(e);
    }
    e->dirty = false;
    e->generation = 1;
    stats.rt_created++;
    e->alloc_bytes = (uint64_t)e->tex.allocatedSize;
    TrackLocked(e, +1);
    entries_.emplace(e->address, e);
    return TouchLocked(e), e->tex;
}

id<MTLTexture> TextureCache::GetDepthTarget(const DepthTarget& db, id<MTLTexture>* grown_from) {
    if (grown_from) *grown_from = nil;
    if (!db.bound || db.mtl_format == 0 || !device_) return nil;
    TimedLock lk(mutex_);
    const uint64_t addr = db.z_read_address ? db.z_read_address : db.stencil_read_address;
    const uint32_t dw = (db.extent_width && db.extent_width <= db.pitch) ? db.extent_width : db.pitch;
    const uint32_t dh = (db.extent_height && db.extent_height <= db.height) ? db.extent_height : db.height;
    const uint32_t layers = db.num_samples > 1 ? 1 : db.num_slices;
    auto range = entries_.equal_range(addr);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind == Entry::Depth && e->mtl_format == db.mtl_format && e->width == dw &&
            e->height == dh && e->samples == db.num_samples) {
            if (e->layers < layers) {
                MTLTextureDescriptor* td = [MTLTextureDescriptor new];
                td.textureType = MTLTextureType2DArray;
                td.pixelFormat = (MTLPixelFormat)db.mtl_format;
                td.width = dw;
                td.height = dh;
                td.arrayLength = layers;
                td.usage = e->tex.usage;
                td.storageMode = MTLStorageModePrivate;
                id<MTLTexture> t = NewTextureLocked(td);
                if (!t) return e->tex;
                if (grown_from) *grown_from = e->tex;
                ReleaseTextureLocked(e->tex);
                RetireLocked(e->tex); // the renderer forgets state keyed by the old texture
                TrackLocked(e, -1);
                e->tex = t;
                e->alloc_bytes = (uint64_t)t.allocatedSize;
                e->layers = layers;
                e->size = (uint64_t)db.slice_bytes * layers;
                TrackLocked(e, +1); // also marks the GPU range index stale
            }
            e->generation++;
            return TouchLocked(e), e->tex;
        }
    }
    Entry* e = new Entry();
    e->kind = Entry::Depth;
    e->address = addr;
    e->mtl_format = db.mtl_format;
    e->width = dw;
    e->height = dh;
    e->layers = db.num_slices;
    e->samples = db.num_samples;
    e->levels = 1;
    e->size = (uint64_t)db.slice_bytes * db.num_slices;
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = db.num_samples > 1 ? MTLTextureType2DMultisample
                                        : (db.num_slices > 1 ? MTLTextureType2DArray : MTLTextureType2D);
    td.pixelFormat = (MTLPixelFormat)db.mtl_format;
    td.width = dw;
    td.height = dh;
    td.arrayLength = db.num_samples > 1 ? 1 : db.num_slices;
    td.sampleCount = db.num_samples;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    td.storageMode = MTLStorageModePrivate; // depth contents are not loaded from guest memory
    e->tex = NewTextureLocked(td);
    if (!e->tex) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    e->dirty = false;
    e->alloc_bytes = (uint64_t)e->tex.allocatedSize;
    TrackLocked(e, +1);
    entries_.emplace(addr, e);
    return TouchLocked(e), e->tex;
}

void TextureCache::InvalidateRange(uint64_t addr, uint64_t size, bool include_rts, uint64_t write_epoch) {
    TimedLock lk(mutex_);
    const uint64_t end = addr + size;
    for (auto& kv : entries_) {
        Entry* e = kv.second;
        if (e->address < end && addr < e->address + e->size) {
            if (e->kind == Entry::Sampled) {
                if (e->gpu_upload_epoch > write_epoch) continue; // copied on the GPU after that write
                e->dirty = true;
                stats.invalidations++;
            } else if (include_rts && e->kind == Entry::Color) {
                UploadLocked(e);
                e->generation++;
                stats.invalidations++;
            }
        }
    }
}

void TextureCache::InvalidateWrites(std::vector<WriteRange>& w) {
    if (w.empty()) return;
    std::sort(w.begin(), w.end(), [](const WriteRange& a, const WriteRange& b) {
        return a.addr != b.addr ? a.addr < b.addr : a.size < b.size;
    });
    // identical ranges: the latest write decides (same as testing each of them)
    size_t n = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        if (n && w[n - 1].addr == w[i].addr && w[n - 1].size == w[i].size)
            w[n - 1].epoch = std::max(w[n - 1].epoch, w[i].epoch);
        else
            w[n++] = w[i];
    }
    w.resize(n);
    std::vector<uint64_t> max_end(n); // max end of w[0..i]
    for (size_t i = 0; i < n; ++i) max_end[i] = std::max(i ? max_end[i - 1] : 0, w[i].addr + w[i].size);
    TimedLock lk(mutex_);
    for (auto& kv : entries_) {
        Entry* e = kv.second;
        if (e->kind != Entry::Sampled) continue;
        const uint64_t a = e->address, b = e->address + e->size;
        // ranges starting before the end of the texture, walked back while they can reach it
        size_t j = std::lower_bound(w.begin(), w.end(), b, [](const WriteRange& r, uint64_t v) { return r.addr < v; }) -
                   w.begin();
        while (j > 0 && max_end[j - 1] > a) {
            const WriteRange& r = w[--j];
            if (r.addr + r.size > a && e->gpu_upload_epoch <= r.epoch) {
                e->dirty = true;
                stats.invalidations++;
                break;
            }
        }
    }
}

id<MTLTexture> TextureCache::FindColorTarget(uint64_t address) const {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second->kind == Entry::Color) return TouchLocked(it->second), it->second->tex;
    return nil;
}

id<MTLTexture> TextureCache::FindColorTarget(uint64_t address, uint32_t width, uint32_t height) const {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        const Entry* e = it->second;
        if (e->kind == Entry::Color && e->tex && e->tex.width == width && e->tex.height == height) return TouchLocked(e), e->tex;
    }
    return nil;
}

id<MTLTexture> TextureCache::FindDepthSibling(const DepthTarget& db, id<MTLTexture> self) const {
    if (!self) return nil;
    TimedLock lk(mutex_);
    const uint64_t addr = db.z_read_address ? db.z_read_address : db.stencil_read_address;
    auto range = entries_.equal_range(addr);
    for (auto it = range.first; it != range.second; ++it) {
        const Entry* e = it->second;
        if (e->kind == Entry::Depth && e->tex && e->tex != self && e->mtl_format == (uint32_t)self.pixelFormat &&
            e->tex.width == self.width && e->tex.height == self.height && e->tex.sampleCount != self.sampleCount)
            return TouchLocked(e), e->tex;
    }
    return nil;
}

id<MTLTexture> TextureCache::FindDepthTarget(uint64_t address) const {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second->kind == Entry::Depth) return TouchLocked(it->second), it->second->tex;
    return nil;
}

bool TextureCache::CloneGpuTexture(uint64_t src_addr, uint64_t dst_addr, uint64_t written_bytes,
                                   id<MTLTexture>* src_out, id<MTLTexture>* dst_out, int* kind_out) {
    if (!device_ || src_addr == dst_addr) return false;
    TimedLock lk(mutex_);
    Entry* s = nullptr;
    auto range = entries_.equal_range(src_addr);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind == Entry::Sampled || !e->tex) continue;
        if (!s || e->kind == Entry::Depth) s = e;
    }
    if (!s) return false;
    Entry* d = nullptr;
    auto drange = entries_.equal_range(dst_addr);
    for (auto it = drange.first; it != drange.second; ++it) {
        Entry* e = it->second;
        if (e->kind == s->kind && e->tex && e->mtl_format == s->mtl_format && e->width == s->width &&
            e->height == s->height && e->layers == s->layers && e->samples == s->samples &&
            e->tex.textureType == s->tex.textureType) {
            d = e;
            break;
        }
    }
    if (!d) {
        d = new Entry(*s);
        d->address = dst_addr;
        d->alias = nil;
        d->alias_generation = ~0ull;
        d->alias_key = 0;
        d->last_write_seq = d->write_lo = d->write_hi = 0; // not the source's writes
        d->newer_epoch = 0;                                // nor its buffer writes
        d->newer_cover.clear();
        d->newer_partial_logged = false;
        MTLTextureDescriptor* td = [MTLTextureDescriptor new];
        td.textureType = s->tex.textureType;
        td.pixelFormat = s->tex.pixelFormat;
        td.width = s->tex.width;
        td.height = s->tex.height;
        td.depth = s->tex.depth;
        td.arrayLength = s->tex.arrayLength;
        td.sampleCount = s->tex.sampleCount;
        td.mipmapLevelCount = s->tex.mipmapLevelCount;
        td.usage = s->tex.usage;
        td.storageMode = s->tex.storageMode;
        d->tex = NewTextureLocked(td);
        if (!d->tex) {
            DropEntryLocked(d);
            return false;
        }
        d->generation = 1;
        d->alloc_bytes = (uint64_t)d->tex.allocatedSize;
        TrackLocked(d, +1);
        entries_.emplace(dst_addr, d);
    }
    d->generation++;
    *src_out = s->tex;
    *dst_out = d->tex;
    TouchLocked(s);
    ExactWriteLocked(d, dst_addr, dst_addr + written_bytes); // the copy writes exactly these bytes
    if (kind_out) *kind_out = s->kind == Entry::Depth ? 2 : 1;
    return true;
}

void TextureCache::ForEachGpuTexture(void (^fn)(uint64_t address, int kind, id<MTLTexture> tex)) const {
    std::vector<std::pair<uint64_t, std::pair<int, id<MTLTexture>>>> v;
    {
        TimedLock lk(mutex_);
        for (const auto& kv : entries_)
            if (kv.second->kind != Entry::Sampled && kv.second->tex)
                v.push_back({kv.second->address, {(int)kv.second->kind, kv.second->tex}});
    }
    for (auto& e : v) fn(e.first, e.second.first, e.second.second);
}

// =====================================================================================
// Frame capture and pass replay (ps4_gnm_capture.h)
// =====================================================================================
void PackSurface(const SurfaceDesc& s, const SurfaceLayout& l, std::vector<uint32_t>* out) {
    out->assign({(uint32_t)s.tile_mode, s.bpp, s.num_samples, s.width, s.height, s.depth, s.pitch, s.levels,
                 s.layers, (uint32_t)s.is_block, (uint32_t)s.is_pow2, (uint32_t)s.alt_tile, s.bank_swizzle,
                 (uint32_t)l.array_mode, l.num_mips, l.total_size, l.micro_tiled_mips});
    for (uint32_t m = 0; m < kMaxMips; ++m) {
        const MipLayout& ml = l.mips[m];
        out->insert(out->end(), {ml.offset, ml.size, ml.pitch, ml.height, ml.slices, (uint32_t)ml.micro_tiled});
    }
}

bool UnpackSurface(const std::vector<uint32_t>& v, SurfaceDesc* s, SurfaceLayout* l) {
    if (v.size() != 17 + 6 * kMaxMips) return false;
    s->tile_mode = (TileMode)v[0];
    s->bpp = v[1];
    s->num_samples = v[2];
    s->width = v[3];
    s->height = v[4];
    s->depth = v[5];
    s->pitch = v[6];
    s->levels = v[7];
    s->layers = v[8];
    s->is_block = v[9] != 0;
    s->is_pow2 = v[10] != 0;
    s->alt_tile = v[11] != 0;
    s->bank_swizzle = v[12];
    l->array_mode = (ArrayMode)v[13];
    l->num_mips = v[14];
    l->total_size = v[15];
    l->micro_tiled_mips = v[16];
    for (uint32_t m = 0; m < kMaxMips; ++m) {
        const uint32_t* w = &v[17 + 6 * m];
        l->mips[m] = MipLayout{w[0], w[1], w[2], w[3], w[4], w[5] != 0};
    }
    return true;
}

void TextureCache::EntryId(const Entry& e, ps4gnm_capture::GpuTextureId* o) {
    o->kind = (int)e.kind;
    o->address = e.address;
    o->size = e.size;
    o->entry_format = e.mtl_format;
    o->tex_type = e.tex_type;
    o->width = e.width;
    o->height = e.height;
    o->depth = e.depth;
    o->levels = e.levels;
    o->layers = e.layers;
    o->samples = e.samples;
    o->dfmt = (uint32_t)e.dfmt;
    o->nfmt = (uint32_t)e.nfmt;
    for (uint32_t k = 0; k < 4; ++k) {
        o->swizzle[k] = (uint8_t)e.swizzle[k];
        o->cb_swizzle[k] = (uint8_t)e.cb_swizzle[k];
    }
    id<MTLTexture> t = e.tex;
    o->mtl_type = (uint32_t)t.textureType;
    o->mtl_format = (uint32_t)t.pixelFormat;
    o->mtl_width = (uint32_t)t.width;
    o->mtl_height = (uint32_t)t.height;
    o->mtl_depth = (uint32_t)t.depth;
    o->mtl_levels = (uint32_t)t.mipmapLevelCount;
    o->mtl_array = (uint32_t)t.arrayLength;
    o->mtl_samples = (uint32_t)t.sampleCount;
    o->usage = (uint32_t)t.usage;
    o->storage_mode = (uint32_t)t.storageMode;
    PackSurface(e.surf, e.layout, &o->surface);
}

bool TextureCache::SameIdentity(const Entry& e, const ps4gnm_capture::GpuTextureId& gid) {
    return (int)e.kind == gid.kind && e.tex && e.address == gid.address && e.mtl_format == gid.entry_format &&
           e.width == gid.width &&
           e.height == gid.height && e.layers == gid.layers && e.samples == gid.samples &&
           (uint32_t)e.tex.pixelFormat == gid.mtl_format && (uint32_t)e.tex.textureType == gid.mtl_type &&
           (uint32_t)e.tex.mipmapLevelCount == gid.mtl_levels;
}

void TextureCache::DescribeGpuTextures(uint64_t address, std::vector<ps4gnm_capture::GpuTextureId>* out) const {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second->kind == Entry::Sampled || !it->second->tex) continue;
        out->emplace_back();
        EntryId(*it->second, &out->back());
    }
}

bool TextureCache::DescribeGpuTexture(id<MTLTexture> tex, ps4gnm_capture::GpuTextureId* out) const {
    if (!tex) return false;
    TimedLock lk(mutex_);
    for (const auto& kv : entries_) {
        const Entry* e = kv.second;
        if (e->kind != Entry::Sampled && e->tex == tex) {
            EntryId(*e, out);
            return true;
        }
    }
    return false;
}

id<MTLTexture> TextureCache::FindGpuTexture(const ps4gnm_capture::GpuTextureId& gid, bool create) {
    if (!device_ || (gid.kind != Entry::Color && gid.kind != Entry::Depth)) return nil;
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(gid.address);
    for (auto it = range.first; it != range.second; ++it)
        if (SameIdentity(*it->second, gid)) return TouchLocked(it->second), it->second->tex;
    if (!create) return nil;
    Entry* e = new Entry();
    e->kind = (Entry::Kind)gid.kind;
    e->address = gid.address;
    e->size = gid.size;
    e->mtl_format = gid.entry_format;
    e->tex_type = gid.tex_type;
    e->width = gid.width;
    e->height = gid.height;
    e->depth = gid.depth;
    e->levels = gid.levels;
    e->layers = gid.layers;
    e->samples = gid.samples;
    e->dfmt = (DataFormat)gid.dfmt;
    e->nfmt = (NumberFormat)gid.nfmt;
    for (uint32_t k = 0; k < 4; ++k) {
        e->swizzle[k] = (CompSwizzle)gid.swizzle[k];
        e->cb_swizzle[k] = (CompSwizzle)gid.cb_swizzle[k];
    }
    if (!UnpackSurface(gid.surface, &e->surf, &e->layout)) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = (MTLTextureType)gid.mtl_type;
    td.pixelFormat = (MTLPixelFormat)gid.mtl_format;
    td.width = gid.mtl_width;
    td.height = gid.mtl_height;
    td.depth = gid.mtl_depth;
    td.mipmapLevelCount = gid.mtl_levels;
    td.arrayLength = gid.mtl_array;
    td.sampleCount = gid.mtl_samples;
    td.usage = (MTLTextureUsage)gid.usage;
    td.storageMode = (MTLStorageMode)gid.storage_mode;
    e->tex = NewTextureLocked(td);
    if (!e->tex) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    e->dirty = false;
    e->generation = 1;
    TouchLocked(e);
    e->alloc_bytes = (uint64_t)e->tex.allocatedSize;
    TrackLocked(e, +1);
    entries_.emplace(e->address, e);
    return e->tex;
}

namespace {
// One blit per level / slice between `tex` and a shared buffer in the RawSize layout.
bool RawBlit(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLTexture> tex, id<MTLBuffer> buf, bool to_texture) {
    using namespace ps4gnm_capture;
    const uint32_t fmt = (uint32_t)tex.pixelFormat, bpe = BytesPerTexel(fmt);
    if (!bpe || tex.sampleCount > 1) return false;
    const bool cube = tex.textureType == MTLTextureTypeCube || tex.textureType == MTLTextureTypeCubeArray;
    const bool is3d = tex.textureType == MTLTextureType3D;
    const NSUInteger slices = tex.arrayLength * (cube ? 6 : 1);
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    NSUInteger off = 0;
    const bool ds = fmt == kDepth32Float_Stencil8;
    for (int plane = 0; plane < (ds ? 2 : 1); ++plane) {
        const NSUInteger pb = plane ? 1 : bpe;
        const MTLBlitOption opt = !ds ? MTLBlitOptionNone
                                      : plane ? MTLBlitOptionStencilFromDepthStencil : MTLBlitOptionDepthFromDepthStencil;
        for (NSUInteger l = 0; l < tex.mipmapLevelCount; ++l) {
            const NSUInteger w = std::max<NSUInteger>(1, tex.width >> l), h = std::max<NSUInteger>(1, tex.height >> l);
            const NSUInteger d = is3d ? std::max<NSUInteger>(1, tex.depth >> l) : 1;
            for (NSUInteger s = 0; s < slices; ++s) {
                if (to_texture)
                    [bl copyFromBuffer:buf sourceOffset:off sourceBytesPerRow:w * pb sourceBytesPerImage:w * h * pb
                            sourceSize:MTLSizeMake(w, h, d) toTexture:tex destinationSlice:s destinationLevel:l
                     destinationOrigin:MTLOriginMake(0, 0, 0) options:opt];
                else
                    [bl copyFromTexture:tex sourceSlice:s sourceLevel:l sourceOrigin:MTLOriginMake(0, 0, 0)
                             sourceSize:MTLSizeMake(w, h, d) toBuffer:buf destinationOffset:off
                 destinationBytesPerRow:w * pb destinationBytesPerImage:w * h * pb options:opt];
                off += w * h * d * pb;
            }
        }
    }
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    (void)dev;
    return cb.status == MTLCommandBufferStatusCompleted && off == buf.length;
}
} // namespace

bool TextureCache::ReadRaw(id<MTLTexture> tex, std::vector<uint8_t>* out) {
    if (!tex || !out || !device_) return false;
    ps4gnm_capture::GpuTextureId gid;
    gid.mtl_format = (uint32_t)tex.pixelFormat;
    gid.mtl_type = (uint32_t)tex.textureType;
    gid.mtl_width = (uint32_t)tex.width;
    gid.mtl_height = (uint32_t)tex.height;
    gid.mtl_depth = (uint32_t)tex.depth;
    gid.mtl_levels = (uint32_t)tex.mipmapLevelCount;
    gid.mtl_array = (uint32_t)tex.arrayLength;
    const uint64_t n = ps4gnm_capture::RawSize(gid);
    if (!n || tex.sampleCount > 1) return false;
    id<MTLBuffer> buf = [device_ newBufferWithLength:n options:MTLResourceStorageModeShared];
    if (!buf || !RawBlit(device_, queue_, tex, buf, false)) return false;
    out->assign((const uint8_t*)buf.contents, (const uint8_t*)buf.contents + n);
    return true;
}

bool TextureCache::WriteRaw(id<MTLTexture> tex, const std::vector<uint8_t>& raw) {
    if (!tex || !device_ || raw.empty()) return false;
    id<MTLBuffer> buf = [device_ newBufferWithBytes:raw.data() length:raw.size() options:MTLResourceStorageModeShared];
    return buf && RawBlit(device_, queue_, tex, buf, true);
}

void TextureCache::RebuildGpuIndexLocked() const {
    if (gpu_index_version_ == gpu_version_) return;
    gpu_entries_.clear();
    for (const auto& kv : entries_)
        if (kv.second->kind != Entry::Sampled) gpu_entries_.push_back(kv.second);
    std::sort(gpu_entries_.begin(), gpu_entries_.end(), [](const Entry* a, const Entry* b) {
        return a->address != b->address ? a->address < b->address : a->size < b->size;
    });
    gpu_ranges_.resize(gpu_entries_.size());
    gpu_max_end_.resize(gpu_entries_.size());
    uint64_t m = 0;
    uint64_t hint[kHintWords] = {};
    for (size_t i = 0; i < gpu_entries_.size(); ++i) {
        const Entry* e = gpu_entries_[i];
        gpu_ranges_[i] = {e->address, e->address + std::max<uint64_t>(e->size, 1)};
        gpu_max_end_[i] = m = std::max(m, gpu_ranges_[i].second);
        const uint64_t g0 = gpu_ranges_[i].first >> kHintShift, g1 = (gpu_ranges_[i].second - 1) >> kHintShift;
        for (uint64_t g = g0; g <= g1 && g < kHintWords * 64; ++g) hint[g >> 6] |= 1ull << (g & 63);
    }
    // entries are added only under the lock (HintAddLocked), so storing the recomputed words drops
    // only granules of removed entries
    for (uint32_t w = 0; w < kHintWords; ++w) gpu_hint_[w].store(hint[w], std::memory_order_release);
    gpu_index_version_ = gpu_version_;
}

bool TextureCache::OverlapsGpuTexture(uint64_t addr, uint64_t size) const {
    TimedLock lk(mutex_);
    RebuildGpuIndexLocked();
    // ranges starting before `end`; one of them overlaps iff the largest end among them > addr
    const uint64_t end = addr + size;
    const auto it = std::lower_bound(gpu_ranges_.begin(), gpu_ranges_.end(), std::make_pair(end, uint64_t(0)));
    const size_t n = (size_t)(it - gpu_ranges_.begin());
    return n > 0 && gpu_max_end_[n - 1] > addr;
}

void TextureCache::GpuEntriesOverlappingLocked(uint64_t addr, uint64_t size, std::vector<Entry*>* out) const {
    out->clear();
    RebuildGpuIndexLocked();
    const uint64_t end = addr + size;
    const auto it = std::lower_bound(gpu_ranges_.begin(), gpu_ranges_.end(), std::make_pair(end, uint64_t(0)));
    // walk back over the ranges starting before `end` while some of them still reach past addr
    for (size_t j = (size_t)(it - gpu_ranges_.begin()); j-- > 0 && gpu_max_end_[j] > addr;)
        if (gpu_ranges_[j].second > addr) out->push_back(gpu_entries_[j]);
}

void TextureCache::NoteBufferWrite(uint64_t addr, uint64_t size, uint64_t epoch) {
    if (!size || addr + size < addr || !HintNear(addr, size)) return;
    TimedLock lk(mutex_);
    GpuEntriesOverlappingLocked(addr, size, &overlap_scratch_);
    const uint64_t end = addr + size;
    for (Entry* e : overlap_scratch_) {
        if (e->kind != Entry::Color || !e->tex || e->samples != 1 || !e->size) continue;
        if (e->newer_epoch) { // already marked: its whole range is guest newer
            e->newer_epoch = std::max<uint64_t>(epoch, 1);
            continue;
        }
        const uint64_t a = e->address, b = e->address + e->size;
        auto& cover = e->newer_cover;
        cover.push_back({std::max(addr, a), std::min(end, b)});
        // sorted, merged intervals (the reach computation of SupersededLocked)
        std::sort(cover.begin(), cover.end());
        size_t k = 0;
        for (size_t i = 1; i < cover.size(); ++i) {
            if (cover[i].first <= cover[k].second) cover[k].second = std::max(cover[k].second, cover[i].second);
            else cover[++k] = cover[i];
        }
        cover.resize(k + 1);
        if (cover[0].first <= a && cover[0].second >= b) {
            cover.clear();
            e->newer_epoch = std::max<uint64_t>(epoch, 1);
            guest_newer_n_.fetch_add(1, std::memory_order_relaxed);
            guest_newer_marked_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        guest_newer_partial_.fetch_add(1, std::memory_order_relaxed);
        if (!e->newer_partial_logged) {
            e->newer_partial_logged = true;
            uint64_t covered = 0;
            for (const auto& c : cover) covered += c.second - c.first;
            fprintf(stderr, "[gnm-draw] buffer write covers part of GPU target 0x%llx (%llu/%llu bytes); not refreshed\n",
                    (unsigned long long)a, (unsigned long long)covered, (unsigned long long)e->size);
        }
    }
}

bool TextureCache::TakeGuestNewer(uint64_t address, id<MTLTexture> tex, GpuUpload* up) {
    *up = GpuUpload{};
    if (!GuestNewerCount()) return false;
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (!e->newer_epoch) continue;
        if (tex && e->tex != tex && tex.parentTexture != e->tex) continue;
        const PixelFormatInfo pf = GetPixelFormat(e->dfmt, e->nfmt);
        up->tex = e->tex;
        up->surf = e->surf;
        up->layout = e->layout;
        up->width = e->width;
        up->height = e->height;
        up->depth = e->depth;
        up->levels = e->levels;
        up->layers = e->layers;
        up->tex_type = e->tex_type;
        up->bytes_per_block = pf.bytes_per_block;
        up->block_width = pf.block_width ? pf.block_width : 1;
        up->block_height = pf.block_height ? pf.block_height : 1;
        return true;
    }
    return false;
}

void TextureCache::FinishGuestNewer(uint64_t address, id<MTLTexture> tex, bool copied, bool write_site) {
    TimedLock lk(mutex_);
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->tex != tex || !e->newer_epoch) continue;
        if (!copied) {
            guest_newer_failed_.fetch_add(1, std::memory_order_relaxed);
            if (write_site) UnmarkLocked(e); // the GPU write that follows supersedes the guest bytes
            return; // a read-only use: stays marked, the next use tries again
        }
        UnmarkLocked(e);
        e->generation++; // alias copies of the old contents are stale
        // the copy writes every byte of the surface: tex-evict sees an exactly known write
        ExactWriteLocked(e, e->address, e->address + e->size);
        guest_newer_refreshed_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
}

TextureCache::GuestNewerStats TextureCache::GetGuestNewerStats() const {
    GuestNewerStats g;
    g.marked = guest_newer_marked_.load(std::memory_order_relaxed);
    g.refreshed = guest_newer_refreshed_.load(std::memory_order_relaxed);
    g.partial = guest_newer_partial_.load(std::memory_order_relaxed);
    g.failed = guest_newer_failed_.load(std::memory_order_relaxed);
    return g;
}

id<MTLTexture> TextureCache::GetStorageTexture(const ImageDesc& img) {
    if (!img.valid || img.address == 0 || img.pixel.mtl_format == 0 || img.pixel.compressed ||
        !device_)
        return nil;
    const SurfaceDesc sd = img.ToSurfaceDesc();
    if (sd.bpp == 0) return nil;
    TimedLock lk(mutex_);
    const uint32_t width = img.width, height = img.height;
    const bool is_array = img.mtl_texture_type == MTLTextureType2DArray ||
                          img.mtl_texture_type == MTLTextureTypeCube ||
                          img.mtl_texture_type == MTLTextureTypeCubeArray;
    const uint32_t layers = is_array ? std::max(1u, img.num_layers) : 1u;
    auto range = entries_.equal_range(img.address);
    for (auto it = range.first; it != range.second; ++it) {
        Entry* e = it->second;
        if (e->kind != Entry::Color || !e->tex || e->samples != 1) continue;
        const PixelFormatInfo rtpf = GetPixelFormat(e->dfmt, e->nfmt);
        if (rtpf.bytes_per_block != img.pixel.bytes_per_block) continue;
        if (e->width < width || e->height < height || e->layers < layers) continue;
        e->generation++;
        GpuWroteLocked(e); // stored to: earlier buffer writes no longer describe it
        if (e->mtl_format == img.pixel.mtl_format) return TouchLocked(e), e->tex;
        id<MTLTexture> v = [e->tex newTextureViewWithPixelFormat:(MTLPixelFormat)img.pixel.mtl_format];
        if (v) return TouchLocked(e), v;
    }
    Entry* e = new Entry();
    e->kind = Entry::Color;
    e->address = img.address;
    e->mtl_format = img.pixel.mtl_format;
    e->width = width;
    e->height = height;
    e->depth = 1;
    e->levels = 1;
    e->layers = layers;
    e->samples = 1;
    e->surf = sd;
    e->surf.levels = 1;
    e->dfmt = img.dfmt;
    e->nfmt = img.nfmt;
    e->cb_swizzle = CompMapping{};
    if (!ComputeSurfaceLayout(e->surf, &e->layout)) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    e->size = e->layout.total_size;
    e->tex_type = layers > 1 ? MTLTextureType2DArray : MTLTextureType2D;
    MTLTextureDescriptor* td = [MTLTextureDescriptor new];
    td.textureType = (MTLTextureType)e->tex_type;
    td.pixelFormat = (MTLPixelFormat)e->mtl_format;
    td.width = width;
    td.height = height;
    td.arrayLength = layers;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
               MTLTextureUsagePixelFormatView;
    td.storageMode = SharedOrManaged(device_);
    e->tex = NewTextureLocked(td);
    if (!e->tex) {
        DropEntryLocked(e); // nothing created yet: reports nothing
        return nil;
    }
    UploadLocked(e);
    e->dirty = false;
    e->generation = 1;
    stats.rt_created++;
    e->alloc_bytes = (uint64_t)e->tex.allocatedSize;
    TrackLocked(e, +1);
    entries_.emplace(e->address, e);
    return TouchLocked(e), e->tex;
}

static bool ReadTextureBytes(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLTexture> tex,
                             uint32_t slice, std::vector<uint8_t>* out, uint32_t* row_bytes) {
    const RowFmt rf = RowFmtFromMtl((uint32_t)tex.pixelFormat);
    const uint32_t bpe = RowFmtBytes(rf);
    if (bpe == 0 || tex.sampleCount > 1) return false;
    const uint32_t w = (uint32_t)tex.width, h = (uint32_t)tex.height;
    *row_bytes = w * bpe;
    out->resize((size_t)*row_bytes * h);
    if (tex.storageMode == MTLStorageModeShared) {
        [tex getBytes:out->data()
            bytesPerRow:*row_bytes
          bytesPerImage:(NSUInteger)*row_bytes * h
             fromRegion:MTLRegionMake2D(0, 0, w, h)
            mipmapLevel:0
                  slice:slice];
        return true;
    }
    id<MTLBuffer> buf = [dev newBufferWithLength:out->size() options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:tex
                     sourceSlice:slice
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(w, h, 1)
                        toBuffer:buf
               destinationOffset:0
          destinationBytesPerRow:*row_bytes
        destinationBytesPerImage:(NSUInteger)*row_bytes * h];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    memcpy(out->data(), buf.contents, out->size());
    return true;
}

bool TextureCache::ReadTextureBGRA8(id<MTLTexture> tex, DataFormat, NumberFormat, CompMapping,
                                    std::vector<uint8_t>* out) {
    if (!tex || !out) return false;
    TimedLock lk(mutex_);
    DrainQueueLocked();
    std::vector<uint8_t> raw;
    uint32_t row;
    if (!ReadTextureBytes(device_, queue_, tex, 0, &raw, &row)) return false;
    const RowFmt rf = RowFmtFromMtl((uint32_t)tex.pixelFormat);
    const uint32_t w = (uint32_t)tex.width, h = (uint32_t)tex.height;
    out->resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y) ConvertRow(rf, raw.data() + (size_t)y * row, out->data() + (size_t)y * w * 4, w);
    return true;
}

bool TextureCache::WriteBackColorTarget(uint64_t address) {
    TimedLock lk(mutex_);
    Entry* e = nullptr;
    auto range = entries_.equal_range(address);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second->kind == Entry::Color) e = it->second;
    if (!e || !mem_base_ || e->address >= mem_size_) return false;
    DrainQueueLocked();
    const uint32_t bpe = e->surf.bpp / 8;
    std::vector<uint8_t> lin((size_t)e->layout.total_size, 0);
    for (uint32_t s = 0; s < e->layers; ++s) {
        std::vector<uint8_t> raw;
        uint32_t row;
        if (!ReadTextureBytes(device_, queue_, e->tex, s, &raw, &row)) return false;
        if (row != bpe * e->width) return false;
        const MipLayout& ml = e->layout.mips[0];
        for (uint32_t y = 0; y < e->height && y < ml.height; ++y)
            memcpy(lin.data() + ml.offset + ((size_t)s * ml.height + y) * ml.pitch * bpe,
                   raw.data() + (size_t)y * row, std::min<size_t>(row, (size_t)ml.pitch * bpe));
    }
    const uint64_t avail = mem_size_ - e->address;
    return TileSurface(e->surf, e->layout, lin.data(), lin.size(), mem_base_ + e->address,
                       (size_t)std::min<uint64_t>(avail, e->layout.total_size), 0, 1, true);
}

bool TextureCache::ReadDisplayBufferBGRA8(uint64_t address, uint32_t width, uint32_t height,
                                          uint32_t pitch, bool tiled, uint32_t videoout_format,
                                          std::vector<uint8_t>* out) {
    if (!out || width == 0 || height == 0) return false;
    id<MTLTexture> rt = FindColorTarget(address);
    if (rt) {
        std::vector<uint8_t> full;
        if (ReadTextureBGRA8(rt, DataFormat::FormatInvalid, NumberFormat::Unorm, CompMapping{}, &full)) {
            out->assign((size_t)width * height * 4, 0);
            const uint32_t tw = (uint32_t)rt.width;
            for (uint32_t y = 0; y < height && y < rt.height; ++y)
                memcpy(out->data() + (size_t)y * width * 4, full.data() + (size_t)y * tw * 4,
                       (size_t)std::min(width, tw) * 4);
            return true;
        }
    }
    out->resize((size_t)width * height * 4);
    return ReadVideoOutBufferBGRA8(mem_base_, mem_size_, address, width, height, pitch, tiled,
                                   videoout_format, out->data(), width * 4);
}

} // namespace ps4gnm
