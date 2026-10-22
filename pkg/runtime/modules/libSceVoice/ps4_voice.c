// libSceVoice (voice chat) for a host without voice chat: the library
// initialises, ports can be created/connected, reads return no data and writes
// are accepted and discarded. Behaviour mirrors shadPS4's voice stubs.
#include "recomp_runtime.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define VOICE_ERROR_ARGUMENT_INVALID ((int32_t)0x8023380F)
#define VOICE_ERROR_RESOURCE_INSUFFICIENT ((int32_t)0x80233803)

#define VOICE_MAX_PORTS 32
#define VOICE_PORTSTATE_IDLE 0

typedef struct VoicePort {
    bool used;
    int32_t type;
    bool muted;
    float volume;
} VoicePort;

// SceVoiceBasePortInfo
typedef struct VoiceBasePortInfo {
    int32_t portType;
    int32_t state;
    uint64_t pEdge;     // uint32_t*
    uint32_t numByte;
    uint32_t frameSize;
    uint16_t numEdge;
    uint16_t reserved;
} VoiceBasePortInfo;

static pthread_mutex_t g_voice_mutex = PTHREAD_MUTEX_INITIALIZER;
static VoicePort g_voice_ports[VOICE_MAX_PORTS];

#define VOICE_LOG_ONCE(name)                                                   \
    do {                                                                       \
        static int warned_;                                                    \
        if (!warned_) {                                                        \
            warned_ = 1;                                                       \
            fprintf(stderr, "[libSceVoice] %s (no voice chat on host)\n", name); \
        }                                                                      \
    } while (0)

static VoicePort *voice_port(uint32_t id) {
    return (id < VOICE_MAX_PORTS && g_voice_ports[id].used) ? &g_voice_ports[id] : NULL;
}

// int sceVoiceInit(SceVoiceInitParam *pArg, SceVoiceVersion version)
void shim_sceVoiceInit(GuestContext *ctx) {
    VOICE_LOG_ONCE("sceVoiceInit");
    pthread_mutex_lock(&g_voice_mutex);
    memset(g_voice_ports, 0, sizeof(g_voice_ports));
    pthread_mutex_unlock(&g_voice_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceEnd(void)
void shim_sceVoiceEnd(GuestContext *ctx) {
    pthread_mutex_lock(&g_voice_mutex);
    memset(g_voice_ports, 0, sizeof(g_voice_ports));
    pthread_mutex_unlock(&g_voice_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceStart(SceVoiceStartParam *pArg)
void shim_sceVoiceStart(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceStop(void)
void shim_sceVoiceStop(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceCreatePort(uint32_t *portId, const SceVoicePortParam *pArg)
void shim_sceVoiceCreatePort(GuestContext *ctx) {
    if (!ctx->rdi || !ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_ARGUMENT_INVALID;
        SHIM_RETURN();
    }
    int32_t type = *(int32_t *)(ctx->mem_base + ctx->rsi); // SceVoicePortParam.portType
    pthread_mutex_lock(&g_voice_mutex);
    int id = -1;
    for (int i = 0; i < VOICE_MAX_PORTS; i++) {
        if (!g_voice_ports[i].used) {
            id = i;
            break;
        }
    }
    if (id >= 0) {
        g_voice_ports[id].used = true;
        g_voice_ports[id].type = type;
        g_voice_ports[id].muted = false;
        g_voice_ports[id].volume = 1.0f;
    }
    pthread_mutex_unlock(&g_voice_mutex);
    if (id < 0) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_RESOURCE_INSUFFICIENT;
        SHIM_RETURN();
    }
    *(uint32_t *)(ctx->mem_base + ctx->rdi) = (uint32_t)id;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceDeletePort(uint32_t portId)
void shim_sceVoiceDeletePort(GuestContext *ctx) {
    pthread_mutex_lock(&g_voice_mutex);
    VoicePort *p = voice_port((uint32_t)ctx->rdi);
    if (p) memset(p, 0, sizeof(*p));
    pthread_mutex_unlock(&g_voice_mutex);
    ctx->rax = 0; // lenient: deleting an unknown port is harmless
    SHIM_RETURN();
}

// int sceVoiceConnectIPortToOPort(uint32_t ips, uint32_t ops)
void shim_sceVoiceConnectIPortToOPort(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceGetPortInfo(uint32_t portId, SceVoiceBasePortInfo *pInfo)
void shim_sceVoiceGetPortInfo(GuestContext *ctx) {
    if (!ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_ARGUMENT_INVALID;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&g_voice_mutex);
    VoicePort *p = voice_port((uint32_t)ctx->rdi);
    int32_t type = p ? p->type : 0;
    pthread_mutex_unlock(&g_voice_mutex);
    VoiceBasePortInfo *info = (VoiceBasePortInfo *)(ctx->mem_base + ctx->rsi);
    info->portType = type;
    info->state = VOICE_PORTSTATE_IDLE;
    info->pEdge = 0;
    info->numByte = 0;   // nothing buffered
    info->frameSize = 1;
    info->numEdge = 0;
    info->reserved = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceReadFromOPort(uint32_t ops, void *data, uint32_t *size)
void shim_sceVoiceReadFromOPort(GuestContext *ctx) {
    if (!ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_ARGUMENT_INVALID;
        SHIM_RETURN();
    }
    *(uint32_t *)(ctx->mem_base + ctx->rdx) = 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceWriteToIPort(uint32_t ips, const void *data, uint32_t *size, int16_t frameGaps)
// Accept everything: *size is left as "bytes consumed".
void shim_sceVoiceWriteToIPort(GuestContext *ctx) {
    if (!ctx->rdx) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_ARGUMENT_INVALID;
        SHIM_RETURN();
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceGetMuteFlag(uint32_t portId, bool *bMuted)
void shim_sceVoiceGetMuteFlag(GuestContext *ctx) {
    if (!ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_ARGUMENT_INVALID;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&g_voice_mutex);
    VoicePort *p = voice_port((uint32_t)ctx->rdi);
    bool muted = p ? p->muted : false;
    pthread_mutex_unlock(&g_voice_mutex);
    *(uint8_t *)(ctx->mem_base + ctx->rsi) = muted ? 1 : 0;
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceSetMuteFlag(uint32_t portId, bool bMuted)
void shim_sceVoiceSetMuteFlag(GuestContext *ctx) {
    pthread_mutex_lock(&g_voice_mutex);
    VoicePort *p = voice_port((uint32_t)ctx->rdi);
    if (p) p->muted = (ctx->rsi & 0xff) != 0;
    pthread_mutex_unlock(&g_voice_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceSetMuteFlagAll(bool bMuted)
void shim_sceVoiceSetMuteFlagAll(GuestContext *ctx) {
    bool m = (ctx->rdi & 0xff) != 0;
    pthread_mutex_lock(&g_voice_mutex);
    for (int i = 0; i < VOICE_MAX_PORTS; i++) {
        if (g_voice_ports[i].used) g_voice_ports[i].muted = m;
    }
    pthread_mutex_unlock(&g_voice_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceGetVolume(uint32_t portId, float *volume)
void shim_sceVoiceGetVolume(GuestContext *ctx) {
    if (!ctx->rsi) {
        ctx->rax = (uint64_t)(int64_t)VOICE_ERROR_ARGUMENT_INVALID;
        SHIM_RETURN();
    }
    pthread_mutex_lock(&g_voice_mutex);
    VoicePort *p = voice_port((uint32_t)ctx->rdi);
    float v = p ? p->volume : 1.0f;
    pthread_mutex_unlock(&g_voice_mutex);
    memcpy(ctx->mem_base + ctx->rsi, &v, sizeof(v));
    ctx->rax = 0;
    SHIM_RETURN();
}

// int sceVoiceSetVolume(uint32_t portId, float volume) -- volume arrives in xmm0.
void shim_sceVoiceSetVolume(GuestContext *ctx) {
    float v;
    memcpy(&v, &ctx->xmm[0], sizeof(v));
    pthread_mutex_lock(&g_voice_mutex);
    VoicePort *p = voice_port((uint32_t)ctx->rdi);
    if (p) p->volume = v;
    pthread_mutex_unlock(&g_voice_mutex);
    ctx->rax = 0;
    SHIM_RETURN();
}
