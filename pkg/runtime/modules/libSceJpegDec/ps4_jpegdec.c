// SPDX-License-Identifier: GPL-2.0-or-later

// libSceJpegDec, ported from shadPS4 (src/core/libraries/jpeg/jpegdec.cpp): the same parameter
// checks, work memory size, handle layout, header parser and output conversion. shadPS4 decodes
// with stb_image; this port uses the macOS ImageIO JPEG decoder. Differences:
//  * Y8U8Y8V8 output (and format 9, which the parameter mask admits) fails with
//    UNSUPPORT_DATA: shadPS4 accepts them but writes nothing.
//  * Data without a JPEG SOI marker is a DECODE_ERROR (stb_image also decodes PNG, BMP...).

#include "ps4_jpegdec.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void *gaddr(uint8_t *mem, uint64_t addr) {
    return addr ? (void *)((uintptr_t)mem + addr) : NULL;
}

static bool valid_ratio(uint32_t ratio) {
    return ratio == 1 || ratio == 2 || ratio == 4 || ratio == 8;
}

static OrbisJpegDecHandleInternal *handle_of(uint8_t *mem, uint64_t handle) {
    if (!handle || (handle & 0x1F) != 0) return NULL;
    OrbisJpegDecHandleInternal *h = gaddr(mem, handle);
    return h->handle == handle ? h : NULL;
}

int32_t sceJpegDecQueryMemorySize(const OrbisJpegDecCreateParam *param) {
    if (!param) return ORBIS_JPEG_DEC_ERROR_INVALID_ADDR;
    if (param->size != sizeof(OrbisJpegDecCreateParam)) return ORBIS_JPEG_DEC_ERROR_INVALID_SIZE;
    const uint32_t mode = param->decode_mode;
    if (mode < 1 || mode > 2) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    const uint32_t width = param->max_width;
    if (width == 0 || width > 0xFFFF) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    const uint32_t aligned_w = ((width + 0x1f) & ~0x1fu) | 0x10;
    const uint32_t sz = mode == 2 ? (aligned_w << 5) * 5 : (aligned_w << 4) * 3;
    return (int32_t)(sz + aligned_w * 6 + 0x7360);
}

int32_t sceJpegDecCreate(uint8_t *mem, const OrbisJpegDecCreateParam *param, uint64_t memory, uint32_t memory_size,
                         uint64_t *handle) {
    if (!param || !memory) return ORBIS_JPEG_DEC_ERROR_INVALID_ADDR;
    const int32_t req = sceJpegDecQueryMemorySize(param);
    if (req < 0) return req;
    if ((uint32_t)req > memory_size) return ORBIS_JPEG_DEC_ERROR_INVALID_SIZE;
    if (!handle) return ORBIS_JPEG_DEC_ERROR_INVALID_ADDR;
    const uint64_t aligned = (memory + 0x1f) & ~0x1fULL;
    OrbisJpegDecHandleInternal *h = gaddr(mem, aligned);
    h->handle = aligned;
    h->handle_size = sizeof(OrbisJpegDecCreateParam);
    h->decode_mode = param->decode_mode;
    h->max_width = (param->max_width + 0x1f) & ~0x1fu;
    h->stride_or_aux = 0;
    *handle = aligned;
    return 0;
}

int32_t sceJpegDecDelete(uint8_t *mem, uint64_t handle) {
    OrbisJpegDecHandleInternal *h = handle_of(mem, handle);
    if (!h) return ORBIS_JPEG_DEC_ERROR_INVALID_HANDLE;
    h->handle = 0;
    return 0;
}

// Reads the frame header (SOF0/1/2) without decoding, as shadPS4 does.
int32_t sceJpegDecParseHeader(uint8_t *mem, const OrbisJpegDecParseParam *param, OrbisJpegDecImageInfo *image_info) {
    if (!param || !param->jpeg_addr) return ORBIS_JPEG_DEC_ERROR_INVALID_ADDR;
    if (param->jpeg_size == 0) return ORBIS_JPEG_DEC_ERROR_INVALID_SIZE;
    if (param->decode_mode > 1) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    const uint32_t ratio = param->downscale_ratio;
    if (!valid_ratio(ratio)) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    if (!image_info) return ORBIS_JPEG_DEC_ERROR_INVALID_ADDR;

    const uint8_t *ptr = gaddr(mem, param->jpeg_addr);
    const uint32_t size = param->jpeg_size;
    if (size < 4 || ptr[0] != 0xFF || ptr[1] != 0xD8) return ORBIS_JPEG_DEC_ERROR_INVALID_DATA;

    uint32_t width = 0, height = 0;
    uint16_t num_components = 3;
    uint8_t component_id[4] = {1, 2, 3, 0};
    uint8_t sampling_factor[4] = {0x22, 0x11, 0x11, 0};
    uint32_t pos = 2;
    while (pos + 4 < size) {
        if (ptr[pos] != 0xFF) {
            pos++;
            continue;
        }
        const uint8_t marker = ptr[pos + 1];
        if (marker == 0xD9 || marker == 0xDA) break;
        const uint16_t len = (uint16_t)((ptr[pos + 2] << 8) | ptr[pos + 3]);
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (pos + 9 < size) {
                height = ((uint32_t)ptr[pos + 5] << 8) | ptr[pos + 6];
                width = ((uint32_t)ptr[pos + 7] << 8) | ptr[pos + 8];
                num_components = ptr[pos + 9];
                for (uint16_t c = 0; c < num_components && c < 4; ++c) {
                    if (pos + 10 + c * 3 + 1 < size) {
                        component_id[c] = ptr[pos + 10 + c * 3];
                        sampling_factor[c] = ptr[pos + 10 + c * 3 + 1];
                    }
                }
            }
            break;
        }
        pos += 2 + len;
    }
    if (width == 0 || height == 0) return ORBIS_JPEG_DEC_ERROR_DECODE_ERROR;

    uint16_t color_space = ORBIS_JPEG_DEC_COLOR_SPACE_UNKNOWN;
    if (num_components == 1) {
        color_space = ORBIS_JPEG_DEC_COLOR_SPACE_GRAYSCALE;
    } else if (num_components == 3) {
        color_space = ORBIS_JPEG_DEC_COLOR_SPACE_YCC;
    }
    uint32_t required_decode_mode = 0;
    if (color_space == ORBIS_JPEG_DEC_COLOR_SPACE_GRAYSCALE) {
        required_decode_mode = 1;
    } else if (color_space == ORBIS_JPEG_DEC_COLOR_SPACE_YCC) {
        uint8_t max_h = 1;
        for (uint16_t c = 0; c < num_components && c < 4; ++c) {
            const uint8_t hf = sampling_factor[c] >> 4;
            if (hf > max_h) max_h = hf;
        }
        const uint32_t scale = 96 / (max_h * 8u);
        uint32_t total_density = 0;
        for (uint16_t c = 0; c < 3 && c < num_components; ++c) {
            total_density += (uint32_t)(sampling_factor[c] >> 4) * (sampling_factor[c] & 0xF);
        }
        required_decode_mode = total_density * scale < 0x25 ? 1 : 2;
    }

    memset(image_info, 0, sizeof(*image_info));
    image_info->image_width = width;
    image_info->image_height = height;
    image_info->color_space = color_space;
    image_info->num_components = num_components;
    memcpy(image_info->component_id, component_id, sizeof(component_id));
    memcpy(image_info->sampling_factor, sampling_factor, sizeof(sampling_factor));
    image_info->decode_mode = param->decode_mode;
    image_info->required_decode_mode = required_decode_mode;
    image_info->output_width = (width + ratio - 1) / ratio;
    image_info->output_height = (height + ratio - 1) / ratio;
    return 0;
}

// Decodes the first image of a JPEG stream; NULL when ImageIO cannot.
static CGImageRef create_image(const uint8_t *data, uint32_t size) {
    CFDataRef cf = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, data, size, kCFAllocatorNull);
    if (!cf) return NULL;
    CGImageSourceRef src = CGImageSourceCreateWithData(cf, NULL);
    CFRelease(cf);
    if (!src) return NULL;
    CGImageRef img = CGImageSourceGetCount(src) > 0 ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
    CFRelease(src);
    return img;
}

// Draws img into an 8-bit, 4-byte-per-pixel buffer (R,G,B,X or B,G,R,X). Grayscale images go
// through a gray context so that no color matching changes the values; RGB images are drawn
// in their own color space for the same reason.
static bool render_rgbx(CGImageRef img, uint8_t *dst, size_t width, size_t height, size_t pitch, bool bgra) {
    CGColorSpaceRef img_cs = CGImageGetColorSpace(img);
    const CGColorSpaceModel model = img_cs ? CGColorSpaceGetModel(img_cs) : kCGColorSpaceModelUnknown;
    if (model == kCGColorSpaceModelMonochrome) {
        uint8_t *gray = malloc(width * height);
        if (!gray) return false;
        CGContextRef g = CGBitmapContextCreate(gray, width, height, 8, width, img_cs, (CGBitmapInfo)kCGImageAlphaNone);
        if (!g) {
            free(gray);
            return false;
        }
        CGContextSetBlendMode(g, kCGBlendModeCopy);
        CGContextDrawImage(g, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height), img);
        CGContextRelease(g);
        for (size_t y = 0; y < height; y++) {
            uint8_t *row = dst + y * pitch;
            const uint8_t *src = gray + y * width;
            for (size_t x = 0; x < width; x++) {
                row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = src[x];
                row[x * 4 + 3] = 0xFF;
            }
        }
        free(gray);
        return true;
    }
    CGColorSpaceRef cs = model == kCGColorSpaceModelRGB ? CGColorSpaceRetain(img_cs)
                                                        : CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGBitmapInfo info = bgra ? (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little
                                   : (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big;
    CGContextRef c = CGBitmapContextCreate(dst, width, height, 8, pitch, cs, info);
    CGColorSpaceRelease(cs);
    if (!c) return false;
    CGContextSetBlendMode(c, kCGBlendModeCopy);
    CGContextDrawImage(c, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height), img);
    CGContextRelease(c);
    for (size_t y = 0; y < height; y++) {
        uint8_t *row = dst + y * pitch;
        for (size_t x = 0; x < width; x++) row[x * 4 + 3] = 0xFF;
    }
    return true;
}

int32_t sceJpegDecDecode(uint8_t *mem, uint64_t handle, const OrbisJpegDecDecodeParam *param,
                         OrbisJpegDecImageInfo *image_info) {
    OrbisJpegDecHandleInternal *h = handle_of(mem, handle);
    if (!h) return ORBIS_JPEG_DEC_ERROR_INVALID_HANDLE;
    if (!param || !param->jpeg_addr || !param->image_addr) return ORBIS_JPEG_DEC_ERROR_INVALID_ADDR;
    if (param->jpeg_size == 0 || param->image_size == 0) return ORBIS_JPEG_DEC_ERROR_INVALID_SIZE;
    if (param->decode_mode > 1) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    const uint32_t ratio = param->downscale_ratio;
    if (!valid_ratio(ratio)) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    const uint16_t format = param->pixel_format;
    if (format > 11 || (0xe03 & (1u << format)) == 0) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    if (param->alpha_value > 0xff) return ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
    if (format != ORBIS_JPEG_DEC_PIXEL_FORMAT_R8G8B8A8 && format != ORBIS_JPEG_DEC_PIXEL_FORMAT_B8G8R8A8 &&
        format != ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8) {
        return ORBIS_JPEG_DEC_ERROR_UNSUPPORT_DATA;
    }

    const uint8_t *jpeg = gaddr(mem, param->jpeg_addr);
    if (param->jpeg_size < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) return ORBIS_JPEG_DEC_ERROR_DECODE_ERROR;
    CGImageRef img = create_image(jpeg, param->jpeg_size);
    if (!img) return ORBIS_JPEG_DEC_ERROR_DECODE_ERROR;
    const uint32_t w = (uint32_t)CGImageGetWidth(img);
    const uint32_t hgt = (uint32_t)CGImageGetHeight(img);
    CGColorSpaceRef img_cs = CGImageGetColorSpace(img);
    const uint16_t channels = img_cs && CGColorSpaceGetModel(img_cs) == kCGColorSpaceModelMonochrome ? 1 : 3;

    int32_t rc = 0;
    if (w > h->max_width) rc = ORBIS_JPEG_DEC_ERROR_EXCEED_MAX_WIDTH;
    const uint32_t out_w = (w + ratio - 1) / ratio;
    const uint32_t out_h = (hgt + ratio - 1) / ratio;
    const uint32_t bpp = format == ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8 ? 1 : 4;
    const uint32_t min_pitch = out_w * bpp;
    if (rc == 0 && param->image_pitch != 0) {
        if ((bpp == 4 && (param->image_pitch & 3) != 0) || param->image_pitch < min_pitch ||
            (uint64_t)param->image_pitch * out_h > param->image_size) {
            rc = ORBIS_JPEG_DEC_ERROR_INVALID_PARAM;
        }
    } else if (rc == 0 && (uint64_t)min_pitch * out_h > param->image_size) {
        rc = ORBIS_JPEG_DEC_ERROR_INVALID_SIZE;
    }
    if (rc != 0) {
        CGImageRelease(img);
        return rc;
    }

    const uint32_t pitch = param->image_pitch != 0 ? param->image_pitch : min_pitch;
    uint8_t *dst = gaddr(mem, param->image_addr);
    const bool bgra = format == ORBIS_JPEG_DEC_PIXEL_FORMAT_B8G8R8A8;
    const uint8_t alpha = param->alpha_value != 0 ? (uint8_t)param->alpha_value : 0xFF;
    if (ratio == 1 && bpp == 4) {
        // Full size 4-byte output: decode straight into the guest image.
        if (!render_rgbx(img, dst, w, hgt, pitch, bgra)) rc = ORBIS_JPEG_DEC_ERROR_DECODE_ERROR;
        if (rc == 0 && alpha != 0xFF) {
            for (uint32_t y = 0; y < out_h; y++) {
                for (uint32_t x = 0; x < out_w; x++) dst[y * pitch + x * 4 + 3] = alpha;
            }
        }
    } else {
        // Downscaling and Y8 take every ratio-th pixel of the full image, as in shadPS4.
        uint8_t *rgb = malloc((size_t)w * hgt * 4);
        if (!rgb || !render_rgbx(img, rgb, w, hgt, (size_t)w * 4, false)) {
            rc = ORBIS_JPEG_DEC_ERROR_DECODE_ERROR;
        } else {
            for (uint32_t y = 0; y < out_h; y++) {
                uint8_t *row = dst + (size_t)y * pitch;
                for (uint32_t x = 0; x < out_w; x++) {
                    const uint8_t *s = rgb + ((size_t)y * ratio * w + (size_t)x * ratio) * 4;
                    if (format == ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8) {
                        row[x] = (uint8_t)((s[0] * 77 + s[1] * 150 + s[2] * 29) >> 8);
                    } else {
                        row[x * 4 + 0] = bgra ? s[2] : s[0];
                        row[x * 4 + 1] = s[1];
                        row[x * 4 + 2] = bgra ? s[0] : s[2];
                        row[x * 4 + 3] = alpha;
                    }
                }
            }
        }
        free(rgb);
    }
    CGImageRelease(img);
    if (rc != 0) return rc;

    if (image_info) {
        memset(image_info, 0, sizeof(*image_info));
        image_info->image_width = w;
        image_info->image_height = hgt;
        image_info->color_space = channels == 1 ? ORBIS_JPEG_DEC_COLOR_SPACE_GRAYSCALE : ORBIS_JPEG_DEC_COLOR_SPACE_YCC;
        image_info->num_components = channels;
        image_info->component_id[0] = 1;
        image_info->component_id[1] = 2;
        image_info->component_id[2] = 3;
        image_info->sampling_factor[0] = 0x22;
        image_info->sampling_factor[1] = 0x11;
        image_info->sampling_factor[2] = 0x11;
        image_info->decode_mode = param->decode_mode;
        image_info->required_decode_mode = 1;
        image_info->output_width = out_w;
        image_info->output_height = out_h;
    }
    return (int32_t)((out_w << 16) | (out_h & 0xFFFF));
}

// Shims: parameter structures are read from guest memory; NULL pointers stay NULL.
void shim_sceJpegDecQueryMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegDecQueryMemorySize(gaddr(ctx->mem_base, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceJpegDecCreate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegDecCreate(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi), ctx->rsi,
                                                   (uint32_t)ctx->rdx, gaddr(ctx->mem_base, ctx->rcx));
    SHIM_RETURN();
}

void shim_sceJpegDecDelete(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegDecDelete(ctx->mem_base, ctx->rdi);
    SHIM_RETURN();
}

void shim_sceJpegDecParseHeader(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegDecParseHeader(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi),
                                                        gaddr(ctx->mem_base, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceJpegDecDecode(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceJpegDecDecode(ctx->mem_base, ctx->rdi, gaddr(ctx->mem_base, ctx->rsi),
                                                   gaddr(ctx->mem_base, ctx->rdx));
    SHIM_RETURN();
}

// Same as sceJpegDecDecode, as in shadPS4.
void shim_sceJpegDecDecodeWithInputControl(GuestContext *ctx) {
    shim_sceJpegDecDecode(ctx);
}
