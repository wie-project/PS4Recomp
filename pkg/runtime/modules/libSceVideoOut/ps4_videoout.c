#include "ps4_videoout.h"
#include "ps4_metal_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define MAX_VIDEO_HANDLES 8
#define MAX_FRAME_BUFFERS 16
#define MAX_EQUEUES_PER_PORT 16

typedef struct {
    int in_use;
    int32_t handle;
    OrbisVideoOutBufferAttribute attr;
    int bufferCount;
    uint64_t buffers[MAX_FRAME_BUFFERS];
    OrbisKernelEqueue flipQueue;
    void *flipQueueUdata;
    int flipQueueRegistered;
    int32_t flipRate;
    OrbisVideoOutFlipStatus flipStatus;
    pthread_mutex_t mutex;

    pthread_cond_t vblankCond;
    SceVideoOutVblankStatus vblankStatus;
    OrbisKernelEqueue vblankQueues[MAX_EQUEUES_PER_PORT];
    void *vblankUdatas[MAX_EQUEUES_PER_PORT];
    int vblankQueueCount;
} VideoOutHandle;

static VideoOutHandle g_video_handles[MAX_VIDEO_HANDLES];
static pthread_mutex_t g_video_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_next_video_handle = 1;

static pthread_t g_vblank_thread;
static int g_vblank_thread_started = 0;
static volatile int g_vblank_thread_running = 1;

static uint64_t get_time_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static uint64_t get_fake_tsc(void) {
#if defined(__aarch64__)
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return get_time_usec() * 2000ULL;
#endif
}

static void *vblank_thread_func(void *arg) {
    (void)arg;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 16666666L; // ~60 Hz (16.666 ms)

    while (g_vblank_thread_running) {
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&g_video_table_mutex);
        for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
            VideoOutHandle *vh = &g_video_handles[i];
            if (!vh->in_use) continue;

            pthread_mutex_lock(&vh->mutex);
            vh->vblankStatus.count++;
            vh->vblankStatus.process_time = get_time_usec();
            vh->vblankStatus.tsc = get_fake_tsc();

            pthread_cond_broadcast(&vh->vblankCond);

            for (int q = 0; q < vh->vblankQueueCount; q++) {
                if (vh->vblankQueues[q] > 0) {
                    ps4_equeue_post_event(vh->vblankQueues[q], 0x7 /* Vblank */, ORBIS_KERNEL_EVFILT_VIDEO_OUT, (int64_t)(vh->vblankStatus.count << 16), vh->vblankUdatas[q]);
                }
            }
            pthread_mutex_unlock(&vh->mutex);
        }
        pthread_mutex_unlock(&g_video_table_mutex);
    }
    return NULL;
}

static VideoOutHandle *find_video_handle_locked(int32_t handle) {
    if (handle <= 0) return NULL;
    for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
        if (g_video_handles[i].in_use && g_video_handles[i].handle == handle) {
            return &g_video_handles[i];
        }
    }
    return NULL;
}

int32_t sceVideoOutOpen(int32_t userId, int32_t busType, int32_t index, const void *param) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = NULL;
    for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
        if (!g_video_handles[i].in_use) {
            vh = &g_video_handles[i];
            break;
        }
    }

    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -ENOMEM;
    }

    vh->handle = g_next_video_handle++;
    vh->in_use = 1;
    vh->bufferCount = 0;
    vh->flipQueue = 0;
    vh->flipQueueUdata = NULL;
    vh->flipQueueRegistered = 0;
    vh->flipRate = 0;
    vh->vblankQueueCount = 0;
    memset(&vh->attr, 0, sizeof(vh->attr));
    memset(&vh->flipStatus, 0, sizeof(vh->flipStatus));
    memset(&vh->vblankStatus, 0, sizeof(vh->vblankStatus));
    memset(vh->vblankQueues, 0, sizeof(vh->vblankQueues));
    memset(vh->vblankUdatas, 0, sizeof(vh->vblankUdatas));
    pthread_mutex_init(&vh->mutex, NULL);
    pthread_cond_init(&vh->vblankCond, NULL);

    if (!g_vblank_thread_started) {
        g_vblank_thread_running = 1;
        pthread_create(&g_vblank_thread, NULL, vblank_thread_func, NULL);
        g_vblank_thread_started = 1;
    }

    int32_t ret = vh->handle;
    pthread_mutex_unlock(&g_video_table_mutex);

    // Initialize native macOS Metal screen
    ps4_metal_screen_init(1920, 1080, "PS4Recomp - Native Metal Screen");

    return ret;
}

int32_t sceVideoOutClose(int32_t handle) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    vh->in_use = 0;
    pthread_cond_broadcast(&vh->vblankCond);
    pthread_mutex_unlock(&vh->mutex);
    pthread_mutex_destroy(&vh->mutex);
    pthread_cond_destroy(&vh->vblankCond);

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

void ps4_videoout_destroy(void) {
    if (g_vblank_thread_started) {
        g_vblank_thread_running = 0;
        pthread_join(g_vblank_thread, NULL);
        g_vblank_thread_started = 0;
    }
    pthread_mutex_lock(&g_video_table_mutex);
    for (int i = 0; i < MAX_VIDEO_HANDLES; i++) {
        if (g_video_handles[i].in_use) {
            pthread_mutex_destroy(&g_video_handles[i].mutex);
            pthread_cond_destroy(&g_video_handles[i].vblankCond);
            g_video_handles[i].in_use = 0;
        }
    }
    pthread_mutex_unlock(&g_video_table_mutex);
}

void sceVideoOutSetBufferAttribute(OrbisVideoOutBufferAttribute *attr, uint32_t pixelFormat, uint32_t tilingMode, uint32_t aspectRatio, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!attr) return;
    attr->format = (int32_t)pixelFormat;
    attr->tmode = (int32_t)tilingMode;
    attr->aspect = (int32_t)aspectRatio;
    attr->width = width;
    attr->height = height;
    attr->pixelPitch = pitch;
    attr->reserved[0] = 0;
    attr->reserved[1] = 0;
}

int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t startIndex, void * const *bufferArray, int32_t bufferCount, const OrbisVideoOutBufferAttribute *attr) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    pthread_mutex_unlock(&g_video_table_mutex);

    if (attr) {
        vh->attr = *attr;
    }

    if (bufferArray && bufferCount > 0) {
        for (int i = 0; i < bufferCount && (startIndex + i) < MAX_FRAME_BUFFERS; i++) {
            vh->buffers[startIndex + i] = (uint64_t)bufferArray[i];
        }
        if (startIndex + bufferCount > vh->bufferCount) {
            vh->bufferCount = startIndex + bufferCount;
        }
    }

    pthread_mutex_unlock(&vh->mutex);
    return 0;
}

int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t fliprate) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    vh->flipRate = fliprate;
    pthread_mutex_unlock(&vh->mutex);

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

int32_t sceVideoOutAddFlipEvent(OrbisKernelEqueue eq, int32_t handle, void *flipArg) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    vh->flipQueue = eq;
    vh->flipQueueUdata = flipArg;
    vh->flipQueueRegistered = 1;
    pthread_mutex_unlock(&vh->mutex);

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

int32_t sceVideoOutSubmitFlip(GuestContext *ctx, int32_t handle, int32_t bufferIndex, uint32_t flipMode, int64_t flipArg) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    pthread_mutex_unlock(&g_video_table_mutex);

    if (bufferIndex < 0 || bufferIndex >= vh->bufferCount) {
        pthread_mutex_unlock(&vh->mutex);
        return -EINVAL;
    }

    uint64_t guest_buf_vaddr = vh->buffers[bufferIndex];
    if (ctx && guest_buf_vaddr != 0) {
        const void *pixels = ctx->mem_base + guest_buf_vaddr;
        int width = vh->attr.width > 0 ? (int)vh->attr.width : 1920;
        int height = vh->attr.height > 0 ? (int)vh->attr.height : 1080;
        size_t pitch = vh->attr.pixelPitch > 0 ? (size_t)vh->attr.pixelPitch * 4 : (size_t)width * 4;

        fprintf(stderr, "[videoout] Presenting frame %lld (buffer %d, %dx%d)\n", (long long)flipArg, bufferIndex, width, height);
        ps4_metal_screen_present_frame(pixels, pitch, width, height);
    }

    vh->flipStatus.num++;
    vh->flipStatus.flipArg = flipArg;
    vh->flipStatus.currentBuffer = bufferIndex;

    if (vh->flipQueueRegistered) {
        ps4_equeue_post_event(vh->flipQueue, 0x6, ORBIS_KERNEL_EVFILT_VIDEO_OUT, (flipArg << 16) | ((uint64_t)handle & 0xffff), vh->flipQueueUdata);
    }

    pthread_mutex_unlock(&vh->mutex);
    return 0;
}

int32_t sceVideoOutGetFlipStatus(int32_t handle, OrbisVideoOutFlipStatus *status) {
    if (!status) return -EINVAL;

    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    *status = vh->flipStatus;
    pthread_mutex_unlock(&vh->mutex);

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

// Shims for guest execution
void shim_sceVideoOutOpen(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    int32_t busType = (int32_t)ctx->rsi;
    int32_t index = (int32_t)ctx->rdx;
    uint64_t paramGuest = ctx->rcx;
    const void *param = paramGuest ? (const void *)(ctx->mem_base + paramGuest) : NULL;

    int32_t h = sceVideoOutOpen(userId, busType, index, param);
    ctx->rax = (uint64_t)(int64_t)h;
    SHIM_RETURN();
}

void shim_sceVideoOutClose(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceVideoOutClose(handle);
    SHIM_RETURN();
}

void shim_sceVideoOutSetBufferAttribute(GuestContext *ctx) {
    uint64_t attrGuest = ctx->rdi;
    uint32_t pixelFormat = (uint32_t)ctx->rsi;
    uint32_t tilingMode = (uint32_t)ctx->rdx;
    uint32_t aspectRatio = (uint32_t)ctx->rcx;
    uint32_t width = (uint32_t)ctx->r8;
    uint32_t height = (uint32_t)ctx->r9;
    // 7th argument passed on stack in System V AMD64 ABI: [rsp + 8]
    uint64_t rsp = ctx->rsp;
    uint32_t pitch = (uint32_t)*(uint64_t *)(ctx->mem_base + rsp + 8);

    OrbisVideoOutBufferAttribute *attr = attrGuest ? (OrbisVideoOutBufferAttribute *)(ctx->mem_base + attrGuest) : NULL;
    sceVideoOutSetBufferAttribute(attr, pixelFormat, tilingMode, aspectRatio, width, height, pitch);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceVideoOutRegisterBuffers(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t startIndex = (int32_t)ctx->rsi;
    uint64_t bufferArrayGuest = ctx->rdx;
    int32_t bufferCount = (int32_t)ctx->rcx;
    uint64_t attrGuest = ctx->r8;

    void * const *bufferArray = bufferArrayGuest ? (void * const *)(ctx->mem_base + bufferArrayGuest) : NULL;
    const OrbisVideoOutBufferAttribute *attr = attrGuest ? (const OrbisVideoOutBufferAttribute *)(ctx->mem_base + attrGuest) : NULL;

    int32_t rc = sceVideoOutRegisterBuffers(handle, startIndex, bufferArray, bufferCount, attr);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutSetFlipRate(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t fliprate = (int32_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceVideoOutSetFlipRate(handle, fliprate);
    SHIM_RETURN();
}

void shim_sceVideoOutAddFlipEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    uint64_t flipArgGuest = ctx->rdx;
    void *flipArg = flipArgGuest ? (void *)(ctx->mem_base + flipArgGuest) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceVideoOutAddFlipEvent(eq, handle, flipArg);
    SHIM_RETURN();
}

void shim_sceVideoOutSubmitFlip(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t bufferIndex = (int32_t)ctx->rsi;
    uint32_t flipMode = (uint32_t)ctx->rdx;
    int64_t flipArg = (int64_t)ctx->rcx;

    int32_t rc = sceVideoOutSubmitFlip(ctx, handle, bufferIndex, flipMode, flipArg);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutGetFlipStatus(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t statusGuest = ctx->rsi;
    OrbisVideoOutFlipStatus *status = statusGuest ? (OrbisVideoOutFlipStatus *)(ctx->mem_base + statusGuest) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceVideoOutGetFlipStatus(handle, status);
    SHIM_RETURN();
}

int32_t sceVideoOutGetResolutionStatus(int32_t handle, OrbisVideoOutResolutionStatus *status) {
    (void)handle;
    if (!status) {
        return -EINVAL;
    }
    memset(status, 0, sizeof(*status));
    status->width = 1920;
    status->height = 1080;
    status->paneWidth = 1920;
    status->paneHeight = 1080;
    status->refreshRate = 60;
    status->screenSize = 55.0f;
    return 0;
}

int32_t sceVideoOutIsFlipPending(int32_t handle) {
    (void)handle;
    return 0;
}

int32_t sceVideoOutUnregisterBuffers(int32_t handle, int32_t setIndex) {
    (void)handle;
    (void)setIndex;
    return 0;
}

void shim_sceVideoOutGetResolutionStatus(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t statusGuest = ctx->rsi;
    OrbisVideoOutResolutionStatus *status = statusGuest ? (OrbisVideoOutResolutionStatus *)(ctx->mem_base + statusGuest) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceVideoOutGetResolutionStatus(handle, status);
    SHIM_RETURN();
}

void shim_sceVideoOutIsFlipPending(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    ctx->rax = (uint64_t)(int64_t)sceVideoOutIsFlipPending(handle);
    SHIM_RETURN();
}

void shim_sceVideoOutUnregisterBuffers(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t setIndex = (int32_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceVideoOutUnregisterBuffers(handle, setIndex);
    SHIM_RETURN();
}

int32_t sceVideoOutDeleteFlipEvent(OrbisKernelEqueue eq, int32_t handle) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    if (vh->flipQueue == eq) {
        vh->flipQueueRegistered = 0;
        vh->flipQueue = 0;
    }
    pthread_mutex_unlock(&vh->mutex);

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

int32_t sceVideoOutGetEventData(const OrbisKernelEvent *ev, int64_t *data) {
    if (!ev || !data) {
        return -EINVAL;
    }
    if (ev->filter != ORBIS_KERNEL_EVFILT_VIDEO_OUT) {
        return -EINVAL;
    }

    int64_t event_data = ev->data >> 16;
    if (ev->ident != 0x6 || ev->data >= 0) {
        *data = event_data;
    } else {
        *data = event_data | (int64_t)0xffff000000000000ULL;
    }
    return 0;
}

int32_t sceVideoOutConfigureOutputMode_(int32_t handle, uint32_t reserved, const void *mode, const void *options, uint32_t size_mode, uint32_t size_options) {
    (void)options;
    (void)size_mode;
    (void)size_options;
    (void)mode;

    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    if (reserved != 0) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

void sceVideoOutConfigureOptionsInitialize_(void *options, uint32_t size) {
    if (options && size >= sizeof(uint32_t)) {
        memset(options, 0, size);
        *(uint32_t *)options = size;
    }
}

void sceVideoOutModeSetAny_(void *mode, uint32_t size) {
    if (mode && size >= sizeof(uint32_t)) {
        memset(mode, 0xff, size);
        *(uint32_t *)mode = size;
    }
}

int32_t sceVideoOutAddVblankEvent(OrbisKernelEqueue eq, int32_t handle, void *udata) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    for (int i = 0; i < vh->vblankQueueCount; i++) {
        if (vh->vblankQueues[i] == eq) {
            vh->vblankUdatas[i] = udata;
            pthread_mutex_unlock(&vh->mutex);
            pthread_mutex_unlock(&g_video_table_mutex);
            return 0;
        }
    }
    if (vh->vblankQueueCount < MAX_EQUEUES_PER_PORT) {
        vh->vblankQueues[vh->vblankQueueCount] = eq;
        vh->vblankUdatas[vh->vblankQueueCount] = udata;
        vh->vblankQueueCount++;
    }
    pthread_mutex_unlock(&vh->mutex);
    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

int32_t sceVideoOutDeleteVblankEvent(OrbisKernelEqueue eq, int32_t handle) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    for (int i = 0; i < vh->vblankQueueCount; i++) {
        if (vh->vblankQueues[i] == eq) {
            for (int j = i; j < vh->vblankQueueCount - 1; j++) {
                vh->vblankQueues[j] = vh->vblankQueues[j + 1];
                vh->vblankUdatas[j] = vh->vblankUdatas[j + 1];
            }
            vh->vblankQueueCount--;
            break;
        }
    }
    pthread_mutex_unlock(&vh->mutex);
    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

int32_t sceVideoOutWaitVblank(int32_t handle) {
    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    pthread_mutex_unlock(&g_video_table_mutex);

    uint64_t cur_count = vh->vblankStatus.count;
    while (vh->in_use && vh->vblankStatus.count == cur_count) {
        pthread_cond_wait(&vh->vblankCond, &vh->mutex);
    }
    pthread_mutex_unlock(&vh->mutex);
    return 0;
}

int32_t sceVideoOutGetVblankStatus(int32_t handle, SceVideoOutVblankStatus *status) {
    if (!status) return -EINVAL;

    pthread_mutex_lock(&g_video_table_mutex);
    VideoOutHandle *vh = find_video_handle_locked(handle);
    if (!vh) {
        pthread_mutex_unlock(&g_video_table_mutex);
        return -EINVAL;
    }

    pthread_mutex_lock(&vh->mutex);
    *status = vh->vblankStatus;
    pthread_mutex_unlock(&vh->mutex);
    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
}

int32_t sceVideoOutGetDeviceCapabilityInfo(int32_t handle, SceVideoOutDeviceCapabilityInfo *info) {
    (void)handle;
    if (!info) return -EINVAL;
    info->capability = 0;
    return 0;
}

int32_t sceVideoOutGetEventCount(const OrbisKernelEvent *ev) {
    if (!ev || ev->filter != ORBIS_KERNEL_EVFILT_VIDEO_OUT) return -EINVAL;
    return (int32_t)(ev->data & 0xffff);
}

int32_t sceVideoOutGetEventId(const OrbisKernelEvent *ev) {
    if (!ev) return -EINVAL;
    return (int32_t)ev->ident;
}

void shim_sceVideoOutAddVblankEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    void *udata = ctx->rdx ? (void *)(ctx->mem_base + ctx->rdx) : NULL;
    int32_t rc = sceVideoOutAddVblankEvent(eq, handle, udata);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutDeleteVblankEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    int32_t rc = sceVideoOutDeleteVblankEvent(eq, handle);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutWaitVblank(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t rc = sceVideoOutWaitVblank(handle);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutGetVblankStatus(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t statusGuest = ctx->rsi;
    SceVideoOutVblankStatus *status = statusGuest ? (SceVideoOutVblankStatus *)(ctx->mem_base + statusGuest) : NULL;
    int32_t rc = sceVideoOutGetVblankStatus(handle, status);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutGetDeviceCapabilityInfo(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint64_t infoGuest = ctx->rsi;
    SceVideoOutDeviceCapabilityInfo *info = infoGuest ? (SceVideoOutDeviceCapabilityInfo *)(ctx->mem_base + infoGuest) : NULL;
    int32_t rc = sceVideoOutGetDeviceCapabilityInfo(handle, info);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutGetDeviceCapabilityInfo_(GuestContext *ctx) {
    shim_sceVideoOutGetDeviceCapabilityInfo(ctx);
}

void shim_sceVideoOutGetEventCount(GuestContext *ctx) {
    uint64_t evGuest = ctx->rdi;
    const OrbisKernelEvent *ev = evGuest ? (const OrbisKernelEvent *)(ctx->mem_base + evGuest) : NULL;
    int32_t rc = sceVideoOutGetEventCount(ev);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutGetEventId(GuestContext *ctx) {
    uint64_t evGuest = ctx->rdi;
    const OrbisKernelEvent *ev = evGuest ? (const OrbisKernelEvent *)(ctx->mem_base + evGuest) : NULL;
    int32_t rc = sceVideoOutGetEventId(ev);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutModeSetAny_(GuestContext *ctx) {
    uint64_t modeGuest = ctx->rdi;
    uint32_t size = (uint32_t)ctx->rsi;
    void *mode = modeGuest ? (void *)(ctx->mem_base + modeGuest) : NULL;
    sceVideoOutModeSetAny_(mode, size);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceVideoOutConfigureOutputMode_(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    uint32_t reserved = (uint32_t)ctx->rsi;
    uint64_t modeGuest = ctx->rdx;
    uint64_t optionsGuest = ctx->rcx;
    uint32_t size_mode = (uint32_t)ctx->r8;
    uint32_t size_options = (uint32_t)ctx->r9;

    const void *mode = modeGuest ? (const void *)(ctx->mem_base + modeGuest) : NULL;
    const void *options = optionsGuest ? (const void *)(ctx->mem_base + optionsGuest) : NULL;

    int32_t rc = sceVideoOutConfigureOutputMode_(handle, reserved, mode, options, size_mode, size_options);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutConfigureOptionsInitialize_(GuestContext *ctx) {
    uint64_t optionsGuest = ctx->rdi;
    uint32_t size = (uint32_t)ctx->rsi;
    void *options = optionsGuest ? (void *)(ctx->mem_base + optionsGuest) : NULL;
    sceVideoOutConfigureOptionsInitialize_(options, size);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceVideoOutDeleteFlipEvent(GuestContext *ctx) {
    OrbisKernelEqueue eq = (OrbisKernelEqueue)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    int32_t rc = sceVideoOutDeleteFlipEvent(eq, handle);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceVideoOutGetEventData(GuestContext *ctx) {
    uint64_t evGuest = ctx->rdi;
    uint64_t dataGuest = ctx->rsi;
    const OrbisKernelEvent *ev = evGuest ? (const OrbisKernelEvent *)(ctx->mem_base + evGuest) : NULL;
    int64_t *data = dataGuest ? (int64_t *)(ctx->mem_base + dataGuest) : NULL;
    int32_t rc = sceVideoOutGetEventData(ev, data);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}
