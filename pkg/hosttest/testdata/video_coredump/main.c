// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCoredump, libSceVideoCoreServerInterface, libSceVideodec, libSceVideodec2 and libSceVdecsw
// against shadPS4's rules: handler registration, decoder resource bookkeeping, the argument /
// struct size / handle checks and their order, the memory sizes the queries report, and the
// blocking Vdecsw sync calls. Guest structures are written at the offsets of the PS4 layouts.
#include "ps4_coredump.h"
#include "ps4_videocore_server.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Runtime symbol the decoders link against (GPU cache invalidation of the written frames).
void ps4_gnm_notify_cpu_write(uint64_t addr, uint64_t size) {
    (void)addr;
    (void)size;
}

void shim_sceVideodecQueryResourceInfo(GuestContext *ctx);
void shim_sceVideodecCreateDecoder(GuestContext *ctx);
void shim_sceVideodecDecode(GuestContext *ctx);
void shim_sceVideodecFlush(GuestContext *ctx);
void shim_sceVideodecReset(GuestContext *ctx);
void shim_sceVideodecDeleteDecoder(GuestContext *ctx);
void shim_sceVideodecMapMemory(GuestContext *ctx);
void shim_sceVideodec2QueryComputeMemoryInfo(GuestContext *ctx);
void shim_sceVideodec2QueryDecoderMemoryInfo(GuestContext *ctx);
void shim_sceVideodec2CreateDecoder(GuestContext *ctx);
void shim_sceVideodec2Decode(GuestContext *ctx);
void shim_sceVideodec2DeleteDecoder(GuestContext *ctx);
void shim_sceVdecswCreateDecoder(GuestContext *ctx);
void shim_sceVdecswDeleteDecoder(GuestContext *ctx);
void shim_sceVdecswSyncDecodeInput(GuestContext *ctx);
void shim_sceVdecswSetDecodeOutput(GuestContext *ctx);
void shim_sceVdecswSyncDecodeOutput(GuestContext *ctx);
void shim_sceVdecswTrySyncDecodeOutput(GuestContext *ctx);
void shim_sceVdecswFinalizeDecodeSequence(GuestContext *ctx);
void shim_sceVdecswGetPictureInfo(GuestContext *ctx);
void shim_sceVdecswGetHevcPictureInfo(GuestContext *ctx);

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define MEM_SIZE 0x1000000ull
#define STACK_TOP 0x10000ull
#define CFG 0x20000ull
#define RSRC 0x21000ull
#define CTRL 0x22000ull
#define FB 0x23000ull
#define IN 0x24000ull
#define PIC 0x25000ull
#define OUT 0x26000ull
#define AU 0x27000ull
#define FB_DATA 0x100000ull

#define VIDEODEC_ERROR_STRUCT_SIZE ((int32_t)0x80C10002)
#define VIDEODEC_ERROR_HANDLE ((int32_t)0x80C10003)
#define VIDEODEC_ERROR_ARGUMENT_POINTER ((int32_t)0x80C1000F)
#define VIDEODEC2_ERROR_STRUCT_SIZE ((int32_t)0x811D0101)
#define VDECSW_ERROR_STRUCT_SIZE ((int32_t)0x81510101)
#define VDECSW_ERROR_DECODER_INSTANCE ((int32_t)0x81510103)
#define VDECSW_ERROR_INPUT_QUEUE_EMPTY ((int32_t)0x81510116)
#define VDECSW_ERROR_DECODE_PENDING ((int32_t)0x81510117)
#define VDECSW_ERROR_OUTPUT_BUFFER_EMPTY ((int32_t)0x81510118)

static uint8_t *mem;

static uint64_t *u64_at(uint64_t a) { return (uint64_t *)(mem + a); }
static uint32_t *u32_at(uint64_t a) { return (uint32_t *)(mem + a); }

static GuestContext *new_ctx(void) {
    GuestContext *c = calloc(1, sizeof(GuestContext));
    CHECK(c);
    c->mem_base = mem;
    c->mem_size = MEM_SIZE;
    return c;
}

static int32_t call(GuestContext *c, void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2,
                    uint64_t a3) {
    c->rsp = STACK_TOP;
    c->rdi = a0, c->rsi = a1, c->rdx = a2, c->rcx = a3;
    shim(c);
    CHECK(c->rsp == STACK_TOP + 8);
    return (int32_t)c->rax;
}

static int test_coredump(void) {
    CHECK(sceCoredumpUnregisterCoredumpHandler() == ORBIS_COREDUMP_ERROR_NOT_REGISTERED);
    CHECK(sceCoredumpRegisterCoredumpHandler(0, 0x4000, 0) == ORBIS_COREDUMP_ERROR_PARAM);
    CHECK(sceCoredumpRegisterCoredumpHandler(0x1000, ORBIS_COREDUMP_MAX_STACK_SIZE, 0) == ORBIS_COREDUMP_ERROR_PARAM);
    CHECK(ps4_coredump_handler() == 0);
    CHECK(sceCoredumpRegisterCoredumpHandler(0x1000, ORBIS_COREDUMP_MAX_STACK_SIZE - 1, 0x2000) == 0);
    CHECK(ps4_coredump_handler() == 0x1000 && ps4_coredump_common() == 0x2000);
    // A second handler is refused even with bad parameters: the registered check comes first.
    CHECK(sceCoredumpRegisterCoredumpHandler(0, 0, 0) == ORBIS_COREDUMP_ERROR_ALREADY_REGISTERED);
    CHECK(sceCoredumpUnregisterCoredumpHandler() == 0);
    CHECK(ps4_coredump_handler() == 0 && ps4_coredump_common() == 0);
    CHECK(sceCoredumpUnregisterCoredumpHandler() == ORBIS_COREDUMP_ERROR_NOT_REGISTERED);
    GuestContext *c = new_ctx();
    CHECK(call(c, shim_sceCoredumpRegisterCoredumpHandler, 0x3000, 0x8000, 0, 0) == 0);
    CHECK(call(c, shim_sceCoredumpGetStopInfoGpu, 0, 0, 0, 0) == ORBIS_COREDUMP_ERROR_NOT_IN_COREDUMP_HANDLER);
    CHECK(call(c, shim_sceCoredumpUnregisterCoredumpHandler, 0, 0, 0, 0) == 0);
    free(c);
    printf("PASS coredump\n");
    return 0;
}

static int test_videocore(void) {
    const OrbisVideoCoreAcquireDecoderResourceParam p = {1, 1920, 1080, 0};
    uint32_t a = 0, b = 0, c = 0;
    CHECK(ps4_videocore_acquire(NULL, 0x1000, &a, false) == ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_videocore_acquire(&p, 0, &a, false) == ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_videocore_acquire(&p, 0x1000, NULL, false) == ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT);
    CHECK(ps4_videocore_acquire(&p, 0x1000, &a, false) == 0 && a == 1);
    CHECK(ps4_videocore_acquire(&p, 0x1000, &b, true) == 0 && b == 2);
    CHECK(ps4_videocore_resource_count() == 2);
    CHECK(ps4_videocore_release(0, false) == ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND);
    CHECK(ps4_videocore_release(0, true) == ORBIS_VIDEO_CORE_ERROR_BE_WRAPPER_RESOURCE_NOT_FOUND);
    CHECK(ps4_videocore_release(99, false) == ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND);
    // Released only through the family that acquired it.
    CHECK(ps4_videocore_release(a, true) == ORBIS_VIDEO_CORE_ERROR_BE_WRAPPER_RESOURCE_NOT_FOUND);
    CHECK(ps4_videocore_release(b, false) == ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND);
    CHECK(ps4_videocore_release(a, false) == 0);
    CHECK(ps4_videocore_release(a, false) == ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND);
    // Identifiers are not reused.
    CHECK(ps4_videocore_acquire(&p, 0x1000, &c, false) == 0 && c == 3);
    for (int i = 0; i < 20; i++) {
        uint32_t id;
        CHECK(ps4_videocore_acquire(&p, 0x1000, &id, i & 1) == 0 && id == 4u + (uint32_t)i);
    }
    CHECK(ps4_videocore_resource_count() == 22);

    GuestContext *g = new_ctx();
    memcpy(mem + CFG, &p, sizeof(p));
    CHECK(call(g, shim_sceVideoCoreAcquireDecoderResourceForBeWrapper, CFG, 0x5000, OUT, 0) == 0);
    CHECK(*u32_at(OUT) == 24);
    CHECK(call(g, shim_sceVideoCoreReleaseDecoderResource, 24, 0, 0, 0) == ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND);
    CHECK(call(g, shim_sceVideoCoreReleaseDecoderResourceForBeWrapper, 24, 0, 0, 0) == 0);
    CHECK(call(g, shim_sceVideoCoreAcquireDecoderResource, CFG, 0, OUT, 0) == ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT);
    free(g);
    printf("PASS videocore-server\n");
    return 0;
}

// libSceVideodec ConfigInfo (0x28) and ResourceInfo (0x38).
static void videodec_cfg(int32_t w, int32_t h, int32_t dpb, uint32_t level) {
    memset(mem + CFG, 0, 0x28);
    *u64_at(CFG) = 0x28;
    *u32_at(CFG + 0x10) = level;
    *(int32_t *)(mem + CFG + 0x14) = w;
    *(int32_t *)(mem + CFG + 0x18) = h;
    *(int32_t *)(mem + CFG + 0x1C) = dpb;
    memset(mem + RSRC, 0xAB, 0x38);
    *u64_at(RSRC) = 0x38;
}

static int test_videodec(void) {
    GuestContext *c = new_ctx();
    CHECK(call(c, shim_sceVideodecQueryResourceInfo, 0, RSRC, 0, 0) == VIDEODEC_ERROR_ARGUMENT_POINTER);
    videodec_cfg(0, 0, 0, 0);
    *u64_at(CFG) = 0x20;
    CHECK(call(c, shim_sceVideodecQueryResourceInfo, CFG, RSRC, 0, 0) == VIDEODEC_ERROR_STRUCT_SIZE);
    // No dimensions: 1920x1080, pitch 2048, 8 DPB frames + 2.
    videodec_cfg(0, 0, 0, 0);
    CHECK(call(c, shim_sceVideodecQueryResourceInfo, CFG, RSRC, 0, 0) == 0);
    CHECK(*u64_at(RSRC + 0x08) == 16ull << 20);              // cpuMemorySize
    CHECK(*u64_at(RSRC + 0x10) == 0 && *u64_at(RSRC + 0x20) == 0);
    CHECK(*u64_at(RSRC + 0x28) == 0x334000);                  // maxFrameBufferSize
    CHECK(*u64_at(RSRC + 0x18) == 0x334000ull * 10 + (8ull << 20)); // cpuGpuMemorySize
    CHECK(*u32_at(RSRC + 0x30) == 0x100);
    // Level 5.0 and up without dimensions: 3840x2160; explicit DPB count.
    videodec_cfg(0, 0, 4, 150);
    CHECK(call(c, shim_sceVideodecQueryResourceInfo, CFG, RSRC, 0, 0) == 0);
    CHECK(*u64_at(RSRC + 0x28) == 0xBE1800);
    CHECK(*u64_at(RSRC + 0x18) == 0xBE1800ull * 6 + (8ull << 20));

    // CreateDecoder: pointers, then sizes; fills the ctrl with version 1.
    videodec_cfg(1280, 720, 4, 31);
    CHECK(call(c, shim_sceVideodecQueryResourceInfo, CFG, RSRC, 0, 0) == 0);
    CHECK(call(c, shim_sceVideodecCreateDecoder, CFG, RSRC, 0, 0) == VIDEODEC_ERROR_ARGUMENT_POINTER);
    *u64_at(RSRC) = 0x30;
    CHECK(call(c, shim_sceVideodecCreateDecoder, CFG, RSRC, CTRL, 0) == VIDEODEC_ERROR_STRUCT_SIZE);
    *u64_at(RSRC) = 0x38;
    memset(mem + CTRL, 0, 0x18);
    CHECK(call(c, shim_sceVideodecCreateDecoder, CFG, RSRC, CTRL, 0) == 0);
    CHECK(*u64_at(CTRL) == 0x18 && *u64_at(CTRL + 8) != 0 && *u64_at(CTRL + 0x10) == 1);

    // Decode: pointers, then ctrl / frame buffer sizes, then the handle, then the access unit.
    *u64_at(FB) = 0x18;
    *u64_at(FB + 8) = FB_DATA;
    *u64_at(FB + 0x10) = 0x200000;
    memset(mem + IN, 0, 0x30);
    *u64_at(IN) = 0x30;
    memset(mem + PIC, 0, 0x70);
    *u64_at(PIC) = 0x70;
    *u32_at(PIC + 8) = 1;
    CHECK(call(c, shim_sceVideodecDecode, CTRL, IN, FB, 0) == VIDEODEC_ERROR_ARGUMENT_POINTER);
    *u64_at(FB) = 0x10;
    CHECK(call(c, shim_sceVideodecDecode, CTRL, IN, FB, PIC) == VIDEODEC_ERROR_STRUCT_SIZE);
    *u64_at(FB) = 0x18;
    CHECK(*u32_at(PIC + 8) == 1); // untouched before the handle is known
    CHECK(call(c, shim_sceVideodecDecode, CTRL, IN, FB, PIC) == (int32_t)0x80C1000A); // AU_POINTER
    CHECK(*u32_at(PIC + 8) == 0);
    *u64_at(IN + 8) = AU;
    CHECK(call(c, shim_sceVideodecDecode, CTRL, IN, FB, PIC) == (int32_t)0x80C1000B); // AU_SIZE
    // An access unit without a slice decodes to no picture.
    static const uint8_t aud[] = {0, 0, 0, 1, 0x09, 0xF0};
    memcpy(mem + AU, aud, sizeof(aud));
    *u64_at(IN + 0x10) = sizeof(aud);
    CHECK(call(c, shim_sceVideodecDecode, CTRL, IN, FB, PIC) == 0);
    CHECK(*u32_at(PIC + 8) == 0);

    // Flush: frame buffer and picture pointers and sizes, then the handle.
    CHECK(call(c, shim_sceVideodecFlush, CTRL, 0, PIC, 0) == VIDEODEC_ERROR_ARGUMENT_POINTER);
    *u64_at(PIC) = 0x68;
    CHECK(call(c, shim_sceVideodecFlush, CTRL, FB, PIC, 0) == VIDEODEC_ERROR_STRUCT_SIZE);
    *u64_at(PIC) = 0x70;
    CHECK(call(c, shim_sceVideodecFlush, CTRL, FB, PIC, 0) == 0);
    CHECK(*u32_at(PIC + 8) == 0);
    CHECK(call(c, shim_sceVideodecReset, CTRL, 0, 0, 0) == 0);
    CHECK(call(c, shim_sceVideodecMapMemory, 0, 0, 0, 0) == 0);

    // Deleting leaves the ctrl as it is; the handle then names no decoder.
    const uint64_t handle = *u64_at(CTRL + 8);
    CHECK(call(c, shim_sceVideodecDeleteDecoder, CTRL, 0, 0, 0) == 0);
    CHECK(*u64_at(CTRL + 8) == handle);
    CHECK(call(c, shim_sceVideodecDeleteDecoder, CTRL, 0, 0, 0) == VIDEODEC_ERROR_HANDLE);
    CHECK(call(c, shim_sceVideodecReset, CTRL, 0, 0, 0) == VIDEODEC_ERROR_HANDLE);
    CHECK(call(c, shim_sceVideodecFlush, CTRL, FB, PIC, 0) == VIDEODEC_ERROR_HANDLE);
    CHECK(call(c, shim_sceVideodecDecode, CTRL, IN, FB, PIC) == VIDEODEC_ERROR_HANDLE);
    free(c);
    printf("PASS videodec\n");
    return 0;
}

static int test_videodec2(void) {
    GuestContext *c = new_ctx();
    *u64_at(RSRC) = 0x18;
    CHECK(call(c, shim_sceVideodec2QueryComputeMemoryInfo, RSRC, 0, 0, 0) == 0);
    CHECK(*u64_at(RSRC + 8) == 16ull << 20 && *u64_at(RSRC + 0x10) == 0);

    memset(mem + CFG, 0, 0x48);
    *u64_at(CFG) = 0x48;
    *u32_at(CFG + 0x0C) = 1; // AVC
    *(int32_t *)(mem + CFG + 0x18) = 1920;
    *(int32_t *)(mem + CFG + 0x1C) = 1080;
    *u32_at(CFG + 0x24) = 2; // decode_pipeline_depth
    *(int32_t *)(mem + CFG + 0x38) = -1;
    memset(mem + RSRC, 0, 0x48);
    *u64_at(RSRC) = 0x48;
    CHECK(call(c, shim_sceVideodec2QueryDecoderMemoryInfo, CFG, RSRC, 0, 0) == 0);
    CHECK(*u64_at(RSRC + 0x08) == 16ull << 20 && *u64_at(RSRC + 0x18) == 16ull << 20 &&
          *u64_at(RSRC + 0x28) == 16ull << 20);
    CHECK(*u64_at(RSRC + 0x38) == 0x2FD000 + 0x4000); // align(1920*1088*3/2, 256) + 0x4000
    CHECK(call(c, shim_sceVideodec2CreateDecoder, CFG, RSRC, OUT, 0) == 0);
    const uint64_t dec = *u64_at(OUT);

    // Decode checks the input and frame buffer sizes, not the output info's (as shadPS4).
    memset(mem + IN, 0, 0x30);
    *u64_at(IN) = 0x30;
    *u64_at(IN + 8) = AU;
    static const uint8_t aud[] = {0, 0, 0, 1, 0x09, 0xF0};
    memcpy(mem + AU, aud, sizeof(aud));
    *u64_at(IN + 0x10) = sizeof(aud);
    memset(mem + FB, 0, 0x20);
    *u64_at(FB) = 0x18;
    *u64_at(FB + 8) = FB_DATA;
    *u64_at(FB + 0x10) = 0x400000;
    memset(mem + PIC, 0, 0x38);
    CHECK(call(c, shim_sceVideodec2Decode, dec, IN, FB, PIC) == VIDEODEC2_ERROR_STRUCT_SIZE);
    *u64_at(FB) = 0x20;
    *u64_at(PIC) = 0;
    CHECK(call(c, shim_sceVideodec2Decode, dec, IN, FB, PIC) == 0);
    CHECK(mem[PIC + 8] == 0 && mem[FB + 0x18] == 0);
    CHECK(call(c, shim_sceVideodec2DeleteDecoder, dec, 0, 0, 0) == 0);
    free(c);
    printf("PASS videodec2\n");
    return 0;
}

static uint64_t vdecsw_create(GuestContext *c) {
    memset(mem + CFG, 0, 0x50);
    *u64_at(CFG) = 0x48;
    *u32_at(CFG + 0x0C) = 1;
    *(int32_t *)(mem + CFG + 0x18) = 1280;
    *(int32_t *)(mem + CFG + 0x1C) = 720;
    *u32_at(CFG + 0x24) = 2;
    memset(mem + RSRC, 0, 0x48);
    *u64_at(RSRC) = 0x48;
    *u64_at(RSRC + 0x08) = 0x10000;
    *u64_at(RSRC + 0x10) = 0x300000;
    *u64_at(RSRC + 0x18) = 0x10000;
    *u64_at(RSRC + 0x20) = 0x310000;
    *u64_at(RSRC + 0x28) = 0x10000;
    *u64_at(RSRC + 0x30) = 0x320000;
    CHECK(call(c, shim_sceVdecswCreateDecoder, CFG, RSRC, OUT, 0) == 0);
    return *u64_at(OUT);
}

static void vdecsw_set_output(GuestContext *c, uint64_t dec) {
    *u64_at(FB) = 0x18;
    *u64_at(FB + 8) = FB_DATA;
    *u64_at(FB + 0x10) = 0x400000;
    CHECK(call(c, shim_sceVdecswSetDecodeOutput, dec, FB, 0, 0) == 0);
}

typedef struct SyncArgs {
    uint64_t dec;
    uint64_t out;
    _Atomic int done;
    int32_t rc;
} SyncArgs;

static void *sync_output_thread(void *arg) {
    SyncArgs *a = arg;
    GuestContext *c = new_ctx();
    a->rc = call(c, shim_sceVdecswSyncDecodeOutput, a->dec, a->out, 0, 0);
    atomic_store(&a->done, 1);
    free(c);
    return NULL;
}

static int test_vdecsw_sync(void) {
    GuestContext *c = new_ctx();
    uint64_t dec = vdecsw_create(c);

    // No input was set: nothing to sync.
    CHECK(call(c, shim_sceVdecswSyncDecodeInput, dec, OUT, 0, 0) == VDECSW_ERROR_INPUT_QUEUE_EMPTY);
    // No output buffer.
    memset(mem + PIC, 0, 0x38);
    *u64_at(PIC) = 0x38;
    CHECK(call(c, shim_sceVdecswSyncDecodeOutput, dec, PIC, 0, 0) == VDECSW_ERROR_OUTPUT_BUFFER_EMPTY);

    // SyncDecodeOutput waits for a picture or the end of the sequence; Try does not.
    vdecsw_set_output(c, dec);
    CHECK(call(c, shim_sceVdecswTrySyncDecodeOutput, dec, PIC, 0, 0) == VDECSW_ERROR_DECODE_PENDING);
    SyncArgs a = {.dec = dec, .out = PIC};
    pthread_t t;
    CHECK(pthread_create(&t, NULL, sync_output_thread, &a) == 0);
    usleep(100 * 1000);
    CHECK(atomic_load(&a.done) == 0);
    // The waiting call took the buffer: the guest can set the next one.
    *u64_at(FB + 8) = FB_DATA + 0x400000;
    CHECK(call(c, shim_sceVdecswSetDecodeOutput, dec, FB, 0, 0) == 0);
    CHECK(call(c, shim_sceVdecswFinalizeDecodeSequence, dec, 0, 0, 0) == 0);
    CHECK(pthread_join(t, NULL) == 0);
    CHECK(a.rc == 0);
    CHECK(mem[PIC + 8] == 0 && mem[PIC + 9] == 1 && mem[PIC + 0xB] == 0); // !valid, last frame, no picture
    // The buffer set meanwhile is still pending; Try after the finalize consumes it.
    memset(mem + OUT, 0, 0x38);
    *u64_at(OUT) = 0x30;
    CHECK(call(c, shim_sceVdecswTrySyncDecodeOutput, dec, OUT, 0, 0) == 0 && mem[OUT + 9] == 1);
    CHECK(call(c, shim_sceVdecswTrySyncDecodeOutput, dec, OUT, 0, 0) == VDECSW_ERROR_OUTPUT_BUFFER_EMPTY);

    // Deleting the decoder wakes a waiting SyncDecodeOutput.
    uint64_t dec2 = vdecsw_create(c);
    vdecsw_set_output(c, dec2);
    SyncArgs b = {.dec = dec2, .out = PIC};
    CHECK(pthread_create(&t, NULL, sync_output_thread, &b) == 0);
    usleep(100 * 1000);
    CHECK(atomic_load(&b.done) == 0);
    CHECK(call(c, shim_sceVdecswDeleteDecoder, dec2, 0, 0, 0) == 0);
    CHECK(pthread_join(t, NULL) == 0);
    CHECK(b.rc == 0 && mem[PIC + 8] == 0 && mem[PIC + 9] == 0);
    CHECK(call(c, shim_sceVdecswSyncDecodeOutput, dec2, PIC, 0, 0) == VDECSW_ERROR_DECODER_INSTANCE);

    // Picture info: no picture -> OK without writing; the OutputInfo size is checked first.
    memset(mem + OUT, 0, 0x38);
    *u64_at(OUT) = 0x38;
    *u64_at(IN) = 0x68;
    CHECK(call(c, shim_sceVdecswGetPictureInfo, OUT, IN, 0, 0) == 0);
    CHECK(call(c, shim_sceVdecswGetHevcPictureInfo, OUT, IN, 0, 0) == 0);
    CHECK(*u64_at(IN) == 0x68 && *u64_at(IN + 8) == 0);
    *u64_at(OUT) = 0x20;
    CHECK(call(c, shim_sceVdecswGetPictureInfo, OUT, IN, 0, 0) == VDECSW_ERROR_STRUCT_SIZE);
    CHECK(call(c, shim_sceVdecswDeleteDecoder, dec, 0, 0, 0) == 0);
    free(c);
    printf("PASS vdecsw-sync\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    mem = calloc(1, MEM_SIZE);
    if (!mem) return 1;
    if (strcmp(argv[1], "coredump") == 0) return test_coredump();
    if (strcmp(argv[1], "videocore-server") == 0) return test_videocore();
    if (strcmp(argv[1], "videodec") == 0) return test_videodec();
    if (strcmp(argv[1], "videodec2") == 0) return test_videodec2();
    if (strcmp(argv[1], "vdecsw-sync") == 0) return test_vdecsw_sync();
    return 2;
}
