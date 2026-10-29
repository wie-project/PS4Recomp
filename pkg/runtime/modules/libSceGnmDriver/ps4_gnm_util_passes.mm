// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - utility shaders and the full-screen helper pass (ps4_gnm_util_passes.h).
#include "ps4_gnm_util_passes.h"

namespace ps4gnm {

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
