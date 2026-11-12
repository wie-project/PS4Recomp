#include "ps4_voice_qos.h"
#include <stdio.h>

#define VOICE_QOS_LOG(fmt, ...) printf("[libSceVoiceQoS] " fmt "\n", ##__VA_ARGS__)

static int g_voice_qos_initialized = 0;

int32_t sceVoiceQoSInit(void) {
    VOICE_QOS_LOG("sceVoiceQoSInit");
    g_voice_qos_initialized = 1;
    return 0;
}

int32_t sceVoiceQoSInitHQ(void) {
    VOICE_QOS_LOG("sceVoiceQoSInitHQ");
    g_voice_qos_initialized = 1;
    return 0;
}

int32_t sceVoiceQoSEnd(void) {
    VOICE_QOS_LOG("sceVoiceQoSEnd");
    g_voice_qos_initialized = 0;
    return 0;
}

int32_t sceVoiceQoSConnect(void) {
    VOICE_QOS_LOG("sceVoiceQoSConnect");
    return 0;
}

int32_t sceVoiceQoSDisconnect(void) {
    VOICE_QOS_LOG("sceVoiceQoSDisconnect");
    return 0;
}

int32_t sceVoiceQoSGetStatus(void) {
    return g_voice_qos_initialized;
}

void shim_sceVoiceQoSInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSInit();
    SHIM_RETURN();
}

void shim_sceVoiceQoSInitHQ(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSInitHQ();
    SHIM_RETURN();
}

void shim_sceVoiceQoSEnd(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSEnd();
    SHIM_RETURN();
}

void shim_sceVoiceQoSConnect(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSConnect();
    SHIM_RETURN();
}

void shim_sceVoiceQoSDisconnect(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSDisconnect();
    SHIM_RETURN();
}

void shim_sceVoiceQoSGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceVoiceQoSGetStatus();
    SHIM_RETURN();
}

#include <pthread.h>
#include <string.h>

#define VOICE_QOS_ERR_INVALID_PARAM ((int32_t)0x804E0805)
#define VOICE_QOS_ERR_INVALID_ID    ((int32_t)0x804E0804)
#define VOICE_QOS_MAX_ENDPOINTS     32
#define VOICE_QOS_MAX_ATTRS         16

typedef struct VoiceQosEndpoint {
    int in_use;
    int32_t id;
    uint32_t attrs[VOICE_QOS_MAX_ATTRS];
} VoiceQosEndpoint;

static pthread_mutex_t g_vqos_mutex = PTHREAD_MUTEX_INITIALIZER;
static VoiceQosEndpoint g_vqos_local[VOICE_QOS_MAX_ENDPOINTS];
static VoiceQosEndpoint g_vqos_remote[VOICE_QOS_MAX_ENDPOINTS];
static int32_t g_vqos_next_local_id = 1;
static int32_t g_vqos_next_remote_id = 1001;

static VoiceQosEndpoint *vqos_find(VoiceQosEndpoint *arr, int32_t id) {
    for (int i = 0; i < VOICE_QOS_MAX_ENDPOINTS; i++) {
        if (arr[i].in_use && arr[i].id == id) return &arr[i];
    }
    return NULL;
}

static int32_t vqos_create_ep(GuestContext *ctx, VoiceQosEndpoint *arr, int32_t *next_id) {
    if (!ctx || !ctx->rdi || !ctx->mem_base || ctx->rdi + sizeof(int32_t) > ctx->mem_size) {
        return VOICE_QOS_ERR_INVALID_PARAM;
    }
    pthread_mutex_lock(&g_vqos_mutex);
    int slot = -1;
    for (int i = 0; i < VOICE_QOS_MAX_ENDPOINTS; i++) {
        if (!arr[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_vqos_mutex);
        return (int32_t)0x804E0808;
    }
    int32_t id = (*next_id)++;
    memset(&arr[slot], 0, sizeof(VoiceQosEndpoint));
    arr[slot].in_use = 1;
    arr[slot].id = id;
    pthread_mutex_unlock(&g_vqos_mutex);
    *(int32_t *)(ctx->mem_base + ctx->rdi) = id;
    return 0;
}

static int32_t vqos_delete_ep(VoiceQosEndpoint *arr, int32_t id) {
    pthread_mutex_lock(&g_vqos_mutex);
    VoiceQosEndpoint *ep = vqos_find(arr, id);
    if (!ep) {
        pthread_mutex_unlock(&g_vqos_mutex);
        return VOICE_QOS_ERR_INVALID_ID;
    }
    ep->in_use = 0;
    pthread_mutex_unlock(&g_vqos_mutex);
    return 0;
}

static int32_t vqos_set_attr(GuestContext *ctx, VoiceQosEndpoint *arr) {
    int32_t id = (int32_t)ctx->rdi;
    uint32_t attr_id = (uint32_t)ctx->rsi;
    uint64_t val_addr = ctx->rdx;
    uint32_t val_size = (uint32_t)ctx->rcx;
    if (!val_addr || !ctx->mem_base || val_addr + val_size > ctx->mem_size) {
        return VOICE_QOS_ERR_INVALID_PARAM;
    }
    pthread_mutex_lock(&g_vqos_mutex);
    VoiceQosEndpoint *ep = vqos_find(arr, id);
    if (!ep) {
        pthread_mutex_unlock(&g_vqos_mutex);
        return VOICE_QOS_ERR_INVALID_ID;
    }
    if (attr_id < VOICE_QOS_MAX_ATTRS && val_size >= 4) {
        memcpy(&ep->attrs[attr_id], ctx->mem_base + val_addr, 4);
    }
    pthread_mutex_unlock(&g_vqos_mutex);
    return 0;
}

static int32_t vqos_get_attr(GuestContext *ctx, VoiceQosEndpoint *arr) {
    int32_t id = (int32_t)ctx->rdi;
    uint32_t attr_id = (uint32_t)ctx->rsi;
    uint64_t val_addr = ctx->rdx;
    uint32_t val_size = (uint32_t)ctx->rcx;
    if (!val_addr || !ctx->mem_base || val_addr + val_size > ctx->mem_size) {
        return VOICE_QOS_ERR_INVALID_PARAM;
    }
    pthread_mutex_lock(&g_vqos_mutex);
    VoiceQosEndpoint *ep = vqos_find(arr, id);
    if (!ep) {
        pthread_mutex_unlock(&g_vqos_mutex);
        return VOICE_QOS_ERR_INVALID_ID;
    }
    if (val_size > 0) {
        memset(ctx->mem_base + val_addr, 0, val_size);
        if (attr_id < VOICE_QOS_MAX_ATTRS && val_size >= 4) {
            memcpy(ctx->mem_base + val_addr, &ep->attrs[attr_id], 4);
        }
    }
    pthread_mutex_unlock(&g_vqos_mutex);
    return 0;
}

void shim_sceVoiceQoSCreateLocalEndpoint(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_create_ep(ctx, g_vqos_local, &g_vqos_next_local_id);
    SHIM_RETURN();
}

void shim_sceVoiceQoSDeleteLocalEndpoint(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_delete_ep(g_vqos_local, (int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceVoiceQoSCreateRemoteEndpoint(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_create_ep(ctx, g_vqos_remote, &g_vqos_next_remote_id);
    SHIM_RETURN();
}

void shim_sceVoiceQoSDeleteRemoteEndpoint(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_delete_ep(g_vqos_remote, (int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceVoiceQoSSetLocalEndpointAttribute(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_set_attr(ctx, g_vqos_local);
    SHIM_RETURN();
}

void shim_sceVoiceQoSGetLocalEndpointAttribute(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_get_attr(ctx, g_vqos_local);
    SHIM_RETURN();
}

void shim_sceVoiceQoSSetRemoteEndpointAttribute(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_set_attr(ctx, g_vqos_remote);
    SHIM_RETURN();
}

void shim_sceVoiceQoSGetRemoteEndpointAttribute(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)vqos_get_attr(ctx, g_vqos_remote);
    SHIM_RETURN();
}

void shim_sceVoiceQoSGetConnectionAttribute(GuestContext *ctx) {
    uint64_t val_addr = ctx->rdx;
    uint32_t val_size = (uint32_t)ctx->rcx;
    if (!val_addr || !ctx->mem_base || val_addr + val_size > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)VOICE_QOS_ERR_INVALID_PARAM;
        SHIM_RETURN();
    }
    if (val_size > 0) memset(ctx->mem_base + val_addr, 0, val_size);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceVoiceQoSReadPacket(GuestContext *ctx) {
    uint64_t size_addr = ctx->rdx;
    if (!size_addr || !ctx->mem_base || size_addr + sizeof(uint32_t) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)VOICE_QOS_ERR_INVALID_PARAM;
        SHIM_RETURN();
    }
    // No incoming peer packets on offline host
    *(uint32_t *)(ctx->mem_base + size_addr) = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_sceVoiceQoSWritePacket(GuestContext *ctx) {
    uint64_t size_addr = ctx->rdx;
    if (!size_addr || !ctx->mem_base || size_addr + sizeof(uint32_t) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)VOICE_QOS_ERR_INVALID_PARAM;
        SHIM_RETURN();
    }
    ctx->rax = 0;
    SHIM_RETURN();
}
