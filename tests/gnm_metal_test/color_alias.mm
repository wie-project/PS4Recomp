// SPDX-License-Identifier: GPL-2.0-or-later
// Native Metal regressions for shared UNORM and sRGB render target bytes.
#import <Metal/Metal.h>
#include "ps4_gnm_resources.h"
#include "ps4_hitch.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

static void RunMipCopy(id<MTLDevice> device, id<MTLCommandQueue> queue, bool srgb_dst) {
    const MTLPixelFormat linear = MTLPixelFormatRGBA8Unorm, srgb = MTLPixelFormatRGBA8Unorm_sRGB;
    const MTLPixelFormat format = srgb_dst ? srgb : linear;
    MTLTextureDescriptor* sd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:srgb_dst ? linear : srgb
                                                                                 width:2 height:2 mipmapped:NO];
    sd.usage = MTLTextureUsageShaderRead;
    sd.storageMode = MTLStorageModeShared;
    id<MTLTexture> src = [device newTextureWithDescriptor:sd];
    Check(src, "mip copy source missing");
    const uint8_t colors[] = {18, 141, 72, 255, 175, 26, 82, 192, 91, 29, 210, 250, 2, 50, 65, 32};
    [src replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 withBytes:colors bytesPerRow:8];
    MTLTextureDescriptor* dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                 width:4 height:4 mipmapped:YES];
    dd.storageMode = MTLStorageModeShared;
    Check(!ps4gnm::MipCopySourceView(src, MTLPixelFormatBGRA8Unorm), "unrelated layouts treated as twins");
    for (uint32_t slices : {0u, 1u}) {
        id<MTLTexture> dst = [device newTextureWithDescriptor:dd];
        Check(dst, "mip copy destination missing");
        id<MTLTexture> view = ps4gnm::MipCopySourceView(src, format, slices);
        Check(view && view.pixelFormat == format && view.parentTexture == src, "mip source view missing");
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromTexture:view sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(2, 2, 1) toTexture:dst destinationSlice:0 destinationLevel:1
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        Check(cb.status == MTLCommandBufferStatusCompleted, "mip copy failed");
        uint8_t copied[sizeof(colors)] = {};
        [dst getBytes:copied bytesPerRow:8 fromRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:1];
        Check(memcmp(copied, colors, sizeof(colors)) == 0, "mip copy changed encoded color bytes");
    }
}

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "Metal device unavailable\n");
            return 77;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        Run(device, queue, true);
        Run(device, queue, false);
        RunMipCopy(device, queue, true);
        RunMipCopy(device, queue, false);
        puts("linear/sRGB render target alias and mip copy regressions passed");
    }
}
