// SPDX-License-Identifier: GPL-2.0-or-later
//
// The compute tessellator (ps4_gnm_tessellator.h) against Metal 3's hardware tessellation: the
// same patches are drawn with drawPatches and with the tessellator's indices over its domain
// points (the indexed indirect draw the renderer makes when there is no hardware tessellation),
// filled with back faces culled for either front face winding (a winding difference shows), and
// the images must be identical. The hardware runs the vertex function once per index of its
// triangle list, so its invocation count must equal the tessellator's index count. (A wireframe
// comparison is not exact: a shared edge drawn in the other direction rasterizes differently.)
// The tessellator's TRIANGLE_CW output is Metal's MTLWindingCounterClockwise, as in the renderer.
// Where the GPU supports Metal 4, the compute path also runs through Metal4Backend (its encoders,
// barriers, argument tables and residency), as the renderer runs it there.
// Exit 77: no Metal device.
#import <Metal/Metal.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include <atomic>
#include <string>

#include "ps4_gnm_backend_metal4.h"
#include "ps4_gnm_tessellator.h"

using namespace ps4gnm_tess;

namespace {

constexpr int kSize = 256;
constexpr uint32_t kDomainSlot = 19; // gcn_recompiler.h GCN_TESS_DOMAIN_BUFFER_SLOT

const char* const kShaders = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; float4 color; };
// The patch's domain onto the target: triangles (u, v, w) as barycentrics of a triangle, quads (u, v)
// as a square; patch p shifted so the patches do not overlap.
static VOut Place(float3 c, uint patch, bool tri) {
    VOut o;
    const float2 p = tri ? float2(-0.9, -0.9) * c.z + float2(0.9, -0.9) * c.x + float2(-0.9, 0.9) * c.y
                         : float2(-0.9 + 1.8 * c.x, -0.9 + 1.8 * c.y);
    o.pos = float4(p * 0.5 + float2(patch & 1 ? 0.5 : -0.5, patch & 2 ? 0.5 : -0.5), 0.5, 1.0);
    o.color = float4(c, 1.0);
    return o;
}
[[patch(triangle, 1)]] vertex VOut hw_tri(float3 c [[position_in_patch]], uint patch [[patch_id]],
                                          device atomic_uint* calls [[buffer(1)]]) {
    atomic_fetch_add_explicit(calls, 1, memory_order_relaxed);
    return Place(c, patch, true);
}
[[patch(quad, 1)]] vertex VOut hw_quad(float2 c [[position_in_patch]], uint patch [[patch_id]],
                                       device atomic_uint* calls [[buffer(1)]]) {
    atomic_fetch_add_explicit(calls, 1, memory_order_relaxed);
    return Place(float3(c, 0.0), patch, false);
}
vertex VOut pull_tri(const device float4* d [[buffer(19)]], uint v [[vertex_id]]) {
    return Place(d[v].xyz, as_type<uint>(d[v].w), true);
}
vertex VOut pull_quad(const device float4* d [[buffer(19)]], uint v [[vertex_id]]) {
    return Place(d[v].xyz, as_type<uint>(d[v].w), false);
}
fragment float4 frag(VOut in [[stage_in]]) { return in.color; }
)MSL";

struct Case {
    uint32_t domain;
    uint32_t partitioning;
    bool clockwise; // the hardware path's MTLWindingClockwise
    float factors[4][6]; // 4 patches; tri: edges 0..2, inside; quad: edges 0..3, inside 0..1
};

id<MTLRenderPipelineState> MakePipeline(id<MTLDevice> dev, id<MTLLibrary> lib, const char* vs, const Case& c, bool hw) {
    MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@(vs)];
    d.fragmentFunction = [lib newFunctionWithName:@"frag"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    if (hw) {
        d.maxTessellationFactor = 64;
        d.tessellationFactorFormat = MTLTessellationFactorFormatHalf;
        d.tessellationFactorStepFunction = MTLTessellationFactorStepFunctionPerPatch;
        d.tessellationControlPointIndexType = MTLTessellationControlPointIndexTypeNone;
        d.tessellationOutputWindingOrder = c.clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise;
        static const MTLTessellationPartitionMode kModes[4] = {
            MTLTessellationPartitionModeInteger, MTLTessellationPartitionModePow2,
            MTLTessellationPartitionModeFractionalOdd, MTLTessellationPartitionModeFractionalEven};
        d.tessellationPartitionMode = kModes[c.partitioning];
    }
    NSError* err = nil;
    id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p) fprintf(stderr, "pipeline %s: %s\n", vs, err.localizedDescription.UTF8String);
    return p;
}

// Draws the case's 4 patches; returns the RGBA8 image and the vertex count (hardware: vertex
// function invocations, compute: the indirect draw's index count).
std::vector<uint8_t> Draw(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLLibrary> lib, const Kernels& k, const Case& c,
                          bool hw, MTLWinding front, uint32_t* vertices) {
    const bool tri = c.domain == kTriangle;
    const uint32_t nf = tri ? 4 : 6;
    std::vector<__fp16> f(4 * nf);
    for (int p = 0; p < 4; ++p)
        for (uint32_t i = 0; i < nf; ++i) f[p * nf + i] = (__fp16)c.factors[p][i];
    id<MTLBuffer> factors = [dev newBufferWithBytes:f.data() length:f.size() * 2 options:MTLResourceStorageModeShared];
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:kSize height:kSize mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> rt = [dev newTextureWithDescriptor:td];
    id<MTLRenderPipelineState> pso =
        MakePipeline(dev, lib, hw ? (tri ? "hw_tri" : "hw_quad") : (tri ? "pull_tri" : "pull_quad"), c, hw);
    if (!pso) return {};
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBuffer> points = nil, indices = nil, result = nil;
    id<MTLBuffer> calls = [dev newBufferWithLength:4 options:MTLResourceStorageModeShared];
    memset(calls.contents, 0, 4);
    if (!hw) {
        Params p{c.domain, c.partitioning, c.clockwise ? (uint32_t)kTrianglesCcw : (uint32_t)kTrianglesCw, 4,
                 4 * kMaxPointsPerPatch, 4 * kMaxIndicesPerPatch, 0, 0};
        id<MTLBuffer> counts = [dev newBufferWithLength:4 * 8 options:MTLResourceStorageModePrivate];
        id<MTLBuffer> offsets = [dev newBufferWithLength:4 * 8 options:MTLResourceStorageModePrivate];
        id<MTLBuffer> feedback = [dev newBufferWithLength:sizeof(Feedback) options:MTLResourceStorageModeShared];
        memset(feedback.contents, 0, sizeof(Feedback));
        result = [dev newBufferWithLength:sizeof(Result) options:MTLResourceStorageModeShared];
        points = [dev newBufferWithLength:(size_t)p.point_capacity * 16 options:MTLResourceStorageModePrivate];
        indices = [dev newBufferWithLength:(size_t)p.index_capacity * 4 options:MTLResourceStorageModePrivate];
        id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
        [e setBytes:&p length:sizeof(p) atIndex:kParamsSlot];
        [e setBuffer:factors offset:0 atIndex:kFactorsSlot];
        [e setBuffer:counts offset:0 atIndex:kCountsSlot];
        [e setBuffer:offsets offset:0 atIndex:kOffsetsSlot];
        [e setBuffer:result offset:0 atIndex:kResultSlot];
        [e setBuffer:points offset:0 atIndex:kPointsSlot];
        [e setBuffer:indices offset:0 atIndex:kIndicesSlot];
        [e setBuffer:feedback offset:0 atIndex:kFeedbackSlot];
        [e setComputePipelineState:k.count];
        [e dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:MTLSizeMake(4, 1, 1)];
        [e memoryBarrierWithScope:MTLBarrierScopeBuffers];
        [e setComputePipelineState:k.offsets];
        [e dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [e memoryBarrierWithScope:MTLBarrierScopeBuffers];
        [e setComputePipelineState:k.emit];
        [e dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:MTLSizeMake(4, 1, 1)];
        [e endEncoding];
    }
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = rt;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> r = [cb renderCommandEncoderWithDescriptor:rp];
    [r setRenderPipelineState:pso];
    [r setFrontFacingWinding:front];
    [r setCullMode:MTLCullModeBack];
    if (hw) {
        [r setVertexBuffer:calls offset:0 atIndex:1];
        [r setTessellationFactorBuffer:factors offset:0 instanceStride:0];
        [r drawPatches:1 patchStart:0 patchCount:4 patchIndexBuffer:nil patchIndexBufferOffset:0 instanceCount:1 baseInstance:0];
    } else {
        [r setVertexBuffer:points offset:0 atIndex:kDomainSlot];
        [r drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt32 indexBuffer:indices
               indexBufferOffset:0 indirectBuffer:result indirectBufferOffset:0];
    }
    [r endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    std::vector<uint8_t> px(kSize * kSize * 4);
    [rt getBytes:px.data() bytesPerRow:kSize * 4 fromRegion:MTLRegionMake2D(0, 0, kSize, kSize) mipmapLevel:0];
    *vertices = hw ? *static_cast<const uint32_t*>(calls.contents) : static_cast<const Result*>(result.contents)->draw[0];
    return px;
}

void Done(void* ctx, const ps4gnm::GnmCompletion& c) {
    static_cast<std::atomic<int>*>(ctx)->store(c.failed ? -1 : 1);
}

// The compute path through Metal4Backend: kernels in a compute encoder with the backend's
// barriers, then the indexed indirect draw in a render pass.
std::vector<uint8_t> DrawMetal4(ps4gnm::Metal4Backend& be, id<MTLLibrary> lib, const Kernels& k, const Case& c,
                                MTLWinding front, uint32_t* vertices) {
    using namespace ps4gnm;
    id<MTLDevice> dev = be.Device();
    static uint64_t seq = 0;
    const bool tri = c.domain == kTriangle;
    const uint32_t nf = tri ? 4 : 6;
    std::vector<__fp16> f(4 * nf);
    for (int p = 0; p < 4; ++p)
        for (uint32_t i = 0; i < nf; ++i) f[p * nf + i] = (__fp16)c.factors[p][i];
    Params p{c.domain, c.partitioning, c.clockwise ? (uint32_t)kTrianglesCcw : (uint32_t)kTrianglesCw, 4,
             4 * kMaxPointsPerPatch, 4 * kMaxIndicesPerPatch, 0, 0};
    id<MTLBuffer> factors = [dev newBufferWithBytes:f.data() length:f.size() * 2 options:MTLResourceStorageModeShared];
    id<MTLBuffer> counts = [dev newBufferWithLength:4 * 8 options:MTLResourceStorageModePrivate];
    id<MTLBuffer> offsets = [dev newBufferWithLength:4 * 8 options:MTLResourceStorageModePrivate];
    id<MTLBuffer> feedback = [dev newBufferWithLength:sizeof(Feedback) options:MTLResourceStorageModeShared];
    memset(feedback.contents, 0, sizeof(Feedback));
    id<MTLBuffer> result = [dev newBufferWithLength:sizeof(Result) options:MTLResourceStorageModeShared];
    id<MTLBuffer> points = [dev newBufferWithLength:(size_t)p.point_capacity * 16 options:MTLResourceStorageModePrivate];
    id<MTLBuffer> indices = [dev newBufferWithLength:(size_t)p.index_capacity * 4 options:MTLResourceStorageModePrivate];
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:kSize height:kSize mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> rt = [dev newTextureWithDescriptor:td];
    for (id<MTLBuffer> b : {factors, counts, offsets, feedback, result, points, indices})
        be.MakeResident(b, GnmResidencyClass::Builtin);
    be.MakeResident(rt, GnmResidencyClass::Texture);
    id<MTLRenderPipelineState> pso = MakePipeline(dev, lib, tri ? "pull_tri" : "pull_quad", c, false);
    if (!pso) return {};

    be.BeginCmd(GnmCmdTarget::Main, @"tessellator test", false);
    be.UseTarget(GnmCmdTarget::Main);
    be.BeginCompute(false);
    be.BindBytes(GnmStage::Compute, kParamsSlot, &p, sizeof(p));
    be.BindBuffer(GnmStage::Compute, kFactorsSlot, factors, 0);
    be.BindBuffer(GnmStage::Compute, kCountsSlot, counts, 0);
    be.BindBuffer(GnmStage::Compute, kOffsetsSlot, offsets, 0);
    be.BindBuffer(GnmStage::Compute, kResultSlot, result, 0);
    be.BindBuffer(GnmStage::Compute, kPointsSlot, points, 0);
    be.BindBuffer(GnmStage::Compute, kIndicesSlot, indices, 0);
    be.BindBuffer(GnmStage::Compute, kFeedbackSlot, feedback, 0);
    be.SetComputePipeline(k.count);
    be.DispatchThreads(MTLSizeMake(4, 1, 1), MTLSizeMake(4, 1, 1));
    be.Barrier(kGnmStageDispatch, kGnmStageDispatch, kGnmScopeBuffers);
    be.SetComputePipeline(k.offsets);
    be.DispatchThreads(MTLSizeMake(1, 1, 1), MTLSizeMake(1, 1, 1));
    be.Barrier(kGnmStageDispatch, kGnmStageDispatch, kGnmScopeBuffers);
    be.SetComputePipeline(k.emit);
    be.DispatchThreads(MTLSizeMake(4, 1, 1), MTLSizeMake(4, 1, 1));
    be.EndEncoder();

    GnmPassDesc pass;
    pass.color[0].tex = rt;
    pass.color[0].load = GnmLoad::Clear;
    pass.clear_color[0] = MTLClearColorMake(0, 0, 0, 1);
    if (!be.BeginRender(pass)) return {};
    be.SetRenderPipeline(pso);
    GnmFixedState fx;
    fx.vp = MTLViewport{0, 0, kSize, kSize, 0, 1};
    fx.sc = MTLScissorRect{0, 0, kSize, kSize};
    fx.cull = MTLCullModeBack;
    fx.winding = front;
    fx.fill = MTLTriangleFillModeFill;
    fx.clip = MTLDepthClipModeClip;
    be.SetFixed(fx, kGnmFixedViewport | kGnmFixedScissor | kGnmFixedCull | kGnmFixedWinding | kGnmFixedFill |
                        kGnmFixedClip);
    be.BindBuffer(GnmStage::Vertex, kDomainSlot, points, 0);
    GnmDraw d;
    d.kind = GnmDrawKind::IndexedIndirect;
    d.prim = MTLPrimitiveTypeTriangle;
    d.index_type = MTLIndexTypeUInt32;
    d.index_buffer = indices;
    d.index_length = (uint64_t)p.index_capacity * 4;
    d.indirect_buffer = result;
    be.Draw(d);
    be.EndEncoder();
    std::atomic<int> done{0};
    be.Commit(++seq, Done, &done);
    for (int i = 0; i < 5000 && !done.load(); ++i) usleep(1000);
    if (done.load() != 1) {
        fprintf(stderr, "FAIL Metal 4 command buffer %s\n", done.load() ? "failed" : "did not complete");
        return {};
    }
    std::vector<uint8_t> px(kSize * kSize * 4);
    [rt getBytes:px.data() bytesPerRow:kSize * 4 fromRegion:MTLRegionMake2D(0, 0, kSize, kSize) mipmapLevel:0];
    *vertices = static_cast<const Result*>(result.contents)->draw[0];
    return px;
}

} // namespace

int main() {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
        printf("no Metal device\n");
        return 77;
    }
    Kernels k;
    std::string why;
    if (!MakeKernels(dev, &k, &why)) {
        fprintf(stderr, "FAIL kernels: %s\n", why.c_str());
        return 1;
    }
    NSError* err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@(kShaders) options:nil error:&err];
    if (!lib) {
        fprintf(stderr, "FAIL shaders: %s\n", err.localizedDescription.UTF8String);
        return 1;
    }
    id<MTLCommandQueue> q = [dev newCommandQueue];
    ps4gnm::Metal4Backend m4;
    std::string m4_why;
    const bool have_m4 = m4.Init(dev, &m4_why);
    if (!have_m4) printf("Metal 4 part skipped: %s\n", m4_why.c_str());
    const Case cases[] = {
        {kTriangle, kInteger, false, {{1, 1, 1, 1}, {2, 3, 4, 5}, {7, 1, 3, 2}, {16, 16, 16, 16}}},
        {kTriangle, kFractionalOdd, true, {{1.5f, 2.25f, 3.75f, 4.5f}, {5.1f, 1.0f, 9.9f, 2.2f}, {1, 1, 1, 1}, {13.3f, 7.7f, 3.3f, 11.1f}}},
        {kTriangle, kFractionalEven, false, {{2.5f, 3.25f, 4.75f, 5.5f}, {6.1f, 2.0f, 9.9f, 3.2f}, {2, 2, 2, 2}, {14.3f, 8.7f, 4.3f, 12.1f}}},
        {kQuad, kInteger, true, {{1, 1, 1, 1, 1, 1}, {2, 3, 4, 5, 6, 7}, {8, 1, 3, 2, 5, 4}, {9, 9, 9, 9, 9, 9}}},
        {kQuad, kFractionalOdd, false, {{1.5f, 2.25f, 3.75f, 4.5f, 5.5f, 2.5f}, {5.1f, 1.0f, 9.9f, 2.2f, 3.3f, 7.7f}, {1, 1, 1, 1, 1, 1}, {13.3f, 7.7f, 3.3f, 11.1f, 6.6f, 4.4f}}},
        {kQuad, kFractionalEven, true, {{2.5f, 3.25f, 4.75f, 5.5f, 6.5f, 3.5f}, {6.1f, 2.0f, 9.9f, 3.2f, 4.3f, 8.7f}, {2, 2, 2, 2, 2, 2}, {14.3f, 8.7f, 4.3f, 12.1f, 7.6f, 5.4f}}},
    };
    int failures = 0;
    for (const Case& c : cases) {
        size_t lit_any = 0;
        for (MTLWinding front : {MTLWindingClockwise, MTLWindingCounterClockwise}) {
            uint32_t hw_vertices = 0, cs_vertices = 0;
            const std::vector<uint8_t> hw = Draw(dev, q, lib, k, c, true, front, &hw_vertices);
            const std::vector<uint8_t> cs = Draw(dev, q, lib, k, c, false, front, &cs_vertices);
            if (hw.empty() || cs.empty()) return 1;
            size_t diff = 0, lit = 0;
            for (size_t i = 0; i < hw.size(); i += 4) {
                if (memcmp(&hw[i], &cs[i], 4)) diff++;
                if (hw[i] | hw[i + 1] | hw[i + 2]) lit++;
            }
            lit_any += lit;
            const bool ok = diff == 0 && hw_vertices == cs_vertices;
            printf("%s domain %s partitioning %u front %s: %zu of %zu lit pixels differ, vertices %u / %u\n",
                   ok ? "ok" : "FAIL", c.domain == kTriangle ? "tri" : "quad", c.partitioning,
                   front == MTLWindingClockwise ? "cw" : "ccw", diff, lit, hw_vertices, cs_vertices);
            if (!ok) failures++;
            if (have_m4) {
                uint32_t m4_vertices = 0;
                const std::vector<uint8_t> m4px = DrawMetal4(m4, lib, k, c, front, &m4_vertices);
                if (m4px.empty()) return 1;
                size_t m4_diff = 0;
                for (size_t i = 0; i < hw.size(); i += 4)
                    if (memcmp(&hw[i], &m4px[i], 4)) m4_diff++;
                const bool m4_ok = m4_diff == 0 && m4_vertices == hw_vertices;
                printf("%s   Metal 4 backend: %zu pixels differ, vertices %u\n", m4_ok ? "ok" : "FAIL", m4_diff,
                       m4_vertices);
                if (!m4_ok) failures++;
            }
        }
        if (!lit_any) {
            printf("FAIL domain %s partitioning %u: nothing drawn\n", c.domain == kTriangle ? "tri" : "quad", c.partitioning);
            failures++;
        }
    }
    return failures ? 1 : 0;
}
