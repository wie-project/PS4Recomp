// SPDX-License-Identifier: GPL-2.0-or-later

// libSceJpegEnc, ported from shadPS4 (src/core/libraries/jpeg/jpegenc.cpp): the same create and
// encode parameter checks, handle layout and memory size. shadPS4 stops after the checks and
// reports the whole output buffer as the JPEG without writing it; this port encodes with the
// macOS ImageIO JPEG encoder:
//  * compression_ratio is used as the quality in percent (1..100, 0 counts as 100);
//  * the encoder chooses its own chroma subsampling, and restart_interval / the MJPEG mode
//    are not applied;
//  * a JPEG larger than jpeg_size fails with INVALID_SIZE and nothing is written.

#include "ps4_jpegenc.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define JPEG_ENC_MAX_IMAGE_DIMENSION 0xFFFFu
#define JPEG_ENC_MAX_IMAGE_PITCH 0xFFFFFFFu
#define JPEG_ENC_MAX_IMAGE_SIZE 0x7FFFFFFFu

static void *gaddr(uint8_t *mem, uint64_t addr) {
    return addr ? (void *)((uintptr_t)mem + addr) : NULL;
}

static int32_t validate_create_param(const OrbisJpegEncCreateParam *param) {
    if (!param) return ORBIS_JPEG_ENC_ERROR_INVALID_ADDR;
    if (param->size != sizeof(OrbisJpegEncCreateParam)) return ORBIS_JPEG_ENC_ERROR_INVALID_SIZE;
    if (param->attr != ORBIS_JPEG_ENC_ATTRIBUTE_NONE) return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    return 0;
}

static int32_t validate_encode_param(const OrbisJpegEncEncodeParam *p) {
    if (!p) return ORBIS_JPEG_ENC_ERROR_INVALID_ADDR;
    const bool y8 = p->pixel_format == ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8;
    if (!p->image || (!y8 && (p->image & 3) != 0)) return ORBIS_JPEG_ENC_ERROR_INVALID_ADDR;
    if (!p->jpeg) return ORBIS_JPEG_ENC_ERROR_INVALID_ADDR;
    if (p->image_size == 0 || p->jpeg_size == 0) return ORBIS_JPEG_ENC_ERROR_INVALID_SIZE;
    if (p->image_width > JPEG_ENC_MAX_IMAGE_DIMENSION || p->image_height > JPEG_ENC_MAX_IMAGE_DIMENSION) {
        return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    if (p->image_pitch == 0 || p->image_pitch > JPEG_ENC_MAX_IMAGE_PITCH || (!y8 && (p->image_pitch & 3) != 0)) {
        return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    const uint64_t calculated = (uint64_t)p->image_height * p->image_pitch;
    if (calculated > JPEG_ENC_MAX_IMAGE_SIZE || calculated > p->image_size) return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    if (p->encode_mode != ORBIS_JPEG_ENC_ENCODE_MODE_NORMAL && p->encode_mode != ORBIS_JPEG_ENC_ENCODE_MODE_MJPEG) {
        return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    if (p->color_space != ORBIS_JPEG_ENC_COLOR_SPACE_YCC && p->color_space != ORBIS_JPEG_ENC_COLOR_SPACE_GRAYSCALE) {
        return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    if (p->sampling_type > ORBIS_JPEG_ENC_SAMPLING_TYPE_420) return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    if (p->restart_interval > (int32_t)JPEG_ENC_MAX_IMAGE_DIMENSION) return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    switch (p->pixel_format) {
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_R8G8B8A8:
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_B8G8R8A8:
        if (p->image_pitch >> 2 < p->image_width || p->color_space != ORBIS_JPEG_ENC_COLOR_SPACE_YCC ||
            p->sampling_type == ORBIS_JPEG_ENC_SAMPLING_TYPE_FULL) {
            return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
        }
        break;
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8U8Y8V8:
        if (p->image_pitch >> 1 < ((p->image_width + 1) & ~1u) || p->color_space != ORBIS_JPEG_ENC_COLOR_SPACE_YCC ||
            p->sampling_type == ORBIS_JPEG_ENC_SAMPLING_TYPE_FULL) {
            return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
        }
        break;
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8:
        if (p->image_pitch < p->image_width || p->color_space != ORBIS_JPEG_ENC_COLOR_SPACE_GRAYSCALE ||
            p->sampling_type != ORBIS_JPEG_ENC_SAMPLING_TYPE_FULL) {
            return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
        }
        break;
    default:
        return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    return 0;
}

static OrbisJpegEncHandleInternal *handle_of(uint8_t *mem, uint64_t handle) {
    if (!handle || (handle & 0x1F) != 0) return NULL;
    OrbisJpegEncHandleInternal *h = gaddr(mem, handle);
    return h->handle == handle ? h : NULL;
}

int32_t sceJpegEncQueryMemorySize(const OrbisJpegEncCreateParam *param) {
    const int32_t rc = validate_create_param(param);
    return rc != 0 ? rc : ORBIS_JPEG_ENC_MINIMUM_MEMORY_SIZE;
}

int32_t sceJpegEncCreate(uint8_t *mem, const OrbisJpegEncCreateParam *param, uint64_t memory, uint32_t memory_size,
                         uint64_t *handle) {
    int32_t rc = validate_create_param(param);
    if (rc != 0) return rc;
    if (!memory) return ORBIS_JPEG_ENC_ERROR_INVALID_ADDR;
    if (memory_size < ORBIS_JPEG_ENC_MINIMUM_MEMORY_SIZE) return ORBIS_JPEG_ENC_ERROR_INVALID_SIZE;
    if (!handle) return ORBIS_JPEG_ENC_ERROR_INVALID_ADDR;
    const uint64_t aligned = (memory + 0x1f) & ~0x1fULL;
    OrbisJpegEncHandleInternal *h = gaddr(mem, aligned);
    h->handle = aligned;
    h->handle_size = sizeof(uint64_t);
    *handle = aligned;
    return 0;
}

int32_t sceJpegEncDelete(uint8_t *mem, uint64_t handle) {
    OrbisJpegEncHandleInternal *h = handle_of(mem, handle);
    if (!h) return ORBIS_JPEG_ENC_ERROR_INVALID_HANDLE;
    h->handle = 0;
    return 0;
}

static uint8_t clamp_u8(int32_t v) {
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

// Y0 U Y1 V pairs (JFIF full-range BT.601) to R,G,B,X.
static void yuyv_to_rgbx(const uint8_t *src, uint32_t pitch, uint32_t width, uint32_t height, uint8_t *dst) {
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *s = src + (size_t)y * pitch;
        uint8_t *d = dst + (size_t)y * width * 4;
        for (uint32_t x = 0; x < width; x++) {
            const uint8_t *pair = s + (x & ~1u) * 2;
            const int32_t yy = pair[(x & 1) * 2] << 16;
            const int32_t u = pair[1] - 128;
            const int32_t v = pair[3] - 128;
            // 16.16 fixed point: 1.402, 0.344136, 0.714136, 1.772; +0x8000 rounds.
            d[x * 4 + 0] = clamp_u8((yy + 91881 * v + 0x8000) >> 16);
            d[x * 4 + 1] = clamp_u8((yy - 22554 * u - 46802 * v + 0x8000) >> 16);
            d[x * 4 + 2] = clamp_u8((yy + 116130 * u + 0x8000) >> 16);
            d[x * 4 + 3] = 0xFF;
        }
    }
}

// A CGImage over the guest pixels (YUYV goes through *converted, which the caller frees).
static CGImageRef create_source_image(const OrbisJpegEncEncodeParam *p, const uint8_t *pixels, uint8_t **converted) {
    size_t bits_per_pixel = 32;
    size_t pitch = p->image_pitch;
    size_t bytes = (size_t)p->image_height * p->image_pitch;
    CGBitmapInfo info = (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big;
    CGColorSpaceRef cs = NULL;
    switch (p->pixel_format) {
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_B8G8R8A8:
        info = (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little;
        cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        break;
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8:
        bits_per_pixel = 8;
        info = (CGBitmapInfo)kCGImageAlphaNone;
        cs = CGColorSpaceCreateWithName(kCGColorSpaceGenericGrayGamma2_2);
        break;
    case ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8U8Y8V8:
        *converted = malloc((size_t)p->image_width * p->image_height * 4);
        if (!*converted) return NULL;
        yuyv_to_rgbx(pixels, p->image_pitch, p->image_width, p->image_height, *converted);
        pixels = *converted;
        pitch = (size_t)p->image_width * 4;
        bytes = pitch * p->image_height;
        cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        break;
    default:
        cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        break;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, bytes, NULL);
    CGImageRef img = provider && cs ? CGImageCreate(p->image_width, p->image_height, 8, bits_per_pixel, pitch, cs, info,
                                                    provider, NULL, false, kCGRenderingIntentDefault)
                                    : NULL;
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(cs);
    return img;
}

int32_t sceJpegEncEncode(uint8_t *mem, uint64_t handle, const OrbisJpegEncEncodeParam *param,
                         OrbisJpegEncOutputInfo *output_info) {
    if (!handle_of(mem, handle)) return ORBIS_JPEG_ENC_ERROR_INVALID_HANDLE;
    const int32_t valid = validate_encode_param(param);
    if (valid != 0) return valid;
    if (param->image_width == 0 || param->image_height == 0) return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;

    uint8_t *converted = NULL;
    CGImageRef img = create_source_image(param, gaddr(mem, param->image), &converted);
    if (!img) {
        free(converted);
        return ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    }
    CFMutableDataRef out = CFDataCreateMutable(kCFAllocatorDefault, 0);
    CGImageDestinationRef dest = out ? CGImageDestinationCreateWithData(out, CFSTR("public.jpeg"), 1, NULL) : NULL;
    int32_t rc = ORBIS_JPEG_ENC_ERROR_INVALID_PARAM;
    if (dest) {
        const uint32_t ratio = param->compression_ratio == 0 ? 100 : param->compression_ratio;
        const double quality = (ratio > 100 ? 100 : ratio) / 100.0;
        CFNumberRef q = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &quality);
        const void *keys[] = {kCGImageDestinationLossyCompressionQuality};
        const void *vals[] = {q};
        CFDictionaryRef props = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks);
        CGImageDestinationAddImage(dest, img, props);
        const bool ok = CGImageDestinationFinalize(dest);
        CFRelease(props);
        CFRelease(q);
        CFRelease(dest);
        if (ok) {
            const CFIndex len = CFDataGetLength(out);
            if ((uint64_t)len > param->jpeg_size) {
                rc = ORBIS_JPEG_ENC_ERROR_INVALID_SIZE;
            } else {
                memcpy(gaddr(mem, param->jpeg), CFDataGetBytePtr(out), (size_t)len);
                if (output_info) {
                    output_info->size = (uint32_t)len;
                    output_info->height = param->image_height;
                }
                rc = 0;
            }
        }
    }
    if (out) CFRelease(out);
    CGImageRelease(img);
    free(converted);
    return rc;
}

// Shims: parameter structures are read from guest memory; NULL pointers stay NULL.
void shim_sceJpegEncQueryMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegEncQueryMemorySize(gaddr(ctx->mem_base, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceJpegEncCreate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegEncCreate(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi), ctx->rsi,
                                                   (uint32_t)ctx->rdx, gaddr(ctx->mem_base, ctx->rcx));
    SHIM_RETURN();
}

void shim_sceJpegEncDelete(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegEncDelete(ctx->mem_base, ctx->rdi);
    SHIM_RETURN();
}

void shim_sceJpegEncEncode(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegEncEncode(ctx->mem_base, ctx->rdi, gaddr(ctx->mem_base, ctx->rsi),
                                                   gaddr(ctx->mem_base, ctx->rdx));
    SHIM_RETURN();
}
