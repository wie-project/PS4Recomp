// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_pngenc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_pngenc_lifecycle(void) {
    OrbisPngEncCreateParam param;
    memset(&param, 0, sizeof(param));
    param.this_size = sizeof(param);
    param.attribute = 0;
    param.max_image_width = 1920;
    param.max_filter_number = 0;

    // QueryMemorySize
    assert(scePngEncQueryMemorySize(NULL) == ORBIS_PNG_ENC_ERROR_INVALID_ADDR);
    param.attribute = 1;
    assert(scePngEncQueryMemorySize(&param) == ORBIS_PNG_ENC_ERROR_INVALID_PARAM);
    param.attribute = 0;
    param.max_image_width = 0;
    assert(scePngEncQueryMemorySize(&param) == ORBIS_PNG_ENC_ERROR_INVALID_SIZE);
    param.max_image_width = 1920;

    int32_t mem_size = scePngEncQueryMemorySize(&param);
    assert(mem_size > 0);

    // Create
    void *work_mem = malloc((size_t)mem_size);
    assert(work_mem != NULL);
    void *handle = NULL;

    assert(scePngEncCreate(NULL, work_mem, (uint32_t)mem_size, &handle) == ORBIS_PNG_ENC_ERROR_INVALID_ADDR);
    param.attribute = 1;
    assert(scePngEncCreate(&param, work_mem, (uint32_t)mem_size, &handle) == ORBIS_PNG_ENC_ERROR_INVALID_ADDR);
    param.attribute = 0;
    assert(scePngEncCreate(&param, NULL, (uint32_t)mem_size, &handle) == ORBIS_PNG_ENC_ERROR_INVALID_ADDR);
    assert(scePngEncCreate(&param, work_mem, (uint32_t)mem_size, NULL) == ORBIS_PNG_ENC_ERROR_INVALID_ADDR);
    param.max_image_width = 1000002; // width - 1 must not exceed 1000000
    assert(scePngEncCreate(&param, work_mem, (uint32_t)mem_size, &handle) == ORBIS_PNG_ENC_ERROR_INVALID_SIZE);
    param.max_image_width = 1920;

    assert(scePngEncCreate(&param, work_mem, (uint32_t)mem_size, &handle) == 0);
    assert(handle == work_mem);

    // Delete
    assert(scePngEncDelete(NULL) == ORBIS_PNG_ENC_ERROR_INVALID_HANDLE);
    assert(scePngEncDelete(handle) == 0);

    free(work_mem);
    printf("PASS pngenc-lifecycle\n");
}

static void test_pngenc_encode(void) {
    OrbisPngEncCreateParam create_param = {
        .this_size = sizeof(create_param),
        .attribute = 0,
        .max_image_width = 64,
        .max_filter_number = 0
    };
    int32_t mem_size = scePngEncQueryMemorySize(&create_param);
    void *work_mem = malloc((size_t)mem_size);
    void *handle = NULL;
    assert(scePngEncCreate(&create_param, work_mem, (uint32_t)mem_size, &handle) == 0);

    // 4x4 RGBA test pixels
    uint32_t pixels[16];
    for (int i = 0; i < 16; i++) {
        pixels[i] = 0xFF0000FF; // Red with full alpha
    }

    uint8_t png_out[4096];
    memset(png_out, 0, sizeof(png_out));

    OrbisPngEncEncodeParam enc_param = {
        .image_mem_addr = (const uint8_t *)pixels,
        .png_mem_addr = png_out,
        .image_mem_size = sizeof(pixels),
        .png_mem_size = sizeof(png_out),
        .image_width = 4,
        .image_height = 4,
        .image_pitch = 4 * 4,
        .pixel_format = 0, // R8G8B8A8
        .color_space = 19, // RGBA
        .bit_depth = 8,
        .clut_number = 0,
        .filter_type = 0,
        .compression_level = 6
    };

    OrbisPngEncOutputInfo out_info;
    memset(&out_info, 0, sizeof(out_info));

    // Test invalid handle / params
    assert(scePngEncEncode(NULL, &enc_param, &out_info) == ORBIS_PNG_ENC_ERROR_INVALID_HANDLE);
    assert(scePngEncEncode(handle, NULL, &out_info) == ORBIS_PNG_ENC_ERROR_INVALID_PARAM);
    enc_param.image_height = 0;
    assert(scePngEncEncode(handle, &enc_param, &out_info) == ORBIS_PNG_ENC_ERROR_INVALID_SIZE);
    enc_param.image_height = 4;
    enc_param.image_mem_size = sizeof(pixels) - 1;
    assert(scePngEncEncode(handle, &enc_param, &out_info) == ORBIS_PNG_ENC_ERROR_INVALID_SIZE);
    enc_param.image_mem_size = sizeof(pixels);

    // Encode
    int32_t enc_rc = scePngEncEncode(handle, &enc_param, &out_info);
    assert(enc_rc > 0);
    assert(out_info.data_size == (uint32_t)enc_rc);
    assert(out_info.processed_height == 4);

    // Verify PNG header signature: 89 50 4E 47 0D 0A 1A 0A
    static const uint8_t expected_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    assert(memcmp(png_out, expected_sig, 8) == 0);

    // Verify IHDR chunk exists right after signature
    assert(memcmp(png_out + 12, "IHDR", 4) == 0);

    // Verify IEND chunk at the end
    assert(memcmp(png_out + enc_rc - 8, "IEND", 4) == 0);

    // Buffer overflow check
    enc_param.png_mem_size = 20; // Too small
    assert(scePngEncEncode(handle, &enc_param, &out_info) == ORBIS_PNG_ENC_ERROR_DATA_OVERFLOW);

    scePngEncDelete(handle);
    free(work_mem);
    printf("PASS pngenc-encode\n");
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *name = argv[1];
    if (!strcmp(name, "pngenc-lifecycle")) test_pngenc_lifecycle();
    else if (!strcmp(name, "pngenc-encode")) test_pngenc_encode();
    else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
