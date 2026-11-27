// SPDX-License-Identifier: GPL-2.0-or-later

// The libSceAudio3d entry points added after shadPS4's audio3d.cpp: sceAudio3dPortCreate (the
// old open call), sceAudio3dObjectSetAttribute (one attribute), sceAudio3dAudioOutOutput (one
// grain of an associated AudioOut port) and the functions shadPS4 stubs. AudioOut is a recording
// double; the host API takes host pointers (base 0), the shims guest addresses.

#include "ps4_audio3d.h"
#include "ps4_audioout_internal.h"
#include <stdio.h>
#include <string.h>

#include "../host_guest_mem.h"

recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

// AudioOut double: handles from 100 on; outputs accepted and counted.
static int32_t g_next_handle = 100;
static int g_outputs;

int32_t ps4_audioout_port_open(int32_t user, int32_t type, int32_t index, uint32_t len,
                               uint32_t freq, uint32_t param) {
    return g_next_handle++;
}
int32_t ps4_audioout_port_close(int32_t handle) { return 0; }
int32_t ps4_audioout_port_writable(int32_t handle) { return 1; }
int32_t ps4_audioout_port_output(int32_t handle, const void *pcm) {
    g_outputs++;
    return 256;
}

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2,
                    uint64_t a3, uint64_t a4) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.mem_size = ~0ull;
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rdx = a2;
    ctx.rcx = a3;
    ctx.r8 = a4;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

static int case_port_create(void) {
    uint32_t port = 77;
    // Before Initialize the open fails as PortOpen does.
    CHECK(ps4_audio3d_port_create(256, 0, 0, &port) == PS4_AUDIO3D_ERROR_NOT_READY);
    CHECK(port == 0xFFFFFFFFu);
    CHECK(ps4_audio3d_initialize(0) == 0);
    CHECK(ps4_audio3d_port_create(256, 0, 0, NULL) == PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_port_create(256, 0, 1, &port) == PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    // The 0x10-byte parameters are validated like PortOpen's: granularity, rate.
    CHECK(ps4_audio3d_port_create(100, 0, 0, &port) == PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_port_create(256, 1, 0, &port) == PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_port_create(512, 0, 0, &port) == 0);
    // Size 0x10 means 512 objects, queue depth 2 and NO_ADVANCE.
    uint32_t level = 9, avail = 9;
    CHECK(ps4_audio3d_port_get_queue_level(port, &level, &avail) == 0);
    CHECK(level == 0 && avail == 2);
    CHECK(ps4_audio3d_port_advance(port) == PS4_AUDIO3D_ERROR_NOT_SUPPORTED);
    uint32_t obj = 0;
    for (int i = 0; i < 512; i++) CHECK(ps4_audio3d_object_reserve(port, &obj) == 0);
    CHECK(ps4_audio3d_object_reserve(port, &obj) == PS4_AUDIO3D_ERROR_OUT_OF_RESOURCES);
    CHECK(ps4_audio3d_port_close(port) == 0);

    // The shim reads the port id pointer from guest memory.
    uint32_t guest_port = 77;
    CHECK(call(shim_sceAudio3dPortCreate, 256, 0, 0, A(&guest_port), 0) == 0);
    CHECK(guest_port != 77);
    CHECK(ps4_audio3d_port_close(guest_port) == 0);
    CHECK(ps4_audio3d_terminate() == 0);
    printf("PASS port-create\n");
    return 0;
}

static uint32_t open_port(void) {
    uint8_t params[0x20];
    uint32_t port = 0xFFFFFFFFu;
    if (ps4_audio3d_get_default_open_parameters(params) != 0) return port;
    ps4_audio3d_port_open(0xFF, params, &port); // ADVANCE_AND_PUSH, granularity 256, depth 2
    return port;
}

static int case_object_set_attribute(void) {
    CHECK(ps4_audio3d_initialize(0) == 0);
    const uint32_t port = open_port();
    CHECK(port != 0xFFFFFFFFu);
    uint32_t obj = 0;
    CHECK(ps4_audio3d_object_reserve(port, &obj) == 0);

    float gain = 0.5f;
    const uint64_t gain_ptr = (uint64_t)(uintptr_t)&gain;
    CHECK(ps4_audio3d_object_set_attribute(port + 1, obj, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 4, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PORT);
    // shadPS4: an empty value is rejected for everything but RESET_STATE, before the attribute
    // is looked at; an unknown port still reports the port.
    CHECK(ps4_audio3d_object_set_attribute(port + 1, obj, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 0, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PORT);
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 0, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_APPLICATION_SPECIFIC,
                                           gain_ptr, 0, 0) == PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_RESET_STATE, 0, 0, 0) == 0);
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 4, 0) == 0);
    CHECK(ps4_audio3d_object_set_attribute(port, obj + 1, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 4, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_OBJECT);
    gain = -1.0f;
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 4, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PARAMETER);

    // PCM through the single call reaches the mix like ObjectSetAttributes: a full gain object
    // fed a constant is heard in the advanced grain.
    static int16_t samples[256];
    for (int i = 0; i < 256; i++) samples[i] = 16384;
    const Ps4Audio3dPcm pcm = {.format = PS4_AUDIO3D_FORMAT_S16,
                               .sample_buffer = (uint64_t)(uintptr_t)samples,
                               .num_samples = 256};
    gain = 1.0f;
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_GAIN, gain_ptr, 4, 0) == 0);
    CHECK(ps4_audio3d_object_set_attribute(port, obj, PS4_AUDIO3D_ATTR_PCM,
                                           (uint64_t)(uintptr_t)&pcm, sizeof(pcm), 0) == 0);
    Ps4Audio3dLevels before, after;
    CHECK(ps4_audio3d_port_get_levels(port, &before) == 0);
    CHECK(ps4_audio3d_port_advance(port) == 0);
    CHECK(ps4_audio3d_port_get_levels(port, &after) == 0);
    CHECK(after.object_grains == before.object_grains + 1);
    CHECK(after.object_sq > before.object_sq);

    // The shim: (port, object, attribute id, value, size).
    gain = 0.25f;
    CHECK(call(shim_sceAudio3dObjectSetAttribute, port, obj, PS4_AUDIO3D_ATTR_GAIN, A(&gain), 4) ==
          0);
    CHECK(call(shim_sceAudio3dObjectSetAttribute, port, obj, PS4_AUDIO3D_ATTR_GAIN, A(&gain), 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PARAMETER);

    CHECK(ps4_audio3d_port_close(port) == 0);
    CHECK(ps4_audio3d_terminate() == 0);
    printf("PASS object-set-attribute\n");
    return 0;
}

static int case_audioout_output(void) {
    CHECK(ps4_audio3d_initialize(0) == 0);
    const uint32_t port = open_port();
    CHECK(port != 0xFFFFFFFFu);
    const int32_t s16 = ps4_audio3d_audioout_open(port, 0xFF, 0, 0, 256, 48000, 1 /* S16 stereo */);
    const int32_t f8 = ps4_audio3d_audioout_open(port, 0xFF, 0, 0, 256, 48000, 7 /* float 8ch */);
    CHECK(s16 > 0 && f8 > 0);

    static int16_t pcm16[256 * 2 + 1];
    static float pcm8[256 * 8 + 1];
    CHECK(ps4_audio3d_audioout_output(s16, 0, 0) == PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_audioout_output(12345, (uint64_t)(uintptr_t)pcm16, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PORT);
    CHECK(ps4_audio3d_audioout_output(-1, (uint64_t)(uintptr_t)pcm16, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PORT);
    // Samples must be naturally aligned.
    CHECK(ps4_audio3d_audioout_output(s16, (uint64_t)(uintptr_t)pcm16 + 1, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    CHECK(ps4_audio3d_audioout_output(f8, (uint64_t)(uintptr_t)pcm8 + 2, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PARAMETER);
    // The return value is the sample count of a grain: len * channels.
    CHECK(ps4_audio3d_audioout_output(s16, (uint64_t)(uintptr_t)pcm16, 0) == 512);
    CHECK(ps4_audio3d_audioout_output(f8, (uint64_t)(uintptr_t)pcm8, 0) == 2048);
    // Queue depth 2: the third grain of a handle waits for a push.
    CHECK(ps4_audio3d_audioout_output(s16, (uint64_t)(uintptr_t)pcm16, 0) == 512);
    CHECK(ps4_audio3d_audioout_output(s16, (uint64_t)(uintptr_t)pcm16, 0) ==
          PS4_AUDIO3D_ERROR_NOT_READY);
    g_outputs = 0;
    CHECK(ps4_audio3d_port_push(port, PS4_AUDIO3D_BLOCKING_SYNC) == 0);
    CHECK(g_outputs == 3); // two S16 grains and the float one
    CHECK(ps4_audio3d_audioout_output(s16, (uint64_t)(uintptr_t)pcm16, 0) == 512);

    // The shim takes (handle, guest address).
    CHECK(call(shim_sceAudio3dAudioOutOutput, (uint64_t)f8, A(pcm8), 0, 0, 0) == 2048);
    CHECK(call(shim_sceAudio3dAudioOutOutput, (uint64_t)f8, 0, 0, 0, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PARAMETER);

    CHECK(ps4_audio3d_audioout_close(s16) == 0);
    CHECK(ps4_audio3d_audioout_output(s16, (uint64_t)(uintptr_t)pcm16, 0) ==
          PS4_AUDIO3D_ERROR_INVALID_PORT);
    CHECK(ps4_audio3d_port_close(port) == 0);
    CHECK(ps4_audio3d_terminate() == 0);
    printf("PASS audioout-output\n");
    return 0;
}

// Stubbed in shadPS4's default backend: they return 0 whatever the arguments.
static int case_stubs(void) {
    void (*const stubs[])(GuestContext *) = {
        shim_sceAudio3dCreateSpeakerArray,      shim_sceAudio3dDeleteSpeakerArray,
        shim_sceAudio3dGetSpeakerArrayMemorySize, shim_sceAudio3dGetSpeakerArrayMixCoefficients,
        shim_sceAudio3dGetSpeakerArrayMixCoefficients2, shim_sceAudio3dPortDestroy,
        shim_sceAudio3dPortFreeState,           shim_sceAudio3dPortGetList,
        shim_sceAudio3dPortGetParameters,       shim_sceAudio3dPortGetState,
        shim_sceAudio3dPortGetStatus,           shim_sceAudio3dPortQueryDebug,
        shim_sceAudio3dReportRegisterHandler,   shim_sceAudio3dReportUnregisterHandler,
        shim_sceAudio3dSetGpuRenderer,          shim_sceAudio3dStrError,
    };
    for (size_t i = 0; i < sizeof(stubs) / sizeof(stubs[0]); i++) {
        CHECK(call(stubs[i], 1, 2, 3, 4, 5) == 0);
    }
    printf("PASS stubs\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "port-create") == 0) return case_port_create();
    if (strcmp(argv[1], "object-set-attribute") == 0) return case_object_set_attribute();
    if (strcmp(argv[1], "audioout-output") == 0) return case_audioout_output();
    if (strcmp(argv[1], "stubs") == 0) return case_stubs();
    return 2;
}
