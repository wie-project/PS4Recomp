// Host test for sceAudio3dPortFlush against a recording AudioOut double. Build and run (from
// the repository root):
//   clang -std=gnu11 -I pkg/runtime/core -I pkg/runtime/modules/libSceAudio3d \
//     -I pkg/runtime/modules/libSceAudioOut tests/audio3d_flush_test/main.c \
//     pkg/runtime/modules/libSceAudio3d/ps4_audio3d.c pkg/runtime/modules/libSceAudio3d/ps4_audio3d_mix.c \
//     -o /tmp/audio3d_flush_test && /tmp/audio3d_flush_test

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ps4_audio3d.h"
#include "ps4_audioout_internal.h"
#include "recomp_context.h"

// The tested functions run no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

// AudioOut double: hands out handles from 100 on and records every output call.
enum { kMaxCalls = 64 };
static int32_t g_next_handle = 100;
static struct {
    int32_t handle;
    int with_pcm;
    float first;
} g_calls[kMaxCalls];
static int g_ncalls;

int32_t ps4_audioout_port_open(int32_t user, int32_t type, int32_t index, uint32_t len, uint32_t freq,
                               uint32_t param) {
    (void)user, (void)type, (void)index, (void)len, (void)freq, (void)param;
    return g_next_handle++;
}
int32_t ps4_audioout_port_close(int32_t handle) {
    (void)handle;
    return 0;
}
int32_t ps4_audioout_port_writable(int32_t handle) {
    (void)handle;
    return 1;
}
int32_t ps4_audioout_port_output(int32_t handle, const void *pcm) {
    assert(g_ncalls < kMaxCalls);
    g_calls[g_ncalls].handle = handle;
    g_calls[g_ncalls].with_pcm = pcm != NULL;
    g_calls[g_ncalls].first = 0.0f;
    if (pcm) memcpy(&g_calls[g_ncalls].first, pcm, sizeof(float));
    g_ncalls++;
    return pcm ? 256 : 0;
}

static uint32_t open_port(void) {
    uint8_t params[0x20];
    assert(ps4_audio3d_get_default_open_parameters(params) == 0); // ADVANCE_AND_PUSH, 256, depth 2
    uint32_t port;
    assert(ps4_audio3d_port_open(0xFF, params, &port) == 0);
    return port;
}

static uint32_t queue_level(uint32_t port) {
    uint32_t level;
    assert(ps4_audio3d_port_get_queue_level(port, &level, NULL) == 0);
    return level;
}

int main(void) {
    assert(ps4_audio3d_initialize(0) == 0);
    assert(ps4_audio3d_port_flush(7) == PS4_AUDIO3D_ERROR_INVALID_PORT);

    uint32_t port = open_port();
    // Nothing queued: nothing is output.
    assert(ps4_audio3d_port_flush(port) == 0);
    assert(g_ncalls == 0);

    // Bed data that was never advanced is mixed into one grain and output.
    static float bed[256 * 2];
    for (int i = 0; i < 256 * 2; i++) bed[i] = 0.25f;
    assert(ps4_audio3d_bed_write(port, 2, PS4_AUDIO3D_FORMAT_FLOAT, bed, 256) == 0);
    assert(ps4_audio3d_port_flush(port) == 0);
    assert(g_ncalls == 1 && g_calls[0].with_pcm && g_calls[0].first == 0.25f);
    int32_t main_handle = g_calls[0].handle;
    assert(queue_level(port) == 0);

    // Advanced grains all go out, in order, without a Push.
    g_ncalls = 0;
    assert(ps4_audio3d_bed_write(port, 2, PS4_AUDIO3D_FORMAT_FLOAT, bed, 256) == 0);
    assert(ps4_audio3d_port_advance(port) == 0);
    for (int i = 0; i < 256 * 2; i++) bed[i] = 0.5f;
    assert(ps4_audio3d_bed_write(port, 2, PS4_AUDIO3D_FORMAT_FLOAT, bed, 256) == 0);
    assert(ps4_audio3d_port_advance(port) == 0);
    assert(queue_level(port) == 2);
    assert(ps4_audio3d_port_flush(port) == 0);
    assert(g_ncalls == 2);
    assert(g_calls[0].handle == main_handle && g_calls[0].first == 0.25f);
    assert(g_calls[1].handle == main_handle && g_calls[1].first == 0.5f);
    assert(queue_level(port) == 0);

    // Associated AudioOut ports: their pending grains go out, then a NULL output waits on each.
    g_ncalls = 0;
    int32_t bus = ps4_audio3d_audioout_open(port, 0xFF, 0, 0, 256, 48000, 4 /* float stereo */);
    assert(bus > 0);
    static float pcm[256 * 2];
    pcm[0] = 0.75f;
    Ps4Audio3dAudioOutParam out[2] = {{.handle = bus, .ptr = (uint64_t)(uintptr_t)pcm},
                                      {.handle = bus, .ptr = (uint64_t)(uintptr_t)pcm}};
    assert(ps4_audio3d_audioout_outputs(out, 2, 0) == 0);
    assert(ps4_audio3d_port_flush(port) == 0);
    assert(g_ncalls == 3);
    assert(g_calls[0].handle == bus && g_calls[0].with_pcm && g_calls[0].first == 0.75f);
    assert(g_calls[1].handle == bus && g_calls[1].with_pcm);
    assert(g_calls[2].handle == bus && !g_calls[2].with_pcm);

    assert(ps4_audio3d_port_close(port) == 0);
    assert(ps4_audio3d_port_flush(port) == PS4_AUDIO3D_ERROR_INVALID_PORT);
    assert(ps4_audio3d_terminate() == 0);
    printf("[audio3d-flush-test] PASSED\n");
    return 0;
}
