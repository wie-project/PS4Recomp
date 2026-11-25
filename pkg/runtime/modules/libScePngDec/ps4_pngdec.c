// SPDX-License-Identifier: GPL-2.0-or-later

// libScePngDec and libScePngEnc. The decoder is ported from shadPS4 (libpng/pngdec.cpp): every
// PNG becomes 8-bit RGBA or BGRA the way shadPS4 sets libpng up (16-bit samples cut to their
// high byte, palettes and gray expanded, tRNS turned into alpha, alpha_value as the alpha of
// images without one, Adam7 images deinterlaced). The PNG format itself is decoded here with
// zlib, which the runtime links already.
//
// Differences from shadPS4: chunk CRCs and image bounds are checked (DECODE_ERROR /
// INVALID_DATA instead of libpng's error longjmp), and an output buffer too small for the image
// fails with INVALID_SIZE instead of being overrun.

#include "ps4_pngdec.h"
#include "ps4_hle_stub.h"
#include "ps4_pngenc.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static const uint8_t k_png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

// What the chunks before the image data say.
typedef struct PngHeader {
    uint32_t width, height;
    uint8_t bit_depth, color_type, interlace;
    uint8_t palette[256][3];
    uint32_t palette_size;
    uint8_t trns_alpha[256]; // palette images
    uint32_t trns_size;
    uint16_t trns_key[3]; // gray (key[0]) or RGB images
    bool has_trns;
    uint32_t first_idat; // offset of the first IDAT chunk
} PngHeader;

static uint32_t channels_of(uint8_t color_type) {
    switch (color_type) {
    case 0: return 1; // gray
    case 2: return 3; // RGB
    case 3: return 1; // palette index
    case 4: return 2; // gray + alpha
    case 6: return 4; // RGBA
    default: return 0;
    }
}

static bool depth_valid(uint8_t color_type, uint8_t depth) {
    switch (color_type) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2:
    case 4:
    case 6: return depth == 8 || depth == 16;
    default: return false;
    }
}

static uint16_t color_space_of(uint8_t color_type) {
    switch (color_type) {
    case 0: return ORBIS_PNG_DEC_COLOR_SPACE_GRAYSCALE;
    case 2: return ORBIS_PNG_DEC_COLOR_SPACE_RGB;
    case 3: return ORBIS_PNG_DEC_COLOR_SPACE_CLUT;
    case 4: return ORBIS_PNG_DEC_COLOR_SPACE_GRAYSCALE_ALPHA;
    default: return ORBIS_PNG_DEC_COLOR_SPACE_RGBA;
    }
}

static bool chunk_crc_ok(const uint8_t *chunk, uint32_t len) {
    uint32_t crc = (uint32_t)crc32(crc32(0L, Z_NULL, 0), chunk + 4, len + 4);
    return crc == be32(chunk + 8 + len);
}

// Reads the signature and the chunks up to the first IDAT.
static int32_t read_header(const uint8_t *png, uint32_t size, PngHeader *h) {
    memset(h, 0, sizeof(*h));
    if (size < 8 || memcmp(png, k_png_sig, 8) != 0) {
        return ORBIS_PNG_DEC_ERROR_INVALID_DATA;
    }
    uint32_t off = 8;
    bool have_ihdr = false;
    while (off + 8 <= size) {
        const uint32_t len = be32(png + off);
        const uint8_t *chunk = png + off;
        const uint8_t *data = chunk + 8;
        // The header ends at the first IDAT, whose data need not be at hand (as png_read_info).
        if (have_ihdr && memcmp(chunk + 4, "IDAT", 4) == 0) {
            if (h->color_type == 3 && h->palette_size == 0) {
                return ORBIS_PNG_DEC_ERROR_INVALID_DATA;
            }
            h->first_idat = off;
            return 0;
        }
        if (off + 12 > size || len > size - off - 12) {
            return ORBIS_PNG_DEC_ERROR_INVALID_DATA;
        }
        if (memcmp(chunk + 4, "IHDR", 4) == 0) {
            if (have_ihdr || len != 13 || !chunk_crc_ok(chunk, len)) {
                return ORBIS_PNG_DEC_ERROR_INVALID_DATA;
            }
            have_ihdr = true;
            h->width = be32(data);
            h->height = be32(data + 4);
            h->bit_depth = data[8];
            h->color_type = data[9];
            h->interlace = data[12];
            if (h->width == 0 || h->height == 0 || h->width > 0x7fffffff || h->height > 0x7fffffff ||
                !depth_valid(h->color_type, h->bit_depth) || data[10] != 0 || data[11] != 0 || h->interlace > 1) {
                return ORBIS_PNG_DEC_ERROR_INVALID_DATA;
            }
        } else if (!have_ihdr) {
            return ORBIS_PNG_DEC_ERROR_INVALID_DATA; // IHDR comes first
        } else if (memcmp(chunk + 4, "PLTE", 4) == 0) {
            if (len % 3 != 0 || len > 768 || !chunk_crc_ok(chunk, len)) {
                return ORBIS_PNG_DEC_ERROR_INVALID_DATA;
            }
            h->palette_size = len / 3;
            memcpy(h->palette, data, len);
        } else if (memcmp(chunk + 4, "tRNS", 4) == 0) {
            h->has_trns = true;
            if (h->color_type == 3) {
                h->trns_size = len > 256 ? 256 : len;
                memcpy(h->trns_alpha, data, h->trns_size);
            } else if (h->color_type == 0 && len >= 2) {
                h->trns_key[0] = be16(data);
            } else if (h->color_type == 2 && len >= 6) {
                h->trns_key[0] = be16(data);
                h->trns_key[1] = be16(data + 2);
                h->trns_key[2] = be16(data + 4);
            } else {
                h->has_trns = false; // not valid for this color type: libpng ignores it
            }
        } else if (memcmp(chunk + 4, "IEND", 4) == 0) {
            break;
        }
        off += 12 + len;
    }
    return ORBIS_PNG_DEC_ERROR_INVALID_DATA; // no image data
}

static void fill_info(const PngHeader *h, OrbisPngDecImageInfo *info) {
    info->image_width = h->width;
    info->image_height = h->height;
    info->color_space = color_space_of(h->color_type);
    info->bit_depth = h->bit_depth;
    info->image_flag = 0;
    if (h->interlace == 1) info->image_flag |= ORBIS_PNG_DEC_IMAGE_FLAG_ADAM7_INTERLACE;
    if (h->has_trns) info->image_flag |= ORBIS_PNG_DEC_IMAGE_FLAG_TRNS_CHUNK_EXIST;
}

// Adam7 passes: x0, y0, dx, dy. A non-interlaced image is the single pass {0, 0, 1, 1}.
static const uint8_t k_adam7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                      {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};

static uint64_t row_bytes(const PngHeader *h, uint32_t w) {
    return ((uint64_t)w * channels_of(h->color_type) * h->bit_depth + 7) / 8;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = (int)a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Reverses the row filter of `cur` (n bytes; prev is the previous unfiltered row or NULL).
static bool unfilter(uint8_t type, uint8_t *cur, const uint8_t *prev, uint64_t n, uint32_t bpp) {
    for (uint64_t i = 0; i < n; i++) {
        const uint8_t a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0;
        const uint8_t c = prev && i >= bpp ? prev[i - bpp] : 0;
        switch (type) {
        case 0: break;
        case 1: cur[i] += a; break;
        case 2: cur[i] += b; break;
        case 3: cur[i] += (uint8_t)(((unsigned)a + b) / 2); break;
        case 4: cur[i] += paeth(a, b, c); break;
        default: return false;
        }
    }
    return true;
}

// Sample s of an unfiltered row, at the image's bit depth.
static uint16_t sample(const uint8_t *row, uint64_t s, uint8_t depth) {
    switch (depth) {
    case 16: return be16(row + s * 2);
    case 8: return row[s];
    default: {
        const uint64_t bit = s * depth;
        return (uint16_t)((row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1));
    }
    }
}

// A sample at the output's 8 bits: 16-bit samples keep their high byte (png_set_strip_16),
// gray below 8 bits is scaled to the full range (png_set_expand_gray_1_2_4_to_8).
static uint8_t to8(uint16_t v, uint8_t depth) {
    return depth == 16 ? (uint8_t)(v >> 8) : depth == 8 ? (uint8_t)v : (uint8_t)(v * 255u / ((1u << depth) - 1));
}

static void convert_pixel(const PngHeader *h, const uint8_t *row, uint32_t x, uint8_t filler, uint8_t out[4]) {
    const uint8_t d = h->bit_depth;
    switch (h->color_type) {
    case 0: {
        const uint16_t g = sample(row, x, d);
        out[0] = out[1] = out[2] = to8(g, d);
        out[3] = h->has_trns ? (g == h->trns_key[0] ? 0 : 255) : filler;
        break;
    }
    case 2: {
        const uint16_t r = sample(row, (uint64_t)x * 3, d), g = sample(row, (uint64_t)x * 3 + 1, d),
                       b = sample(row, (uint64_t)x * 3 + 2, d);
        out[0] = to8(r, d);
        out[1] = to8(g, d);
        out[2] = to8(b, d);
        out[3] = h->has_trns ? (r == h->trns_key[0] && g == h->trns_key[1] && b == h->trns_key[2] ? 0 : 255) : filler;
        break;
    }
    case 3: {
        const uint16_t i = sample(row, x, d);
        if (i < h->palette_size) {
            memcpy(out, h->palette[i], 3);
        } else {
            out[0] = out[1] = out[2] = 0; // libpng: an index past the palette is black
        }
        out[3] = h->has_trns ? (i < h->trns_size ? h->trns_alpha[i] : 255) : filler;
        break;
    }
    case 4:
        out[0] = out[1] = out[2] = to8(sample(row, (uint64_t)x * 2, d), d);
        out[3] = to8(sample(row, (uint64_t)x * 2 + 1, d), d);
        break;
    default:
        for (int c = 0; c < 4; c++) out[c] = to8(sample(row, (uint64_t)x * 4 + c, d), d);
        break;
    }
}

// Inflates the IDAT stream into raw (raw_size bytes, exactly).
static int32_t inflate_idat(const uint8_t *png, uint32_t size, uint32_t off, uint8_t *raw, uint64_t raw_size) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        return ORBIS_PNG_DEC_ERROR_FATAL;
    }
    zs.next_out = raw;
    uint64_t produced = 0;
    int zr = Z_OK;
    while (off + 12 <= size && zr != Z_STREAM_END) {
        const uint32_t len = be32(png + off);
        if (len > size - off - 12) break;
        const uint8_t *chunk = png + off;
        if (memcmp(chunk + 4, "IDAT", 4) == 0) {
            if (!chunk_crc_ok(chunk, len)) break;
            zs.next_in = (Bytef *)(chunk + 8);
            zs.avail_in = len;
            while (zs.avail_in > 0 && produced < raw_size) {
                const uint64_t room = raw_size - produced;
                zs.avail_out = room > 0x40000000u ? 0x40000000u : (uInt)room;
                const uInt before = zs.avail_out;
                zr = inflate(&zs, Z_NO_FLUSH);
                produced += before - zs.avail_out;
                if (zr != Z_OK) break;
            }
            if (zr != Z_OK && zr != Z_STREAM_END) break;
        } else if (memcmp(chunk + 4, "IEND", 4) == 0) {
            break;
        }
        off += 12 + len;
    }
    inflateEnd(&zs);
    return produced == raw_size ? 0 : ORBIS_PNG_DEC_ERROR_DECODE_ERROR;
}

static int32_t decode_image(const uint8_t *png, uint32_t size, const PngHeader *h, uint8_t *dst, uint64_t stride,
                            bool bgra, uint8_t filler) {
    const int passes = h->interlace ? 7 : 1;
    const uint32_t bpp = (channels_of(h->color_type) * h->bit_depth + 7) / 8;
    uint64_t raw_size = 0;
    for (int p = 0; p < passes; p++) {
        const uint8_t *a = h->interlace ? k_adam7[p] : (const uint8_t[4]){0, 0, 1, 1};
        const uint32_t pw = h->width > a[0] ? (h->width - a[0] + a[2] - 1) / a[2] : 0;
        const uint32_t ph = h->height > a[1] ? (h->height - a[1] + a[3] - 1) / a[3] : 0;
        if (pw && ph) raw_size += (row_bytes(h, pw) + 1) * ph;
    }
    if (raw_size > ((uint64_t)1 << 34)) {
        return ORBIS_PNG_DEC_ERROR_UNSUPPORT_DATA;
    }
    uint8_t *raw = malloc((size_t)raw_size);
    if (!raw) {
        return ORBIS_PNG_DEC_ERROR_FATAL;
    }
    int32_t r = inflate_idat(png, size, h->first_idat, raw, raw_size);
    uint8_t *row = raw;
    for (int p = 0; p < passes && r == 0; p++) {
        const uint8_t *a = h->interlace ? k_adam7[p] : (const uint8_t[4]){0, 0, 1, 1};
        const uint32_t pw = h->width > a[0] ? (h->width - a[0] + a[2] - 1) / a[2] : 0;
        const uint32_t ph = h->height > a[1] ? (h->height - a[1] + a[3] - 1) / a[3] : 0;
        if (!pw || !ph) continue;
        const uint64_t n = row_bytes(h, pw);
        const uint8_t *prev = NULL;
        for (uint32_t y = 0; y < ph; y++, row += n + 1) {
            if (!unfilter(row[0], row + 1, prev, n, bpp)) {
                r = ORBIS_PNG_DEC_ERROR_DECODE_ERROR;
                break;
            }
            prev = row + 1;
            uint8_t *out_row = dst + (uint64_t)(a[1] + y * a[3]) * stride;
            for (uint32_t x = 0; x < pw; x++) {
                uint8_t *px = out_row + (uint64_t)(a[0] + x * a[2]) * 4;
                convert_pixel(h, row + 1, x, filler, px);
                if (bgra) {
                    const uint8_t t = px[0];
                    px[0] = px[2];
                    px[2] = t;
                }
            }
        }
    }
    free(raw);
    return r;
}

int32_t scePngDecQueryMemorySize(const OrbisPngDecCreateParam *param) {
    if (!param) {
        return ORBIS_PNG_DEC_ERROR_INVALID_PARAM;
    }
    if (param->attribute > 1) {
        return ORBIS_PNG_DEC_ERROR_INVALID_ADDR; // sic: shadPS4 reports a bad attribute this way here
    }
    if (param->max_image_width - 1 > 1000000) {
        return ORBIS_PNG_DEC_ERROR_INVALID_SIZE;
    }
    return PS4_PNG_DEC_WORK_MEMORY_SIZE;
}

int32_t scePngDecCreate(const OrbisPngDecCreateParam *param, uint64_t memory, uint32_t memory_size,
                        uint64_t *handle) {
    if (!param || param->attribute > 1) {
        return ORBIS_PNG_DEC_ERROR_INVALID_PARAM;
    }
    if (!memory || !handle) {
        return ORBIS_PNG_DEC_ERROR_INVALID_ADDR;
    }
    if (param->max_image_width - 1 > 1000000) {
        return ORBIS_PNG_DEC_ERROR_INVALID_SIZE;
    }
    // The decoder keeps no state between calls: the work memory only names it.
    *handle = memory;
    return 0;
}

int32_t scePngDecDelete(uint64_t handle) {
    return handle ? 0 : ORBIS_PNG_DEC_ERROR_INVALID_HANDLE;
}

int32_t scePngDecParseHeader(uint8_t *mem, const OrbisPngDecParseParam *param, OrbisPngDecImageInfo *info) {
    if (!param) {
        return ORBIS_PNG_DEC_ERROR_INVALID_PARAM;
    }
    if (!param->png_mem_addr || !info) {
        return ORBIS_PNG_DEC_ERROR_INVALID_ADDR;
    }
    PngHeader h;
    int32_t r = read_header(mem + param->png_mem_addr, param->png_mem_size, &h);
    if (r == 0) {
        fill_info(&h, info);
    }
    return r;
}

int32_t scePngDecDecode(uint8_t *mem, uint64_t handle, const OrbisPngDecDecodeParam *param,
                        OrbisPngDecImageInfo *info) {
    if (!handle) {
        return ORBIS_PNG_DEC_ERROR_INVALID_HANDLE;
    }
    if (!param) {
        return ORBIS_PNG_DEC_ERROR_INVALID_PARAM;
    }
    if (!param->png_mem_addr || !param->image_mem_addr) {
        return ORBIS_PNG_DEC_ERROR_INVALID_ADDR;
    }
    const uint8_t *png = mem + param->png_mem_addr;
    PngHeader h;
    int32_t r = read_header(png, param->png_mem_size, &h);
    if (r != 0) {
        return r;
    }
    if (info) {
        fill_info(&h, info);
    }
    const uint64_t stride = param->image_pitch > 0 ? param->image_pitch : (uint64_t)h.width * 4;
    if (stride < (uint64_t)h.width * 4 || stride * (h.height - 1) + (uint64_t)h.width * 4 > param->image_mem_size) {
        return ORBIS_PNG_DEC_ERROR_INVALID_SIZE;
    }
    r = decode_image(png, param->png_mem_size, &h, mem + param->image_mem_addr, stride,
                     param->pixel_format == ORBIS_PNG_DEC_PIXEL_FORMAT_B8G8R8A8, (uint8_t)param->alpha_value);
    if (r != 0) {
        return r;
    }
    return h.width > 32767 || h.height > 32767 ? 0 : (int32_t)((h.width << 16) | h.height);
}

// ---- libScePngEnc ----


// Ported from shadPS4 (libpng/pngenc.cpp): 8-bit RGB or RGBA PNGs from RGBA/BGRA pixels, each
// row with the allowed filter that libpng's heuristic picks (smallest sum of absolute filtered
// bytes). Differences from shadPS4: rows are image_pitch apart in both color spaces (4 * width
// when 0; shadPS4 ignores the pitch for RGBA and needs it for RGB), and the PNG is always 8 bits
// per sample, which is what the RGBA8 input holds (shadPS4 passes bit_depth on to libpng with
// 8-bit rows).

#define PNG_ENC_WORK_MEMORY_SIZE 16

int32_t scePngEncQueryMemorySize(const OrbisPngEncCreateParam *param) {
    if (!param) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (param->attribute != 0 || param->max_filter_number > 5) return ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
    if (param->max_image_width - 1 > 1000000) return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;
    return PNG_ENC_WORK_MEMORY_SIZE;
}

int32_t scePngEncCreate(const OrbisPngEncCreateParam *param, void *memoryAddress, uint32_t memorySize, void **handle) {
    if (!param || param->attribute != 0) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (!memoryAddress || !handle) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (param->max_image_width - 1 > 1000000) return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;
    // The encoder keeps no state between calls: the work memory only names it.
    *handle = memoryAddress;
    return 0;
}

int32_t scePngEncDelete(void *handle) {
    return handle ? 0 : ORBIS_PNG_ENC_ERROR_INVALID_HANDLE;
}

static void png_write_be32(uint8_t *dst, uint32_t val) {
    dst[0] = (uint8_t)(val >> 24);
    dst[1] = (uint8_t)(val >> 16);
    dst[2] = (uint8_t)(val >> 8);
    dst[3] = (uint8_t)val;
}

// Writes a chunk whose data is already at dst + 8.
static uint32_t png_finish_chunk(uint8_t *dst, const char *type, uint32_t length) {
    png_write_be32(dst, length);
    memcpy(dst + 4, type, 4);
    png_write_be32(dst + 8 + length, (uint32_t)crc32(crc32(0L, Z_NULL, 0), dst + 4, 4 + length));
    return 12 + length;
}

// Filters `row` (n bytes, bpp per pixel; prev NULL for the first row) into out[1..n] with the
// allowed filter (mask bits: None 1... as OrbisPngEncFilterType, with None = 0 meaning only
// None) whose output has the smallest sum of absolute values, and stores its type in out[0].
static void filter_row(const uint8_t *row, const uint8_t *prev, uint32_t n, uint32_t bpp, uint16_t mask, uint8_t *out,
                       uint8_t *scratch) {
    // OrbisPngEncFilterType: None 0 (alone), Sub 1, Up 2, Average 4, Paeth 8; All 15 = every one.
    const bool allowed[5] = {mask == 0 || mask == 15, (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                             (mask & 8) != 0};
    uint64_t best_sum = UINT64_MAX;
    for (uint8_t f = 0; f < 5; f++) {
        if (!allowed[f]) continue;
        uint64_t sum = 0;
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0;
            const uint8_t c = prev && i >= bpp ? prev[i - bpp] : 0;
            uint8_t pred = 0;
            switch (f) {
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (uint8_t)(((unsigned)a + b) / 2); break;
            case 4: pred = paeth(a, b, c); break;
            default: break;
            }
            scratch[i] = (uint8_t)(row[i] - pred);
            sum += scratch[i] < 128 ? scratch[i] : 256 - scratch[i];
        }
        if (sum < best_sum) {
            best_sum = sum;
            out[0] = f;
            memcpy(out + 1, scratch, n);
        }
    }
}

int32_t scePngEncEncode(void *handle, const OrbisPngEncEncodeParam *param, OrbisPngEncOutputInfo *outputInfo) {
    if (!handle) return ORBIS_PNG_ENC_ERROR_INVALID_HANDLE;
    if (!param) return ORBIS_PNG_ENC_ERROR_INVALID_PARAM;
    if (!param->image_mem_addr || !param->png_mem_addr) return ORBIS_PNG_ENC_ERROR_INVALID_ADDR;
    if (param->png_mem_size == 0 || param->image_mem_size == 0 || param->image_height == 0 || param->image_width == 0) {
        return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;
    }

    const bool has_alpha = param->color_space == 19; // OrbisPngEncColorSpace::RGBA; anything else is RGB
    const uint32_t channels = has_alpha ? 4 : 3;
    const uint64_t src_pitch = param->image_pitch ? param->image_pitch : (uint64_t)param->image_width * 4;
    if (src_pitch * (param->image_height - 1) + (uint64_t)param->image_width * 4 > param->image_mem_size) {
        return ORBIS_PNG_ENC_ERROR_INVALID_SIZE;
    }
    const bool swap_rb = param->pixel_format == 1; // B8G8R8A8
    const uint32_t n = param->image_width * channels;
    const uint64_t raw_size = (uint64_t)(n + 1) * param->image_height;
    uint8_t *raw = malloc((size_t)raw_size);
    uint8_t *rows = malloc((size_t)n * 3); // current row, previous row, scratch
    if (!raw || !rows) {
        free(raw);
        free(rows);
        return ORBIS_PNG_ENC_ERROR_FATAL;
    }
    uint8_t *cur = rows, *prev = rows + n, *scratch = rows + 2 * n;
    for (uint32_t y = 0; y < param->image_height; y++) {
        const uint8_t *src = param->image_mem_addr + y * src_pitch;
        for (uint32_t x = 0; x < param->image_width; x++, src += 4) {
            uint8_t *d = cur + x * channels;
            d[0] = swap_rb ? src[2] : src[0];
            d[1] = src[1];
            d[2] = swap_rb ? src[0] : src[2];
            if (has_alpha) d[3] = src[3];
        }
        filter_row(cur, y ? prev : NULL, n, channels, param->filter_type, raw + (uint64_t)y * (n + 1), scratch);
        uint8_t *t = prev;
        prev = cur;
        cur = t;
    }
    free(rows);

    static const uint8_t png_sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t *out = param->png_mem_addr;
    const uint32_t capacity = param->png_mem_size;
    // Signature, IHDR, an IDAT header and CRC, IEND.
    if (capacity < 8 + 25 + 12 + 12) {
        free(raw);
        return ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW;
    }
    memcpy(out, png_sig, 8);
    uint32_t offset = 8;
    uint8_t *ihdr = out + offset + 8;
    png_write_be32(ihdr, param->image_width);
    png_write_be32(ihdr + 4, param->image_height);
    ihdr[8] = 8;
    ihdr[9] = has_alpha ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    offset += png_finish_chunk(out + offset, "IHDR", 13);

    uLongf idat_size = capacity - offset - 12 - 12;
    const int level = param->compression_level > 9 ? 9 : param->compression_level;
    const int zr = compress2(out + offset + 8, &idat_size, raw, (uLong)raw_size, level);
    free(raw);
    if (zr != Z_OK) {
        if (outputInfo) outputInfo->processed_height = 0;
        return ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW;
    }
    offset += png_finish_chunk(out + offset, "IDAT", (uint32_t)idat_size);
    offset += png_finish_chunk(out + offset, "IEND", 0);

    if (outputInfo) {
        outputInfo->data_size = offset;
        outputInfo->processed_height = param->image_height;
    }
    return (int32_t)offset;
}

// ---- Guest ABI ----

#define GPTR(type, addr) ((addr) ? (type)(ctx->mem_base + (addr)) : (type)NULL)

void shim_scePngDecQueryMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngDecQueryMemorySize(GPTR(const OrbisPngDecCreateParam *, ctx->rdi));
    SHIM_RETURN();
}

void shim_scePngDecCreate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngDecCreate(GPTR(const OrbisPngDecCreateParam *, ctx->rdi), ctx->rsi,
                                                  (uint32_t)ctx->rdx, GPTR(uint64_t *, ctx->rcx));
    SHIM_RETURN();
}

void shim_scePngDecDelete(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngDecDelete(ctx->rdi);
    SHIM_RETURN();
}

void shim_scePngDecParseHeader(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngDecParseHeader(ctx->mem_base, GPTR(const OrbisPngDecParseParam *, ctx->rdi),
                                                       GPTR(OrbisPngDecImageInfo *, ctx->rsi));
    SHIM_RETURN();
}

void shim_scePngDecDecode(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngDecDecode(ctx->mem_base, ctx->rdi, GPTR(const OrbisPngDecDecodeParam *, ctx->rsi),
                                                  GPTR(OrbisPngDecImageInfo *, ctx->rdx));
    SHIM_RETURN();
}

// Stubbed in shadPS4 as well.
PS4_HLE_STUB(scePngDecDecodeWithInputControl)


void shim_scePngEncQueryMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngEncQueryMemorySize(GPTR(const OrbisPngEncCreateParam *, ctx->rdi));
    SHIM_RETURN();
}

void shim_scePngEncCreate(GuestContext *ctx) {
    void *mem = GPTR(void *, ctx->rsi);
    void *host_handle = NULL;
    int32_t r = scePngEncCreate(GPTR(const OrbisPngEncCreateParam *, ctx->rdi), mem, (uint32_t)ctx->rdx,
                                ctx->rcx ? &host_handle : NULL);
    if (r == 0) *(uint64_t *)(ctx->mem_base + ctx->rcx) = ctx->rsi; // the guest handle is the work memory
    ctx->rax = (uint64_t)(int64_t)r;
    SHIM_RETURN();
}

void shim_scePngEncDelete(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)scePngEncDelete(GPTR(void *, ctx->rdi));
    SHIM_RETURN();
}

void shim_scePngEncEncode(GuestContext *ctx) {
    const OrbisPngEncEncodeParam *param = GPTR(const OrbisPngEncEncodeParam *, ctx->rsi);
    OrbisPngEncEncodeParam host_param;
    if (param) {
        // The guest structure holds guest addresses where the C API takes host pointers.
        host_param = *param;
        host_param.image_mem_addr = GPTR(const uint8_t *, (uint64_t)(uintptr_t)param->image_mem_addr);
        host_param.png_mem_addr = GPTR(uint8_t *, (uint64_t)(uintptr_t)param->png_mem_addr);
    }
    ctx->rax = (uint64_t)(int64_t)scePngEncEncode(GPTR(void *, ctx->rdi), param ? &host_param : NULL,
                                                  GPTR(OrbisPngEncOutputInfo *, ctx->rdx));
    SHIM_RETURN();
}
