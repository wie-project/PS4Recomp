// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4 GNM Metal Graphics Backend for PS4Recomp.
 * Translates AMD GCN Liverpool PM4 command streams directly to Apple Metal API.
 */

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "ps4_gnm_metal.h"
#include "ps4_gnmdriver.h"
#include "ps4_metal_screen.h"
#include "spirv_msl.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

// PM4 Liverpool Opcode constants
#define PM4_OP_NOP                         0x10
#define PM4_OP_SET_BASE                    0x11
#define PM4_OP_CLEAR_STATE                 0x12
#define PM4_OP_INDEX_BUFFER_SIZE           0x13
#define PM4_OP_DISPATCH_DIRECT             0x15
#define PM4_OP_DISPATCH_INDIRECT           0x16
#define PM4_OP_DRAW_INDIRECT               0x24
#define PM4_OP_DRAW_INDEX_INDIRECT         0x25
#define PM4_OP_INDEX_BASE                  0x26
#define PM4_OP_DRAW_INDEX_2                0x27
#define PM4_OP_CONTEXT_CONTROL             0x28
#define PM4_OP_INDEX_TYPE                  0x2A
#define PM4_OP_DRAW_INDIRECT_MULTI         0x2C
#define PM4_OP_DRAW_INDEX_AUTO             0x2D
#define PM4_OP_NUM_INSTANCES               0x2F
#define PM4_OP_DRAW_INDEX_OFFSET_2         0x35
#define PM4_OP_WRITE_DATA                  0x37
#define PM4_OP_DRAW_INDEX_INDIRECT_MULTI   0x38
#define PM4_OP_WAIT_REG_MEM                0x3C
#define PM4_OP_EVENT_WRITE                 0x46
#define PM4_OP_EVENT_WRITE_EOP             0x47
#define PM4_OP_SET_CONFIG_REG              0x68
#define PM4_OP_SET_CONTEXT_REG             0x69
#define PM4_OP_SET_SH_REG                  0x76
#define PM4_OP_SET_UCONFIG_REG             0x79

// Sea Islands GCN Register Base Offsets
#define CONTEXT_REG_BASE                   0xA000
#define SH_REG_BASE                        0x2C00
#define CONFIG_REG_BASE                    0x2000
#define UCONFIG_REG_BASE                   0xC000

// Context Register Offsets (relative to 0xA000)
#define CTX_REG_PA_SC_WINDOW_SCISSOR_TL    0x0200
#define CTX_REG_PA_SC_WINDOW_SCISSOR_BR    0x0201
#define CTX_REG_PA_CL_VPORT_XSCALE         0x0208
#define CTX_REG_PA_CL_VPORT_XOFFSET        0x0209
#define CTX_REG_PA_CL_VPORT_YSCALE         0x020A
#define CTX_REG_PA_CL_VPORT_YOFFSET        0x020B
#define CTX_REG_PA_CL_VPORT_ZSCALE         0x020C
#define CTX_REG_PA_CL_VPORT_ZOFFSET        0x020D
#define CTX_REG_CB_BLEND0_CONTROL          0x01E0
#define CTX_REG_CB_COLOR_CONTROL           0x01E8
#define CTX_REG_DB_DEPTH_CONTROL           0x0203
#define CTX_REG_VGT_PRIMITIVE_TYPE         0x02A1
#define CTX_REG_CB_COLOR0_BASE             0x0100

// SH Register Offsets (relative to 0x2C00)
#define SH_REG_SPI_SHADER_PGM_LO_PS        0x0008
#define SH_REG_SPI_SHADER_PGM_HI_PS        0x0009
#define SH_REG_SPI_SHADER_USER_DATA_PS_0   0x000C
#define SH_REG_SPI_SHADER_PGM_LO_VS        0x0048
#define SH_REG_SPI_SHADER_PGM_HI_VS        0x0049
#define SH_REG_SPI_SHADER_USER_DATA_VS_0   0x004C
#define SH_REG_COMPUTE_PGM_LO              0x0200
#define SH_REG_COMPUTE_PGM_HI              0x0201
#define SH_REG_COMPUTE_USER_DATA_0         0x0204

// Embedded Metal Shading Language source for built-in baseline and detiling pipelines
static const char *s_default_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float4 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
};

struct RasterizerData {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
};

vertex RasterizerData default_vertex(uint vid [[vertex_id]],
                                    constant float4 *positions [[buffer(0)]],
                                    constant float4 *colors [[buffer(1)]],
                                    constant float2 *uvs [[buffer(2)]]) {
    RasterizerData out;
    if (positions) {
        out.position = positions[vid];
    } else {
        out.position = float4(0.0, 0.0, 0.0, 1.0);
    }
    out.color = colors ? colors[vid] : float4(1.0, 1.0, 1.0, 1.0);
    out.texcoord = uvs ? uvs[vid] : float2(0.0, 0.0);
    return out;
}

fragment float4 default_fragment(RasterizerData in [[stage_in]],
                                 texture2d<float> colorTexture [[texture(0)]],
                                 sampler textureSampler [[sampler(0)]]) {
    if (!is_null_texture(colorTexture)) {
        return in.color * colorTexture.sample(textureSampler, in.texcoord);
    }
    return in.color;
}

// Detiling kernel for AMD GCN 2D micro-tile layout to linear BGRA8
kernel void gcn_detile_kernel(device const uint32_t *src_tiled [[buffer(0)]],
                              texture2d<float, access::write> dst_texture [[texture(0)]],
                              constant uint2 &dimensions [[buffer(1)]],
                              uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= dimensions.x || gid.y >= dimensions.y) return;
    uint x = gid.x;
    uint y = gid.y;
    uint tile_x = x / 8;
    uint tile_y = y / 8;
    uint in_x = x % 8;
    uint in_y = y % 8;
    uint tiles_per_row = (dimensions.x + 7) / 8;
    uint tile_idx = tile_y * tiles_per_row + tile_x;
    uint morton = ((in_x & 1) << 0) | ((in_y & 1) << 1) |
                  ((in_x & 2) << 1) | ((in_y & 2) << 2) |
                  ((in_x & 4) << 2) | ((in_y & 4) << 3);
    uint pixel_idx = tile_idx * 64 + morton;
    uint32_t raw_pixel = src_tiled[pixel_idx];
    float b = float((raw_pixel >> 0) & 0xFF) / 255.0;
    float g = float((raw_pixel >> 8) & 0xFF) / 255.0;
    float r = float((raw_pixel >> 16) & 0xFF) / 255.0;
    float a = float((raw_pixel >> 24) & 0xFF) / 255.0;
    dst_texture.write(float4(r, g, b, a), gid);
}
)";

namespace {

struct GnmMetalState {
    std::mutex mutex;
    bool initialized = false;

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> cmdQueue = nil;
    id<MTLLibrary> defaultLibrary = nil;
    id<MTLRenderPipelineState> defaultRenderPipeline = nil;
    id<MTLComputePipelineState> detileComputePipeline = nil;
    id<MTLDepthStencilState> defaultDepthStencilState = nil;

    id<MTLCommandBuffer> currentCmdBuffer = nil;
    id<MTLRenderCommandEncoder> currentRenderEncoder = nil;
    id<MTLComputeCommandEncoder> currentComputeEncoder = nil;

    // Viewport & Scissor
    MTLViewport viewport{0.0, 0.0, 1920.0, 1080.0, 0.0, 1.0};
    MTLScissorRect scissor{0, 0, 1920, 1080};

    // State Tracking
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    MTLIndexType indexType = MTLIndexTypeUInt16;
    uint64_t indexBufferGpuAddr = 0;
    uint32_t indexBufferSize = 0;

    // Shader addresses
    uint64_t vsGpuAddr = 0;
    uint64_t psGpuAddr = 0;
    uint64_t csGpuAddr = 0;

    // User Data SGPRs
    uint64_t vsUserData[16] = {0};
    uint64_t psUserData[16] = {0};
    uint64_t csUserData[16] = {0};

    // Render Target & Depth
    uint64_t colorBufferBase[8] = {0};
    uint32_t colorBufferInfo[8] = {0};
    uint64_t depthBufferBase = 0;

    // Blend & Depth Controls
    BOOL blendEnable = NO;
    MTLBlendFactor srcRgbFactor = MTLBlendFactorOne;
    MTLBlendFactor dstRgbFactor = MTLBlendFactorZero;
    MTLBlendOperation rgbOp = MTLBlendOperationAdd;
    MTLBlendFactor srcAlphaFactor = MTLBlendFactorOne;
    MTLBlendFactor dstAlphaFactor = MTLBlendFactorZero;
    MTLBlendOperation alphaOp = MTLBlendOperationAdd;
    MTLColorWriteMask writeMask = MTLColorWriteMaskAll;

    BOOL depthWriteEnable = NO;
    MTLCompareFunction depthCompareFunc = MTLCompareFunctionAlways;

    // Pipeline cache
    std::unordered_map<uint64_t, id<MTLRenderPipelineState>> renderPipelineCache;
    std::unordered_map<uint64_t, id<MTLComputePipelineState>> computePipelineCache;
};

static GnmMetalState g_state;

static MTLBlendFactor TranslateBlendFactor(uint32_t factor) {
    switch (factor) {
    case 0: return MTLBlendFactorZero;
    case 1: return MTLBlendFactorOne;
    case 2: return MTLBlendFactorSourceColor;
    case 3: return MTLBlendFactorOneMinusSourceColor;
    case 4: return MTLBlendFactorSourceAlpha;
    case 5: return MTLBlendFactorOneMinusSourceAlpha;
    case 6: return MTLBlendFactorDestinationAlpha;
    case 7: return MTLBlendFactorOneMinusDestinationAlpha;
    case 8: return MTLBlendFactorDestinationColor;
    case 9: return MTLBlendFactorOneMinusDestinationColor;
    case 10: return MTLBlendFactorSourceAlphaSaturated;
    case 13: return MTLBlendFactorBlendColor;
    case 14: return MTLBlendFactorOneMinusBlendColor;
    case 19: return MTLBlendFactorBlendAlpha;
    case 20: return MTLBlendFactorOneMinusBlendAlpha;
    default: return MTLBlendFactorOne;
    }
}

static MTLBlendOperation TranslateBlendOp(uint32_t op) {
    switch (op) {
    case 0: return MTLBlendOperationAdd;
    case 1: return MTLBlendOperationSubtract;
    case 2: return MTLBlendOperationMin;
    case 3: return MTLBlendOperationMax;
    case 4: return MTLBlendOperationReverseSubtract;
    default: return MTLBlendOperationAdd;
    }
}

static MTLCompareFunction TranslateCompareFunc(uint32_t func) {
    switch (func) {
    case 0: return MTLCompareFunctionNever;
    case 1: return MTLCompareFunctionLess;
    case 2: return MTLCompareFunctionEqual;
    case 3: return MTLCompareFunctionLessEqual;
    case 4: return MTLCompareFunctionGreater;
    case 5: return MTLCompareFunctionNotEqual;
    case 6: return MTLCompareFunctionGreaterEqual;
    case 7: return MTLCompareFunctionAlways;
    default: return MTLCompareFunctionAlways;
    }
}

static MTLPrimitiveType TranslatePrimitiveType(uint32_t type) {
    switch (type) {
    case 1: return MTLPrimitiveTypePoint;
    case 2: return MTLPrimitiveTypeLine;
    case 3: return MTLPrimitiveTypeLineStrip;
    case 4: return MTLPrimitiveTypeTriangle;
    case 6: return MTLPrimitiveTypeTriangleStrip;
    case 17: return MTLPrimitiveTypeTriangle;
    default: return MTLPrimitiveTypeTriangle;
    }
}

// Zero-copy Apple Silicon unified memory wrapper
static id<MTLBuffer> WrapGuestMemory(GuestContext *ctx, uint64_t guest_addr, size_t size, size_t *out_offset) {
    if (!ctx || !ctx->mem_base || guest_addr == 0 || size == 0) return nil;
    static long s_page_size = sysconf(_SC_PAGESIZE);
    if (s_page_size <= 0) s_page_size = 16384;

    uint64_t page_mask = (uint64_t)s_page_size - 1;
    uint64_t aligned_addr = guest_addr & ~page_mask;
    size_t offset = (size_t)(guest_addr - aligned_addr);
    size_t aligned_len = (size + offset + page_mask) & ~page_mask;

    if (out_offset) *out_offset = offset;
    void *host_ptr = ctx->mem_base + aligned_addr;

    return [g_state.device newBufferWithBytesNoCopy:host_ptr
                                             length:aligned_len
                                            options:MTLResourceStorageModeShared
                                        deallocator:nil];
}

} // namespace

int ps4_gnm_metal_init(GuestContext *ctx) {
    (void)ctx;
    std::scoped_lock lock(g_state.mutex);
    if (g_state.initialized) return 0;

    @autoreleasepool {
        g_state.device = (__bridge id<MTLDevice>)ps4_metal_screen_get_device();
        if (!g_state.device) {
            g_state.device = MTLCreateSystemDefaultDevice();
        }
        if (!g_state.device) {
            fprintf(stderr, "[ps4-gnm-metal] Failed to obtain Metal device\n");
            return -1;
        }

        g_state.cmdQueue = (__bridge id<MTLCommandQueue>)ps4_metal_screen_get_command_queue();
        if (!g_state.cmdQueue) {
            g_state.cmdQueue = [g_state.device newCommandQueue];
        }

        // Compile built-in default baseline library
        NSError *error = nil;
        NSString *mslSource = [NSString stringWithUTF8String:s_default_msl];
        g_state.defaultLibrary = [g_state.device newLibraryWithSource:mslSource options:nil error:&error];
        if (!g_state.defaultLibrary) {
            fprintf(stderr, "[ps4-gnm-metal] Failed to compile default MSL shaders: %s\n",
                    error.localizedDescription.UTF8String);
            return -1;
        }

        id<MTLFunction> vertFunc = [g_state.defaultLibrary newFunctionWithName:@"default_vertex"];
        id<MTLFunction> fragFunc = [g_state.defaultLibrary newFunctionWithName:@"default_fragment"];

        MTLRenderPipelineDescriptor *pDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pDesc.vertexFunction = vertFunc;
        pDesc.fragmentFunction = fragFunc;
        pDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pDesc.colorAttachments[0].blendingEnabled = NO;

        g_state.defaultRenderPipeline = [g_state.device newRenderPipelineStateWithDescriptor:pDesc error:&error];
        if (!g_state.defaultRenderPipeline) {
            fprintf(stderr, "[ps4-gnm-metal] Failed to create default render pipeline: %s\n",
                    error.localizedDescription.UTF8String);
            return -1;
        }

        // Compile detile compute kernel
        id<MTLFunction> detileKernel = [g_state.defaultLibrary newFunctionWithName:@"gcn_detile_kernel"];
        if (detileKernel) {
            g_state.detileComputePipeline = [g_state.device newComputePipelineStateWithFunction:detileKernel error:&error];
        }

        // Default Depth-Stencil State
        MTLDepthStencilDescriptor *dsDesc = [[MTLDepthStencilDescriptor alloc] init];
        dsDesc.depthWriteEnabled = NO;
        dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
        g_state.defaultDepthStencilState = [g_state.device newDepthStencilStateWithDescriptor:dsDesc];

        g_state.initialized = true;
        fprintf(stderr, "[ps4-gnm-metal] Native Apple Metal GNM Command Processor initialized (%s)\n",
                g_state.device.name.UTF8String);
    }
    return 0;
}

static void EnsureRenderEncoder(GuestContext *ctx) {
    (void)ctx;
    if (g_state.currentRenderEncoder) return;

    if (g_state.currentComputeEncoder) {
        [g_state.currentComputeEncoder endEncoding];
        g_state.currentComputeEncoder = nil;
    }

    if (!g_state.currentCmdBuffer) {
        g_state.currentCmdBuffer = [g_state.cmdQueue commandBuffer];
    }

    id<MTLTexture> screenTex = (__bridge id<MTLTexture>)ps4_metal_screen_get_texture();
    if (!screenTex) return;

    MTLRenderPassDescriptor *passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = screenTex;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    g_state.currentRenderEncoder = [g_state.currentCmdBuffer renderCommandEncoderWithDescriptor:passDesc];
    if (g_state.currentRenderEncoder) {
        [g_state.currentRenderEncoder setViewport:g_state.viewport];
        [g_state.currentRenderEncoder setScissorRect:g_state.scissor];
        [g_state.currentRenderEncoder setRenderPipelineState:g_state.defaultRenderPipeline];
        if (g_state.defaultDepthStencilState) {
            [g_state.currentRenderEncoder setDepthStencilState:g_state.defaultDepthStencilState];
        }
    }
}

static id<MTLRenderPipelineState> GetOrCreateRenderPipeline(GuestContext *ctx) {
    uint64_t key = g_state.vsGpuAddr ^ (g_state.psGpuAddr << 24) ^
                  ((uint64_t)g_state.blendEnable << 48) ^
                  ((uint64_t)g_state.primitiveType << 52);

    auto it = g_state.renderPipelineCache.find(key);
    if (it != g_state.renderPipelineCache.end()) {
        return it->second;
    }

    // Attempt SPIRV-Cross translation if SPIR-V binary is present at shader address
    if (ctx && ctx->mem_base && g_state.vsGpuAddr != 0) {
        const uint32_t *magic = (const uint32_t *)(ctx->mem_base + g_state.vsGpuAddr);
        if (*magic == 0x07230203) { // SPIR-V Magic Number
            try {
                // If a SPIR-V binary was emitted, translate directly to MSL
                spirv_cross::CompilerMSL msl(magic, 512);
                spirv_cross::CompilerMSL::Options options;
                options.set_msl_version(2, 3);
                msl.set_msl_options(options);
                std::string mslSource = msl.compile();

                NSError *err = nil;
                id<MTLLibrary> lib = [g_state.device newLibraryWithSource:[NSString stringWithUTF8String:mslSource.c_str()]
                                                                 options:nil
                                                                   error:&err];
                if (lib) {
                    id<MTLFunction> vert = [lib newFunctionWithName:@"main0"];
                    if (vert) {
                        MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
                        desc.vertexFunction = vert;
                        desc.fragmentFunction = [g_state.defaultLibrary newFunctionWithName:@"default_fragment"];
                        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                        desc.colorAttachments[0].blendingEnabled = g_state.blendEnable;
                        if (g_state.blendEnable) {
                            desc.colorAttachments[0].sourceRGBBlendFactor = g_state.srcRgbFactor;
                            desc.colorAttachments[0].destinationRGBBlendFactor = g_state.dstRgbFactor;
                            desc.colorAttachments[0].rgbBlendOperation = g_state.rgbOp;
                            desc.colorAttachments[0].sourceAlphaBlendFactor = g_state.srcAlphaFactor;
                            desc.colorAttachments[0].destinationAlphaBlendFactor = g_state.dstAlphaFactor;
                            desc.colorAttachments[0].alphaBlendOperation = g_state.alphaOp;
                        }
                        id<MTLRenderPipelineState> pso = [g_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
                        if (pso) {
                            g_state.renderPipelineCache[key] = pso;
                            return pso;
                        }
                    }
                }
            } catch (...) {
                // Fall back smoothly to default pipeline
            }
        }
    }

    g_state.renderPipelineCache[key] = g_state.defaultRenderPipeline;
    return g_state.defaultRenderPipeline;
}

int32_t ps4_gnm_metal_process_command_buffers(GuestContext *ctx, uint32_t count,
                                             const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                             const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes) {
    if (!ctx || count == 0) return 0;
    std::scoped_lock lock(g_state.mutex);
    if (!g_state.initialized) {
        if (ps4_gnm_metal_init(ctx) != 0) return -1;
    }

    @autoreleasepool {
        for (uint32_t b = 0; b < count; b++) {
            // 1. Process CCB (Constant Command Buffer)
            if (ccb_gpu_addrs && ccb_sizes_in_bytes && ccb_gpu_addrs[b] && ccb_sizes_in_bytes[b]) {
                const uint32_t *ccb_words = (const uint32_t *)(ctx->mem_base + ccb_gpu_addrs[b]);
                uint32_t ccb_dw = ccb_sizes_in_bytes[b] / 4;
                uint32_t cpc = 0;
                while (cpc < ccb_dw) {
                    uint32_t header = ccb_words[cpc];
                    uint32_t type = (header >> 30) & 0x3;
                    if (type == 3) {
                        uint32_t opcode = (header >> 8) & 0xFF;
                        uint32_t payload_dw = ((header >> 16) & 0x3FFF) + 1;
                        const uint32_t *payload = &ccb_words[cpc + 1];

                        if (opcode == PM4_OP_WRITE_DATA && payload_dw >= 3) {
                            uint64_t dst_addr = (uint64_t)payload[1] | ((uint64_t)payload[2] << 32);
                            if (dst_addr != 0 && ctx->mem_base) {
                                uint32_t *dst_mem = (uint32_t *)(ctx->mem_base + dst_addr);
                                for (uint32_t w = 3; w < payload_dw; w++) {
                                    *dst_mem++ = payload[w];
                                }
                            }
                        }
                        cpc += 1 + payload_dw;
                    } else {
                        cpc++;
                    }
                }
            }

            // 2. Process DCB (Draw Command Buffer)
            if (dcb_gpu_addrs && dcb_sizes_in_bytes && dcb_gpu_addrs[b] && dcb_sizes_in_bytes[b]) {
                const uint32_t *dcb_words = (const uint32_t *)(ctx->mem_base + dcb_gpu_addrs[b]);
                uint32_t dcb_dw = dcb_sizes_in_bytes[b] / 4;
                uint32_t pc = 0;

                while (pc < dcb_dw) {
                    uint32_t header = dcb_words[pc];
                    uint32_t type = (header >> 30) & 0x3;

                    if (type == 3) {
                        uint32_t opcode = (header >> 8) & 0xFF;
                        uint32_t payload_dw = ((header >> 16) & 0x3FFF) + 1;
                        if (pc + 1 + payload_dw > dcb_dw) break;
                        const uint32_t *payload = &dcb_words[pc + 1];

                        switch (opcode) {
                        case PM4_OP_NOP: {
                            // Marker or flip patch
                            break;
                        }

                        case PM4_OP_CLEAR_STATE: {
                            g_state.viewport = {0.0, 0.0, 1920.0, 1080.0, 0.0, 1.0};
                            g_state.scissor = {0, 0, 1920, 1080};
                            g_state.primitiveType = MTLPrimitiveTypeTriangle;
                            break;
                        }

                        case PM4_OP_SET_CONTEXT_REG: {
                            if (payload_dw >= 1) {
                                uint32_t reg_offset = payload[0] & 0xFFFF;
                                for (uint32_t r = 1; r < payload_dw; r++) {
                                    uint32_t curr_reg = reg_offset + (r - 1);
                                    uint32_t val = payload[r];

                                    if (curr_reg == CTX_REG_PA_CL_VPORT_XSCALE) {
                                        float w = *(float *)&val * 2.0f;
                                        if (w > 0) g_state.viewport.width = w;
                                    } else if (curr_reg == CTX_REG_PA_CL_VPORT_XOFFSET) {
                                        float x = *(float *)&val - (g_state.viewport.width / 2.0f);
                                        g_state.viewport.originX = x >= 0 ? x : 0;
                                    } else if (curr_reg == CTX_REG_PA_CL_VPORT_YSCALE) {
                                        float h = -(*(float *)&val * 2.0f);
                                        if (h > 0) g_state.viewport.height = h;
                                    } else if (curr_reg == CTX_REG_PA_CL_VPORT_YOFFSET) {
                                        float y = *(float *)&val - (g_state.viewport.height / 2.0f);
                                        g_state.viewport.originY = y >= 0 ? y : 0;
                                    } else if (curr_reg == CTX_REG_PA_SC_WINDOW_SCISSOR_BR) {
                                        uint32_t br_x = val & 0x3FFF;
                                        uint32_t br_y = (val >> 16) & 0x3FFF;
                                        if (br_x > g_state.scissor.x) g_state.scissor.width = br_x - g_state.scissor.x;
                                        if (br_y > g_state.scissor.y) g_state.scissor.height = br_y - g_state.scissor.y;
                                    } else if (curr_reg == CTX_REG_PA_SC_WINDOW_SCISSOR_TL) {
                                        g_state.scissor.x = val & 0x3FFF;
                                        g_state.scissor.y = (val >> 16) & 0x3FFF;
                                    } else if (curr_reg == CTX_REG_VGT_PRIMITIVE_TYPE) {
                                        g_state.primitiveType = TranslatePrimitiveType(val & 0x3F);
                                    } else if (curr_reg == CTX_REG_CB_BLEND0_CONTROL) {
                                        g_state.blendEnable = (val & 1) != 0;
                                        g_state.srcRgbFactor = TranslateBlendFactor((val >> 1) & 0x1F);
                                        g_state.dstRgbFactor = TranslateBlendFactor((val >> 6) & 0x1F);
                                        g_state.rgbOp = TranslateBlendOp((val >> 11) & 0x7);
                                        g_state.srcAlphaFactor = TranslateBlendFactor((val >> 16) & 0x1F);
                                        g_state.dstAlphaFactor = TranslateBlendFactor((val >> 21) & 0x1F);
                                        g_state.alphaOp = TranslateBlendOp((val >> 26) & 0x7);
                                    } else if (curr_reg == CTX_REG_DB_DEPTH_CONTROL) {
                                        g_state.depthWriteEnable = (val & 0x1) != 0;
                                        g_state.depthCompareFunc = TranslateCompareFunc((val >> 1) & 0x7);
                                    }
                                }
                            }
                            break;
                        }

                        case PM4_OP_SET_SH_REG: {
                            if (payload_dw >= 1) {
                                uint32_t reg_offset = payload[0] & 0xFFFF;
                                for (uint32_t r = 1; r < payload_dw; r++) {
                                    uint32_t curr_reg = reg_offset + (r - 1);
                                    uint32_t val = payload[r];

                                    if (curr_reg == SH_REG_SPI_SHADER_PGM_LO_VS) {
                                        g_state.vsGpuAddr = (g_state.vsGpuAddr & 0xFFFFFFFF00000000ULL) | (uint64_t)val;
                                    } else if (curr_reg == SH_REG_SPI_SHADER_PGM_HI_VS) {
                                        g_state.vsGpuAddr = (g_state.vsGpuAddr & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
                                    } else if (curr_reg == SH_REG_SPI_SHADER_PGM_LO_PS) {
                                        g_state.psGpuAddr = (g_state.psGpuAddr & 0xFFFFFFFF00000000ULL) | (uint64_t)val;
                                    } else if (curr_reg == SH_REG_SPI_SHADER_PGM_HI_PS) {
                                        g_state.psGpuAddr = (g_state.psGpuAddr & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
                                    } else if (curr_reg >= SH_REG_SPI_SHADER_USER_DATA_VS_0 && curr_reg < SH_REG_SPI_SHADER_USER_DATA_VS_0 + 16) {
                                        g_state.vsUserData[curr_reg - SH_REG_SPI_SHADER_USER_DATA_VS_0] = val;
                                    } else if (curr_reg >= SH_REG_SPI_SHADER_USER_DATA_PS_0 && curr_reg < SH_REG_SPI_SHADER_USER_DATA_PS_0 + 16) {
                                        g_state.psUserData[curr_reg - SH_REG_SPI_SHADER_USER_DATA_PS_0] = val;
                                    }
                                }
                            }
                            break;
                        }

                        case PM4_OP_INDEX_BASE: {
                            if (payload_dw >= 2) {
                                g_state.indexBufferGpuAddr = (uint64_t)payload[0] | ((uint64_t)payload[1] << 32);
                            }
                            break;
                        }

                        case PM4_OP_INDEX_BUFFER_SIZE: {
                            if (payload_dw >= 1) {
                                g_state.indexBufferSize = payload[0];
                            }
                            break;
                        }

                        case PM4_OP_INDEX_TYPE: {
                            if (payload_dw >= 1) {
                                g_state.indexType = (payload[0] == 0) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
                            }
                            break;
                        }

                        case PM4_OP_DRAW_INDEX_2: {
                            if (payload_dw >= 3) {
                                uint32_t indexCount = payload[0];
                                uint64_t indexBase = (uint64_t)payload[1] | ((uint64_t)payload[2] << 32);
                                if (indexBase == 0) indexBase = g_state.indexBufferGpuAddr;

                                EnsureRenderEncoder(ctx);
                                if (g_state.currentRenderEncoder && indexCount > 0 && indexBase != 0) {
                                    size_t ibOffset = 0;
                                    size_t ibBytes = indexCount * (g_state.indexType == MTLIndexTypeUInt16 ? 2 : 4);
                                    id<MTLBuffer> ibBuf = WrapGuestMemory(ctx, indexBase, ibBytes, &ibOffset);

                                    id<MTLRenderPipelineState> pso = GetOrCreateRenderPipeline(ctx);
                                    [g_state.currentRenderEncoder setRenderPipelineState:pso];
                                    [g_state.currentRenderEncoder setViewport:g_state.viewport];
                                    [g_state.currentRenderEncoder setScissorRect:g_state.scissor];

                                    if (ibBuf) {
                                        [g_state.currentRenderEncoder drawIndexedPrimitives:g_state.primitiveType
                                                                                 indexCount:indexCount
                                                                                  indexType:g_state.indexType
                                                                                indexBuffer:ibBuf
                                                                          indexBufferOffset:ibOffset];
                                    }
                                }
                            }
                            break;
                        }

                        case PM4_OP_DRAW_INDEX_AUTO: {
                            if (payload_dw >= 1) {
                                uint32_t vertexCount = payload[0];
                                EnsureRenderEncoder(ctx);
                                if (g_state.currentRenderEncoder && vertexCount > 0) {
                                    id<MTLRenderPipelineState> pso = GetOrCreateRenderPipeline(ctx);
                                    [g_state.currentRenderEncoder setRenderPipelineState:pso];
                                    [g_state.currentRenderEncoder setViewport:g_state.viewport];
                                    [g_state.currentRenderEncoder setScissorRect:g_state.scissor];

                                    [g_state.currentRenderEncoder drawPrimitives:g_state.primitiveType
                                                                     vertexStart:0
                                                                     vertexCount:vertexCount];
                                }
                            }
                            break;
                        }

                        case PM4_OP_DRAW_INDEX_OFFSET_2: {
                            if (payload_dw >= 3) {
                                uint32_t indexCount = payload[0];
                                uint32_t indexOffset = payload[1];
                                EnsureRenderEncoder(ctx);
                                if (g_state.currentRenderEncoder && indexCount > 0 && g_state.indexBufferGpuAddr != 0) {
                                    size_t ibOffset = 0;
                                    size_t ibBytes = (indexCount + indexOffset) * (g_state.indexType == MTLIndexTypeUInt16 ? 2 : 4);
                                    id<MTLBuffer> ibBuf = WrapGuestMemory(ctx, g_state.indexBufferGpuAddr, ibBytes, &ibOffset);

                                    id<MTLRenderPipelineState> pso = GetOrCreateRenderPipeline(ctx);
                                    [g_state.currentRenderEncoder setRenderPipelineState:pso];
                                    [g_state.currentRenderEncoder setViewport:g_state.viewport];
                                    [g_state.currentRenderEncoder setScissorRect:g_state.scissor];

                                    if (ibBuf) {
                                        size_t elemSize = (g_state.indexType == MTLIndexTypeUInt16 ? 2 : 4);
                                        [g_state.currentRenderEncoder drawIndexedPrimitives:g_state.primitiveType
                                                                                 indexCount:indexCount
                                                                                  indexType:g_state.indexType
                                                                                indexBuffer:ibBuf
                                                                          indexBufferOffset:ibOffset + indexOffset * elemSize];
                                    }
                                }
                            }
                            break;
                        }

                        case PM4_OP_DISPATCH_DIRECT: {
                            if (payload_dw >= 3) {
                                uint32_t tgx = payload[0];
                                uint32_t tgy = payload[1];
                                uint32_t tgz = payload[2];

                                if (g_state.currentRenderEncoder) {
                                    [g_state.currentRenderEncoder endEncoding];
                                    g_state.currentRenderEncoder = nil;
                                }

                                if (!g_state.currentComputeEncoder) {
                                    if (!g_state.currentCmdBuffer) {
                                        g_state.currentCmdBuffer = [g_state.cmdQueue commandBuffer];
                                    }
                                    g_state.currentComputeEncoder = [g_state.currentCmdBuffer computeCommandEncoder];
                                }

                                if (g_state.currentComputeEncoder && g_state.detileComputePipeline) {
                                    [g_state.currentComputeEncoder setComputePipelineState:g_state.detileComputePipeline];
                                    MTLSize gridSize = MTLSizeMake(tgx, tgy, tgz);
                                    MTLSize tgSize = MTLSizeMake(8, 8, 1);
                                    [g_state.currentComputeEncoder dispatchThreadgroups:gridSize threadsPerThreadgroup:tgSize];
                                }
                            }
                            break;
                        }

                        case PM4_OP_WRITE_DATA: {
                            if (payload_dw >= 3) {
                                uint64_t dst_addr = (uint64_t)payload[1] | ((uint64_t)payload[2] << 32);
                                if (dst_addr != 0 && ctx->mem_base) {
                                    uint32_t *dst_mem = (uint32_t *)(ctx->mem_base + dst_addr);
                                    for (uint32_t w = 3; w < payload_dw; w++) {
                                        *dst_mem++ = payload[w];
                                    }
                                }
                            }
                            break;
                        }

                        default:
                            break;
                        }

                        pc += 1 + payload_dw;
                    } else if (type == 2) {
                        pc += 1;
                    } else if (type == 0) {
                        uint32_t count_dw = (header >> 16) & 0x3FFF;
                        pc += 1 + count_dw + 1;
                    } else {
                        pc += 1;
                    }
                }
            }
        }

        // Commit active Metal command buffer
        if (g_state.currentRenderEncoder) {
            [g_state.currentRenderEncoder endEncoding];
            g_state.currentRenderEncoder = nil;
        }
        if (g_state.currentComputeEncoder) {
            [g_state.currentComputeEncoder endEncoding];
            g_state.currentComputeEncoder = nil;
        }
        if (g_state.currentCmdBuffer) {
            [g_state.currentCmdBuffer commit];
            g_state.currentCmdBuffer = nil;
        }
    }
    return 0;
}

void ps4_gnm_metal_wait_gpu_idle(void) {
    std::scoped_lock lock(g_state.mutex);
    if (g_state.currentCmdBuffer) {
        if (g_state.currentRenderEncoder) {
            [g_state.currentRenderEncoder endEncoding];
            g_state.currentRenderEncoder = nil;
        }
        if (g_state.currentComputeEncoder) {
            [g_state.currentComputeEncoder endEncoding];
            g_state.currentComputeEncoder = nil;
        }
        [g_state.currentCmdBuffer commit];
        [g_state.currentCmdBuffer waitUntilCompleted];
        g_state.currentCmdBuffer = nil;
    }
}

void ps4_gnm_metal_destroy(void) {
    std::scoped_lock lock(g_state.mutex);
    if (!g_state.initialized) return;

    ps4_gnm_metal_wait_gpu_idle();

    @autoreleasepool {
        g_state.renderPipelineCache.clear();
        g_state.computePipelineCache.clear();
        g_state.defaultDepthStencilState = nil;
        g_state.defaultRenderPipeline = nil;
        g_state.detileComputePipeline = nil;
        g_state.defaultLibrary = nil;
        g_state.cmdQueue = nil;
        g_state.device = nil;
        g_state.initialized = false;
    }
}
