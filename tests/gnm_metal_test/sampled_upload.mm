// SPDX-License-Identifier: GPL-2.0-or-later
// Repeated CPU updates must reach GPU readers, including padded rows and truncated inputs.
#import <Metal/Metal.h>
#include "ps4_gnm_resources.h"
#include "ps4_hitch.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

// Match the PS4 Pro mode used by the retail-game performance runs.
extern "C" int32_t sceKernelIsNeoMode(void) { return 1; }
static std::atomic<uint64_t> hitch_ns[PS4_HITCH_KINDS];
static double upload_ms;
extern "C" void ps4_gnm_hitch_note_upload(double ms) { upload_ms += ms; }
extern "C" uint64_t ps4_hitch_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
extern "C" void ps4_hitch_add_ns(int kind, uint64_t ns) { hitch_ns[kind].fetch_add(ns); }
extern "C" uint64_t ps4_hitch_take_ns(int kind) { return hitch_ns[kind].exchange(0); }

static void Check(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}

static void CheckMipsAndSlices(id<MTLDevice> device, id<MTLCommandQueue> queue) {
    using namespace ps4gnm;
    for (MTLTextureType type : {MTLTextureType2DArray, MTLTextureType3D, MTLTextureTypeCube}) {
        ImageDesc image;
        image.valid = true; image.address = 65536;
        image.width = image.height = image.pitch = 16;
        image.depth = type == MTLTextureType3D ? 3 : 1;
        image.num_layers = type == MTLTextureTypeCube ? 6 : (type == MTLTextureType2DArray ? 3 : 1);
        image.layout_layers = type == MTLTextureType2DArray ? 4 : image.num_layers;
        image.num_levels = 3;
        image.type = type == MTLTextureType3D ? ImageType::Color3D :
                     (type == MTLTextureTypeCube ? ImageType::Cube : ImageType::Color2DArray);
        image.tile_mode = TileMode::DisplayLinearAligned;
        image.dfmt = DataFormat::Format8_8_8_8; image.nfmt = NumberFormat::Unorm;
        image.pixel = GetPixelFormat(image.dfmt, image.nfmt);
        image.mtl_texture_type = type;
        const SurfaceDesc surface = image.ToSurfaceDesc();
        SurfaceLayout layout;
        Check(ComputeSurfaceLayout(surface, &layout), "mip/slice layout unavailable");
        std::vector<uint8_t> memory(image.address + layout.total_size);
        for (size_t i = image.address; i < memory.size(); ++i) memory[i] = (i * 13 + i / 257) & 255;
        TextureCache cache(device, queue, memory.data(), memory.size());
        id<MTLTexture> texture = cache.GetTexture(image);
        Check(texture != nil, "mip/slice texture missing");
        for (unsigned mip = 0; mip < image.num_levels; ++mip) {
            const unsigned w = image.width >> mip, h = image.height >> mip;
            const unsigned slices = type == MTLTextureType3D ? std::max(1u, image.depth >> mip) : image.num_layers;
            const unsigned row = 256, slice_bytes = row * h;
            id<MTLBuffer> output = [device newBufferWithLength:slice_bytes * slices options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> encoder = [cb blitCommandEncoder];
            for (unsigned slice = 0; slice < (type == MTLTextureType3D ? 1 : slices); ++slice)
                [encoder copyFromTexture:texture sourceSlice:slice sourceLevel:mip
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(w, h, type == MTLTextureType3D ? slices : 1)
                    toBuffer:output destinationOffset:slice * slice_bytes destinationBytesPerRow:row
                    destinationBytesPerImage:slice_bytes];
            [encoder endEncoding]; [cb commit]; [cb waitUntilCompleted];
            Check(cb.status == MTLCommandBufferStatusCompleted, "mip/slice GPU read failed");
            const MipLayout& ml = layout.mips[mip];
            const uint8_t* result = (const uint8_t*)output.contents;
            for (unsigned slice = 0; slice < slices; ++slice)
                for (unsigned y = 0; y < h; ++y)
                    Check(memcmp(result + slice * slice_bytes + y * row,
                        memory.data() + image.address + ml.offset + (slice * ml.height + y) * ml.pitch * 4,
                        w * 4) == 0, "GPU read incorrect mip or slice contents");
        }
    }
}

int main() {
    @autoreleasepool {
        using namespace ps4gnm;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "Metal device unavailable\n");
            return 77;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:@"#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "kernel void pixels(texture2d<float, access::read> src [[texture(0)]], "
            "device uchar4* dst [[buffer(0)]], uint2 p [[thread_position_in_grid]]) { "
            "if (p.x < src.get_width() && p.y < src.get_height()) "
            "dst[p.y * src.get_width() + p.x] = uchar4(round(src.read(p) * 255.0f)); }"
            options:nil error:&error];
        Check(library != nil, "GPU readback shader compilation failed");
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:
            [library newFunctionWithName:@"pixels"] error:&error];
        Check(pipeline != nil, "GPU readback pipeline creation failed");
        constexpr unsigned width = 61, height = 29, pitch = 64;
        id<MTLBuffer> output = [device newBufferWithLength:width * height * 4 options:MTLResourceStorageModeShared];
        for (unsigned variant = 0; variant < 4; ++variant) {
            const TileMode mode = variant & 1 ? TileMode::DisplayLinearGeneral : TileMode::DisplayLinearAligned;
            const unsigned readable_rows = variant < 2 ? height : height - 1;
            std::vector<uint8_t> memory(variant < 2 ? 1 << 20 : 65536 + readable_rows * pitch * 4);
            TextureCache cache(device, queue, memory.data(), memory.size());
            ImageDesc image;
            image.valid = true;
            image.address = 65536;
            image.width = width; image.height = height; image.depth = 1; image.pitch = pitch;
            image.tile_mode = mode;
            image.type = ImageType::Color2D;
            image.dfmt = DataFormat::Format8_8_8_8;
            image.nfmt = NumberFormat::Unorm;
            image.pixel = GetPixelFormat(image.dfmt, image.nfmt);
            image.mtl_texture_type = MTLTextureType2D;
            for (unsigned frame = 0; frame < 8; ++frame) {
                for (unsigned y = 0; y < readable_rows; ++y)
                    for (unsigned x = 0; x < pitch * 4; ++x)
                        memory[image.address + y * pitch * 4 + x] = (x * 17 + y * 31 + frame * 23) & 255;
                id<MTLTexture> texture = cache.GetTexture(image);
                Check(texture != nil, "sampled texture missing");
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                id<MTLComputeCommandEncoder> encoder = [cb computeCommandEncoder];
                [encoder setComputePipelineState:pipeline];
                [encoder setTexture:texture atIndex:0];
                [encoder setBuffer:output offset:0 atIndex:0];
                [encoder dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                [encoder endEncoding]; [cb commit]; [cb waitUntilCompleted];
                Check(cb.status == MTLCommandBufferStatusCompleted, "GPU texture read failed");
                const uint8_t* result = (const uint8_t*)output.contents;
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width * 4; ++x)
                        Check(result[y * width * 4 + x] == (y < readable_rows ? memory[image.address + y * pitch * 4 + x] : 0),
                              "GPU sampled stale or incorrectly laid out CPU texture data");
            }
        }
        CheckMipsAndSlices(device, queue);
        printf("sampled texture CPU updates reached GPU readers (%.3f ms uploading)\n", upload_ms);
    }
}
