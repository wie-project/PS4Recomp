// SPDX-License-Identifier: GPL-2.0-or-later

// libSceJpegEnc and libSceJpegDec on host memory (guest base 0, so guest addresses are host
// pointers): parameter checks, handles, encode -> parse -> decode round trips.

#include "ps4_jpegdec.h"
#include "ps4_jpegenc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define A(p) ((uint64_t)(uintptr_t)(p))
#define W 64
#define H 32

// Four 16-pixel-wide vertical bars: red, green, blue, gray; JPEG blocks never straddle two.
static const uint8_t k_bars[4][3] = {{220, 30, 30}, {30, 200, 40}, {40, 50, 210}, {128, 128, 128}};

static int near(int a, int b) {
    return abs(a - b) <= 6;
}

static uint64_t enc_handle(void) {
    static uint8_t mem[0x900] __attribute__((aligned(64)));
    const OrbisJpegEncCreateParam cp = {sizeof(cp), 0};
    uint64_t h = 0;
    return sceJpegEncCreate(NULL, &cp, A(mem + 1), sizeof(mem) - 1, &h) == 0 ? h : 0;
}

static uint64_t dec_handle(uint32_t max_width) {
    static uint8_t mem[0x40000] __attribute__((aligned(64)));
    const OrbisJpegDecCreateParam cp = {sizeof(cp), 1, max_width};
    uint64_t h = 0;
    return sceJpegDecCreate(NULL, &cp, A(mem), sizeof(mem), &h) == 0 ? h : 0;
}

// Encodes the bars as RGBA (or BGRA) into jpeg; returns its size or 0.
static uint32_t encode_bars(uint8_t *jpeg, uint32_t jpeg_cap, int bgra) {
    static uint8_t img[H][W * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint8_t *c = k_bars[x / 16];
            img[y][x * 4 + 0] = bgra ? c[2] : c[0];
            img[y][x * 4 + 1] = c[1];
            img[y][x * 4 + 2] = bgra ? c[0] : c[2];
            img[y][x * 4 + 3] = 0x11;
        }
    }
    OrbisJpegEncEncodeParam p = {A(img), A(jpeg), sizeof(img), jpeg_cap, W, H, W * 4,
                                 bgra ? ORBIS_JPEG_ENC_PIXEL_FORMAT_B8G8R8A8 : ORBIS_JPEG_ENC_PIXEL_FORMAT_R8G8B8A8,
                                 ORBIS_JPEG_ENC_ENCODE_MODE_NORMAL, ORBIS_JPEG_ENC_COLOR_SPACE_YCC,
                                 ORBIS_JPEG_ENC_SAMPLING_TYPE_420, 100, 0};
    OrbisJpegEncOutputInfo info = {0, 0};
    if (sceJpegEncEncode(NULL, enc_handle(), &p, &info) != 0 || info.height != H) return 0;
    return info.size;
}

static int test_enc_checks(void) {
    OrbisJpegEncCreateParam cp = {sizeof(cp), 0};
    CHECK(sceJpegEncQueryMemorySize(&cp) == ORBIS_JPEG_ENC_MINIMUM_MEMORY_SIZE);
    CHECK(sceJpegEncQueryMemorySize(NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_ADDR);
    cp.size = 4;
    CHECK(sceJpegEncQueryMemorySize(&cp) == ORBIS_JPEG_ENC_ERROR_INVALID_SIZE);
    cp.size = sizeof(cp), cp.attr = 1;
    CHECK(sceJpegEncQueryMemorySize(&cp) == ORBIS_JPEG_ENC_ERROR_INVALID_PARAM);
    cp.attr = 0;

    static uint8_t mem[0x900] __attribute__((aligned(64)));
    uint64_t h = 0;
    CHECK(sceJpegEncCreate(NULL, &cp, A(mem), 0x7FF, &h) == ORBIS_JPEG_ENC_ERROR_INVALID_SIZE);
    CHECK(sceJpegEncCreate(NULL, &cp, 0, 0x800, &h) == ORBIS_JPEG_ENC_ERROR_INVALID_ADDR);
    CHECK(sceJpegEncCreate(NULL, &cp, A(mem + 3), 0x800, &h) == 0 && h == A(mem + 32));
    CHECK(sceJpegEncDelete(NULL, h) == 0);
    CHECK(sceJpegEncDelete(NULL, h) == ORBIS_JPEG_ENC_ERROR_INVALID_HANDLE);
    CHECK(sceJpegEncDelete(NULL, h + 8) == ORBIS_JPEG_ENC_ERROR_INVALID_HANDLE);

    static uint8_t img[16 * 16 * 4] __attribute__((aligned(4)));
    static uint8_t out[4096];
    h = enc_handle();
    OrbisJpegEncEncodeParam p = {A(img), A(out), sizeof(img), sizeof(out), 16, 16, 64,
                                 ORBIS_JPEG_ENC_PIXEL_FORMAT_R8G8B8A8, 0, ORBIS_JPEG_ENC_COLOR_SPACE_YCC,
                                 ORBIS_JPEG_ENC_SAMPLING_TYPE_420, 90, 0};
    CHECK(sceJpegEncEncode(NULL, 0, &p, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_HANDLE);
    OrbisJpegEncEncodeParam bad = p;
    bad.sampling_type = ORBIS_JPEG_ENC_SAMPLING_TYPE_FULL; // RGBA needs subsampling
    CHECK(sceJpegEncEncode(NULL, h, &bad, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_PARAM);
    bad = p, bad.image_pitch = 60;
    CHECK(sceJpegEncEncode(NULL, h, &bad, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_PARAM);
    bad = p, bad.image = A(img + 1);
    CHECK(sceJpegEncEncode(NULL, h, &bad, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_ADDR);
    bad = p, bad.color_space = ORBIS_JPEG_ENC_COLOR_SPACE_GRAYSCALE;
    CHECK(sceJpegEncEncode(NULL, h, &bad, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_PARAM);
    bad = p, bad.image_size = 16 * 64 - 1;
    CHECK(sceJpegEncEncode(NULL, h, &bad, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_PARAM);
    bad = p, bad.jpeg_size = 16; // the JPEG does not fit
    CHECK(sceJpegEncEncode(NULL, h, &bad, NULL) == ORBIS_JPEG_ENC_ERROR_INVALID_SIZE);
    OrbisJpegEncOutputInfo info;
    CHECK(sceJpegEncEncode(NULL, h, &p, &info) == 0 && info.size > 100 && info.size <= sizeof(out));
    CHECK(out[0] == 0xFF && out[1] == 0xD8 && out[info.size - 2] == 0xFF && out[info.size - 1] == 0xD9);
    printf("PASS enc-checks\n");
    return 0;
}

static int test_dec_checks(void) {
    OrbisJpegDecCreateParam cp = {sizeof(cp), 1, 1920};
    CHECK(sceJpegDecQueryMemorySize(&cp) == 134080);
    cp.decode_mode = 2, cp.max_width = 100;
    CHECK(sceJpegDecQueryMemorySize(&cp) == 53440);
    cp.decode_mode = 3;
    CHECK(sceJpegDecQueryMemorySize(&cp) == ORBIS_JPEG_DEC_ERROR_INVALID_PARAM);
    cp.decode_mode = 1, cp.max_width = 0x10000;
    CHECK(sceJpegDecQueryMemorySize(&cp) == ORBIS_JPEG_DEC_ERROR_INVALID_PARAM);
    cp.size = 8, cp.max_width = 64;
    CHECK(sceJpegDecQueryMemorySize(&cp) == ORBIS_JPEG_DEC_ERROR_INVALID_SIZE);
    cp.size = sizeof(cp);

    static uint8_t mem[0x10000] __attribute__((aligned(64)));
    uint64_t h = 0;
    CHECK(sceJpegDecCreate(NULL, &cp, A(mem), 100, &h) == ORBIS_JPEG_DEC_ERROR_INVALID_SIZE);
    CHECK(sceJpegDecCreate(NULL, &cp, A(mem + 5), sizeof(mem) - 5, &h) == 0 && h == A(mem + 32));
    CHECK(((OrbisJpegDecHandleInternal *)(uintptr_t)h)->max_width == 64);
    CHECK(sceJpegDecDelete(NULL, h) == 0 && sceJpegDecDelete(NULL, h) == ORBIS_JPEG_DEC_ERROR_INVALID_HANDLE);

    // A hand-written header: SOI, an APP0 segment, SOF0 640x480 with 4:2:0 sampling.
    static const uint8_t hdr[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xC0, 0x00, 0x11,
                                  0x08, 0x01, 0xE0, 0x02, 0x80, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01,
                                  0x03, 0x11, 0x01, 0xFF, 0xD9};
    OrbisJpegDecParseParam pp = {A(hdr), sizeof(hdr), 0, 4};
    OrbisJpegDecImageInfo info;
    CHECK(sceJpegDecParseHeader(NULL, &pp, &info) == 0);
    CHECK(info.image_width == 640 && info.image_height == 480 && info.num_components == 3);
    CHECK(info.color_space == ORBIS_JPEG_DEC_COLOR_SPACE_YCC && info.sampling_factor[0] == 0x22);
    CHECK(info.output_width == 160 && info.output_height == 120 && info.required_decode_mode == 1);
    pp.downscale_ratio = 3;
    CHECK(sceJpegDecParseHeader(NULL, &pp, &info) == ORBIS_JPEG_DEC_ERROR_INVALID_PARAM);
    pp.downscale_ratio = 1, pp.jpeg_size = 3;
    CHECK(sceJpegDecParseHeader(NULL, &pp, &info) == ORBIS_JPEG_DEC_ERROR_INVALID_DATA);
    static const uint8_t no_sof[] = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x00};
    pp.jpeg_addr = A(no_sof), pp.jpeg_size = sizeof(no_sof);
    CHECK(sceJpegDecParseHeader(NULL, &pp, &info) == ORBIS_JPEG_DEC_ERROR_DECODE_ERROR);
    printf("PASS dec-checks\n");
    return 0;
}

static int test_round_trip(void) {
    static uint8_t jpeg[16384];
    const uint32_t size = encode_bars(jpeg, sizeof(jpeg), 0);
    CHECK(size > 0);
    OrbisJpegDecParseParam pp = {A(jpeg), size, 0, 1};
    OrbisJpegDecImageInfo info;
    CHECK(sceJpegDecParseHeader(NULL, &pp, &info) == 0 && info.image_width == W && info.image_height == H);

    const uint64_t h = dec_handle(W);
    static uint8_t out[H][W * 4 + 16];
    OrbisJpegDecDecodeParam dp = {A(jpeg), A(out), 0, size, sizeof(out), 0, 0, 1,
                                  ORBIS_JPEG_DEC_PIXEL_FORMAT_R8G8B8A8, 0, W * 4 + 16};
    memset(out, 0x5A, sizeof(out));
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == ((W << 16) | H));
    CHECK(info.output_width == W && info.num_components == 3);
    for (int y = 0; y < H; y += 7) {
        for (int x = 4; x < W; x += 16) {
            const uint8_t *px = &out[y][x * 4];
            const uint8_t *c = k_bars[x / 16];
            CHECK(near(px[0], c[0]) && near(px[1], c[1]) && near(px[2], c[2]) && px[3] == 0xFF);
        }
        CHECK(out[y][W * 4] == 0x5A); // the pitch padding is not written
    }

    // BGRA with a fixed alpha, downscaled by 4.
    dp.pixel_format = ORBIS_JPEG_DEC_PIXEL_FORMAT_B8G8R8A8, dp.alpha_value = 0x80, dp.downscale_ratio = 4;
    dp.image_pitch = 0;
    static uint8_t small[(H / 4) * (W / 4) * 4];
    dp.image_addr = A(small), dp.image_size = sizeof(small);
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == (((W / 4) << 16) | (H / 4)));
    for (int x = 1; x < W / 4; x += 4) {
        const uint8_t *px = &small[(2 * (W / 4) + x) * 4];
        const uint8_t *c = k_bars[x * 4 / 16];
        CHECK(near(px[2], c[0]) && near(px[1], c[1]) && near(px[0], c[2]) && px[3] == 0x80);
    }

    // Y8: BT.601 luma of each bar.
    static uint8_t luma[H * W];
    dp.pixel_format = ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8, dp.downscale_ratio = 1, dp.alpha_value = 0;
    dp.image_addr = A(luma), dp.image_size = sizeof(luma);
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == ((W << 16) | H));
    for (int b = 0; b < 4; b++) {
        const int want = (k_bars[b][0] * 77 + k_bars[b][1] * 150 + k_bars[b][2] * 29) >> 8;
        CHECK(near(luma[5 * W + b * 16 + 8], want));
    }

    // Parameter and data errors.
    dp.image_size = sizeof(luma) - 1;
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == ORBIS_JPEG_DEC_ERROR_INVALID_SIZE);
    dp.image_size = sizeof(luma), dp.pixel_format = ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8U8Y8V8;
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == ORBIS_JPEG_DEC_ERROR_UNSUPPORT_DATA);
    dp.pixel_format = 2;
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == ORBIS_JPEG_DEC_ERROR_INVALID_PARAM);
    dp.pixel_format = ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8;
    CHECK(sceJpegDecDecode(NULL, dec_handle(32), &dp, &info) == ORBIS_JPEG_DEC_ERROR_EXCEED_MAX_WIDTH);
    jpeg[0] = 0;
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) == ORBIS_JPEG_DEC_ERROR_DECODE_ERROR);
    printf("PASS round-trip\n");
    return 0;
}

static int test_enc_formats(void) {
    static uint8_t jpeg[16384];
    static uint8_t out[H * W * 4];
    const uint64_t h = dec_handle(W);
    OrbisJpegDecImageInfo info;

    // BGRA input decodes to the same colors.
    uint32_t size = encode_bars(jpeg, sizeof(jpeg), 1);
    CHECK(size > 0);
    OrbisJpegDecDecodeParam dp = {A(jpeg), A(out), 0, size, sizeof(out), 0, 0, 1,
                                  ORBIS_JPEG_DEC_PIXEL_FORMAT_R8G8B8A8, 0, 0};
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) > 0);
    CHECK(near(out[(3 * W + 40) * 4 + 2], k_bars[2][2]) && near(out[(3 * W + 40) * 4 + 0], k_bars[2][0]));

    // Y8 input gives a one-component JPEG.
    static uint8_t gray[H][W];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) gray[y][x] = (uint8_t)(x < 32 ? 40 : 200);
    OrbisJpegEncEncodeParam p = {A(gray), A(jpeg), sizeof(gray), sizeof(jpeg), W, H, W,
                                 ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8, 0, ORBIS_JPEG_ENC_COLOR_SPACE_GRAYSCALE,
                                 ORBIS_JPEG_ENC_SAMPLING_TYPE_FULL, 100, 0};
    OrbisJpegEncOutputInfo oi;
    CHECK(sceJpegEncEncode(NULL, enc_handle(), &p, &oi) == 0);
    OrbisJpegDecParseParam pp = {A(jpeg), oi.size, 0, 1};
    CHECK(sceJpegDecParseHeader(NULL, &pp, &info) == 0 && info.num_components == 1 &&
          info.color_space == ORBIS_JPEG_DEC_COLOR_SPACE_GRAYSCALE);
    dp.jpeg_size = oi.size, dp.pixel_format = ORBIS_JPEG_DEC_PIXEL_FORMAT_Y8;
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) > 0 && info.num_components == 1);
    CHECK(near(out[4 * W + 8], 40) && near(out[4 * W + 56], 200));

    // Y8U8Y8V8 input: a pure red in full-range YCbCr is Y 76, U 85, V 255.
    static uint8_t yuyv[H][W * 2] __attribute__((aligned(4)));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x += 2) {
            yuyv[y][x * 2 + 0] = 76, yuyv[y][x * 2 + 1] = 85, yuyv[y][x * 2 + 2] = 76, yuyv[y][x * 2 + 3] = 255;
        }
    p = (OrbisJpegEncEncodeParam){A(yuyv), A(jpeg), sizeof(yuyv), sizeof(jpeg), W, H, W * 2,
                                  ORBIS_JPEG_ENC_PIXEL_FORMAT_Y8U8Y8V8, 0, ORBIS_JPEG_ENC_COLOR_SPACE_YCC,
                                  ORBIS_JPEG_ENC_SAMPLING_TYPE_422, 100, 0};
    CHECK(sceJpegEncEncode(NULL, enc_handle(), &p, &oi) == 0);
    dp.jpeg_size = oi.size, dp.pixel_format = ORBIS_JPEG_DEC_PIXEL_FORMAT_R8G8B8A8;
    CHECK(sceJpegDecDecode(NULL, h, &dp, &info) > 0);
    const uint8_t *px = &out[(10 * W + 10) * 4];
    CHECK(near(px[0], 254) && near(px[1], 0) && near(px[2], 0));
    printf("PASS enc-formats\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "enc-checks") == 0) return test_enc_checks();
    if (strcmp(argv[1], "dec-checks") == 0) return test_dec_checks();
    if (strcmp(argv[1], "round-trip") == 0) return test_round_trip();
    if (strcmp(argv[1], "enc-formats") == 0) return test_enc_formats();
    return 2;
}
