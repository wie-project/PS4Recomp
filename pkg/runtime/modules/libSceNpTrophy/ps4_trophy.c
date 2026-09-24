#include "ps4_trophy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define MAX_TROPHY_CONTEXTS 16
#define MAX_TROPHY_HANDLES 16

typedef struct {
    int in_use;
    int32_t id;
    int32_t user_id;
    int32_t registered_handle;
    uint32_t unlocked_trophies[8]; // bitmask for up to 256 trophies
} TrophyContext;

typedef struct {
    int in_use;
    int32_t id;
} TrophyHandle;

static TrophyContext g_contexts[MAX_TROPHY_CONTEXTS];
static TrophyHandle g_handles[MAX_TROPHY_HANDLES];
static pthread_mutex_t g_trophy_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_next_context_id = 1;
static int32_t g_next_handle_id = 1;
static int g_trophy_inited = 0;

int32_t sceNpTrophyInit(void *params) {
    (void)params;
    pthread_mutex_lock(&g_trophy_mutex);
    if (!g_trophy_inited) {
        memset(g_contexts, 0, sizeof(g_contexts));
        memset(g_handles, 0, sizeof(g_handles));
        g_trophy_inited = 1;
    }
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyTerm(void) {
    pthread_mutex_lock(&g_trophy_mutex);
    g_trophy_inited = 0;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyCreateContext(int32_t *context, int32_t user, uint32_t unk, uint64_t unk2) {
    (void)unk;
    (void)unk2;
    if (!context) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_trophy_mutex);
    if (!g_trophy_inited) {
        g_trophy_inited = 1;
        memset(g_contexts, 0, sizeof(g_contexts));
        memset(g_handles, 0, sizeof(g_handles));
    }

    TrophyContext *slot = NULL;
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (!g_contexts[i].in_use) {
            slot = &g_contexts[i];
            break;
        }
    }

    if (!slot) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -ENOMEM;
    }

    slot->in_use = 1;
    slot->id = g_next_context_id++;
    slot->user_id = user;
    slot->registered_handle = 0;
    memset(slot->unlocked_trophies, 0, sizeof(slot->unlocked_trophies));

    *context = slot->id;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyDestroyContext(int32_t context) {
    pthread_mutex_lock(&g_trophy_mutex);
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) {
            g_contexts[i].in_use = 0;
            pthread_mutex_unlock(&g_trophy_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_trophy_mutex);
    return -EINVAL;
}

int32_t sceNpTrophyCreateHandle(int32_t *handle) {
    if (!handle) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_trophy_mutex);
    if (!g_trophy_inited) {
        g_trophy_inited = 1;
        memset(g_contexts, 0, sizeof(g_contexts));
        memset(g_handles, 0, sizeof(g_handles));
    }

    TrophyHandle *slot = NULL;
    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            slot = &g_handles[i];
            break;
        }
    }

    if (!slot) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -ENOMEM;
    }

    slot->in_use = 1;
    slot->id = g_next_handle_id++;

    *handle = slot->id;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyDestroyHandle(int32_t handle) {
    pthread_mutex_lock(&g_trophy_mutex);
    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].id == handle) {
            g_handles[i].in_use = 0;
            pthread_mutex_unlock(&g_trophy_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_trophy_mutex);
    return -EINVAL;
}

int32_t sceNpTrophyRegisterContext(int32_t context, int32_t handle, uint64_t unk) {
    (void)unk;
    pthread_mutex_lock(&g_trophy_mutex);

    int context_found = 0;
    int handle_found = 0;
    TrophyContext *ctx_ptr = NULL;

    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) {
            context_found = 1;
            ctx_ptr = &g_contexts[i];
            break;
        }
    }

    for (int i = 0; i < MAX_TROPHY_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].id == handle) {
            handle_found = 1;
            break;
        }
    }

    if (!context_found || !handle_found) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -EINVAL;
    }

    ctx_ptr->registered_handle = handle;
    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyUnlockTrophy(int32_t context, int32_t handle, int32_t trophyId, int32_t *platId) {
    pthread_mutex_lock(&g_trophy_mutex);

    TrophyContext *ctx_ptr = NULL;
    for (int i = 0; i < MAX_TROPHY_CONTEXTS; i++) {
        if (g_contexts[i].in_use && g_contexts[i].id == context) {
            ctx_ptr = &g_contexts[i];
            break;
        }
    }

    if (!ctx_ptr) {
        pthread_mutex_unlock(&g_trophy_mutex);
        return -EINVAL;
    }

    (void)handle;

    if (trophyId >= 0 && trophyId < 256) {
        ctx_ptr->unlocked_trophies[trophyId / 32] |= (1U << (trophyId % 32));
    }

    if (platId) {
        *platId = 0;
    }

    printf("\n==================================================\n");
    printf("🏆 [PS4 TROPHY UNLOCKED] Context: %d, Trophy ID: %d\n", context, trophyId);
    printf("==================================================\n\n");
    fflush(stdout);

    pthread_mutex_unlock(&g_trophy_mutex);
    return 0;
}

int32_t sceNpTrophyShowTrophyList(int32_t context, int32_t handle) {
    (void)context;
    (void)handle;
    printf("[ps4-recomp] sceNpTrophyShowTrophyList called\n");
    return 0;
}

// Guest ABI Shims
void shim_sceNpTrophyInit(GuestContext *ctx) {
    uint64_t paramsGuest = ctx->rdi;
    void *params = paramsGuest ? (void *)(ctx->mem_base + paramsGuest) : NULL;
    int32_t ret = sceNpTrophyInit(params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyTerm(GuestContext *ctx) {
    int32_t ret = sceNpTrophyTerm();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyCreateContext(GuestContext *ctx) {
    uint64_t contextGuest = ctx->rdi;
    int32_t user = (int32_t)ctx->rsi;
    uint32_t unk = (uint32_t)ctx->rdx;
    uint64_t unk2 = ctx->rcx;

    int32_t *context = contextGuest ? (int32_t *)(ctx->mem_base + contextGuest) : NULL;
    int32_t ret = sceNpTrophyCreateContext(context, user, unk, unk2);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyDestroyContext(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t ret = sceNpTrophyDestroyContext(context);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyCreateHandle(GuestContext *ctx) {
    uint64_t handleGuest = ctx->rdi;
    int32_t *handle = handleGuest ? (int32_t *)(ctx->mem_base + handleGuest) : NULL;
    int32_t ret = sceNpTrophyCreateHandle(handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyDestroyHandle(GuestContext *ctx) {
    int32_t handle = (int32_t)ctx->rdi;
    int32_t ret = sceNpTrophyDestroyHandle(handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyRegisterContext(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    uint64_t unk = ctx->rdx;
    int32_t ret = sceNpTrophyRegisterContext(context, handle, unk);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyUnlockTrophy(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    int32_t trophyId = (int32_t)ctx->rdx;
    uint64_t platIdGuest = ctx->rcx;
    int32_t *platId = platIdGuest ? (int32_t *)(ctx->mem_base + platIdGuest) : NULL;

    int32_t ret = sceNpTrophyUnlockTrophy(context, handle, trophyId, platId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceNpTrophyShowTrophyList(GuestContext *ctx) {
    int32_t context = (int32_t)ctx->rdi;
    int32_t handle = (int32_t)ctx->rsi;
    int32_t ret = sceNpTrophyShowTrophyList(context, handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
