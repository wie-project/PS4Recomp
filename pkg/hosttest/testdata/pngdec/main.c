// SPDX-License-Identifier: GPL-2.0-or-later

// libScePngDec: PNGs of every color type and bit depth, plain and Adam7-interlaced, with all
// five row filters, written here with zlib and decoded through the guest API. The writer is
// checked against ImageIO (it must produce PNGs that ImageIO reads back to the same pixels),
// and the decoder's output is checked pixel by pixel against the samples written.

#include "ps4_pngdec.h"
#include "ps4_pngenc.h"
#include <ImageIO/ImageIO.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

// Guest memory and where things go in it.
#define MEM_SIZE (8u << 20)
#define PNG_AT 0x1000u
#define IMAGE_AT 0x200000u
#define PARAM_AT 0x100u
static uint8_t *g_mem;

// ---- A PNG writer ----

typedef struct Spec {
    uint8_t color_type, depth, interlace;
    uint32_t w, h;
    uint32_t palette_size;
    int trns; // 0: none, 1: tRNS chunk
} Spec;

static uint32_t channels(uint8_t ct) {
    return ct == 2 ? 3 : ct == 4 ? 2 : ct == 6 ? 4 : 1;
}

// The sample c of pixel (x, y), at the spec's depth.
static uint16_t sample_at(const Spec *s, uint32_t x, uint32_t y, uint32_t c) {
    const uint32_t max = (1u << s->depth) - 1;
    uint32_t v = (x * 37 + y * 91 + c * 53 + x * y * 7) * 0x9E37u;
    v ^= v >> 13;
    if (s->color_type == 3) return (uint16_t)(v % s->palette_size);
    return (uint16_t)(v & max);
}

static uint32_t be32_test(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static uint32_t add_chunk(uint8_t *out, uint32_t off, const char *type, const uint8_t *data, uint32_t len) {
    put32(out + off, len);
    memcpy(out + off + 4, type, 4);
    if (len) memcpy(out + off + 8, data, len);
    put32(out + off + 8 + len, (uint32_t)crc32(crc32(0, Z_NULL, 0), out + off + 4, len + 4));
    return off + 12 + len;
}

static const uint8_t k_adam7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                      {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};

static uint8_t paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

// Writes the PNG at out; returns its size. Rows cycle through filters 0..4.
static uint32_t write_png(const Spec *s, uint8_t *out) {
    const uint32_t ch = channels(s->color_type), bpp = (ch * s->depth + 7) / 8;
    uint8_t *raw = calloc(1, 1 << 20), *prev = calloc(1, 1 << 16), *row = calloc(1, 1 << 16);
    uint32_t raw_n = 0, filter = 0;
    const int passes = s->interlace ? 7 : 1;
    for (int p = 0; p < passes; p++) {
        const uint8_t *a = s->interlace ? k_adam7[p] : (const uint8_t[4]){0, 0, 1, 1};
        const uint32_t pw = s->w > a[0] ? (s->w - a[0] + a[2] - 1) / a[2] : 0;
        const uint32_t ph = s->h > a[1] ? (s->h - a[1] + a[3] - 1) / a[3] : 0;
        if (!pw || !ph) continue;
        const uint32_t n = (pw * ch * s->depth + 7) / 8;
        memset(prev, 0, n);
        for (uint32_t y = 0; y < ph; y++) {
            memset(row, 0, n);
            for (uint32_t x = 0; x < pw; x++) {
                for (uint32_t c = 0; c < ch; c++) {
                    const uint16_t v = sample_at(s, a[0] + x * a[2], a[1] + y * a[3], c);
                    const uint32_t i = x * ch + c;
                    if (s->depth == 16) {
                        row[i * 2] = (uint8_t)(v >> 8), row[i * 2 + 1] = (uint8_t)v;
                    } else if (s->depth == 8) {
                        row[i] = (uint8_t)v;
                    } else {
                        const uint32_t bit = i * s->depth;
                        row[bit / 8] |= (uint8_t)(v << (8 - s->depth - bit % 8));
                    }
                }
            }
            const uint8_t f = (uint8_t)(filter++ % 5);
            raw[raw_n++] = f;
            for (uint32_t i = 0; i < n; i++) {
                const int left = i >= bpp ? row[i - bpp] : 0, up = y ? prev[i] : 0, ul = y && i >= bpp ? prev[i - bpp] : 0;
                const int pred = f == 0 ? 0 : f == 1 ? left : f == 2 ? up : f == 3 ? (left + up) / 2 : paeth(left, up, ul);
                raw[raw_n++] = (uint8_t)(row[i] - pred);
            }
            memcpy(prev, row, n);
        }
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(out, sig, 8);
    uint8_t ihdr[13] = {0};
    put32(ihdr, s->w);
    put32(ihdr + 4, s->h);
    ihdr[8] = s->depth, ihdr[9] = s->color_type, ihdr[12] = s->interlace;
    uint32_t off = add_chunk(out, 8, "IHDR", ihdr, 13);
    if (s->color_type == 3) {
        uint8_t plte[768];
        for (uint32_t i = 0; i < s->palette_size; i++) {
            plte[i * 3] = (uint8_t)(i * 7), plte[i * 3 + 1] = (uint8_t)(255 - i), plte[i * 3 + 2] = (uint8_t)(i * 13);
        }
        off = add_chunk(out, off, "PLTE", plte, s->palette_size * 3);
    }
    if (s->trns) {
        uint8_t t[256];
        uint32_t len = 0;
        if (s->color_type == 3) {
            len = s->palette_size / 2; // alpha for the first half of the palette only
            for (uint32_t i = 0; i < len; i++) t[i] = (uint8_t)(i * 29);
        } else {
            // The color of pixel (0, 0) is transparent.
            for (uint32_t c = 0; c < channels(s->color_type); c++) {
                const uint16_t v = sample_at(s, 0, 0, c);
                t[len++] = (uint8_t)(v >> 8), t[len++] = (uint8_t)v;
            }
        }
        off = add_chunk(out, off, "tRNS", t, len);
    }
    // The image data split over two IDAT chunks.
    uLongf zlen = compressBound(raw_n);
    uint8_t *z = malloc(zlen);
    compress2(z, &zlen, raw, raw_n, 9);
    off = add_chunk(out, off, "IDAT", z, (uint32_t)(zlen / 2));
    off = add_chunk(out, off, "IDAT", z + zlen / 2, (uint32_t)(zlen - zlen / 2));
    off = add_chunk(out, off, "IEND", NULL, 0);
    free(z), free(raw), free(prev), free(row);
    return off;
}

// The RGBA8 pixel the decoder must produce for (x, y).
static void expected(const Spec *s, uint32_t x, uint32_t y, uint8_t filler, uint8_t px[4]) {
    const uint32_t max = (1u << s->depth) - 1;
#define TO8(v) (uint8_t)(s->depth == 16 ? (v) >> 8 : (v) * 255u / max)
    uint16_t v[4];
    for (uint32_t c = 0; c < channels(s->color_type); c++) v[c] = sample_at(s, x, y, c);
    switch (s->color_type) {
    case 0:
        px[0] = px[1] = px[2] = TO8(v[0]);
        px[3] = s->trns ? (v[0] == sample_at(s, 0, 0, 0) ? 0 : 255) : filler;
        break;
    case 2:
        for (int c = 0; c < 3; c++) px[c] = TO8(v[c]);
        px[3] = s->trns ? (v[0] == sample_at(s, 0, 0, 0) && v[1] == sample_at(s, 0, 0, 1) &&
                                   v[2] == sample_at(s, 0, 0, 2)
                               ? 0
                               : 255)
                        : filler;
        break;
    case 3:
        px[0] = (uint8_t)(v[0] * 7), px[1] = (uint8_t)(255 - v[0]), px[2] = (uint8_t)(v[0] * 13);
        px[3] = s->trns ? (v[0] < s->palette_size / 2 ? (uint8_t)(v[0] * 29) : 255) : filler;
        break;
    case 4:
        px[0] = px[1] = px[2] = TO8(v[0]);
        px[3] = TO8(v[1]);
        break;
    default:
        for (int c = 0; c < 4; c++) px[c] = TO8(v[c]);
    }
#undef TO8
}

// ---- Guest calls ----

static uint64_t g_handle;

static int32_t decode(uint32_t png_size, uint16_t format, uint16_t alpha, uint32_t pitch, uint32_t image_size,
                      OrbisPngDecImageInfo *info) {
    OrbisPngDecDecodeParam *p = (OrbisPngDecDecodeParam *)(g_mem + PARAM_AT);
    p->png_mem_addr = PNG_AT;
    p->png_mem_size = png_size;
    p->image_mem_addr = IMAGE_AT;
    p->image_mem_size = image_size;
    p->pixel_format = format;
    p->alpha_value = alpha;
    p->image_pitch = pitch;
    return scePngDecDecode(g_mem, g_handle, p, info);
}

static void setup(void) {
    g_mem = calloc(1, MEM_SIZE);
    OrbisPngDecCreateParam cp = {sizeof(cp), 0, 4096};
    g_handle = 0;
    scePngDecCreate(&cp, 0x40, 16, &g_handle);
}

static int test_create(void) {
    setup();
    OrbisPngDecCreateParam cp = {sizeof(cp), 0, 1920};
    CHECK(scePngDecQueryMemorySize(NULL) == ORBIS_PNG_DEC_ERROR_INVALID_PARAM);
    CHECK(scePngDecQueryMemorySize(&cp) == PS4_PNG_DEC_WORK_MEMORY_SIZE);
    cp.attribute = 2;
    CHECK(scePngDecQueryMemorySize(&cp) == ORBIS_PNG_DEC_ERROR_INVALID_ADDR);
    uint64_t h = 0;
    CHECK(scePngDecCreate(&cp, 0x40, 16, &h) == ORBIS_PNG_DEC_ERROR_INVALID_PARAM);
    cp.attribute = 1; // BitDepth16
    cp.max_image_width = 0;
    CHECK(scePngDecQueryMemorySize(&cp) == ORBIS_PNG_DEC_ERROR_INVALID_SIZE);
    CHECK(scePngDecCreate(&cp, 0x40, 16, &h) == ORBIS_PNG_DEC_ERROR_INVALID_SIZE);
    cp.max_image_width = 1000001;
    CHECK(scePngDecQueryMemorySize(&cp) == PS4_PNG_DEC_WORK_MEMORY_SIZE);
    CHECK(scePngDecCreate(&cp, 0, 16, &h) == ORBIS_PNG_DEC_ERROR_INVALID_ADDR);
    CHECK(scePngDecCreate(&cp, 0x40, 16, &h) == 0 && h == 0x40);
    CHECK(scePngDecDelete(h) == 0 && scePngDecDelete(0) == ORBIS_PNG_DEC_ERROR_INVALID_HANDLE);
    printf("PASS create\n");
    return 0;
}

static int test_header(void) {
    setup();
    Spec s = {3, 4, 1, 300, 200, 11, 1};
    write_png(&s, g_mem + PNG_AT);
    OrbisPngDecParseParam pp = {PNG_AT, 4096, 0};
    OrbisPngDecImageInfo info;
    CHECK(scePngDecParseHeader(g_mem, NULL, &info) == ORBIS_PNG_DEC_ERROR_INVALID_PARAM);
    CHECK(scePngDecParseHeader(g_mem, &pp, &info) == 0);
    CHECK(info.image_width == 300 && info.image_height == 200 && info.bit_depth == 4 &&
          info.color_space == ORBIS_PNG_DEC_COLOR_SPACE_CLUT &&
          info.image_flag == (ORBIS_PNG_DEC_IMAGE_FLAG_ADAM7_INTERLACE | ORBIS_PNG_DEC_IMAGE_FLAG_TRNS_CHUNK_EXIST));
    Spec rgba = {6, 16, 0, 5, 7, 0, 0};
    write_png(&rgba, g_mem + PNG_AT);
    CHECK(scePngDecParseHeader(g_mem, &pp, &info) == 0 && info.color_space == ORBIS_PNG_DEC_COLOR_SPACE_RGBA &&
          info.bit_depth == 16 && info.image_flag == 0);
    g_mem[PNG_AT + 1] = 'X';
    CHECK(scePngDecParseHeader(g_mem, &pp, &info) == ORBIS_PNG_DEC_ERROR_INVALID_DATA);
    printf("PASS header\n");
    return 0;
}

// The test's PNGs must be real PNGs: ImageIO reads them back to the written samples.
static int writer_matches_imageio(const Spec *s, uint32_t size) {
    CFDataRef data = CFDataCreate(NULL, g_mem + PNG_AT, size);
    CGImageSourceRef src = CGImageSourceCreateWithData(data, NULL);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
    // 8-bit RGB, possibly with a skipped fourth byte.
    const size_t bpp = img ? CGImageGetBitsPerPixel(img) / 8 : 0;
    const CGImageAlphaInfo ai = img ? CGImageGetAlphaInfo(img) : kCGImageAlphaNone;
    int ok = img && CGImageGetWidth(img) == s->w && CGImageGetHeight(img) == s->h && CGImageGetBitsPerComponent(img) == 8 &&
             (bpp == 3 || (bpp == 4 && ai == kCGImageAlphaNoneSkipLast));
    if (!ok && img) fprintf(stderr, "ImageIO: %zu bytes per pixel, alpha info %d\n", bpp, (int)ai);
    if (ok) {
        CFDataRef px = CGDataProviderCopyData(CGImageGetDataProvider(img));
        const uint8_t *p = CFDataGetBytePtr(px);
        const size_t bpr = CGImageGetBytesPerRow(img);
        for (uint32_t y = 0; y < s->h && ok; y++)
            for (uint32_t x = 0; x < s->w && ok; x++)
                for (uint32_t c = 0; c < 3; c++) ok &= p[y * bpr + x * bpp + c] == sample_at(s, x, y, c);
        CFRelease(px);
    }
    if (img) CFRelease(img);
    if (src) CFRelease(src);
    CFRelease(data);
    return ok;
}

static int test_writer(void) {
    setup();
    for (uint8_t il = 0; il < 2; il++) {
        Spec s = {2, 8, il, 23, 19, 0, 0};
        CHECK(writer_matches_imageio(&s, write_png(&s, g_mem + PNG_AT)));
    }
    printf("PASS writer\n");
    return 0;
}

static int check_image(const Spec *s, uint32_t pitch, bool bgra, uint8_t filler) {
    for (uint32_t y = 0; y < s->h; y++) {
        for (uint32_t x = 0; x < s->w; x++) {
            uint8_t want[4];
            expected(s, x, y, filler, want);
            const uint8_t *got = g_mem + IMAGE_AT + (uint64_t)y * pitch + x * 4;
            const uint8_t r = bgra ? got[2] : got[0], b = bgra ? got[0] : got[2];
            if (r != want[0] || got[1] != want[1] || b != want[2] || got[3] != want[3]) {
                fprintf(stderr, "type %u depth %u interlace %u trns %d: pixel (%u,%u) = %u,%u,%u,%u, want %u,%u,%u,%u\n",
                        s->color_type, s->depth, s->interlace, s->trns, x, y, r, got[1], b, got[3], want[0], want[1],
                        want[2], want[3]);
                return 0;
            }
        }
    }
    return 1;
}

static int test_formats(void) {
    setup();
    static const uint8_t types[][2] = {{0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1},
                                       {3, 2}, {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};
    for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
        for (uint8_t il = 0; il < 2; il++) {
            for (int trns = 0; trns < 2; trns++) {
                const uint8_t ct = types[t][0];
                if (trns && (ct == 4 || ct == 6)) continue; // no tRNS with an alpha channel
                const uint32_t pal = ct == 3 ? (types[t][1] == 8 ? 200 : 1u << types[t][1]) : 0;
                Spec s = {ct, types[t][1], il, 13, 11, pal, trns};
                const uint32_t size = write_png(&s, g_mem + PNG_AT);
                OrbisPngDecImageInfo info;
                memset(g_mem + IMAGE_AT, 0xEE, 13 * 11 * 4);
                CHECK(decode(size, ORBIS_PNG_DEC_PIXEL_FORMAT_R8G8B8A8, 0x1C0, 0, 13 * 11 * 4, &info) == (13 << 16 | 11));
                CHECK(info.image_width == 13 && info.bit_depth == s.depth &&
                      ((info.image_flag & ORBIS_PNG_DEC_IMAGE_FLAG_TRNS_CHUNK_EXIST) != 0) == trns);
                CHECK(check_image(&s, 13 * 4, false, 0xC0));
            }
        }
    }
    printf("PASS formats\n");
    return 0;
}

static int test_layout(void) {
    setup();
    Spec s = {2, 8, 1, 31, 9, 0, 0};
    const uint32_t size = write_png(&s, g_mem + PNG_AT);
    // BGRA rows 200 bytes apart; the padding between rows is left alone.
    memset(g_mem + IMAGE_AT, 0x5A, 200 * 9);
    CHECK(decode(size, ORBIS_PNG_DEC_PIXEL_FORMAT_B8G8R8A8, 0x80, 200, 200 * 8 + 31 * 4, NULL) == (31 << 16 | 9));
    CHECK(check_image(&s, 200, true, 0x80));
    CHECK(g_mem[IMAGE_AT + 31 * 4] == 0x5A && g_mem[IMAGE_AT + 199] == 0x5A);

    // Errors.
    CHECK(decode(size, 0, 0xff, 200, 200 * 8 + 31 * 4 - 1, NULL) == ORBIS_PNG_DEC_ERROR_INVALID_SIZE);
    CHECK(decode(size, 0, 0xff, 100, 1 << 20, NULL) == ORBIS_PNG_DEC_ERROR_INVALID_SIZE); // pitch < row
    CHECK(scePngDecDecode(g_mem, 0, (OrbisPngDecDecodeParam *)(g_mem + PARAM_AT), NULL) ==
          ORBIS_PNG_DEC_ERROR_INVALID_HANDLE);
    CHECK(scePngDecDecode(g_mem, g_handle, NULL, NULL) == ORBIS_PNG_DEC_ERROR_INVALID_PARAM);
    g_mem[PNG_AT + size - 20] ^= 0x40; // inside the second IDAT: its CRC no longer matches
    CHECK(decode(size, 0, 0xff, 0, 1 << 20, NULL) == ORBIS_PNG_DEC_ERROR_DECODE_ERROR);
    CHECK(decode(40, 0, 0xff, 0, 1 << 20, NULL) == ORBIS_PNG_DEC_ERROR_INVALID_DATA); // cut before IDAT

    // Images past 32767 pixels decode but return 0 (a 40000x1 gray image).
    Spec wide = {0, 1, 0, 40000, 1, 0, 0};
    const uint32_t wsize = write_png(&wide, g_mem + PNG_AT);
    CHECK(decode(wsize, 0, 0xff, 0, 40000 * 4, NULL) == 0);
    CHECK(check_image(&wide, 40000 * 4, false, 0xff));
    printf("PASS layout\n");
    return 0;
}

// The filter types of the rows of an 8-bit non-interlaced PNG (single IDAT, as PngEnc writes).
static int row_filters_within(const uint8_t *png, uint32_t w, uint32_t h, uint32_t ch, unsigned allowed_types) {
    const uint32_t idat_len = be32_test(png + 33);
    if (memcmp(png + 37, "IDAT", 4) != 0) return 0;
    uLongf n = (uLongf)(w * ch + 1) * h;
    uint8_t *raw = malloc(n);
    int ok = uncompress(raw, &n, png + 41, idat_len) == Z_OK && n == (uLongf)(w * ch + 1) * h;
    for (uint32_t y = 0; ok && y < h; y++) ok = (allowed_types >> raw[(uint64_t)y * (w * ch + 1)]) & 1;
    free(raw);
    return ok;
}

// libScePngEnc output: decoded by ImageIO (RGB) and by the decoder (RGBA) back to the input,
// using only the filters the mask allows.
static int test_encode_round_trip(void) {
    setup();
    const uint32_t w = 37, h = 21, pitch = 160;
    uint8_t *src = malloc(pitch * h);
    for (uint32_t i = 0; i < pitch * h; i++) src[i] = (uint8_t)((i * 2654435761u) >> 24);
    uint8_t work[16];
    void *enc = NULL;
    OrbisPngEncCreateParam cp = {sizeof(cp), 0, w, 5};
    CHECK(scePngEncCreate(&cp, work, sizeof(work), &enc) == 0);
    // OrbisPngEncFilterType mask -> PNG filter types it allows (bit n: type n).
    static const uint16_t masks[] = {0, 1, 2, 4, 8, 15, 6};
    static const unsigned types[] = {1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4, 0x1f, (1u << 2) | (1u << 3)};
    for (size_t m = 0; m < sizeof(masks) / sizeof(masks[0]); m++) {
        for (int alpha = 0; alpha < 2; alpha++) {
            const bool bgra = (m & 1) != 0;
            OrbisPngEncEncodeParam ep = {src, g_mem + PNG_AT, pitch * h, 1 << 20, w, h, pitch, bgra ? 1 : 0,
                                         alpha ? 19 : 3, 8, 0, masks[m], (uint16_t)(m == 3 ? 12 : m)};
            OrbisPngEncOutputInfo out;
            const int32_t size = scePngEncEncode(enc, &ep, &out);
            CHECK(size > 0 && out.data_size == (uint32_t)size && out.processed_height == h);
            CHECK(row_filters_within(g_mem + PNG_AT, w, h, alpha ? 4 : 3, types[m]));
            // Decoded RGBA must equal the input (BGRA input swapped back), alpha 0xff when dropped.
            memset(g_mem + IMAGE_AT, 0, w * h * 4);
            CHECK(decode((uint32_t)size, ORBIS_PNG_DEC_PIXEL_FORMAT_R8G8B8A8, 0xff, 0, w * h * 4, NULL) == (int32_t)(w << 16 | h));
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    const uint8_t *in = src + y * pitch + x * 4, *got = g_mem + IMAGE_AT + (y * w + x) * 4;
                    CHECK(got[0] == in[bgra ? 2 : 0] && got[1] == in[1] && got[2] == in[bgra ? 0 : 2]);
                    CHECK(got[3] == (alpha ? in[3] : 0xff));
                }
            }
            if (!alpha) {
                // An independent reader agrees.
                CFDataRef data = CFDataCreate(NULL, g_mem + PNG_AT, size);
                CGImageSourceRef isrc = CGImageSourceCreateWithData(data, NULL);
                CGImageRef img = isrc ? CGImageSourceCreateImageAtIndex(isrc, 0, NULL) : NULL;
                CHECK(img && CGImageGetWidth(img) == w && CGImageGetHeight(img) == h && CGImageGetBitsPerComponent(img) == 8);
                const size_t bpp = CGImageGetBitsPerPixel(img) / 8;
                CFDataRef px = CGDataProviderCopyData(CGImageGetDataProvider(img));
                const uint8_t *p = CFDataGetBytePtr(px);
                for (uint32_t y = 0; y < h; y++)
                    for (uint32_t x = 0; x < w; x++)
                        for (uint32_t c = 0; c < 3; c++)
                            CHECK(p[y * CGImageGetBytesPerRow(img) + x * bpp + c] ==
                                  src[y * pitch + x * 4 + (bgra ? 2 - c : c)]);
                CFRelease(px), CFRelease(img), CFRelease(isrc), CFRelease(data);
            }
        }
    }
    // Too small an output buffer.
    OrbisPngEncEncodeParam ep = {src, g_mem + PNG_AT, pitch * h, 100, w, h, pitch, 0, 19, 8, 0, 15, 6};
    CHECK(scePngEncEncode(enc, &ep, NULL) == ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW);
    free(src);
    printf("PASS encode-round-trip\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    if (strcmp(c, "create") == 0) return test_create();
    if (strcmp(c, "header") == 0) return test_header();
    if (strcmp(c, "writer") == 0) return test_writer();
    if (strcmp(c, "formats") == 0) return test_formats();
    if (strcmp(c, "layout") == 0) return test_layout();
    if (strcmp(c, "encode-round-trip") == 0) return test_encode_round_trip();
    return 2;
}
