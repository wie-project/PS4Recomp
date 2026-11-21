// SPDX-License-Identifier: GPL-2.0-or-later
// The S# LOD bias reaches the Metal sampler: a texture whose levels are red, green and blue is
// drawn with point mip filtering at LOD 0 (4x4 target) and LOD 2 (1x1 target); the bias moves
// the level that is read. The biased LODs (1.25) lie inside a level: GPUs may round or truncate
// a LOD that falls exactly on a level boundary when picking the nearest mip.
#include "ps4_gnm_resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Check(bool condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

// An S# with point min/mag/mip filtering, LOD range 0..2 and the given bias (1/256 steps).
static void Sharp(float bias, uint32_t ss[4]) {
    const uint32_t b = (uint32_t)(int32_t)(bias * 256.f) & 0x3fffu;
    ss[0] = 0;                         // wrap, no aniso, no compare
    ss[1] = 0u | (2u * 256u << 12);    // MIN_LOD 0, MAX_LOD 2.0 (u4.8)
    ss[2] = b | (1u << 26);            // LOD_BIAS, point mag/min, MIP_FILTER point
    ss[3] = 0;                         // transparent black border
}

static MTLSamplerDescriptor* SamplerWithBias(float bias) {
    uint32_t ss[4];
    Sharp(bias, ss);
    ps4gnm::SamplerDesc sd;
    Check(ps4gnm::DecodeSampler(ss, &sd), "decode the S#");
    Check(sd.lod_bias == bias, "decoded LOD bias");
    MTLSamplerDescriptor* sampler = ps4gnm::MakeSamplerDescriptor(sd, false);
    if (@available(macOS 26.0, *)) Check(sampler.lodBias == bias, "Metal descriptor LOD bias");
    return sampler;
}

// Returns the level (0 red, 1 green, 2 blue, -1 other) read at the center of a size x size target.
static int LevelRead(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLRenderPipelineState> pso, id<MTLTexture> tex,
                     MTLSamplerDescriptor* sampler, uint32_t size) {
    id<MTLSamplerState> smp = [dev newSamplerStateWithDescriptor:sampler];
    Check(smp != nil, "create Metal sampler");
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:size height:size mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> rt = [dev newTextureWithDescriptor:td];
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = rt;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
    [re setRenderPipelineState:pso];
    [re setFragmentTexture:tex atIndex:0];
    [re setFragmentSamplerState:smp atIndex:0];
    [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [re endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    Check(cb.status == MTLCommandBufferStatusCompleted, "render completed");
    uint8_t px[4];
    [rt getBytes:px bytesPerRow:size * 4 fromRegion:MTLRegionMake2D(size / 2, size / 2, 1, 1) mipmapLevel:0];
    if (px[0] == 255 && px[1] == 0 && px[2] == 0) return 0;
    if (px[0] == 0 && px[1] == 255 && px[2] == 0) return 1;
    if (px[0] == 0 && px[1] == 0 && px[2] == 255) return 2;
    return -1;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (@available(macOS 26.0, *)) {
        } else {
            fprintf(stderr, "Metal samplers have no LOD bias before macOS 26\n");
            return 77;
        }
        const struct {
            float bias;
            uint32_t lod;
            int level;
            const char* name;
        } cases[] = {{0.f, 0, 0, "LOD 0 without bias reads level 0"},
                     {1.25f, 0, 1, "LOD 0 with bias +1.25 reads level 1"},
                     {0.f, 2, 2, "LOD 2 without bias reads level 2"},
                     {-0.75f, 2, 1, "LOD 2 with bias -0.75 reads level 1"}};
        if (argc == 2 && strcmp(argv[1], "--descriptor-only") == 0) {
            for (const auto& c : cases) SamplerWithBias(c.bias);
            return 0;
        }
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            fprintf(stderr, "no Metal device\n");
            return 77;
        }
        NSError* error = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@R"(
#include <metal_stdlib>
using namespace metal;
struct V { float4 position [[position]]; float2 uv; };
vertex V vs(uint i [[vertex_id]]) {
    float2 xy[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return {float4(xy[i], 0, 1), xy[i] * float2(0.5, -0.5) + 0.5};
}
fragment float4 fs(V v [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {
    return t.sample(s, v.uv);
}
)" options:nil error:&error];
        Check(lib != nil, "compile the regression shaders");
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&error];
        Check(pso != nil, "render pipeline");

        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                      width:4 height:4 mipmapped:YES];
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        const uint32_t colors[3] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u}; // red, green, blue (RGBA8 little endian)
        for (uint32_t level = 0; level < 3; ++level) {
            const uint32_t n = 4u >> level;
            uint32_t texels[16];
            for (uint32_t k = 0; k < n * n; ++k) texels[k] = colors[level];
            [tex replaceRegion:MTLRegionMake2D(0, 0, n, n) mipmapLevel:level withBytes:texels bytesPerRow:n * 4];
        }
        id<MTLCommandQueue> q = [dev newCommandQueue];
        bool biasIgnored = false;
        for (const auto& c : cases) {
            const uint32_t size = 4u >> c.lod;
            const int got = LevelRead(dev, q, pso, tex, SamplerWithBias(c.bias), size);
            if (got == c.level) continue;

            // Skip only when the hosted virtual GPU also ignores a directly configured Metal sampler.
            if (c.bias != 0.f && got == (int)c.lod && [dev.name isEqualToString:@"Apple Paravirtual device"]) {
                MTLSamplerDescriptor* control = [MTLSamplerDescriptor new];
                control.mipFilter = MTLSamplerMipFilterNearest;
                control.lodMaxClamp = 2.f;
                control.supportArgumentBuffers = YES;
                if (@available(macOS 26.0, *)) control.lodBias = c.bias;
                if (LevelRead(dev, q, pso, tex, control, size) == got) {
                    biasIgnored = true;
                    continue;
                }
            }
            fprintf(stderr, "read level %d\n", got);
            Check(false, c.name);
        }
        if (biasIgnored) {
            fprintf(stderr, "Apple Paravirtual device ignores lodBias even on a directly configured Metal sampler\n");
            return 77;
        }
        printf("sampler LOD bias: levels move with the S# bias\n");
    }
    return 0;
}
