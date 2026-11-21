// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_gnm_backend_metal3.h"
#include "ps4_gnm_resources.h"
#include "ps4_hitch.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

extern "C" int32_t sceKernelIsNeoMode(void) { return 1; }
extern "C" void ps4_gnm_hitch_note_upload(double) {}
extern "C" uint64_t ps4_hitch_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
extern "C" void ps4_hitch_add_ns(int, uint64_t) {}

static void Check(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main() {
    using namespace ps4gnm;
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device || !device.supportsBCTextureCompression) return 77;
        Metal3Backend be;
        Check(be.Init(device, nullptr), "Metal backend initialization");
        std::vector<uint8_t> memory(1 << 20);
        TextureCache cache(device, be.SideQueue(), memory.data(), memory.size());

        ImageDesc image;
        image.valid = true;
        image.address = 65536;
        image.width = image.pitch = 128;
        image.height = 64;
        image.num_levels = 7;
        image.last_level = image.num_levels - 1;
        image.type = ImageType::Color2D;
        image.mtl_texture_type = MTLTextureType2D;
        image.tile_mode = TileMode::Thin1DThin;
        image.dfmt = DataFormat::FormatBc3;
        image.nfmt = NumberFormat::Srgb;
        image.pixel = GetPixelFormat(image.dfmt, image.nfmt);
        SurfaceLayout layout{};
        Check(ComputeSurfaceLayout(image.ToSurfaceDesc(), &layout), "BC3 layout");

        id<MTLTexture> sources[kMaxMips];
        id<MTLTexture> actual[kMaxMips];
        std::vector<uint8_t> expected[kMaxMips];
        uint64_t addresses[kMaxMips] = {};
        for (uint32_t m = 0; m < layout.num_mips; ++m) {
            const auto& l = layout.mips[m];
            ImageDesc target = image;
            target.address += l.offset;
            target.pitch = l.pitch;
            target.width = (std::max(1u, image.width >> m) + 3) / 4;
            target.height = (std::max(1u, image.height >> m) + 3) / 4;
            target.num_levels = 1;
            target.last_level = 0;
            target.alt_tile_mode = true; // Neo's alternate macro layout does not change 1D tiling.
            target.dfmt = DataFormat::Format32_32_32_32;
            target.nfmt = NumberFormat::Uint;
            target.pixel = GetPixelFormat(target.dfmt, target.nfmt);
            actual[m] = cache.GetStorageTexture(target);
            Check(actual[m] != nil, "packed target allocation");
            expected[m].resize(target.width * target.height * 16);
            for (size_t i = 0; i < expected[m].size(); ++i) expected[m][i] = (i * 73 + (i / 16) * 19 + m * 37) & 255;
            [actual[m] replaceRegion:MTLRegionMake2D(0, 0, target.width, target.height)
                         mipmapLevel:0 withBytes:expected[m].data() bytesPerRow:target.width * 16];
            if (m + 1 < layout.num_mips)
                Check(cache.FindPackedMipTargets(image, sources) == 0, "incomplete chain accepted");
        }
        Check(cache.FindPackedMipTargets(image, sources, addresses) == layout.num_mips, "packed chain not found");
        auto other = image;
        other.pitch *= 2;
        Check(cache.FindPackedMipTargets(other, sources) == 0, "incompatible pitch accepted");
        Check(cache.FindPackedMipTargets(image, sources, addresses) == layout.num_mips, "packed chain lost");

        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBC3_RGBA_sRGB
                                                                                    width:image.width height:image.height mipmapped:YES];
        td.mipmapLevelCount = image.num_levels;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> tex = [device newTextureWithDescriptor:td];
        id<MTLBuffer> staging = [device newBufferWithLength:expected[0].size() options:MTLResourceStorageModePrivate];
        Check(tex && staging, "compressed destination or staging allocation");
        be.BeginCmd(GnmCmdTarget::Upload, @"packed mip regression", true);
        be.UseTarget(GnmCmdTarget::Upload);
        be.BeginCopies();
        for (uint32_t m = 0; m < layout.num_mips; ++m) {
            Check(sources[m] == actual[m] && addresses[m] == image.address + layout.mips[m].offset,
                  "wrong packed mip source");
            be.CopyPackedMipLevel(sources[m], staging, tex, m,
                                  MTLSizeMake(std::max(1u, image.width >> m), std::max(1u, image.height >> m), 1));
        }
        be.EndEncoder();
        be.CommitUpload();
        id<MTLCommandBuffer> wait = [be.SideQueue() commandBuffer];
        [wait commit];
        [wait waitUntilCompleted];
        for (uint32_t m = 0; m < layout.num_mips; ++m) {
            std::vector<uint8_t> bytes(expected[m].size());
            [tex getBytes:bytes.data() bytesPerRow:sources[m].width * 16
               fromRegion:MTLRegionMake2D(0, 0, std::max(1u, image.width >> m), std::max(1u, image.height >> m)) mipmapLevel:m];
            Check(bytes == expected[m], "compressed mip differs from rendered block bytes");
        }
        puts("packed render-target mip chain copied byte-for-byte");
    }
}
