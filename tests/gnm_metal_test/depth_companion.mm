// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_gnm_depth_companion.h"
#include <array>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void Check(bool condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

// Render identical depth-tested soft-particle draws with and without pass splits.
// The source depth remains live: a later draw must still pass against its original
// value. Read back every pixel, not just a success status from the command buffer.
static std::array<float, 256> Render(id<MTLDevice> dev, id<MTLTexture> companion, bool merge) {
    NSError* error = nil;
    NSString* source = @R"(
#include <metal_stdlib>
using namespace metal;
struct V { float4 position [[position]]; };
vertex V vs(uint i [[vertex_id]], constant float2& p [[buffer(0)]]) {
    float2 xy[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
    return {float4(xy[i], p.x, 1)};
}
fragment float4 fs(V v [[stage_in]], constant float2& p [[buffer(0)]],
                   texture2d<float> companion [[texture(0)]]) {
    return float4(p.y * companion.read(uint2(v.position.xy)).r, 0, 0, 1);
}
)";
    id<MTLLibrary> lib = [dev newLibraryWithSource:source options:nil error:&error];
    Check(lib != nil, "compile depth companion rendering regression");
    MTLRenderPipelineDescriptor* pipeline = [MTLRenderPipelineDescriptor new];
    pipeline.vertexFunction = [lib newFunctionWithName:@"vs"];
    pipeline.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pipeline.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
    pipeline.colorAttachments[0].blendingEnabled = YES;
    pipeline.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
    pipeline.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
    pipeline.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pipeline error:&error];
    Check(pso != nil, "create regression pipeline");
    MTLDepthStencilDescriptor* ds = [MTLDepthStencilDescriptor new];
    ds.depthCompareFunction = MTLCompareFunctionLessEqual;
    ds.depthWriteEnabled = NO;
    id<MTLDepthStencilState> dss = [dev newDepthStencilStateWithDescriptor:ds];
    Check(dss != nil, "create read-only depth test");
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                        width:16 height:16 mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> output = [dev newTextureWithDescriptor:td];
    td.pixelFormat = MTLPixelFormatDepth32Float;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [dev newTextureWithDescriptor:td];
    Check(output && depth, "create render targets");
    __unsafe_unretained id<MTLTexture> color[8] = {};
    color[0] = output;
    const bool keep_pass =
        merge && ps4gnm::IndependentDepthCompanion(companion, 1, 1, depth, color);
    Check(!merge || keep_pass, "current independent companion permits merging");
    id<MTLCommandQueue> queue = [dev newCommandQueue];
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    Check(cb != nil, "create regression command buffer");
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = output;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture = depth;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = 0.5;
    const float draws[][2] = {{0.25f, 0.25f}, {0.75f, 0.5f}, {0.4f, 0.5f}, {0.45f, 0.25f}};
    id<MTLRenderCommandEncoder> enc = nil;
    for (int i = 0; i < 4; ++i) {
        if (!enc) {
            enc = [cb renderCommandEncoderWithDescriptor:pass];
            Check(enc != nil, "begin regression pass");
            [enc setRenderPipelineState:pso];
            [enc setDepthStencilState:dss];
            [enc setFragmentTexture:companion atIndex:0];
        }
        [enc setVertexBytes:draws[i] length:sizeof(draws[i]) atIndex:0];
        [enc setFragmentBytes:draws[i] length:sizeof(draws[i]) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        if (!keep_pass || i == 3) {
            [enc endEncoding];
            enc = nil;
            pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            pass.depthAttachment.loadAction = MTLLoadActionLoad;
        }
    }
    [cb commit];
    [cb waitUntilCompleted];
    Check(cb.status == MTLCommandBufferStatusCompleted, "regression GPU work completed");
    std::array<float, 256> pixels;
    [output getBytes:pixels.data() bytesPerRow:16 * sizeof(float)
          fromRegion:MTLRegionMake2D(0, 0, 16, 16) mipmapLevel:0];
    return pixels;
}

int main() {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        Check(dev != nil, "Metal device available");
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                            width:16 height:16 mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsagePixelFormatView;
        desc.storageMode = MTLStorageModeShared;
        id<MTLTexture> companion = [dev newTextureWithDescriptor:desc];
        id<MTLTexture> other = [dev newTextureWithDescriptor:desc];
        id<MTLTexture> view = [companion newTextureViewWithPixelFormat:MTLPixelFormatR32Float];
        id<MTLTexture> sibling = [companion newTextureViewWithPixelFormat:MTLPixelFormatR32Float];
        Check(companion && other && view && sibling, "textures and views created");
        __unsafe_unretained id<MTLTexture> color[8] = {};
        color[0] = other;
        using ps4gnm::IndependentDepthCompanion;
        Check(IndependentDepthCompanion(companion, 4, 4, other, color), "current independent companion");
        Check(!IndependentDepthCompanion(nil, 4, 4, other, color), "missing companion retains hazard");
        Check(!IndependentDepthCompanion(companion, 3, 4, other, color), "stale companion retains hazard");
        Check(!IndependentDepthCompanion(companion, 4, 4, companion, color), "depth attachment alias");
        Check(!IndependentDepthCompanion(companion, 4, 4, view, color), "depth attachment view alias");
        for (int i = 0; i < 8; ++i) {
            color[i] = companion;
            Check(!IndependentDepthCompanion(companion, 4, 4, nil, color), "color attachment alias");
            color[i] = view;
            Check(!IndependentDepthCompanion(companion, 4, 4, nil, color), "color attachment view alias");
            Check(!IndependentDepthCompanion(sibling, 4, 4, nil, color), "sibling views alias");
            color[i] = nil;
        }
        Check(IndependentDepthCompanion(companion, 4, 4, nil, color), "no attachments");
        std::array<float, 256> depths;
        depths.fill(0.5f);
        [companion replaceRegion:MTLRegionMake2D(0, 0, 16, 16) mipmapLevel:0
                       withBytes:depths.data() bytesPerRow:16 * sizeof(float)];
        const auto split = Render(dev, companion, false);
        const auto merged = Render(dev, companion, true);
        for (size_t i = 0; i < split.size(); ++i) {
            Check(fabsf(split[i] - 0.5f) < 0.00001f, "split depth-tested reference pixel");
            Check(split[i] == merged[i], "merged and split pixels identical");
        }
        puts("depth companion hazard checks passed");
    }
    return 0;
}
