// SPDX-License-Identifier: GPL-2.0-or-later
// MipChainFor's compute copy (mip_level_copy, with the views it binds) against the blit copy it
// replaces between dispatches: every byte of the destination level, for each format the renderer
// allows, a 2D array chain and a cube chain, and a source window that starts past slice 0.
#include "ps4_gnm_util_passes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static void Check(bool condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

// Random texels without NaN or infinity encodings (their payloads need not survive a float
// round trip); denormals stay in.
static void Fill(MTLPixelFormat f, std::vector<uint8_t>& bytes, uint32_t seed) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        bytes[i] = (uint8_t)(seed >> 24);
    }
    if (f == MTLPixelFormatRGBA16Float || f == MTLPixelFormatRG16Float) {
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
            uint16_t h;
            memcpy(&h, &bytes[i], 2);
            if ((h & 0x7c00) == 0x7c00) h &= ~0x4000;
            memcpy(&bytes[i], &h, 2);
        }
    } else if (f == MTLPixelFormatRG11B10Float) {
        for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
            uint32_t v;
            memcpy(&v, &bytes[i], 4);
            // R, G, B exponent fields and their top bits
            const uint32_t exps[3] = {0x7c0u, 0x7c0u << 11, 0x1fu << 27}, tops[3] = {0x400u, 0x400u << 11, 0x80000000u};
            for (int k = 0; k < 3; ++k)
                if ((v & exps[k]) == exps[k]) v &= ~tops[k];
            memcpy(&bytes[i], &v, 4);
        }
    }
}

static std::vector<uint8_t> ReadLevel(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLTexture> t, uint32_t level,
                                      uint32_t slices, uint32_t bpp) {
    const uint32_t w = (uint32_t)t.width >> level, h = (uint32_t)t.height >> level;
    const NSUInteger image = (NSUInteger)w * h * bpp;
    id<MTLBuffer> buf = [dev newBufferWithLength:image * slices options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    for (uint32_t s = 0; s < slices; ++s)
        [bl copyFromTexture:t sourceSlice:s sourceLevel:level sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:MTLSizeMake(w, h, 1) toBuffer:buf destinationOffset:image * s
     destinationBytesPerRow:(NSUInteger)w * bpp destinationBytesPerImage:image];
    [bl endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    const uint8_t* p = (const uint8_t*)buf.contents;
    return std::vector<uint8_t>(p, p + image * slices);
}

static void Run(id<MTLDevice> dev, id<MTLCommandQueue> q, id<MTLComputePipelineState> pso, MTLPixelFormat f,
                uint32_t bpp, MTLTextureType chain_type) {
    const uint32_t W = 74, H = 38, levels = 4, level = 2, src_slices = 15, first = 3;
    const uint32_t slices = chain_type == MTLTextureTypeCube ? 6 : 5;
    const uint32_t w = W >> level;
    const uint32_t ch = (chain_type == MTLTextureTypeCube ? W : H) >> level; // the level's height
    MTLTextureDescriptor* sd = [MTLTextureDescriptor new];
    sd.textureType = MTLTextureType2DArray;
    sd.pixelFormat = f;
    sd.width = w + 3; // a larger source surface: only the level's size is copied
    sd.height = ch + 1;
    sd.arrayLength = src_slices;
    sd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsagePixelFormatView;
    sd.storageMode = MTLStorageModeShared;
    id<MTLTexture> src = [dev newTextureWithDescriptor:sd];
    Check(src != nil, "source texture");
    std::vector<uint8_t> texels((size_t)sd.width * sd.height * bpp);
    for (uint32_t s = 0; s < src_slices; ++s) {
        Fill(f, texels, 0x9e3779b9u * (s + 1) + (uint32_t)f);
        [src replaceRegion:MTLRegionMake2D(0, 0, sd.width, sd.height) mipmapLevel:0 slice:s withBytes:texels.data()
               bytesPerRow:sd.width * bpp bytesPerImage:texels.size()];
    }
    MTLTextureDescriptor* cd = [MTLTextureDescriptor new];
    cd.textureType = chain_type;
    cd.pixelFormat = f;
    cd.width = W;
    cd.height = chain_type == MTLTextureTypeCube ? W : H;
    cd.mipmapLevelCount = levels;
    cd.arrayLength = chain_type == MTLTextureTypeCube ? 1 : slices;
    cd.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsagePixelFormatView;
    cd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> by_blit = [dev newTextureWithDescriptor:cd], by_kernel = [dev newTextureWithDescriptor:cd];
    Check(by_blit && by_kernel, "chain textures");

    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    for (uint32_t s = 0; s < slices; ++s)
        [bl copyFromTexture:src sourceSlice:first + s sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:MTLSizeMake(w, ch, 1) toTexture:by_blit destinationSlice:s destinationLevel:level
          destinationOrigin:MTLOriginMake(0, 0, 0)];
    [bl endEncoding];
    // as MipChainFor binds them
    id<MTLTexture> sv = [src newTextureViewWithPixelFormat:f textureType:MTLTextureType2DArray
                                                    levels:NSMakeRange(0, 1) slices:NSMakeRange(0, src_slices)];
    id<MTLTexture> dv = [by_kernel newTextureViewWithPixelFormat:f textureType:MTLTextureType2DArray
                                                          levels:NSMakeRange(level, 1) slices:NSMakeRange(0, slices)];
    Check(sv && dv, "texture views");
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent];
    const uint32_t p[4] = {w, ch, first, slices};
    [ce setComputePipelineState:pso];
    [ce setTexture:sv atIndex:0];
    [ce setTexture:dv atIndex:1];
    [ce setBytes:p length:sizeof(p) atIndex:0];
    [ce dispatchThreads:MTLSizeMake(w, ch, slices) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [ce endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    Check(cb.status == MTLCommandBufferStatusCompleted, "copies completed");
    const auto a = ReadLevel(dev, q, by_blit, level, slices, bpp), b = ReadLevel(dev, q, by_kernel, level, slices, bpp);
    if (a != b) {
        size_t i = 0;
        while (a[i] == b[i]) ++i;
        fprintf(stderr, "format %lu type %lu: byte %zu differs (blit %02x, kernel %02x)\n", (unsigned long)f,
                (unsigned long)chain_type, i, a[i], b[i]);
        Check(false, "kernel copy matches the blit copy");
    }
}

int main() {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            fprintf(stderr, "no Metal device\n");
            return 77;
        }
        ps4gnm::GnmUtil util;
        std::string why;
        Check(ps4gnm::MakeGnmUtil(dev, &util, &why) && util.mip_copy, "mip_level_copy pipeline");
        id<MTLCommandQueue> q = [dev newCommandQueue];
        const struct {
            MTLPixelFormat f;
            uint32_t bpp;
        } formats[] = {{MTLPixelFormatRG11B10Float, 4}, {MTLPixelFormatRGBA16Float, 8}, {MTLPixelFormatRG16Float, 4},
                       {MTLPixelFormatRGBA8Unorm, 4},   {MTLPixelFormatRG8Unorm, 2},     {MTLPixelFormatR32Float, 4},
                       {MTLPixelFormatRGB10A2Unorm, 4}, {MTLPixelFormatRG16Unorm, 4}};
        for (const auto& fm : formats) {
            Run(dev, q, util.mip_copy, fm.f, fm.bpp, MTLTextureType2DArray);
            Run(dev, q, util.mip_copy, fm.f, fm.bpp, MTLTextureTypeCube);
        }
        printf("mip level copy: %zu formats match the blit copy\n", sizeof(formats) / sizeof(formats[0]));
    }
    return 0;
}
