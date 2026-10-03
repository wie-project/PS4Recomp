// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps4_ulobjmgr.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ULOBJ_HANDLES 0x4000
#define POSIX_EINVAL 22

typedef struct {
    int in_use;
    uint64_t obj;
    int32_t type;
} UlObjectEntry;

static UlObjectEntry g_ulobj_table[MAX_ULOBJ_HANDLES];
static pthread_mutex_t g_ulobj_lock = PTHREAD_MUTEX_INITIALIZER;

void shim__sceUlobjmgrRegisterObject(GuestContext *ctx) {
    uint64_t obj = ctx->rdi;
    int32_t type = (int32_t)ctx->rsi;
    uint64_t out_handle_gaddr = ctx->rdx;

    if (obj == 0 || type == 0 || out_handle_gaddr == 0) {
        ctx->rax = POSIX_EINVAL;
        SHIM_RETURN();
    }

    uint32_t *out_handle = (uint32_t *)(ctx->mem_base + out_handle_gaddr);
    pthread_mutex_lock(&g_ulobj_lock);
    int slot = -1;
    for (int i = 1; i < MAX_ULOBJ_HANDLES; i++) {
        if (!g_ulobj_table[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&g_ulobj_lock);
        ctx->rax = POSIX_EINVAL;
        SHIM_RETURN();
    }

    g_ulobj_table[slot].in_use = 1;
    g_ulobj_table[slot].obj = obj;
    g_ulobj_table[slot].type = type;
    *out_handle = (uint32_t)slot;
    pthread_mutex_unlock(&g_ulobj_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__sceUlobjmgrUnregisterObject(GuestContext *ctx) {
    uint32_t handle = (uint32_t)ctx->rdi;
    if (handle >= MAX_ULOBJ_HANDLES) {
        ctx->rax = POSIX_EINVAL;
        SHIM_RETURN();
    }

    pthread_mutex_lock(&g_ulobj_lock);
    if (handle > 0) {
        g_ulobj_table[handle].in_use = 0;
    }
    pthread_mutex_unlock(&g_ulobj_lock);

    ctx->rax = 0;
    SHIM_RETURN();
}
