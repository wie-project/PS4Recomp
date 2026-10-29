// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4Recomp - GNM resource helpers for the native Metal backend.
 *
 *  - GCN DataFormat/NumberFormat -> MTLPixelFormat / MTLVertexFormat (+ swizzle, block info)
 *  - T# / V# / S# descriptor decoding (Sea Islands ISA tables 8.5 / 8.11 / 8.12)
 *  - CB_* / DB_* / PA_* / VGT_* register decoding into Metal-ready plain structs
 *  - Liverpool (GCN 1.1) surface tiling: size/pitch/padding per mip, CPU detile / retile
 *  - A small Metal texture cache with guest-range invalidation and render-target tracking
 *
 * Large parts are ported from shadPS4 (https://github.com/shadps4-emu/shadPS4):
 *   src/video_core/amdgpu/{pixel_format.h,pixel_format.cpp,resource.h,tiling.h,tiling.cpp,
 *                          regs_color.h,regs_depth.h,regs_primitive.h,regs.h,liverpool.h}
 *   src/video_core/texture_cache/{tile.h,image_info.cpp}
 *   src/video_core/host_shaders/tiling.comp
 *   src/video_core/renderer_vulkan/{liverpool_to_vk.cpp,vk_rasterizer.cpp,vk_graphics_pipeline.cpp}
 * SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Everything outside the `#ifdef __OBJC__` blocks is plain C++17 and has no Metal dependency
 * (Metal enum values are carried as uint32_t so the decoding structs can be used from C++).
 * Guest GPU addresses are guest virtual addresses: host pointer = mem_base + addr.
 */

#ifndef PS4_GNM_RESOURCES_H
#define PS4_GNM_RESOURCES_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

#include "ps4_gnm_membudget.h"
#include "ps4_gnm_capture.h"

namespace ps4gnm {

// =====================================================================================
// 1. Formats
// =====================================================================================

// Table 8.13 Data and Image Formats [Sea Islands Series Instruction Set Architecture]
// (CB_COLORn_INFO.FORMAT uses the same numbering for the color formats.)
enum class DataFormat : uint32_t {
    FormatInvalid = 0,
    Format8 = 1,
    Format16 = 2,
    Format8_8 = 3,
    Format32 = 4,
    Format16_16 = 5,
    Format10_11_11 = 6,
    Format11_11_10 = 7,
    Format10_10_10_2 = 8,
    Format2_10_10_10 = 9,
    Format8_8_8_8 = 10,
    Format32_32 = 11,
    Format16_16_16_16 = 12,
    Format32_32_32 = 13,
    Format32_32_32_32 = 14,
    Format5_6_5 = 16,
    Format1_5_5_5 = 17,
    Format5_5_5_1 = 18,
    Format4_4_4_4 = 19,
    Format8_24 = 20,
    Format24_8 = 21,
    FormatX24_8_32 = 22,
    FormatGB_GR = 32,
    FormatBG_RG = 33,
    Format5_9_9_9 = 34,
    FormatBc1 = 35,
    FormatBc2 = 36,
    FormatBc3 = 37,
    FormatBc4 = 38,
    FormatBc5 = 39,
    FormatBc6 = 40,
    FormatBc7 = 41,
    FormatFmask8_S2_F1 = 44,
    FormatFmask64_S16_F8 = 56,
    Format4_4 = 57,
    Format6_5_5 = 58,
    Format1 = 59,
    Format1_Reversed = 60,
    Format32_As_8 = 61,
    Format32_As_8_8 = 62,
    Format32_As_32_32_32_32 = 63,
};

enum class NumberFormat : uint32_t {
    Unorm = 0,
    Snorm = 1,
    Uscaled = 2,
    Sscaled = 3,
    Uint = 4,
    Sint = 5,
    SnormNz = 6,
    Float = 7,
    Srgb = 9,
    Ubnorm = 10,
    UbnormNz = 11,
    Ubint = 12,
    Ubscaled = 13,
};

enum class CompSwizzle : uint8_t {
    Zero = 0,
    One = 1,
    Red = 4,
    Green = 5,
    Blue = 6,
    Alpha = 7,
};

// Conversions the shader has to do because no host format matches (see shadPS4 pixel_format.h)
enum class NumberConversion : uint32_t {
    None = 0,
    UintToUscaled = 1,
    SintToSscaled = 2,
    UnormToUbnorm = 3,
    Sint8ToSnormNz = 4,
    Sint16ToSnormNz = 5,
    Uint32ToUnorm = 6,
    SrgbToNorm = 7,
};

struct CompMapping {
    CompSwizzle r = CompSwizzle::Red;
    CompSwizzle g = CompSwizzle::Green;
    CompSwizzle b = CompSwizzle::Blue;
    CompSwizzle a = CompSwizzle::Alpha;

    CompSwizzle operator[](uint32_t i) const { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; }
    CompSwizzle& operator[](uint32_t i) { return i == 0 ? r : i == 1 ? g : i == 2 ? b : a; }
    bool operator==(const CompMapping& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const CompMapping& o) const { return !(*this == o); }
    bool IsIdentity() const {
        return r == CompSwizzle::Red && g == CompSwizzle::Green && b == CompSwizzle::Blue &&
               a == CompSwizzle::Alpha;
    }
    // Remaps an RGBA bit mask (bit0=R..bit3=A) through the swizzle (shadPS4 CompMapping::ApplyMask).
    uint32_t ApplyMask(uint32_t mask) const;
    CompMapping Inverse() const;
};

DataFormat RemapDataFormat(DataFormat format);
NumberFormat RemapNumberFormat(NumberFormat format, DataFormat data_format);
CompMapping RemapSwizzle(DataFormat format, CompMapping swizzle);
NumberConversion MapNumberConversion(NumberFormat num_fmt, DataFormat data_fmt);

uint32_t NumComponents(DataFormat format);
uint32_t NumBitsPerBlock(DataFormat format);    // bits per texel, or per 4x4 block for BCn (0 = unknown)
uint32_t NumBitsPerElement(DataFormat format);  // bits per texel (BCn: 4 or 8)
inline bool IsBlockCoded(DataFormat f) { return f >= DataFormat::FormatBc1 && f <= DataFormat::FormatBc7; }
inline bool IsInteger(NumberFormat n) { return n == NumberFormat::Sint || n == NumberFormat::Uint; }
const char* NameOf(DataFormat f);
const char* NameOf(NumberFormat f);

struct PixelFormatInfo {
    uint32_t mtl_format = 0;      // MTLPixelFormat value, 0 (MTLPixelFormatInvalid) if unsupported
    uint32_t bytes_per_block = 0; // bytes per texel, or per block for compressed formats
    uint32_t block_width = 1;     // 4 for BCn
    uint32_t block_height = 1;
    bool compressed = false;
    // The Metal format stores R and B swapped relative to the Vulkan format shadPS4 picks;
    // ComposeViewSwizzle() folds this into the view swizzle.
    bool swap_rb = false;
    bool is_integer = false;
    bool is_srgb = false;
    bool exact = true; // false when the Metal format is only an approximation
};

// Takes *remapped* formats (RemapDataFormat / RemapNumberFormat applied), exactly like
// shadPS4's LiverpoolToVK::SurfaceFormat(). Returns mtl_format 0 if there is no mapping.
PixelFormatInfo GetPixelFormat(DataFormat dfmt, NumberFormat nfmt);
// Convenience: raw T#/CB fields -> remaps applied internally.
PixelFormatInfo GetPixelFormatRaw(uint32_t raw_dfmt, uint32_t raw_nfmt);
// Final texture-view swizzle: shadPS4 swizzle (already RemapSwizzle'd) composed with swap_rb.
CompMapping ComposeViewSwizzle(const PixelFormatInfo& info, CompMapping remapped_swizzle);
// Render targets: Metal attachments cannot swizzle, so for 4-component 8888 surfaces with a
// BGRA component swap we choose a BGRA Metal format (memory layout identical).
// Returns the MTLPixelFormat for a color buffer, and sets *export_swizzle to the swizzle the
// pixel shader export still has to apply (identity when the format absorbs it).
uint32_t GetColorTargetPixelFormat(DataFormat dfmt, NumberFormat nfmt, CompMapping cb_swizzle,
                                   CompMapping* export_swizzle, bool* exact);

enum class ZFormat : uint32_t { Invalid = 0, Z16 = 1, Z32Float = 3 };
enum class StencilFormat : uint32_t { Invalid = 0, Stencil8 = 1 };
// MTLPixelFormat for a depth/stencil buffer (Z16+S8 has no Metal equivalent -> Depth32Float_Stencil8,
// exact=false).
uint32_t GetDepthPixelFormat(ZFormat z, StencilFormat s, bool* exact);

struct VertexFormatInfo {
    uint32_t mtl_vertex_format = 0; // MTLVertexFormat value, 0 = MTLVertexFormatInvalid
    uint32_t size_bytes = 0;
    uint32_t components = 0;
    // true when the fetched value must still be converted in the shader (USCALED/SSCALED read
    // as int, SNORM_NZ, UINT32-as-UNORM, ...). The raw integer format is returned in that case.
    bool shader_conversion = false;
};
// Raw V# / vertex-fetch dfmt/nfmt (the remaps are applied internally).
VertexFormatInfo GetVertexFormat(uint32_t raw_dfmt, uint32_t raw_nfmt);

// =====================================================================================
// 4. Tiling (declared early, used by the descriptor helpers)
// =====================================================================================

enum class PipeConfig : uint32_t {
    P2 = 0,
    P8_32x32_8x16 = 10,
    P8_32x32_16x16 = 12,
    P16_32x32_8x16 = 16,
};

enum class MicroTileMode : uint32_t { Display = 0, Thin = 1, Depth = 2, Rotated = 3, Thick = 4 };

enum class MacroTileMode : uint32_t {
    Mode_1x4_16 = 0, Mode_1x2_16 = 1, Mode_1x1_16 = 2, Mode_1x1_16_Dup = 3,
    Mode_1x1_8 = 4, Mode_1x1_4 = 5, Mode_1x1_2 = 6, Mode_1x1_2_Dup = 7,
    Mode_1x8_16 = 8, Mode_1x4_16_Dup = 9, Mode_1x2_16_Dup = 10, Mode_1x1_16_Dup2 = 11,
    Mode_1x1_8_Dup = 12, Mode_1x1_4_Dup = 13, Mode_1x1_2_Dup2 = 14, Mode_1x1_2_Dup3 = 15,
};

enum class ArrayMode : uint32_t {
    ArrayLinearGeneral = 0, ArrayLinearAligned = 1, Array1DTiledThin1 = 2, Array1DTiledThick = 3,
    Array2DTiledThin1 = 4, ArrayPrtTiledThin1 = 5, ArrayPrt2DTiledThin1 = 6, Array2DTiledThick = 7,
    Array2DTiledXThick = 8, ArrayPrtTiledThick = 9, ArrayPrt2DTiledThick = 10,
    ArrayPrt3DTiledThin1 = 11, Array3DTiledThin1 = 12, Array3DTiledThick = 13,
    Array3DTiledXThick = 14, ArrayPrt3DTiledThick = 15,
};

// The PS4 GB_TILE_MODEn table: T#.TILING_INDEX / CB_COLORn_ATTRIB.TILE_MODE_INDEX /
// DB_Z_INFO.TILE_MODE_INDEX index this directly (base PS4 table; see shadPS4 tiling.h).
enum class TileMode : uint32_t {
    Depth2DThin64 = 0, Depth2DThin128 = 1, Depth2DThin256 = 2, Depth2DThin512 = 3,
    Depth2DThin1K = 4, Depth1DThin = 5, Depth2DThinPrt256 = 6, Depth2DThinPrt1K = 7,
    DisplayLinearAligned = 8, Display1DThin = 9, Display2DThin = 10, DisplayThinPrt = 11,
    Display2DThinPrt = 12, Thin1DThin = 13, Thin2DThin = 14, Thin3DThin = 15, ThinThinPrt = 16,
    Thin2DThinPrt = 17, Thin3DThinPrt = 18, Thick1DThick = 19, Thick2DThick = 20,
    Thick3DThick = 21, ThickThickPrt = 22, Thick2DThickPrt = 23, Thick3DThickPrt = 24,
    Thick2DXThick = 25, Thick3DXThick = 26, DisplayLinearGeneral = 31,
};

bool IsValidTileMode(uint32_t tile_index);
const char* NameOf(TileMode t);
ArrayMode GetArrayMode(TileMode t);
MicroTileMode GetMicroTileMode(TileMode t);
PipeConfig GetPipeConfig(TileMode t);
PipeConfig GetAltPipeConfig(TileMode t);
uint32_t GetSampleSplit(TileMode t);
uint32_t GetTileSplitHw(TileMode t);
bool IsMacroTiled(ArrayMode a);
bool IsPrt(ArrayMode a);
uint32_t GetMicroTileThickness(ArrayMode a);
uint32_t GetPipeCount(PipeConfig p);
uint32_t GetBankWidth(MacroTileMode m);
uint32_t GetBankHeight(MacroTileMode m);
uint32_t GetNumBanks(MacroTileMode m);
uint32_t GetMacrotileAspect(MacroTileMode m);
uint32_t GetAltBankHeight(MacroTileMode m);
uint32_t GetAltNumBanks(MacroTileMode m);
uint32_t GetAltMacrotileAspect(MacroTileMode m);
uint32_t CalculateTileSplit(TileMode t, ArrayMode a, MicroTileMode mt, uint32_t bpp);
MacroTileMode CalculateMacrotileMode(TileMode t, uint32_t bpp, uint32_t num_samples);
// Macro tile width/height in elements (shadPS4 tile.h macro_tile_extents tables, computed).
void GetMacroTileExtents(TileMode t, uint32_t bpp, uint32_t num_samples, bool alt,
                         uint32_t* width, uint32_t* height);

static const uint32_t kMaxMips = 16;

// Input description of a surface. Dimensions are in texels; BCn surfaces are handled in 4x4
// blocks internally (is_block).
struct SurfaceDesc {
    TileMode tile_mode = TileMode::DisplayLinearAligned;
    uint32_t bpp = 32;          // bits per element (per 4x4 block for BCn): 8,16,32,64,96,128
    uint32_t num_samples = 1;
    uint32_t width = 1;         // texels (informational: the layout uses pitch, as shadPS4 does)
    uint32_t height = 1;        // texels
    uint32_t depth = 1;         // texels (3D only, else 1)
    uint32_t pitch = 1;         // texels (T#.PITCH+1 / CB pitch)
    uint32_t levels = 1;
    uint32_t layers = 1;        // array slices (cube: 6 * cubes)
    bool is_block = false;      // BCn: 4x4 blocks
    bool is_pow2 = false;       // T#.POW2PAD
    bool alt_tile = false;      // Neo alt tile mode (sizes only; see report)
    uint32_t bank_swizzle = 0;  // from T# on Neo alt tile mode, 0 on base PS4
};

struct MipLayout {
    uint32_t offset = 0;  // byte offset from the surface base (tiled and linear layouts agree)
    uint32_t size = 0;    // bytes including all slices
    uint32_t pitch = 0;   // padded row pitch in elements (blocks for BCn)
    uint32_t height = 0;  // padded height in elements (blocks for BCn)
    uint32_t slices = 0;  // number of (padded) slices stored = mip_depth*layers rounded to thickness
    bool micro_tiled = false; // a macro tiled surface's small mip that falls back to 1D tiling
};

struct SurfaceLayout {
    ArrayMode array_mode = ArrayMode::ArrayLinearAligned;
    uint32_t num_mips = 0;
    uint32_t total_size = 0;       // guest bytes of the whole surface
    uint32_t micro_tiled_mips = 0; // bitmask (shadPS4 ImageInfo::micro_tiled_mips)
    MipLayout mips[kMaxMips];
    // Linear layout used by DetileSurface/TileSurface: mip m starts at mips[m].offset, slices are
    // pitch*height*bpe bytes, rows pitch*bpe bytes -> same total_size as the tiled surface.
    uint32_t LinearRowBytes(uint32_t mip, uint32_t bpp) const { return mips[mip].pitch * bpp / 8; }
    uint32_t LinearSliceBytes(uint32_t mip, uint32_t bpp) const {
        return mips[mip].pitch * mips[mip].height * bpp / 8;
    }
};

// Port of shadPS4 ComputeImageSize (tile.h). Returns false on unsupported parameters.
bool ComputeSurfaceLayout(const SurfaceDesc& desc, SurfaceLayout* out);
// Frame capture / replay (ps4_gnm_capture.h): a texture-cache entry's SurfaceDesc and SurfaceLayout
// as kPackedSurfaceWords uint32 values (desc fields in declaration order, then array_mode, num_mips,
// total_size, micro_tiled_mips, then offset/size/pitch/height/slices/micro_tiled of each mip).
constexpr uint32_t kPackedSurfaceWords = 17 + 6 * kMaxMips;
void PackSurface(const SurfaceDesc& s, const SurfaceLayout& l, std::vector<uint32_t>* out);
bool UnpackSurface(const std::vector<uint32_t>& v, SurfaceDesc* s, SurfaceLayout* l);
// Single-level layout from an explicit padded pitch/height/slice size (CB/DB registers).
void MakeSingleLevelLayout(const SurfaceDesc& desc, uint32_t padded_pitch, uint32_t padded_height,
                           uint32_t slice_bytes, SurfaceLayout* out);

// Byte offset (relative to the surface base) of element (x,y,slice) of `mip` in the tiled
// surface. x,y in elements (blocks for BCn), within the padded mip. Sample 0 only.
// Direct port of tiling.comp (ComputeSurfaceAddrFromCoord{Micro,Macro}Tiled).
uint64_t TiledElementOffset(const SurfaceDesc& desc, const SurfaceLayout& layout, uint32_t mip,
                            uint32_t x, uint32_t y, uint32_t slice);

// CPU detile (tiled guest bytes -> linear layout described in SurfaceLayout) and retile.
// `tiled`/`linear` both point at the surface base (mip 0 offset 0); sizes are bounds-checked
// (elements whose tiled address falls outside tiled_size are skipped / zero filled).
// visible_only: only process the visible width x height region (in texels) of each mip
// instead of the full padded mip (faster; padding of the linear buffer is left untouched).
bool DetileSurface(const SurfaceDesc& desc, const SurfaceLayout& layout, const uint8_t* tiled,
                   size_t tiled_size, uint8_t* linear, size_t linear_size,
                   uint32_t first_mip = 0, uint32_t num_mips = ~0u, bool visible_only = false);
bool TileSurface(const SurfaceDesc& desc, const SurfaceLayout& layout, const uint8_t* linear,
                 size_t linear_size, uint8_t* tiled, size_t tiled_size, uint32_t first_mip = 0,
                 uint32_t num_mips = ~0u, bool visible_only = false);
// For the GPU detiler: map[i] = tiled byte offset of the element at linear byte offset i * bpe
// (layout of DetileSurface; ~0u for elements that are not visible or lie past tiled_size).
bool BuildDetileMap(const SurfaceDesc& desc, const SurfaceLayout& layout, size_t tiled_size,
                    uint32_t first_mip, uint32_t num_mips, std::vector<uint32_t>* map);

// =====================================================================================
// 2. Descriptors
// =====================================================================================

enum class ImageType : uint32_t {
    Invalid = 0, Color1D = 8, Color2D = 9, Color3D = 10, Cube = 11, Color1DArray = 12,
    Color2DArray = 13, Color2DMsaa = 14, Color2DMsaaArray = 15,
};
const char* NameOf(ImageType t);

// T# (image resource, 8 dwords; a 4 dword T# is accepted with the upper half zeroed)
struct ImageDesc {
    uint32_t raw[8] = {};
    bool valid = false;           // TYPE has bit 3 set
    uint64_t address = 0;         // BASE_ADDRESS << 8
    uint32_t raw_dfmt = 0, raw_nfmt = 0;
    DataFormat dfmt = DataFormat::FormatInvalid;     // remapped
    NumberFormat nfmt = NumberFormat::Unorm;          // remapped
    NumberConversion conversion = NumberConversion::None;
    uint32_t width = 0, height = 0, depth = 0;        // texels (+1 applied; depth=1 if not 3D)
    uint32_t pitch = 0;                               // texels (PITCH+1)
    uint32_t base_level = 0, last_level = 0;
    uint32_t base_array = 0, last_array = 0;
    // num_layers: the slices the image has and its Metal texture holds (depth + 1, 6 per cube,
    // 1 for 3D); layout_layers: the slices of its guest layout, num_layers padded to a power of
    // two with POW2PAD (shadPS4 Image::NumLayers), which only moves the levels after the first
    uint32_t num_levels = 1, num_layers = 1, num_samples = 1, layout_layers = 1;
    float min_lod = 0.f;
    ImageType type = ImageType::Invalid;
    TileMode tile_mode = TileMode::DisplayLinearAligned;
    bool tiled = false;
    bool pow2pad = false;
    bool alt_tile_mode = false;   // Neo only
    uint8_t bank_swizzle = 0;
    CompMapping dst_sel_raw;      // as written in the T#
    CompMapping swizzle;          // RemapSwizzle()d (relative to the shadPS4 Vulkan format)
    PixelFormatInfo pixel;        // Metal pixel format
    CompMapping view_swizzle;     // swizzle to put on the Metal texture view
    uint32_t mtl_texture_type = 0;// MTLTextureType value

    // neo_mode: the runtime reports sceKernelIsNeoMode()=1; like shadPS4, alt_tile_mode then
    // selects the alt macro-tile extents for the *layout* (addressing stays non-alt, see report).
    SurfaceDesc ToSurfaceDesc(bool neo_mode) const;
    SurfaceDesc ToSurfaceDesc() const; // uses the runtime's sceKernelIsNeoMode()
    uint32_t NumViewLevels() const;
    uint32_t NumViewLayers(bool is_array) const;
};
bool DecodeImage(const uint32_t* dwords, uint32_t num_dwords, ImageDesc* out);
// T#s DecodeImage found valid but no Metal texture can be made of (decoded as invalid: a null
// binding). The unused entries of T# tables decode to such T#s.
uint64_t UncreatableImageCount();

// V# (buffer resource, 4 dwords)
struct BufferDesc {
    uint32_t raw[4] = {};
    bool valid = false;           // TYPE == 0
    uint64_t address = 0;         // 40-bit byte address
    uint32_t stride = 0;
    uint32_t num_records = 0;
    uint32_t size = 0;            // stride ? stride*num_records : num_records
    bool cache_swizzle = false, swizzle_enable = false, add_tid_enable = false;
    uint32_t index_stride = 8;    // 8/16/32/64
    uint32_t element_size = 2;    // 2/4/8/16
    uint32_t raw_dfmt = 0, raw_nfmt = 0;   // 4-bit dfmt / 3-bit nfmt of the buffer
    DataFormat dfmt = DataFormat::FormatInvalid;
    NumberFormat nfmt = NumberFormat::Unorm;
    CompMapping dst_sel_raw;
    CompMapping swizzle;          // RemapSwizzle()d
    VertexFormatInfo vertex;      // MTLVertexFormat for vertex-fetch use
};
bool DecodeBuffer(const uint32_t dwords[4], BufferDesc* out);

enum class ClampMode : uint32_t {
    Wrap = 0, Mirror = 1, ClampLastTexel = 2, MirrorOnceLastTexel = 3, ClampHalfBorder = 4,
    MirrorOnceHalfBorder = 5, ClampBorder = 6, MirrorOnceBorder = 7,
};
enum class FilterMode : uint32_t { Blend = 0, Min = 1, Max = 2 };
enum class TexFilter : uint32_t { Point = 0, Bilinear = 1, AnisoPoint = 2, AnisoLinear = 3 };
enum class MipFilter : uint32_t { None = 0, Point = 1, Linear = 2 };
enum class BorderColor : uint32_t { TransparentBlack = 0, OpaqueBlack = 1, White = 2, Custom = 3 };

// S# (sampler, 4 dwords) -> MTLSamplerDescriptor fields
struct SamplerDesc {
    uint32_t raw[4] = {};
    // raw fields
    ClampMode clamp_x = ClampMode::Wrap, clamp_y = ClampMode::Wrap, clamp_z = ClampMode::Wrap;
    uint32_t max_aniso_ratio = 0;   // 0..4 -> 1,2,4,8,16
    uint32_t depth_compare = 0;     // GCN DepthCompare (same numbering as CompareFunc)
    bool force_unnormalized = false;
    bool force_degamma = false;
    bool trunc_coord = false;
    bool disable_cube_wrap = false;
    FilterMode filter_mode = FilterMode::Blend;
    TexFilter mag_filter = TexFilter::Point, min_filter = TexFilter::Point;
    MipFilter mip_filter = MipFilter::None;
    uint32_t z_filter = 0;
    BorderColor border_color_type = BorderColor::TransparentBlack;
    uint32_t border_color_ptr = 0;  // index into TA_BC_BASE_ADDR table (float4 entries)
    // derived (Metal enum values)
    float min_lod = 0.f, max_lod = 0.f, lod_bias = 0.f; // Metal samplers have no LOD bias: apply in shader
    float max_anisotropy = 1.f;
    uint32_t mtl_min_filter = 0, mtl_mag_filter = 0;   // MTLSamplerMinMagFilter
    uint32_t mtl_mip_filter = 0;                       // MTLSamplerMipFilter
    uint32_t mtl_address_s = 0, mtl_address_t = 0, mtl_address_r = 0; // MTLSamplerAddressMode
    uint32_t mtl_border_color = 0;                     // MTLSamplerBorderColor
    uint32_t mtl_compare = 0;                          // MTLCompareFunction (use only for *_C ops)
    bool normalized_coords = true;
    bool exact = true;  // false if something was approximated (custom border, reduction, mirror-once-border)
};
bool DecodeSampler(const uint32_t dwords[4], SamplerDesc* out);

#ifdef __OBJC__
MTLTextureSwizzleChannels ToMTLSwizzle(CompMapping m);
// compare: set compareFunction (only for samplers used with depth-compare instructions)
MTLSamplerDescriptor* MakeSamplerDescriptor(const SamplerDesc& s, bool compare);
#endif

// =====================================================================================
// 3. Register file decoding (dword register offsets, regs[] indexed by the full offset)
// =====================================================================================

namespace reg {
enum : uint32_t {
    DB_RENDER_CONTROL = 0xA000,
    DB_DEPTH_VIEW = 0xA002,
    DB_RENDER_OVERRIDE = 0xA003,
    DB_HTILE_DATA_BASE = 0xA005,
    DB_DEPTH_BOUNDS_MIN = 0xA008,
    DB_DEPTH_BOUNDS_MAX = 0xA009,
    DB_STENCIL_CLEAR = 0xA00A,
    DB_DEPTH_CLEAR = 0xA00B,
    PA_SC_SCREEN_SCISSOR_TL = 0xA00C,
    PA_SC_SCREEN_SCISSOR_BR = 0xA00D,
    DB_DEPTH_INFO = 0xA00F,
    DB_Z_INFO = 0xA010,
    DB_STENCIL_INFO = 0xA011,
    DB_Z_READ_BASE = 0xA012,
    DB_STENCIL_READ_BASE = 0xA013,
    DB_Z_WRITE_BASE = 0xA014,
    DB_STENCIL_WRITE_BASE = 0xA015,
    DB_DEPTH_SIZE = 0xA016,
    DB_DEPTH_SLICE = 0xA017,
    TA_BC_BASE_ADDR = 0xA020,
    PA_SC_WINDOW_OFFSET = 0xA080,
    PA_SC_WINDOW_SCISSOR_TL = 0xA081,
    PA_SC_WINDOW_SCISSOR_BR = 0xA082,
    CB_TARGET_MASK = 0xA08E,
    CB_SHADER_MASK = 0xA08F,
    PA_SC_GENERIC_SCISSOR_TL = 0xA090,
    PA_SC_GENERIC_SCISSOR_BR = 0xA091,
    PA_SC_VPORT_SCISSOR_0_TL = 0xA094, // + 2*i, BR = TL + 1
    PA_SC_VPORT_ZMIN_0 = 0xA0B4,       // + 2*i, ZMAX = ZMIN + 1
    VGT_MULTI_PRIM_IB_RESET_INDX = 0xA103,
    CB_BLEND_RED = 0xA105,             // ..ALPHA = 0xA108
    DB_STENCIL_CONTROL = 0xA10B,
    DB_STENCILREFMASK = 0xA10C,
    DB_STENCILREFMASK_BF = 0xA10D,
    PA_CL_VPORT_XSCALE = 0xA10F,       // + 6*i: XSCALE XOFFSET YSCALE YOFFSET ZSCALE ZOFFSET
    SPI_SHADER_POS_FORMAT = 0xA1C3,
    SPI_SHADER_Z_FORMAT = 0xA1C4,
    SPI_SHADER_COL_FORMAT = 0xA1C5,
    CB_BLEND0_CONTROL = 0xA1E0,        // + i
    DB_DEPTH_CONTROL = 0xA200,
    DB_EQAA = 0xA201,
    CB_COLOR_CONTROL = 0xA202,
    DB_SHADER_CONTROL = 0xA203,
    PA_CL_CLIP_CNTL = 0xA204,
    PA_SU_SC_MODE_CNTL = 0xA205,
    PA_CL_VTE_CNTL = 0xA206,
    PA_CL_VS_OUT_CNTL = 0xA207,
    PA_SU_LINE_CNTL = 0xA282,
    PA_SC_MODE_CNTL_0 = 0xA292,
    VGT_MULTI_PRIM_IB_RESET_EN = 0xA2A5,
    PA_SU_POLY_OFFSET_DB_FMT_CNTL = 0xA2DE,
    PA_SU_POLY_OFFSET_CLAMP = 0xA2DF,
    PA_SU_POLY_OFFSET_FRONT_SCALE = 0xA2E0,
    PA_SU_POLY_OFFSET_FRONT_OFFSET = 0xA2E1,
    PA_SU_POLY_OFFSET_BACK_SCALE = 0xA2E2,
    PA_SU_POLY_OFFSET_BACK_OFFSET = 0xA2E3,
    PA_SC_AA_CONFIG = 0xA2F8,
    CB_COLOR0_BASE = 0xA318,           // + 15*n
    CB_COLOR_STRIDE = 15,
    CB_COLOR_PITCH = 1, CB_COLOR_SLICE = 2, CB_COLOR_VIEW = 3, CB_COLOR_INFO = 4,
    CB_COLOR_ATTRIB = 5, CB_COLOR_DCC_CONTROL = 6, CB_COLOR_CMASK = 7, CB_COLOR_CMASK_SLICE = 8,
    CB_COLOR_FMASK = 9, CB_COLOR_FMASK_SLICE = 10, CB_COLOR_CLEAR_WORD0 = 11,
    CB_COLOR_CLEAR_WORD1 = 12,
    VGT_PRIMITIVE_TYPE = 0xC242,       // uconfig on CI/Liverpool
};
} // namespace reg

static const uint32_t kNumColorBuffers = 8;
static const uint32_t kNumViewports = 16;

struct ColorTarget {
    bool bound = false;          // BASE != 0 && FORMAT != 0
    bool enabled = false;        // bound && CB_TARGET_MASK nibble != 0 && mode != Disable
    uint32_t index = 0;
    uint32_t regs[13] = {};      // raw CB_COLORn_BASE..CLEAR_WORD1
    uint64_t address = 0;
    uint32_t pitch = 0;          // texels (padded)
    uint32_t height = 0;         // texels (padded) = slice*64/pitch
    uint32_t slice_bytes = 0;    // bytes per slice (all samples)
    uint32_t base_slice = 0, num_slices = 1;
    uint32_t raw_format = 0, raw_number_type = 0;
    DataFormat dfmt = DataFormat::FormatInvalid;  // remapped
    NumberFormat nfmt = NumberFormat::Unorm;      // remapped (CB SNORM_NZ slot means SRGB)
    NumberConversion conversion = NumberConversion::None;
    uint32_t comp_swap = 0;      // 0 std, 1 alt, 2 std-rev, 3 alt-rev
    CompMapping swizzle;         // shadPS4 ColorBuffer::Swizzle()
    uint32_t bpp = 0;
    TileMode tile_mode = TileMode::DisplayLinearAligned;
    uint32_t fmask_tile_mode = 0;
    bool tiled = false;
    bool linear_general = false;
    uint32_t num_samples = 1, num_fragments = 1;
    bool fast_clear = false, compression = false, dcc = false;
    bool blend_clamp = false, blend_bypass = false, round_truncate = false, force_dst_alpha_1 = false;
    bool alt_tile_mode = false;
    uint32_t endian = 0;
    uint64_t cmask_address = 0, fmask_address = 0;
    uint32_t clear_word0 = 0, clear_word1 = 0;
    uint32_t target_mask = 0;    // CB_TARGET_MASK nibble (RGBA bits of the *memory* components)
    uint32_t shader_mask = 0;    // CB_SHADER_MASK nibble
    uint32_t export_format = 0;  // SPI_SHADER_COL_FORMAT nibble
    // Metal
    uint32_t mtl_format = 0;     // MTLPixelFormat for the attachment
    CompMapping export_swizzle;  // swizzle the shader export still must apply
    uint32_t mtl_write_mask = 0; // MTLColorWriteMask (target_mask through swizzle)
    bool format_exact = true;
    // Actual surface extent from the NOP hint that follows the CB register setup (shadPS4
    // last_cb_extent); 0 = unknown -> pitch x height.
    uint32_t extent_width = 0, extent_height = 0;
    SurfaceDesc ToSurfaceDesc() const;
    void GetLayout(SurfaceLayout* out) const;
};

struct DepthTarget {
    bool bound = false;          // Z or stencil read base + valid format
    uint32_t regs[8] = {};       // DB_Z_INFO..DB_DEPTH_SLICE raw
    ZFormat z_format = ZFormat::Invalid;
    StencilFormat stencil_format = StencilFormat::Invalid;
    uint32_t num_samples = 1;
    TileMode tile_mode = TileMode::Depth2DThin64;
    bool tiled = false;
    uint32_t tile_split = 0, stencil_tile_split = 0;
    bool htile_enable = false;   // DB_Z_INFO.TILE_SURFACE_ENABLE
    bool htile_linear = false;   // DB_HTILE_SURFACE.LINEAR
    bool allow_expclear = false;
    uint64_t z_read_address = 0, stencil_read_address = 0;
    uint64_t z_write_address = 0, stencil_write_address = 0;
    uint64_t htile_address = 0;
    uint32_t pitch = 0, height = 0;   // texels (padded)
    uint32_t slice_bytes = 0;         // depth bytes per slice
    uint32_t base_slice = 0, num_slices = 1;
    bool z_read_only = false, stencil_read_only = false;
    bool depth_clear_enable = false, stencil_clear_enable = false;
    float depth_clear = 0.f;
    uint32_t stencil_clear = 0;
    uint32_t bpp = 0;                 // 16 / 32
    uint32_t mtl_format = 0;          // MTLPixelFormat
    bool format_exact = true;
    uint32_t extent_width = 0, extent_height = 0; // NOP hint (shadPS4 last_db_extent), 0 = unknown
    bool DepthValid() const { return z_read_address != 0 && z_format != ZFormat::Invalid; }
    bool StencilValid() const {
        return stencil_read_address != 0 && stencil_format != StencilFormat::Invalid;
    }
    SurfaceDesc ToSurfaceDesc() const;
    void GetLayout(SurfaceLayout* out) const;
};

struct BlendState {
    bool enable = false;         // CB_BLENDn_CONTROL.ENABLE && !CB.BLEND_BYPASS
    uint32_t raw = 0;
    uint32_t src_rgb = 1, dst_rgb = 0, src_alpha = 1, dst_alpha = 0; // MTLBlendFactor
    uint32_t op_rgb = 0, op_alpha = 0;                                // MTLBlendOperation
    uint32_t write_mask = 0xF;   // MTLColorWriteMask
    bool dual_source = false;
    bool unsupported = false;    // factor not representable / ROP3 not COPY etc.
};

struct StencilFace {
    uint32_t compare = 7;        // MTLCompareFunction
    uint32_t fail = 0, depth_fail = 0, pass = 0; // MTLStencilOperation
    uint8_t read_mask = 0xFF, write_mask = 0xFF, reference = 0;
};

struct DepthStencilState {
    bool depth_test = false;     // DEPTH_ENABLE && depth buffer valid
    bool depth_write = false;
    uint32_t depth_compare = 7;  // MTLCompareFunction (Always)
    bool stencil_test = false;
    StencilFace front, back;
    bool depth_bounds = false;   // not supported by Metal -> must be emulated/ignored
    float depth_bounds_min = 0.f, depth_bounds_max = 1.f;
    bool unsupported_stencil_op = false;
};

struct Viewport {
    // MTLViewport values. The shader's clip-space position is used unmodified (GCN convention)
    // unless flip_y is set, in which case the vertex shader must negate position.y.
    double origin_x = 0, origin_y = 0, width = 0, height = 0, znear = 0, zfar = 1;
    bool flip_y = false;
};

struct ScissorRect { uint32_t x = 0, y = 0, width = 0, height = 0; }; // MTLScissorRect

enum class PrimitiveType : uint32_t {
    None = 0, PointList = 1, LineList = 2, LineStrip = 3, TriangleList = 4, TriangleFan = 5,
    TriangleStrip = 6, PatchPrimitive = 9, AdjLineList = 10, AdjLineStrip = 11,
    AdjTriangleList = 12, AdjTriangleStrip = 13, RectList = 17, LineLoop = 18, QuadList = 19,
    QuadStrip = 20, Polygon = 21,
};
const char* NameOf(PrimitiveType p);

struct RasterState {
    PrimitiveType prim = PrimitiveType::None;
    uint32_t mtl_primitive = 3;  // MTLPrimitiveType (Triangle)
    bool needs_emulation = false;// fan/polygon/quad/rect/line-loop/adjacency/patch: convert indices
    uint32_t cull_mode = 0;      // MTLCullMode
    bool cull_front_and_back = false; // Metal has no FrontAndBack: skip drawing triangles
    uint32_t winding = 0;        // MTLWinding
    uint32_t fill_mode = 0;      // MTLTriangleFillMode
    bool depth_bias = false;
    float bias_constant = 0.f, bias_slope = 0.f, bias_clamp = 0.f;
    bool rasterizer_discard = false;  // PA_CL_CLIP_CNTL.DX_RASTERIZATION_KILL
    bool clip_disabled = false;       // CLIP_DISABLE or RectList: positions are in screen space
    bool clip_space_minus_w = false;  // z in [-w,w] (GL style): shader must remap z to [0,w]
    bool depth_clip = true;           // false -> MTLDepthClipModeClamp
    bool provoking_last = false;
    bool primitive_restart = false;
    uint32_t restart_index = 0xFFFFFFFF;
    float line_width = 1.f;
    bool window_offset_on_vertices = false; // PA_SU_SC_MODE_CNTL.VTX_WINDOW_OFFSET_ENABLE (unsupported)
};

struct RenderState {
    ColorTarget color[kNumColorBuffers];
    uint32_t color_mode = 1;     // CB_COLOR_CONTROL.MODE (0 disable, 1 normal, 2 elim fast clear,
                                 // 3 resolve, 5 fmask decompress)
    uint32_t rop3 = 0xCC;
    BlendState blend[kNumColorBuffers];
    float blend_constants[4] = {0, 0, 0, 0};
    DepthTarget depth;
    DepthStencilState ds;
    Viewport viewports[kNumViewports];
    ScissorRect scissors[kNumViewports];
    uint32_t num_viewports = 0;
    ScissorRect scissor;         // combined screen/window/generic scissor
    // true when the combined (or a viewport) scissor is empty although viewports are set; seen in
    // the captured register dumps where the scissor registers were never written - the caller
    // may want to fall back to the render target extent.
    bool scissor_empty = false;
    RasterState raster;
    uint32_t msaa_samples = 1;   // PA_SC_AA_CONFIG
    uint32_t render_width = 0, render_height = 0; // min extent over bound RTs/DB (0 if none)
};

// regs must hold at least 0xD000 dwords (the runtime uses uint32_t regs[0x10000]).
void DecodeColorTarget(const uint32_t* regs, uint32_t index, ColorTarget* out);
void DecodeDepthTarget(const uint32_t* regs, DepthTarget* out);
// HTILE bytes of one slice of a depth surface of pitch x height texels on a GPU with `pipes` pipes
// (AMD addrlib ComputeHtileInfo for SI/CI: 32 bits per 8x8 tile, the surface aligned to the HTILE
// macro tile, ComputeTileDataWidthAndHeight with 16 KiB cache bits; linear HTILE
// (DB_HTILE_SURFACE.LINEAR): 128 x 8*pipes texels). 0 for an unsupported pipe count.
uint64_t HtileSliceBytes(uint32_t pitch, uint32_t height, uint32_t pipes, bool linear);
// The same for a decoded depth target: its pipe config from its tile mode on the base PS4. 0 when
// the layout is not known exactly: Neo mode (the depth target's alternate tiling, with 16 pipes,
// is not decoded) and pipe configs other than 2 / 8 pipes.
uint64_t DepthHtileSliceBytes(const DepthTarget& d, bool neo_mode);
void DecodeRenderState(const uint32_t* regs, RenderState* out);
void PrintRenderState(const RenderState& rs, FILE* f, bool verbose = false);

#ifdef __OBJC__
void ApplyBlend(const BlendState& b, uint32_t pixel_format, MTLRenderPipelineColorAttachmentDescriptor* d);
MTLDepthStencilDescriptor* MakeDepthStencilDescriptor(const DepthStencilState& ds);
MTLViewport ToMTLViewport(const Viewport& v);
MTLScissorRect ToMTLScissor(const ScissorRect& s, uint32_t rt_width, uint32_t rt_height);
#endif

// =====================================================================================
// 5. Texture cache / readback
// =====================================================================================

// Pixel conversion of one linear row to BGRA8 (B,G,R,A bytes). Supports the common color
// formats (8888, 2_10_10_10, 16_16_16_16 float/unorm, 10_11_11 float, 32_32_32_32 float,
// 5_6_5, 1_5_5_5, 8, 8_8, 16, 32 float). `swap_rb` selects A8R8G8B8 (BGRA memory) for 8888.
bool ConvertRowToBGRA8(DataFormat dfmt, NumberFormat nfmt, CompMapping swizzle, const uint8_t* src,
                       uint8_t* dst, uint32_t width);

// VideoOut pixel formats (sceVideoOutSetBufferAttribute)
enum : uint32_t {
    kVideoOutA8R8G8B8Srgb = 0x80000000u,
    kVideoOutA8B8G8R8Srgb = 0x80002200u,
    kVideoOutA2R10G10B10 = 0x88060000u,
    kVideoOutA2R10G10B10Srgb = 0x88000000u,
    kVideoOutA2R10G10B10Bt2020Pq = 0x88740000u,
    kVideoOutA16R16G16B16Float = 0xC1060000u,
};

// CPU path: read a VideoOut buffer straight from guest memory (tiled = Display2DThin with the
// pitch rounded to 128 like shadPS4, or linear) into `out` (width*height*4 bytes BGRA8).
bool ReadVideoOutBufferBGRA8(const uint8_t* mem_base, uint64_t mem_size, uint64_t address,
                             uint32_t width, uint32_t height, uint32_t pitch, bool tiled,
                             uint32_t videoout_format, uint8_t* out, uint32_t out_row_bytes);

#ifdef __OBJC__
class TextureCache {
public:
    TextureCache(id<MTLDevice> device, id<MTLCommandQueue> queue, uint8_t* mem_base, uint64_t mem_size);
    ~TextureCache();

    // Sampled texture for a T#. Uploads (detiles) on first use or after invalidation. If the
    // address belongs to a GPU-written render target with the same layout, the RT texture is
    // returned (viewed with the T# format/swizzle when bit compatible). Returns nil on failure.
    id<MTLTexture> GetTexture(const ImageDesc& img);

    // GPU upload of a sampled texture whose guest memory is written by GPU work that has not
    // completed yet (the CPU cannot read it). GetTextureForGpuUpload finds/creates the texture
    // like GetTexture but never reads guest memory: if its contents predate the guest writes
    // with epoch <= write_epoch, it fills `up` (texture + layout) and marks the texture as
    // uploaded at `upload_epoch`; the caller then encodes the copy from guest memory behind the
    // writing GPU work. up->tex is nil if no upload is needed.
    struct GpuUpload {
        id<MTLTexture> tex = nil;
        SurfaceDesc surf;
        SurfaceLayout layout;
        uint32_t width = 0, height = 0, depth = 1, levels = 1, layers = 1, tex_type = 0;
        uint32_t bytes_per_block = 0, block_width = 1, block_height = 1;
    };
    id<MTLTexture> GetTextureForGpuUpload(const ImageDesc& img, uint64_t write_epoch, uint64_t upload_epoch,
                                          GpuUpload* up);
    // Like GetTexture, but a texture that needs an upload from guest memory (new or stale)
    // whose surface is at least min_bytes is not uploaded by the CPU: `up` is filled, the
    // texture is marked current at upload_epoch and the caller encodes the copy on the GPU.
    // Smaller textures are uploaded by the CPU as in GetTexture. up->tex is nil when nothing
    // is left to do.
    id<MTLTexture> GetTextureForGpuRefresh(const ImageDesc& img, uint64_t upload_epoch, uint64_t min_bytes,
                                           GpuUpload* up);
    // A texture handed out for a GPU upload whose copy could not be encoded: stale again, so
    // the next GetTexture uploads it on the CPU.
    void MarkStale(uint64_t address, id<MTLTexture> tex);
    // Debug (PS4_GNM_VERIFY_GPU_UPLOAD): CPU upload of the T#'s guest memory into `dst`, which
    // must have the cached texture's descriptor. The cache entries are not changed.
    bool DebugCpuUploadInto(const ImageDesc& img, id<MTLTexture> dst);

    // Render target / depth textures (created on first use, contents loaded from guest memory
    // once when load_from_guest is true; afterwards owned by the GPU and never re-uploaded).
    id<MTLTexture> GetColorTarget(const ColorTarget& cb, bool load_from_guest = true);
    // One texture per depth surface: views of later slices grow it to a larger 2D array, and the
    // texture it replaced is returned in *grown_from so the caller can copy its slices over.
    // The replaced texture is also queued for TakeRetired.
    id<MTLTexture> GetDepthTarget(const DepthTarget& db, id<MTLTexture>* grown_from = nullptr);
    // The GPU-owned surface `tex` at `address` gets every byte of guest [lo, hi) written by the open
    // command buffer (a clear of that range). Only such exactly known writes let a surface supersede
    // older ones (EvictSuperseded); a draw's pixels, or a storage image's stores, are not known.
    // The range is clipped to the entry. Returns false if no such entry exists.
    // seq: the command buffer the write belongs to (0 = the open one, the SetUse value).
    bool NoteExactWrite(uint64_t address, id<MTLTexture> tex, uint64_t lo, uint64_t hi, uint64_t seq = 0);

    // CPU wrote [addr, addr+size): sampled textures overlapping are re-uploaded on next use.
    // Render-target textures are not touched (GPU owns them) unless include_render_targets.
    // write_epoch: the GPU write being retired (see GetTextureForGpuUpload): textures already
    // uploaded by the GPU after that write stay valid. ~0 (CPU writes) invalidates everything.
    void InvalidateRange(uint64_t addr, uint64_t size, bool include_render_targets = false,
                         uint64_t write_epoch = ~0ull);
    // InvalidateRange(addr, size, false, epoch) for every range of a retired command buffer,
    // under one lock and one pass over the textures (the ranges are sorted in place).
    struct WriteRange {
        uint64_t addr, size, epoch;
    };
    void InvalidateWrites(std::vector<WriteRange>& writes);

    // ---- Buffer writes over GPU-owned color surfaces ----
    // A GPU-owned surface's contents exist only in its texture: guest memory under it is stale.
    // When GPU work stores to its memory as a buffer (a written V# with an exact range, or a CP DMA),
    // on the PS4 those bytes are the surface's new contents.
    // GPU work at write epoch `epoch` writes exactly guest [addr, addr + size). A color surface whose
    // whole range [address, address + size) such writes cover since its last GPU write is marked
    // "guest newer" (its texture is refreshed from guest memory before its next GPU use). Partial
    // cover accumulates and never marks: the uncovered bytes may exist only in the texture (logged
    // once per surface). Walks only the GPU-owned entries overlapping the range; a range no GPU-owned
    // surface comes near (64 MiB granules, checked without the lock) returns at once.
    // Call it only for work that has really been encoded (or, for CPU-side DMA, done).
    void NoteBufferWrite(uint64_t addr, uint64_t size, uint64_t epoch);
    // The marked color surface at `address` whose texture is `tex` (or a view of it; nil: any marked
    // color surface at the address): fills `up` to copy guest memory into its texture (its own
    // layout). The mark stays until FinishGuestNewer. False when nothing is marked there (up->tex nil).
    bool TakeGuestNewer(uint64_t address, id<MTLTexture> tex, GpuUpload* up);
    // The copy of TakeGuestNewer's `up` was encoded (copied: unmark the surface, make its alias copies
    // stale, credit the copy as an exactly known write of its whole range) or could not be (counted
    // as a failed refresh). A failed refresh before a read-only use leaves the surface marked, so
    // the next use tries again; before a GPU write (write_site: render-target or storage bind, fast
    // clear, resolve or copy destination) it drops the mark: the write makes the texture the newer
    // copy, and a later refresh would overwrite it with the older guest bytes.
    void FinishGuestNewer(uint64_t address, id<MTLTexture> tex, bool copied, bool write_site);
    // Marked surfaces right now (cheap: checked before any lookup).
    size_t GuestNewerCount() const { return guest_newer_n_.load(std::memory_order_relaxed); }
    // Totals since start: surfaces marked, refreshes handed out, surfaces partly covered (logged).
    struct GuestNewerStats {
        uint64_t marked = 0, refreshed = 0, partial = 0, failed = 0;
    };
    GuestNewerStats GetGuestNewerStats() const;

    // Drop everything (e.g. on device reset).
    void Clear();
    // Tests: the cache's lock, to hold it from outside (a thread that must not need it goes on).
    std::mutex& TestMutex() { return mutex_; }

    // Copy a GPU-owned render target back into guest memory (retiled), e.g. before the CPU or a
    // non-cached consumer reads it. Waits for the queue to drain.
    bool WriteBackColorTarget(uint64_t address);

    // Presentation / screenshot: BGRA8 copy of the display buffer at `address`. Uses the GPU
    // render target if one is cached at that address, otherwise detiles guest memory.
    bool ReadDisplayBufferBGRA8(uint64_t address, uint32_t width, uint32_t height, uint32_t pitch,
                                bool tiled, uint32_t videoout_format, std::vector<uint8_t>* out);
    // Read back any cached color texture (waits for the queue) as BGRA8.
    bool ReadTextureBGRA8(id<MTLTexture> tex, DataFormat dfmt, NumberFormat nfmt, CompMapping swz,
                          std::vector<uint8_t>* out);

    // Lookup only (no creation)
    id<MTLTexture> FindColorTarget(uint64_t address) const;
    // Color target at an address with the given size (several surfaces of different sizes can
    // share an address over time); nil if none matches.
    id<MTLTexture> FindColorTarget(uint64_t address, uint32_t width, uint32_t height) const;
    id<MTLTexture> FindDepthTarget(uint64_t address) const;
    // Another depth surface of the same memory, format and size but a different sample count
    // (PS4 Pro checkerboard rendering uses one buffer as 2x and as 1x depth); nil if none.
    id<MTLTexture> FindDepthSibling(const DepthTarget& db, id<MTLTexture> self) const;

    // Storage (UAV) image for a T#: a GPU-owned texture (treated like a render target: the
    // initial contents are loaded from guest memory once, later sampling of the same address
    // aliases it, presentation finds it). Reuses a render target at the same address when the
    // texel size matches. Usage includes ShaderRead|ShaderWrite|RenderTarget|PixelFormatView.
    id<MTLTexture> GetStorageTexture(const ImageDesc& img);

    // GPU-side copies of whole surfaces (e.g. a compute shader copying a depth buffer through
    // buffer loads/stores, shadPS4 Rasterizer::IsComputeImageCopy): find the GPU-owned texture at
    // src_addr and find/create one with the same description at dst_addr. The caller encodes the
    // blit. kind_out: 1 color, 2 depth. written_bytes: the guest bytes the copy writes from
    // dst_addr (an exactly known write, see NoteExactWrite).
    bool CloneGpuTexture(uint64_t src_addr, uint64_t dst_addr, uint64_t written_bytes, id<MTLTexture>* src,
                         id<MTLTexture>* dst, int* kind_out);

    // Debugging: visit every GPU-owned texture (kind: 1 color/storage, 2 depth).
    void ForEachGpuTexture(void (^fn)(uint64_t address, int kind, id<MTLTexture> tex)) const;
    // ---- Frame capture and pass replay (ps4_gnm_capture.h) ----
    // Identities of the GPU-owned entries (color/storage, depth) at `address`.
    void DescribeGpuTextures(uint64_t address, std::vector<ps4gnm_capture::GpuTextureId>* out) const;
    // Identity of the GPU-owned entry whose texture is `tex`; false if none (views, EQAA twins).
    bool DescribeGpuTexture(id<MTLTexture> tex, ps4gnm_capture::GpuTextureId* out) const;
    // The GPU-owned entry with this identity; with `create`, made (empty, not loaded from guest
    // memory) when there is none. nil if none / on failure.
    id<MTLTexture> FindGpuTexture(const ps4gnm_capture::GpuTextureId& gid, bool create);
    // All levels and slices in the ps4gnm_capture::RawSize layout (depth+stencil: both planes),
    // through a blit on this cache's queue; waits. False for multisampled or unsupported formats.
    bool ReadRaw(id<MTLTexture> tex, std::vector<uint8_t>* out);
    bool WriteRaw(id<MTLTexture> tex, const std::vector<uint8_t>& raw);

    // True if [addr, addr+size) overlaps a GPU-owned (render target / storage) texture.
    bool OverlapsGpuTexture(uint64_t addr, uint64_t size) const;

    struct Stats {
        uint64_t uploads = 0, upload_bytes = 0, hits = 0, misses = 0, invalidations = 0,
                 rt_created = 0, rt_aliased = 0;
    };
    Stats stats;

    // Memory held by the cache's own textures (MTLResource.allocatedSize when created), by kind.
    // Readable from any thread (e.g. the GPU memory sampler) without the cache lock.
    struct MemStats {
        uint64_t sampled_count = 0, sampled_bytes = 0; // detiled from guest memory (re-creatable)
        uint64_t gpu_count = 0, gpu_bytes = 0;         // render targets, depth, storage (GPU-owned)
    };
    MemStats GetMemStats() const;

    // Eviction of sampled textures (docs/gpu-memory-budget.md). Every lookup that returns a
    // texture stamps its entry with the command buffer sequence number and time set here: the
    // renderer calls SetUse at each commit with the number of the command buffer that work
    // encoded from now on belongs to.
    void SetUse(uint64_t seq, uint64_t now_ns);
    // Every texture the cache creates (created) and every one it lets go (released: entry deleted,
    // texture replaced, alias copy replaced, the cache destroyed), for the Metal 4 backend's
    // residency sets. Views are not reported (a view counts as its parent texture). Set once,
    // before the first lookup. Called with the cache's lock held, on the thread that creates or lets
    // go of the texture: in the renderer always the holder of the encoder's state (the encoder
    // thread, or the thread running encoder work inline; ps4_gnm_draw.mm checks it, FATAL).
    struct TextureHooks {
        void* ctx = nullptr;
        void (*created)(void* ctx, id<MTLTexture> t) = nullptr;
        void (*released)(void* ctx, id<MTLTexture> t) = nullptr;
    };
    void SetHooks(const TextureHooks& h) { hooks_ = h; }
    // The GPU copy of a target into the sampled copy an alias views (different dimensions). Set: the
    // renderer encodes it on its own timeline (its upload command buffer). Unset: a command buffer of
    // the cache's queue, committed at once (tests that use the cache alone). Called with the cache's
    // lock held, by the holder of the encoder's state (the renderer encodes the copy into its own
    // command buffers there).
    struct GpuCopyHook {
        void* ctx = nullptr;
        void (*copy)(void* ctx, id<MTLTexture> src, id<MTLTexture> dst, MTLSize size) = nullptr;
    };
    void SetGpuCopyHook(const GpuCopyHook& h) { copy_hook_ = h; }
    // Evicts sampled textures chosen by ps4_texcache_select: only entries whose last command
    // buffer has completed (completed_seq), least recently used first. GPU-owned surfaces are
    // never evicted here (see EvictSuperseded). An evicted texture still referenced by a command buffer lives until that
    // command buffer is done; its next lookup creates and uploads it again.
    struct EvictResult {
        uint64_t count = 0, bytes = 0;
    };
    EvictResult EvictSampled(const ps4_texcache_policy& p, bool gpu_over, uint64_t now_ns, uint64_t completed_seq);
    // GPU-owned surfaces: how many are superseded (see EvictSuperseded) and how many were not
    // looked up for idle_ns. Takes the lock and walks the GPU-owned entries.
    struct GpuOwnedStats {
        uint64_t superseded_count = 0, superseded_bytes = 0, idle_count = 0, idle_bytes = 0;
    };
    GpuOwnedStats GetGpuOwnedStats(uint64_t idle_ns) const;
    // Evicts superseded GPU-owned surfaces: ones whose whole guest range exactly known writes of
    // other GPU-owned surfaces (NoteExactWrite, compute copies) covered after this one's last use
    // (on the PS4 their bytes are overwritten), not looked up for idle_ns and whose last command
    // buffer has completed. Partial cover, draws, storage stores, or a newer surface that was only
    // created or read keep the surface: GPU-only contents are never dropped.
    EvictResult EvictSuperseded(uint64_t idle_ns, uint64_t now_ns, uint64_t completed_seq);
    // GPU-owned textures the cache dropped (superseded evictions, depth arrays replaced when they
    // grew), handed to the renderer so it forgets state keyed by their pointers before they are
    // freed; the cache keeps them alive until taken. Cheap when there are none.
    void TakeRetired(std::vector<id<MTLTexture>>* out);
    // Totals since start (any thread).
    uint64_t EvictedCount() const { return evicted_count_.load(std::memory_order_relaxed); }
    uint64_t EvictedBytes() const { return evicted_bytes_.load(std::memory_order_relaxed); }
    uint64_t EvictedGpuCount() const { return evicted_gpu_count_.load(std::memory_order_relaxed); }
    uint64_t EvictedGpuBytes() const { return evicted_gpu_bytes_.load(std::memory_order_relaxed); }

private:
    struct Entry;
    bool UploadLocked(Entry* e);
    bool UploadLockedImpl(Entry* e);
    id<MTLTexture> GetTextureImpl(const ImageDesc& img, const uint64_t* gpu_epochs, GpuUpload* up,
                                  uint64_t refresh_min_bytes = 0);
    // Waits for the work committed so far on the cache's queue (queue_: the backend's SideQueue).
    // On Metal 3 that is the renderer's own queue, so the renderer's committed work is drained too.
    // On Metal 4 it is a separate side queue: the renderer's timeline is NOT drained, and a reader
    // of GPU-written textures must drain the renderer first (Flush(true), as DumpTargets and the
    // writeback paths do).
    void DrainQueueLocked();
    // Adds (sign +1) or removes (sign -1) an entry's texture in the MemStats counters.
    void TrackLocked(const Entry* e, int sign);
    // Stamps the entry's last use with the SetUse values (ExactWriteLocked: and an exactly known
    // write of guest [lo, hi), clipped to the entry).
    void TouchLocked(const Entry* e) const;
    static void EntryId(const Entry& e, ps4gnm_capture::GpuTextureId* out);
    static bool SameIdentity(const Entry& e, const ps4gnm_capture::GpuTextureId& gid);
    void ExactWriteLocked(const Entry* e, uint64_t lo, uint64_t hi, uint64_t seq = 0) const;
    // A GPU write to the surface (render-target or storage bind, copy, clear): buffer writes
    // recorded before it no longer describe its contents.
    void GpuWroteLocked(const Entry* e) const;
    // Drops the entry's guest-newer mark (before it is deleted).
    void UnmarkLocked(const Entry* e);
    // GPU-owned entries overlapping [addr, addr + size), from the sorted range index.
    void GpuEntriesOverlappingLocked(uint64_t addr, uint64_t size, std::vector<Entry*>* out) const;
    void RebuildGpuIndexLocked() const;
    void RetireLocked(id<MTLTexture> t);
    TextureHooks hooks_;
    GpuCopyHook copy_hook_;
    id<MTLTexture> NewTextureLocked(MTLTextureDescriptor* td); // [device_ newTextureWithDescriptor:], reported
    void ReleaseTextureLocked(id<MTLTexture> t);               // reported released (nil: nothing)
    // The texture an alias holds of its own: the copy an alias view was made from (a view of the
    // entry's texture holds nothing of its own).
    static id<MTLTexture> AliasCopy(const Entry* e);
    // deletes e after reporting its texture and its alias copy released
    void DropEntryLocked(Entry* e);
    std::vector<id<MTLTexture>> retired_;
    std::atomic<bool> retired_pending_{false};

    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    uint8_t* mem_base_;
    uint64_t mem_size_;
    mutable std::mutex mutex_;
    std::unordered_multimap<uint64_t, Entry*> entries_; // keyed by guest address
    // Address ranges of GPU-owned entries sorted by start, with the running maximum of their
    // ends, for OverlapsGpuTexture. gpu_version_ changes whenever a GPU-owned entry is added,
    // removed or resized (TrackLocked); the index is rebuilt when it was built for another
    // version. (The entry count is no such signal: sampled entries are evicted.)
    mutable std::vector<std::pair<uint64_t, uint64_t>> gpu_ranges_;
    mutable std::vector<uint64_t> gpu_max_end_;
    mutable std::vector<Entry*> gpu_entries_; // the entry of each gpu_ranges_ element
    std::vector<Entry*> overlap_scratch_;     // NoteBufferWrite, kept to avoid allocations
    std::atomic<size_t> guest_newer_n_{0};    // entries with a guest-newer mark
    std::atomic<uint64_t> guest_newer_marked_{0}, guest_newer_refreshed_{0}, guest_newer_partial_{0},
        guest_newer_failed_{0};
    // GPU-owned entries by 64 MiB guest granule (a bit per granule below 256 GiB; set when an entry is
    // added, recomputed with the range index): NoteBufferWrite's check without the lock
    static constexpr uint32_t kHintShift = 26, kHintWords = 64;
    mutable std::atomic<uint64_t> gpu_hint_[kHintWords] = {};
    void HintAddLocked(uint64_t addr, uint64_t size) const;
    bool HintNear(uint64_t addr, uint64_t size) const;
    uint64_t gpu_version_ = 0;
    mutable uint64_t gpu_index_version_ = ~0ull;
    std::atomic<uint64_t> sampled_count_{0}, sampled_bytes_{0}, gpu_count_{0}, gpu_bytes_{0};
    std::atomic<uint64_t> evicted_count_{0}, evicted_bytes_{0}, evicted_gpu_count_{0}, evicted_gpu_bytes_{0};
    // GPU-owned entries sorted by address, with their superseded flags (SupersededLocked)
    void SupersededLocked(std::vector<Entry*>* v, std::vector<char>* sup) const;
    // last use stamped on lookups (SetUse); before the first commit, command buffer 1
    std::atomic<uint64_t> use_seq_{1}, use_ns_{0};
    // EvictSampled scratch, kept to avoid allocations per pass
    std::vector<ps4_texcache_item> evict_items_;
    std::vector<std::unordered_multimap<uint64_t, Entry*>::iterator> evict_its_;
};
#endif

} // namespace ps4gnm

#endif // PS4_GNM_RESOURCES_H
