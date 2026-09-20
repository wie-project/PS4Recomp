#include "ps4_videoout.h"
#include "ps4_metal_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define MAX_VIDEO_HANDLES 8
#define MAX_FRAME_BUFFERS 16

typedef struct {
    int in_use;
    int32_t handle;
    OrbisVideoOutBufferAttribute attr;
    int bufferCount;
    uint64_t buffers[MAX_FRAME_BUFFERS];
    OrbisKernelEqueue flipQueue;
    int flipQueueRegistered;
    int32_t flipRate;
    OrbisVideoOutFlipStatus flipStatus;
    pthread_mutex_t mutex;
} VideoOutHandle;

static VideoOutHandle g_video_handles[MAX_VIDEO_HANDLES];
static pthread_mutex_t g_video_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_next_video_handle = 1;

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
    vh->flipQueueRegistered = 0;
    vh->flipRate = 0;
    memset(&vh->attr, 0, sizeof(vh->attr));
    memset(&vh->flipStatus, 0, sizeof(vh->flipStatus));
    pthread_mutex_init(&vh->mutex, NULL);

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
    pthread_mutex_unlock(&vh->mutex);
    pthread_mutex_destroy(&vh->mutex);

    pthread_mutex_unlock(&g_video_table_mutex);
    return 0;
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
        ps4_equeue_post_event(vh->flipQueue, (uint64_t)handle, ORBIS_KERNEL_EVFILT_VIDEO_OUT, flipArg, NULL);
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
