// SPDX-License-Identifier: GPL-2.0-or-later
// Native Metal regression: a T# with more rows than the newest render target at its address, but
// that target's row layout, reads that target's rows. DRIVECLUB's reflection probe draws a
// 768x384 target (pitch 1024) where the scene's 1920x1080 target was, then samples it with a
// 768x512 T# of pitch 1024; reading the older scene target put the main view into the probe.
#import <Metal/Metal.h>
#include "ps4_gnm_resources.h"
#include "ps4_hitch.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

extern "C" int32_t sceKernelIsNeoMode(void) { return 1; }
static std::atomic<uint64_t> hitch_ns[PS4_HITCH_KINDS];
static double upload_ms;
extern "C" void ps4_gnm_hitch_note_upload(double ms) { upload_ms += ms; }
extern "C" uint64_t ps4_hitch_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
extern "C" void ps4_hitch_add_ns(int kind, uint64_t ns) { hitch_ns[kind].fetch_add(ns); }
extern "C" uint64_t ps4_hitch_take_ns(int kind) { return hitch_ns[kind].exchange(0); }

static void Check(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

using namespace ps4gnm;

static ColorTarget Target(uint32_t pitch, uint32_t height) {
    ColorTarget t;
    t.bound = t.enabled = true;
    t.address = 65536;
    t.pitch = pitch;
    t.height = height;
    t.bpp = 32;
    t.dfmt = DataFormat::Format8_8_8_8;
    t.nfmt = NumberFormat::Unorm;
    t.mtl_format = MTLPixelFormatRGBA8Unorm;
    return t;
}

static void Clear(id<MTLCommandQueue> queue, id<MTLTexture> t, double r, double g, double b) {
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = t;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, 1);
    [[cb renderCommandEncoderWithDescriptor:pass] endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
}

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "Metal device unavailable\n");
            return 77;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        std::vector<uint8_t> memory(1 << 20);
        TextureCache cache(device, queue, memory.data(), memory.size());

        // the older, larger target (another pitch), then the newer prefix target
        ColorTarget scene = Target(64, 48);
        id<MTLTexture> scene_tex = cache.GetColorTarget(scene, false);
        Check(scene_tex != nil, "scene target missing");
        Clear(queue, scene_tex, 1, 0, 0); // red
        ColorTarget probe = Target(32, 16);
        probe.extent_width = 24;
        id<MTLTexture> probe_tex = cache.GetColorTarget(probe, false);
        Check(probe_tex != nil && probe_tex != scene_tex, "probe target missing");
        Clear(queue, probe_tex, 0, 1, 0); // green, then blue in the second round

        ImageDesc image;
        image.valid = true;
        image.address = probe.address;
        image.width = 24;
        image.height = 32;
        image.pitch = 32;
        image.num_levels = 1;
        image.type = ImageType::Color2D;
        image.tile_mode = TileMode::DisplayLinearAligned;
        image.dfmt = DataFormat::Format8_8_8_8;
        image.nfmt = NumberFormat::Unorm;
        image.pixel = GetPixelFormat(image.dfmt, image.nfmt);
        image.mtl_texture_type = MTLTextureType2D;

        for (int round = 0; round < 2; ++round) {
            id<MTLTexture> t = cache.GetTexture(image);
            Check(t != nil && t.width == 24 && t.height == 32, "sampled texture missing");
            id<MTLBuffer> out = [device newBufferWithLength:24 * 32 * 4 options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
            [bl copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                     sourceSize:MTLSizeMake(24, 32, 1) toBuffer:out destinationOffset:0
         destinationBytesPerRow:24 * 4 destinationBytesPerImage:24 * 32 * 4];
            [bl endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            const uint8_t* px = (const uint8_t*)out.contents;
            const uint8_t* p = px + (5 * 24 + 7) * 4;
            Check(p[0] == 0 && p[1] == (round ? 0 : 255) && p[2] == (round ? 255 : 0),
                  "a prefix row did not read the newer target's current contents");
            Check(px[(20 * 24 + 7) * 4] == 255 && px[(20 * 24 + 7) * 4 + 1] == 0,
                  "a row past the prefix did not keep the covering target");
            // the prefix target changes: the next read must see it
            probe_tex = cache.GetColorTarget(probe, false);
            Clear(queue, probe_tex, 0, 0, 1);
        }
        puts("render target prefix regression passed");
    }
    return 0;
}
