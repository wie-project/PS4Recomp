// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_audioin.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_AUDIO_IN_PORTS 16

typedef struct {
    int in_use;
    uint32_t len;
    uint32_t freq;
    uint32_t param;
    uint32_t bytes_per_frame;
} Ps4AudioInPort;

static Ps4AudioInPort g_audio_in_ports[MAX_AUDIO_IN_PORTS];
static pthread_mutex_t g_audio_in_lock = PTHREAD_MUTEX_INITIALIZER;

void shim_sceAudioInOpen(GuestContext *ctx) {
    uint32_t len = (uint32_t)ctx->rcx;
    uint32_t freq = (uint32_t)ctx->r8;
    uint32_t param = (uint32_t)ctx->r9;

    if (len == 0) len = 256;
    if (freq == 0) freq = 48000;

    pthread_mutex_lock(&g_audio_in_lock);
    int slot = -1;
    for (int i = 1; i < MAX_AUDIO_IN_PORTS; i++) {
        if (!g_audio_in_ports[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_audio_in_lock);
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }

    Ps4AudioInPort *p = &g_audio_in_ports[slot];
    p->in_use = 1;
    p->len = len;
    p->freq = freq;
    p->param = param;
    p->bytes_per_frame = (param == 0) ? 2 : 4; // mono vs stereo S16
    pthread_mutex_unlock(&g_audio_in_lock);

    ctx->rax = (uint64_t)slot;
    SHIM_RETURN();
}

void shim_sceAudioInClose(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    if (handle <= 0 || handle >= MAX_AUDIO_IN_PORTS) {
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_audio_in_lock);
    g_audio_in_ports[handle].in_use = 0;
    pthread_mutex_unlock(&g_audio_in_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAudioInInput(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t dest_gaddr = ctx->rsi;

    if (handle <= 0 || handle >= MAX_AUDIO_IN_PORTS || dest_gaddr == 0) {
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_audio_in_lock);
    Ps4AudioInPort *p = &g_audio_in_ports[handle];
    if (!p->in_use) {
        pthread_mutex_unlock(&g_audio_in_lock);
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }

    void *dest = (void *)(ctx->mem_base + dest_gaddr);
    memset(dest, 0, p->len * p->bytes_per_frame);
    pthread_mutex_unlock(&g_audio_in_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceAudioInGetSilentState(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    if (handle <= 0 || handle >= MAX_AUDIO_IN_PORTS) {
        ctx->rax = (uint64_t)-1;
        SHIM_RETURN();
    }
    // Return 1 indicating microphone is silent / muted
    ctx->rax = 1;
    SHIM_RETURN();
}
