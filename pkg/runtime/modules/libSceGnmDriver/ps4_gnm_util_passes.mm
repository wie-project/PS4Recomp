// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - utility shaders and the full-screen helper pass (ps4_gnm_util_passes.h).
#include "ps4_gnm_util_passes.h"

namespace ps4gnm {

id<MTLTexture> PresentTextureView(id<MTLTexture> src, uint32_t pixel_format) {
    MTLPixelFormat f = src.pixelFormat;
    MTLPixelFormat lin = f == MTLPixelFormatBGRA8Unorm_sRGB ? MTLPixelFormatBGRA8Unorm
                         : f == MTLPixelFormatRGBA8Unorm_sRGB ? MTLPixelFormatRGBA8Unorm : f;
    // Storage-image UINT/SINT is a shader interpretation, not the display format.
    // Sampling an integer texture as texture2d<float> gives zero on Metal.
    if (f == MTLPixelFormatRGBA8Uint || f == MTLPixelFormatRGBA8Sint) {
        if (pixel_format == 0x80000000u) lin = MTLPixelFormatBGRA8Unorm;
        else if (pixel_format == 0x80002200u) lin = MTLPixelFormatRGBA8Unorm;
    }
    const bool swap_rb = f == MTLPixelFormatRGB10A2Unorm && (pixel_format & 0xFF000000u) == 0x88000000u;
    if (swap_rb) {
        const MTLTextureSwizzleChannels sw = MTLTextureSwizzleChannelsMake(
            MTLTextureSwizzleBlue, MTLTextureSwizzleGreen, MTLTextureSwizzleRed, MTLTextureSwizzleAlpha);
        return [src newTextureViewWithPixelFormat:lin textureType:MTLTextureType2D
                                          levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1) swizzle:sw];
    }
    if (lin != f || src.textureType != MTLTextureType2D)
        return [src newTextureViewWithPixelFormat:lin textureType:MTLTextureType2D
                                          levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];
    return src;
}

const char* const kGnmUtilMsl = R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; };
vertex VOut present_vs(uint vid [[vertex_id]]) {
    VOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}
struct DepthOutC { float depth [[depth(any)]]; };
fragment float4 depth2color_ms_fs(VOut in [[stage_in]], uint sid [[sample_id]], depth2d_ms<float> src [[texture(0)]]) {
    uint2 p = min(uint2(in.pos.xy), uint2(src.get_width() - 1, src.get_height() - 1));
    return float4(src.read(p, sid), 0.0, 0.0, 1.0);
}
fragment DepthOutC depth_from_color_fs(VOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    uint2 p = min(uint2(in.pos.xy), uint2(src.get_width() - 1, src.get_height() - 1));
    DepthOutC o;
    o.depth = src.read(p).r;
    return o;
}
fragment float4 eqaa_resolve_f(VOut in [[stage_in]], texture2d_ms<float> src [[texture(0)]]) {
    return src.read(uint2(in.pos.xy), 0);
}
fragment uint4 eqaa_resolve_u(VOut in [[stage_in]], texture2d_ms<uint> src [[texture(0)]]) {
    return src.read(uint2(in.pos.xy), 0);
}
fragment int4 eqaa_resolve_i(VOut in [[stage_in]], texture2d_ms<int> src [[texture(0)]]) {
    return src.read(uint2(in.pos.xy), 0);
}
fragment float4 eqaa_spread_f(VOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    return src.read(uint2(in.pos.xy));
}
fragment uint4 eqaa_spread_u(VOut in [[stage_in]], texture2d<uint> src [[texture(0)]]) {
    return src.read(uint2(in.pos.xy));
}
fragment int4 eqaa_spread_i(VOut in [[stage_in]], texture2d<int> src [[texture(0)]]) {
    return src.read(uint2(in.pos.xy));
}
struct StencilOut { uint stencil [[stencil]]; };
fragment StencilOut stencil_from_color_fs(VOut in [[stage_in]], texture2d<uint> src [[texture(0)]]) {
    uint2 p = min(uint2(in.pos.xy), uint2(src.get_width() - 1, src.get_height() - 1));
    StencilOut o;
    o.stencil = src.read(p).r;
    return o;
}
struct DepthOut { float depth [[depth(any)]]; };
fragment DepthOut depth_from_ms_fs(VOut in [[stage_in]], depth2d_ms<float> src [[texture(0)]]) {
    uint2 p = min(uint2(in.pos.xy), uint2(src.get_width() - 1, src.get_height() - 1));
    DepthOut o;
    o.depth = src.read(p, 0);
    return o;
}
fragment DepthOut depth_to_ms_fs(VOut in [[stage_in]], depth2d<float> src [[texture(0)]]) {
    uint2 p = min(uint2(in.pos.xy), uint2(src.get_width() - 1, src.get_height() - 1));
    DepthOut o;
    o.depth = src.read(p);
    return o;
}
fragment float4 depth2color_fs(VOut in [[stage_in]], depth2d<float> src [[texture(0)]]) {
    uint2 p = min(uint2(in.pos.xy), uint2(src.get_width() - 1, src.get_height() - 1));
    return float4(src.read(p), 0.0, 0.0, 1.0);
}
// GPU texture upload: linear element i <- tiled element at byte offset map[i]
kernel void detile_gather(device const uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]],
                          device const uint* map [[buffer(2)]], constant uint4& p [[buffer(3)]],
                          uint i [[thread_position_in_grid]]) {
    if (i >= p.y) return;
    const uint o = map[i];
    if (o == 0xffffffffu) return;
    const uint bpe = p.x;
    const ulong d = ulong(i) * bpe;
    switch (bpe) {
    case 1: dst[d] = src[o]; break;
    case 2: *(device ushort*)(dst + d) = *(device const ushort*)(src + o); break;
    case 4: *(device uint*)(dst + d) = *(device const uint*)(src + o); break;
    case 8: *(device uint2*)(dst + d) = *(device const uint2*)(src + o); break;
    case 16: *(device uint4*)(dst + d) = *(device const uint4*)(src + o); break;
    default: for (uint k = 0; k < bpe; ++k) dst[d + k] = src[o + k]; break;
    }
}
// Indirect quad-list draws (ps4_gnm_draw.mm, EncodeQuadListIndirect). The guest's argument
// record: indexed {count, instances, start index, base vertex, start instance}, non-indexed
// {count, instances, start vertex, start instance}.
// quad_list_prep (one thread) writes the triangle-list draw's
// MTLDrawIndexedPrimitivesIndirectArguments; the record's base vertex (non-indexed: start vertex)
// and start instance only where flags say the CP writes them to the fetch shader's SGPRs,
// otherwise the draw's own offsets. Indexed: the quads stop at the first one wholly at or past
// max_index; their count / 4 * 6 indices go to the draw's region of the pool (region, in indices:
// reserved by the CPU for the worst case of max_index, so they always fit), indexStart is the
// region, and `conv` gets the threadgroup count (64 quads per group) and {region, quads, first
// index} for quad_list_indices. Non-indexed: the shared pattern of cap_quads quads; a draw past it
// draws nothing and is recorded in `need` ([0] the largest count, [1] such draws).
struct QuadIndirectParams {
    uint flags; // 1: indexed, 2: 32-bit indices, 4: base vertex from the record, 8: start instance from it
    uint max_index;
    uint cap_quads;
    uint base_vertex, base_instance; // otherwise
    uint region;                     // indices
    uint pad[2];
};
kernel void quad_list_prep(device const uint* rec [[buffer(0)]], device uint* args [[buffer(1)]],
                           device uint* conv [[buffer(2)]], device atomic_uint* need [[buffer(3)]],
                           constant QuadIndirectParams& p [[buffer(4)]], uint tid [[thread_position_in_grid]]) {
    if (tid != 0) return;
    const bool indexed = (p.flags & 1) != 0;
    const uint start = indexed ? rec[2] : 0;
    uint quads = rec[0] / 4, at = 0;
    if (indexed) {
        const uint avail = p.max_index > start ? p.max_index - start : 0;
        quads = min(quads, (avail + 3) / 4); // at most (max_index + 3) / 4: the region's worst case
        at = p.region;
        conv[0] = (quads + 63) / 64;
        conv[1] = 1;
        conv[2] = 1;
        conv[4] = at;
        conv[5] = quads;
        conv[6] = start;
    } else if (quads > p.cap_quads) {
        atomic_fetch_max_explicit(&need[0], quads, memory_order_relaxed);
        atomic_fetch_add_explicit(&need[1], 1u, memory_order_relaxed);
        quads = 0;
    }
    args[0] = quads * 6;
    args[1] = rec[1];
    args[2] = at;
    args[3] = (p.flags & 4) != 0 ? (indexed ? rec[3] : rec[2]) : p.base_vertex;
    args[4] = (p.flags & 8) != 0 ? (indexed ? rec[4] : rec[3]) : p.base_instance;
}
// quad q: indices start + 4q .. + 3 of the guest's index buffer (one at or past max_index reads 0)
// as triangles 0 1 2 and 0 2 3, at pool index at + 6q.
kernel void quad_list_indices(device const uint* conv [[buffer(0)]], device const uchar* src [[buffer(1)]],
                              device uint* dst [[buffer(2)]], constant QuadIndirectParams& p [[buffer(3)]],
                              uint q [[thread_position_in_grid]]) {
    const uint at = conv[4], quads = conv[5], start = conv[6];
    if (q >= quads) return;
    uint v[4];
    for (uint k = 0; k < 4; ++k) {
        const uint i = start + 4 * q + k;
        v[k] = i >= p.max_index ? 0u
             : (p.flags & 2) != 0 ? ((device const uint*)src)[i]
                                  : uint(((device const ushort*)src)[i]);
    }
    device uint* d = dst + at + 6 * q;
    d[0] = v[0];
    d[1] = v[1];
    d[2] = v[2];
    d[3] = v[0];
    d[4] = v[2];
    d[5] = v[3];
}
// An indirect record copied with up to two fields replaced (mask bit k: field index[k] = value[k]):
// the base vertex / start instance the CP writes to no SGPR (ps4_gnm_draw.mm, IndirectRecordOffsets).
struct ArgsPatchParams {
    uint words, mask;
    uint index[2], value[2];
    uint pad[2];
};
kernel void indirect_args_patch(device const uint* rec [[buffer(0)]], device uint* out [[buffer(1)]],
                                constant ArgsPatchParams& p [[buffer(2)]], uint tid [[thread_position_in_grid]]) {
    if (tid != 0) return;
    for (uint i = 0; i < p.words; ++i) out[i] = rec[i];
    for (uint k = 0; k < 2; ++k)
        if ((p.mask >> k) & 1) out[p.index[k]] = p.value[k];
}
fragment float4 present_fs(VOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {
    uint2 p = uint2(in.pos.xy);
    p = min(p, uint2(src.get_width() - 1, src.get_height() - 1));
    return float4(src.read(p).rgb, 1.0);
}
)";

bool MakeGnmUtil(id<MTLDevice> dev, GnmUtil* out, std::string* why) {
    NSError* err = nil;
    out->lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kGnmUtilMsl] options:nil error:&err];
    if (!out->lib) {
        if (why) *why = err ? err.localizedDescription.UTF8String : "no library";
        return false;
    }
    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [out->lib newFunctionWithName:@"present_vs"];
    pd.fragmentFunction = [out->lib newFunctionWithName:@"present_fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    out->present = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!out->present && why) *why = std::string("present: ") + (err ? err.localizedDescription.UTF8String : "?");
    MTLRenderPipelineDescriptor* dd = [MTLRenderPipelineDescriptor new];
    dd.vertexFunction = pd.vertexFunction;
    dd.fragmentFunction = [out->lib newFunctionWithName:@"depth2color_fs"];
    dd.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
    err = nil;
    out->depth2color = [dev newRenderPipelineStateWithDescriptor:dd error:&err];
    if (!out->depth2color && why && why->empty())
        *why = std::string("depth2color: ") + (err ? err.localizedDescription.UTF8String : "?");
    if (id<MTLFunction> f = [out->lib newFunctionWithName:@"detile_gather"]) {
        err = nil;
        out->detile = [dev newComputePipelineStateWithFunction:f error:&err];
        if (!out->detile && why && why->empty())
            *why = std::string("detile_gather: ") + (err ? err.localizedDescription.UTF8String : "?");
    }
    const struct {
        const char* name;
        __strong id<MTLComputePipelineState>* pso;
    } kernels[] = {{"quad_list_prep", &out->quad_prep}, {"quad_list_indices", &out->quad_indices},
                   {"indirect_args_patch", &out->args_patch}};
    for (const auto& k : kernels) {
        id<MTLFunction> f = [out->lib newFunctionWithName:[NSString stringWithUTF8String:k.name]];
        if (!f) continue;
        err = nil;
        *k.pso = [dev newComputePipelineStateWithFunction:f error:&err];
        if (!*k.pso && why && why->empty())
            *why = std::string(k.name) + ": " + (err ? err.localizedDescription.UTF8String : "?");
    }
    return true;
}

bool FullscreenPass(GnmBackend& be, const GnmPassDesc& pd, id<MTLRenderPipelineState> pso, id<MTLDepthStencilState> dss,
                    id<MTLTexture> src) {
    if (!be.BeginRender(pd)) return false;
    be.SetRenderPipeline(pso);
    if (dss) be.SetDepthStencil(dss);
    be.BindTexture(GnmStage::Fragment, 0, src);
    GnmDraw d;
    d.count = 3;
    be.Draw(d);
    be.EndEncoder();
    return true;
}

} // namespace ps4gnm
