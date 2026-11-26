// SPDX-License-Identifier: GPL-2.0-or-later
//
// libSceVideoCoreServerInterface. Follows shadPS4's VideoCoreResourceManager: acquiring checks
// the three pointers (ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT), hands out the next identifier
// and records the request; releasing identifier 0, an unknown identifier, or one acquired
// through the other family fails with the family's "resource not found" error.
#include "ps4_videocore_server.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct VideoCoreResource {
    uint32_t id;
    bool is_be_wrapper;
    OrbisVideoCoreAcquireDecoderResourceParam param;
    uint64_t callback;
} VideoCoreResource;

static pthread_mutex_t g_vcs_lock = PTHREAD_MUTEX_INITIALIZER;
static VideoCoreResource *g_vcs_resources; // unordered; grows by doubling
static uint32_t g_vcs_count;
static uint32_t g_vcs_capacity;
static uint32_t g_vcs_next_id = 1;

static const char *vcs_family(bool is_be_wrapper) { return is_be_wrapper ? "BeWrapper" : "Standard"; }

static int32_t vcs_not_found(bool is_be_wrapper) {
    return is_be_wrapper ? ORBIS_VIDEO_CORE_ERROR_BE_WRAPPER_RESOURCE_NOT_FOUND
                         : ORBIS_VIDEO_CORE_ERROR_RESOURCE_NOT_FOUND;
}

int32_t ps4_videocore_acquire(const OrbisVideoCoreAcquireDecoderResourceParam *param, uint64_t callback,
                              uint32_t *out_resource_id, bool is_be_wrapper) {
    if (!param || !callback || !out_resource_id) {
        fprintf(stderr, "[libSceVideoCoreServerInterface] invalid arguments to acquire\n");
        return ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_vcs_lock);
    if (g_vcs_count == g_vcs_capacity) {
        const uint32_t cap = g_vcs_capacity ? g_vcs_capacity * 2 : 8;
        VideoCoreResource *grown = (VideoCoreResource *)realloc(g_vcs_resources, cap * sizeof(*grown));
        if (!grown) {
            pthread_mutex_unlock(&g_vcs_lock);
            fprintf(stderr, "[libSceVideoCoreServerInterface] out of host memory\n");
            return ORBIS_VIDEO_CORE_ERROR_INVALID_ARGUMENT;
        }
        g_vcs_resources = grown;
        g_vcs_capacity = cap;
    }
    const uint32_t id = g_vcs_next_id++;
    g_vcs_resources[g_vcs_count++] = (VideoCoreResource){
        .id = id,
        .is_be_wrapper = is_be_wrapper,
        .param = *param,
        .callback = callback,
    };
    pthread_mutex_unlock(&g_vcs_lock);
    *out_resource_id = id;
    fprintf(stderr,
            "[libSceVideoCoreServerInterface] acquired %s decoder resource id=%u, type=%u, %ux%u, spec=%u\n",
            vcs_family(is_be_wrapper), id, param->resource_type, param->max_width, param->max_height,
            param->stream_spec);
    return 0;
}

int32_t ps4_videocore_release(uint32_t resource_id, bool is_be_wrapper) {
    if (resource_id == 0) return vcs_not_found(is_be_wrapper);
    pthread_mutex_lock(&g_vcs_lock);
    uint32_t i = 0;
    while (i < g_vcs_count && g_vcs_resources[i].id != resource_id) i++;
    if (i == g_vcs_count || g_vcs_resources[i].is_be_wrapper != is_be_wrapper) {
        const bool mismatch = i < g_vcs_count;
        pthread_mutex_unlock(&g_vcs_lock);
        fprintf(stderr, "[libSceVideoCoreServerInterface] release %s resource id=%u: %s\n",
                vcs_family(is_be_wrapper), resource_id, mismatch ? "acquired by the other family" : "not found");
        return vcs_not_found(is_be_wrapper);
    }
    g_vcs_resources[i] = g_vcs_resources[--g_vcs_count];
    pthread_mutex_unlock(&g_vcs_lock);
    return 0;
}

uint32_t ps4_videocore_resource_count(void) {
    pthread_mutex_lock(&g_vcs_lock);
    const uint32_t n = g_vcs_count;
    pthread_mutex_unlock(&g_vcs_lock);
    return n;
}

static void vcs_acquire_shim(GuestContext *ctx, bool is_be_wrapper) {
    const OrbisVideoCoreAcquireDecoderResourceParam *param =
        ctx->rdi ? (const OrbisVideoCoreAcquireDecoderResourceParam *)(ctx->mem_base + ctx->rdi) : NULL;
    uint32_t *out_id = ctx->rdx ? (uint32_t *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)(int64_t)ps4_videocore_acquire(param, ctx->rsi, out_id, is_be_wrapper);
    SHIM_RETURN();
}

// int sceVideoCoreAcquireDecoderResource(const SceVideoCoreAcquireDecoderResourceParam *param,
//                                        SceVideoCoreCallback cb, uint32_t *outResourceId)
void shim_sceVideoCoreAcquireDecoderResource(GuestContext *ctx) { vcs_acquire_shim(ctx, false); }

void shim_sceVideoCoreAcquireDecoderResourceForBeWrapper(GuestContext *ctx) { vcs_acquire_shim(ctx, true); }

// int sceVideoCoreReleaseDecoderResource(uint32_t resourceId)
void shim_sceVideoCoreReleaseDecoderResource(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_videocore_release((uint32_t)ctx->rdi, false);
    SHIM_RETURN();
}

void shim_sceVideoCoreReleaseDecoderResourceForBeWrapper(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)ps4_videocore_release((uint32_t)ctx->rdi, true);
    SHIM_RETURN();
}
