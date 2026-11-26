// SPDX-License-Identifier: GPL-2.0-or-later

// libSceVideoRecording and libSceRudp against shadPS4: sceVideoRecordingSetInfo's accepted info
// values, the functions shadPS4 leaves to its unresolved stub (0, nothing written), and the
// Rudp stubs.
#include "ps4_rudp.h"
#include "ps4_video_recording.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define MEM_SIZE 0x10000ull
#define STACK_TOP 0x8000ull
#define DATA 0x100ull
#define OUT 0x200ull

static uint8_t mem[MEM_SIZE];
static GuestContext ctx;

static int64_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2) {
    ctx.rsp -= 8;
    ctx.rdi = a0, ctx.rsi = a1, ctx.rdx = a2;
    shim(&ctx);
    if (ctx.rsp != STACK_TOP) {
        fprintf(stderr, "shim left rsp at 0x%llx\n", (unsigned long long)ctx.rsp);
        exit(1);
    }
    return (int64_t)ctx.rax;
}

static int test_video_recording(void) {
    static const struct {
        uint32_t info;
        int ok;
    } cases[] = {
        {0x0, 0},   {0x1, 0},    {0x2, 1},    {0x3, 0},    {0x5, 0},    {0x6, 1},    {0x7, 1},
        {0x8, 1},   {0x9, 0},    {0xC, 0},    {0xD, 1},    {0xE, 0},    {0xA01, 0},  {0xA006, 0},
        {0xA007, 1}, {0xA008, 1}, {0xA009, 1}, {0xA00A, 0}, {0xFFFFFFFF, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int32_t want = cases[i].ok ? 0 : ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE;
        if (sceVideoRecordingSetInfo(cases[i].info, mem + DATA, 4) != want) {
            fprintf(stderr, "SetInfo(0x%x)\n", cases[i].info);
            return 1;
        }
    }
    // The info value is checked before the data pointer.
    CHECK(sceVideoRecordingSetInfo(ORBIS_VIDEO_RECORDING_INFO_SUBTITLE, NULL, 0) ==
          ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE);
    CHECK(call(shim_sceVideoRecordingSetInfo, ORBIS_VIDEO_RECORDING_INFO_CHAPTER, DATA, 4) == 0);
    CHECK(call(shim_sceVideoRecordingSetInfo, ORBIS_VIDEO_RECORDING_INFO_CHAPTER, 0, 4) ==
          (int64_t)ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE);
    CHECK(call(shim_sceVideoRecordingSetInfo, 0x4, DATA, 4) == (int64_t)ORBIS_VIDEO_RECORDING_ERROR_INVALID_VALUE);

    // shadPS4's unresolved stub: 0 and no output written.
    memset(mem + OUT, 0xAB, 16);
    CHECK(call(shim_sceVideoRecordingQueryMemSize2, DATA, OUT, 0) == 0);
    CHECK(call(shim_sceVideoRecordingOpen2, DATA, OUT, 0) == 0);
    CHECK(call(shim_sceVideoRecordingStart, 0, 0, 0) == 0);
    CHECK(call(shim_sceVideoRecordingGetStatus, 0, 0, 0) == 0);
    CHECK(call(shim_sceVideoRecordingStop, 0, 0, 0) == 0);
    CHECK(call(shim_sceVideoRecordingClose, 0, 0, 0) == 0);
    for (int i = 0; i < 16; i++) CHECK(mem[OUT + i] == 0xAB);
    puts("PASS video-recording");
    return 0;
}

static int test_rudp(void) {
    CHECK(call(shim_sceRudpInit, 0, 0, 0) == 0);
    CHECK(call(shim_sceRudpCreateContext, 0, 0, OUT) == 0);
    CHECK(call(shim_sceRudpCreateContext, 0, 0, OUT + 4) == 0);
    const int32_t *ids = (const int32_t *)(mem + OUT);
    CHECK(ids[0] > 0 && ids[1] > 0 && ids[0] != ids[1]);
    CHECK(call(shim_sceRudpCreateContext, 0, 0, 0) == 0);
    CHECK(call(shim_sceRudpGetSizeWritable, (uint64_t)ids[0], 0, 0) == 0);
    CHECK(call(shim_sceRudpGetSizeReadable, (uint64_t)ids[0], 0, 0) == 0);
    CHECK(call(shim_sceRudpWrite, (uint64_t)ids[0], DATA, 16) == 0);
    CHECK(call(shim_sceRudpRead, (uint64_t)ids[0], DATA, 16) == 0);
    CHECK(call(shim_sceRudpTerminate, 0, 0, 0) == 0);
    puts("PASS rudp");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    ctx.mem_base = mem;
    ctx.mem_size = MEM_SIZE;
    ctx.rsp = STACK_TOP;
    if (strcmp(argv[1], "video-recording") == 0) return test_video_recording();
    if (strcmp(argv[1], "rudp") == 0) return test_rudp();
    return 2;
}
