// SPDX-License-Identifier: GPL-2.0-or-later

// libSceAudioIn, following shadPS4 (src/core/libraries/audio/audioin.cpp and sdl_audio_in.cpp):
// seven input ports, handles 0x30000000 | type << 16 | port, S16 mono/stereo at 16 or 48 kHz.
// Each port captures the host default input device through an AudioQueue into a ring buffer;
// sceAudioInInput blocks until one grain is available.

#include "ps4_audioin.h"
#include <AudioToolbox/AudioToolbox.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define AUDIO_IN_QUEUE_BUFFERS 3
// sceAudioInInput gives up and returns 0 frames when a grain does not arrive in this time.
#define AUDIO_IN_READ_TIMEOUT_MS 1000

typedef struct AudioInPort {
    pthread_mutex_t io;     // serializes sceAudioInInput against sceAudioInClose
    pthread_mutex_t ring_mu;
    pthread_cond_t ring_cv;
    AudioQueueRef queue;    // NULL: no capture device, reads return silence
    uint8_t *ring;
    uint32_t ring_cap;
    uint32_t ring_head;     // read position
    uint32_t ring_size;
    uint32_t grain_bytes;
    uint32_t samples_num;
    bool device_available;
} AudioInPort;

static AudioInPort *g_ports[ORBIS_AUDIO_IN_NUM_PORTS];
static pthread_mutex_t g_ports_mu = PTHREAD_MUTEX_INITIALIZER;

static bool capture_disabled(void) {
    const char *e = getenv("PS4_AUDIO_IN");
    return e && strcmp(e, "none") == 0;
}

// Drops the oldest bytes when the ring is full, so a game that reads late gets recent audio.
static void ring_write_locked(AudioInPort *p, const uint8_t *src, uint32_t n) {
    if (n >= p->ring_cap) {
        src += n - p->ring_cap;
        n = p->ring_cap;
    }
    const uint32_t free_bytes = p->ring_cap - p->ring_size;
    if (n > free_bytes) {
        const uint32_t drop = n - free_bytes;
        p->ring_head = (p->ring_head + drop) % p->ring_cap;
        p->ring_size -= drop;
    }
    uint32_t tail = (p->ring_head + p->ring_size) % p->ring_cap;
    const uint32_t first = n < p->ring_cap - tail ? n : p->ring_cap - tail;
    memcpy(p->ring + tail, src, first);
    memcpy(p->ring, src + first, n - first);
    p->ring_size += n;
}

static void ring_read_locked(AudioInPort *p, uint8_t *dst, uint32_t n) {
    const uint32_t first = n < p->ring_cap - p->ring_head ? n : p->ring_cap - p->ring_head;
    memcpy(dst, p->ring + p->ring_head, first);
    memcpy(dst + first, p->ring, n - first);
    p->ring_head = (p->ring_head + n) % p->ring_cap;
    p->ring_size -= n;
}

static void capture_callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer,
                             const AudioTimeStamp *start, UInt32 npackets,
                             const AudioStreamPacketDescription *descs) {
    AudioInPort *p = user;
    if (buffer->mAudioDataByteSize > 0) {
        pthread_mutex_lock(&p->ring_mu);
        ring_write_locked(p, buffer->mAudioData, buffer->mAudioDataByteSize);
        pthread_cond_broadcast(&p->ring_cv);
        pthread_mutex_unlock(&p->ring_mu);
    }
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

// Starts capture from the default input device; on failure the port stays silent.
static void port_start_capture(AudioInPort *p, uint32_t freq, uint32_t channels) {
    AudioStreamBasicDescription fmt = {
        .mSampleRate = freq,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
        .mBytesPerPacket = 2 * channels,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = 2 * channels,
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 16,
    };
    AudioQueueRef q = NULL;
    OSStatus st = AudioQueueNewInput(&fmt, capture_callback, p, NULL, NULL, 0, &q);
    if (st != noErr) {
        fprintf(stderr, "[AudioIn] AudioQueueNewInput failed: %d\n", (int)st);
        return;
    }
    for (int i = 0; i < AUDIO_IN_QUEUE_BUFFERS; i++) {
        AudioQueueBufferRef b = NULL;
        st = AudioQueueAllocateBuffer(q, p->grain_bytes, &b);
        if (st == noErr) st = AudioQueueEnqueueBuffer(q, b, 0, NULL);
        if (st != noErr) break;
    }
    if (st == noErr) st = AudioQueueStart(q, NULL);
    if (st != noErr) {
        fprintf(stderr, "[AudioIn] starting capture failed: %d\n", (int)st);
        AudioQueueDispose(q, true);
        return;
    }
    p->queue = q;
    p->device_available = true;
}

static void port_destroy(AudioInPort *p) {
    if (p->queue) {
        AudioQueueStop(p->queue, true);
        AudioQueueDispose(p->queue, true);
    }
    pthread_cond_destroy(&p->ring_cv);
    pthread_mutex_destroy(&p->ring_mu);
    pthread_mutex_destroy(&p->io);
    free(p->ring);
    free(p);
}

// Returns the port index of a handle, or -1 when the handle is malformed.
static int port_of_handle(int32_t handle) {
    const int port = handle & 0xFF;
    if (port >= ORBIS_AUDIO_IN_NUM_PORTS || (handle & 0x7f000000) != ORBIS_AUDIO_IN_HANDLE_TAG) {
        return -1;
    }
    return port;
}

int32_t sceAudioInOpen(int32_t userId, uint32_t type, uint32_t index, uint32_t len, uint32_t freq,
                       uint32_t param) {
    (void)userId;
    (void)index;
    if (len == 0 || len > ORBIS_AUDIO_IN_MAX_GRAIN) {
        return ORBIS_AUDIO_IN_ERROR_INVALID_SIZE;
    }
    if (param != ORBIS_AUDIO_IN_PARAM_FORMAT_S16_MONO && param != ORBIS_AUDIO_IN_PARAM_FORMAT_S16_STEREO) {
        return ORBIS_AUDIO_IN_ERROR_INVALID_PARAM;
    }
    if (freq != 16000 && freq != 48000) {
        return ORBIS_AUDIO_IN_ERROR_INVALID_FREQ;
    }
    const uint32_t channels = param == ORBIS_AUDIO_IN_PARAM_FORMAT_S16_STEREO ? 2 : 1;

    pthread_mutex_lock(&g_ports_mu);
    int port = -1;
    for (int i = 0; i < ORBIS_AUDIO_IN_NUM_PORTS; i++) {
        if (!g_ports[i]) {
            port = i;
            break;
        }
    }
    if (port < 0) {
        pthread_mutex_unlock(&g_ports_mu);
        return ORBIS_AUDIO_IN_ERROR_PORT_FULL;
    }

    AudioInPort *p = calloc(1, sizeof(*p));
    if (!p) {
        pthread_mutex_unlock(&g_ports_mu);
        return ORBIS_AUDIO_IN_ERROR_OUT_OF_MEMORY;
    }
    p->samples_num = len;
    p->grain_bytes = len * 2 * channels;
    // Half a second of audio, and never less than four grains.
    p->ring_cap = freq / 2 * 2 * channels;
    if (p->ring_cap < 4 * p->grain_bytes) p->ring_cap = 4 * p->grain_bytes;
    p->ring = malloc(p->ring_cap);
    if (!p->ring) {
        free(p);
        pthread_mutex_unlock(&g_ports_mu);
        return ORBIS_AUDIO_IN_ERROR_OUT_OF_MEMORY;
    }
    pthread_mutex_init(&p->io, NULL);
    pthread_mutex_init(&p->ring_mu, NULL);
    pthread_cond_init(&p->ring_cv, NULL);
    if (!capture_disabled()) {
        port_start_capture(p, freq, channels);
    }
    g_ports[port] = p;
    pthread_mutex_unlock(&g_ports_mu);

    return (int32_t)(ORBIS_AUDIO_IN_HANDLE_TAG | (type << 16) | (uint32_t)port);
}

int32_t sceAudioInHqOpen(int32_t userId, uint32_t type, uint32_t index, uint32_t len, uint32_t freq,
                         uint32_t param) {
    return sceAudioInOpen(userId, type, index, len, freq, param);
}

int32_t sceAudioInClose(int32_t handle) {
    const int port = port_of_handle(handle);
    if (port < 0) {
        return ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE;
    }
    pthread_mutex_lock(&g_ports_mu);
    AudioInPort *p = g_ports[port];
    if (!p) {
        pthread_mutex_unlock(&g_ports_mu);
        return ORBIS_AUDIO_IN_ERROR_NOT_OPENED;
    }
    g_ports[port] = NULL;
    // Wait for an in-flight sceAudioInInput on this port before freeing it.
    pthread_mutex_lock(&p->io);
    pthread_mutex_unlock(&g_ports_mu);
    pthread_mutex_unlock(&p->io);
    port_destroy(p);
    return 0;
}

// Locks and returns the open port of a handle, or returns an error code in *err.
static AudioInPort *acquire_port(int32_t handle, int32_t *err) {
    const int port = port_of_handle(handle);
    if (port < 0) {
        *err = ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE;
        return NULL;
    }
    pthread_mutex_lock(&g_ports_mu);
    AudioInPort *p = g_ports[port];
    if (!p) {
        pthread_mutex_unlock(&g_ports_mu);
        *err = ORBIS_AUDIO_IN_ERROR_NOT_OPENED;
        return NULL;
    }
    pthread_mutex_lock(&p->io);
    pthread_mutex_unlock(&g_ports_mu);
    return p;
}

int32_t sceAudioInInput(int32_t handle, void *dest) {
    if (port_of_handle(handle) < 0) {
        return ORBIS_AUDIO_IN_ERROR_INVALID_HANDLE;
    }
    if (!dest) {
        return ORBIS_AUDIO_IN_ERROR_INVALID_POINTER;
    }
    int32_t err = 0;
    AudioInPort *p = acquire_port(handle, &err);
    if (!p) {
        return err;
    }
    int32_t frames;
    if (!p->queue) {
        memset(dest, 0, p->grain_bytes);
        frames = (int32_t)p->samples_num;
    } else {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec deadline = {
            .tv_sec = now.tv_sec + AUDIO_IN_READ_TIMEOUT_MS / 1000,
            .tv_nsec = (long)now.tv_usec * 1000 + (long)(AUDIO_IN_READ_TIMEOUT_MS % 1000) * 1000000L,
        };
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&p->ring_mu);
        int rc = 0;
        while (p->ring_size < p->grain_bytes && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&p->ring_cv, &p->ring_mu, &deadline);
        }
        if (p->ring_size >= p->grain_bytes) {
            ring_read_locked(p, dest, p->grain_bytes);
            frames = (int32_t)p->samples_num;
        } else {
            frames = 0;
        }
        pthread_mutex_unlock(&p->ring_mu);
    }
    pthread_mutex_unlock(&p->io);
    return frames;
}

int32_t sceAudioInGetSilentState(int32_t handle) {
    int32_t err = 0;
    AudioInPort *p = acquire_port(handle, &err);
    if (!p) {
        return err;
    }
    const int32_t state = p->device_available ? 0 : ORBIS_AUDIO_IN_SILENT_STATE_DEVICE_NONE;
    pthread_mutex_unlock(&p->io);
    return state;
}

void shim_sceAudioInOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAudioInOpen((int32_t)ctx->rdi, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx,
                                                 (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9);
    SHIM_RETURN();
}

void shim_sceAudioInHqOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAudioInHqOpen((int32_t)ctx->rdi, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx,
                                                   (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9);
    SHIM_RETURN();
}

void shim_sceAudioInClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAudioInClose((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceAudioInInput(GuestContext *ctx) {
    void *dest = ctx->rsi ? (void *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceAudioInInput((int32_t)ctx->rdi, dest);
    SHIM_RETURN();
}

void shim_sceAudioInGetSilentState(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceAudioInGetSilentState((int32_t)ctx->rdi);
    SHIM_RETURN();
}
