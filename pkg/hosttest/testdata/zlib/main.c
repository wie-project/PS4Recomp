// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_zlib.h"
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

// Library state is checked before the arguments, as in shadPS4.
static int test_order(void) {
    uint64_t id = 0;
    uint32_t len = 0;
    int32_t status = 0;
    CHECK(sceZlibInflate(NULL, 0, NULL, 0, NULL) == ORBIS_ZLIB_ERROR_NOT_INITIALIZED);
    CHECK(sceZlibWaitForDone(NULL, NULL) == ORBIS_ZLIB_ERROR_NOT_INITIALIZED);
    CHECK(sceZlibGetResult(1, NULL, NULL) == ORBIS_ZLIB_ERROR_NOT_INITIALIZED);
    CHECK(sceZlibFinalize() == ORBIS_ZLIB_ERROR_NOT_INITIALIZED);

    CHECK(sceZlibInitialize(NULL, 0) == 0);
    CHECK(sceZlibInitialize(NULL, 0) == ORBIS_ZLIB_ERROR_ALREADY_INITIALIZED);
    uint8_t src[4] = {0}, dst[2048];
    CHECK(sceZlibInflate(src, 4, dst, 1000, &id) == ORBIS_ZLIB_ERROR_INVALID); // not 2 KiB units
    CHECK(sceZlibInflate(src, 4, dst, 66 * 1024, &id) == ORBIS_ZLIB_ERROR_INVALID); // over 64 KiB
    CHECK(sceZlibInflate(src, 4, dst, 2048, NULL) == ORBIS_ZLIB_ERROR_INVALID);
    CHECK(sceZlibWaitForDone(NULL, NULL) == ORBIS_ZLIB_ERROR_INVALID);
    CHECK(sceZlibGetResult(1, NULL, &status) == ORBIS_ZLIB_ERROR_INVALID);
    CHECK(sceZlibGetResult(99, &len, &status) == ORBIS_ZLIB_ERROR_NOT_FOUND);
    const uint32_t timeout = 10;
    CHECK(sceZlibWaitForDone(&id, &timeout) == ORBIS_ZLIB_ERROR_TIMEDOUT);
    CHECK(sceZlibFinalize() == 0);
    printf("PASS order\n");
    return 0;
}

static int test_inflate(void) {
    static uint8_t plain[6000], packed[8192], out[8192], small[2048];
    for (size_t i = 0; i < sizeof(plain); i++) plain[i] = (uint8_t)(i * 7 + i / 100);
    uLongf packed_len = sizeof(packed);
    CHECK(compress(packed, &packed_len, plain, sizeof(plain)) == Z_OK);

    CHECK(sceZlibInitialize(NULL, 0) == 0);
    uint64_t id1 = 0, id2 = 0, done = 0;
    CHECK(sceZlibInflate(packed, (uint32_t)packed_len, out, sizeof(out), &id1) == 0);
    CHECK(sceZlibInflate(packed, (uint32_t)packed_len, small, sizeof(small), &id2) == 0);
    CHECK(id1 == 1 && id2 == 2);

    uint32_t len = 0;
    int32_t status = 0;
    CHECK(sceZlibWaitForDone(&done, NULL) == 0 && done == id1);
    CHECK(sceZlibGetResult(id1, &len, &status) == 0);
    CHECK(status == 0 && len == sizeof(plain) && memcmp(out, plain, sizeof(plain)) == 0);
    CHECK(sceZlibWaitForDone(&done, NULL) == 0 && done == id2);
    CHECK(sceZlibGetResult(id2, &len, &status) == 0 && status == ORBIS_ZLIB_ERROR_NOSPACE);

    uint8_t junk[16];
    memset(junk, 0x77, sizeof(junk));
    uint64_t id3 = 0;
    CHECK(sceZlibInflate(junk, sizeof(junk), out, 2048, &id3) == 0);
    CHECK(sceZlibWaitForDone(&done, NULL) == 0 && done == id3);
    CHECK(sceZlibGetResult(id3, &len, &status) == 0 && status == ORBIS_ZLIB_ERROR_FATAL);
    CHECK(sceZlibFinalize() == 0);

    // A new session starts numbering again and has no old results.
    CHECK(sceZlibInitialize(NULL, 0) == 0);
    CHECK(sceZlibGetResult(id1, &len, &status) == ORBIS_ZLIB_ERROR_NOT_FOUND);
    CHECK(sceZlibInflate(packed, (uint32_t)packed_len, out, sizeof(out), &id1) == 0 && id1 == 1);
    CHECK(sceZlibFinalize() == 0);
    printf("PASS inflate\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "order") == 0) return test_order();
    if (strcmp(argv[1], "inflate") == 0) return test_inflate();
    return 2;
}
