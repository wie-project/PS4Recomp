// SPDX-License-Identifier: GPL-2.0-or-later
// Native Metal regression: a linear HUD must load the scene written through an sRGB attachment.
#import <Metal/Metal.h>
#include "ps4_gnm_resources.h"
#include "ps4_hitch.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

static std::atomic<uint64_t> hitch_ns[PS4_HITCH_KINDS];
static double upload_ms;
extern "C" void ps4_gnm_hitch_note_upload(double ms) { upload_ms += ms; }
extern "C" uint64_t ps4_hitch_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
extern "C" void ps4_hitch_add_ns(int kind, uint64_t ns) { hitch_ns[kind].fetch_add(ns); }
extern "C" uint64_t ps4_hitch_take_ns(int kind) { return hitch_ns[kind].exchange(0); }

static void Check(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static void Run(id<MTLDevice> device, id<MTLCommandQueue> queue, bool srgb_first) {
    using namespace ps4gnm;
    std::vector<uint8_t> memory(1 << 20);
    TextureCache cache(device, queue, memory.data(), memory.size());
    ColorTarget scene;
    scene.bound = scene.enabled = true;
    scene.address = 65536;
    scene.pitch = scene.height = 64;
    scene.bpp = 32;
    scene.dfmt = DataFormat::Format8_8_8_8;
    scene.nfmt = srgb_first ? NumberFormat::Srgb : NumberFormat::Unorm;
    scene.mtl_format = srgb_first ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
    ColorTarget hud = scene;
    hud.nfmt = srgb_first ? NumberFormat::Unorm : NumberFormat::Srgb;
    hud.mtl_format = srgb_first ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatRGBA8Unorm_sRGB;
    id<MTLTexture> first_hud = nil;
    for (unsigned frame = 0; frame < 4; ++frame) {
        id<MTLTexture> scene_tex = cache.GetColorTarget(scene, false);
        Check(scene_tex != nil, "scene attachment missing");
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = scene_tex;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        const double value = frame < 2 ? 1 : 0.5;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(frame & 1 ? 0 : value, 0, frame & 1 ? value : 0, 1);
        id<MTLRenderCommandEncoder> encoder = [cb renderCommandEncoderWithDescriptor:pass];
        [encoder endEncoding];

        id<MTLTexture> hud_tex = cache.GetColorTarget(hud, false);
        Check(hud_tex != nil && hud_tex.pixelFormat == hud.mtl_format, "HUD attachment format missing");
        if (first_hud) Check(hud_tex == first_hud, "HUD view allocated again in steady state");
        first_hud = hud_tex;
        pass.colorAttachments[0].texture = hud_tex;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        encoder = [cb renderCommandEncoderWithDescriptor:pass];
        [encoder endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        Check(cb.status == MTLCommandBufferStatusCompleted, "Metal attachment passes failed");

        uint8_t pixel[4] = {};
        [hud_tex getBytes:pixel bytesPerRow:4 fromRegion:MTLRegionMake2D(1, 1, 1, 1) mipmapLevel:0];
        const unsigned encoded = frame < 2 ? 255 : (srgb_first ? 188 : 128);
        Check(pixel[0] == (frame & 1 ? 0 : encoded) && pixel[2] == (frame & 1 ? encoded : 0) && pixel[3] == 255,
              "HUD loaded stale scene contents after switching linear/sRGB render target format");
        Check(cache.stats.rt_created == 1, "linear/sRGB aliases allocated independent render targets");
        Check(hud_tex.parentTexture == scene_tex, "HUD view does not share the scene's backing storage");

        cb = [queue commandBuffer];
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 1, 0, 1);
        encoder = [cb renderCommandEncoderWithDescriptor:pass];
        [encoder endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        Check(cb.status == MTLCommandBufferStatusCompleted, "HUD write failed");
        scene_tex = cache.GetColorTarget(scene, false);
        [scene_tex getBytes:pixel bytesPerRow:4 fromRegion:MTLRegionMake2D(1, 1, 1, 1) mipmapLevel:0];
        Check(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0, "scene view did not observe the HUD write");
    }
    // A replacement backing texture must not keep the previous attachment's cached view.
    scene.num_slices = hud.num_slices = 2;
    id<MTLTexture> grown = cache.GetColorTarget(scene, false);
    id<MTLTexture> grown_hud = cache.GetColorTarget(hud, false);
    Check(grown && grown.arrayLength == 2 && grown_hud && grown_hud.parentTexture == grown,
          "array growth retained a view of the old backing texture");
    Check(grown_hud != first_hud, "array growth reused the old attachment view");

    id<MTLTexture> source = nil, clone = nil;
    int kind = 0;
    const uint64_t clone_address = scene.address * 2;
    Check(cache.CloneGpuTexture(scene.address, clone_address, 64 * 64 * 4, &source, &clone, &kind),
          "GPU texture clone failed");
    hud.address = clone_address;
    id<MTLTexture> clone_hud = cache.GetColorTarget(hud, false);
    Check(clone_hud && clone_hud.parentTexture == clone && clone_hud.parentTexture != grown,
          "cloned target retained the source's attachment view");
}

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        Check(device != nil, "Metal device unavailable");
        id<MTLCommandQueue> queue = [device newCommandQueue];
        Run(device, queue, true);
        Run(device, queue, false);
        puts("linear/sRGB render target alias regression passed");
    }
}
