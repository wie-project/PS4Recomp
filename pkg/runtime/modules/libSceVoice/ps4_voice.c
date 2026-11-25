// SPDX-License-Identifier: GPL-2.0-or-later

// libSceVoice, ported from shadPS4 (src/core/libraries/voice). Voice chat needs the online
// service, which is not emulated: the library keeps its ports (type, mute flag, volume, bit
// rate) and validates every call as the console does, but no voice data flows. Output ports
// read nothing and input ports take nothing; the microphone port reports no device.

#include "ps4_voice.h"
#include "ps4_audioin.h"
#include "ps4_hle_stub.h"
#include <math.h>
#include <pthread.h>
#include <string.h>

typedef struct VoicePort {
    bool allocated;
    int32_t type;
    bool muted;
    float volume;
    uint32_t bitrate;
} VoicePort;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized;
static VoicePort g_ports[ORBIS_VOICE_MAX_PORT];

static void reset_port(VoicePort *p, bool allocated, int32_t type) {
    p->allocated = allocated;
    p->type = type;
    p->muted = false;
    p->volume = 1.0f;
    p->bitrate = 48000;
}

static void reset_ports(void) {
    for (int i = 0; i < ORBIS_VOICE_MAX_PORT; i++) {
        reset_port(&g_ports[i], false, 0);
    }
}

static int32_t check_port(uint32_t port_id) {
    if (!g_initialized) return ORBIS_VOICE_ERROR_NOT_INIT;
    if (port_id >= ORBIS_VOICE_MAX_PORT) return ORBIS_VOICE_ERROR_PORT_INVALID;
    return 0;
}

static VoicePort *find_port(uint32_t port_id) {
    return g_ports[port_id].allocated ? &g_ports[port_id] : NULL;
}

// Runs `body` under the lock for a valid port ID, returning check_port's error otherwise.
#define WITH_PORT(port_id, body)                 \
    do {                                         \
        pthread_mutex_lock(&g_mutex);            \
        int32_t r_ = check_port(port_id);        \
        if (r_ == 0) {                           \
            body                                 \
        }                                        \
        pthread_mutex_unlock(&g_mutex);          \
        return r_;                               \
    } while (0)

static int32_t init_internal(const OrbisVoiceInitParam *param) {
    pthread_mutex_lock(&g_mutex);
    int32_t r = 0;
    if (g_initialized) {
        r = ORBIS_VOICE_ERROR_ALREADY_INIT;
    } else if (!param) {
        r = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
    } else {
        reset_ports();
        g_initialized = true;
    }
    pthread_mutex_unlock(&g_mutex);
    return r;
}

int32_t sceVoiceInit(const OrbisVoiceInitParam *param, int32_t container) {
    return init_internal(param);
}

int32_t sceVoiceInitHQ(const OrbisVoiceInitParam *param, int32_t container) {
    return init_internal(param);
}

static int32_t if_initialized(void) {
    pthread_mutex_lock(&g_mutex);
    int32_t r = g_initialized ? 0 : ORBIS_VOICE_ERROR_NOT_INIT;
    pthread_mutex_unlock(&g_mutex);
    return r;
}

int32_t sceVoiceEnd(void) {
    pthread_mutex_lock(&g_mutex);
    int32_t r = 0;
    if (!g_initialized) {
        r = ORBIS_VOICE_ERROR_NOT_INIT;
    } else {
        g_initialized = false;
        reset_ports();
    }
    pthread_mutex_unlock(&g_mutex);
    return r;
}

int32_t sceVoiceStart(const OrbisVoiceStartParam *param) {
    int32_t r = if_initialized();
    if (r == 0 && (!param || !param->memory)) r = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
    return r;
}

int32_t sceVoiceStop(void) {
    return if_initialized();
}

int32_t sceVoiceCreatePort(int32_t *port_id, const OrbisVoicePortParam *param) {
    pthread_mutex_lock(&g_mutex);
    int32_t r = 0;
    if (!g_initialized) {
        r = ORBIS_VOICE_ERROR_NOT_INIT;
    } else if (!port_id || !param || param->type < ORBIS_VOICE_PORTTYPE_IN_DEVICE ||
               param->type > ORBIS_VOICE_PORTTYPE_OUT_DEVICE) {
        r = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
    } else {
        r = ORBIS_VOICE_ERROR_RESOURCE_INSUFFICIENT;
        for (int32_t i = 0; i < ORBIS_VOICE_MAX_PORT; i++) {
            if (!g_ports[i].allocated) {
                reset_port(&g_ports[i], true, param->type);
                *port_id = i;
                r = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return r;
}

int32_t sceVoiceUpdatePort(uint32_t port_id, const OrbisVoicePortParam *param) {
    WITH_PORT(port_id, {
        if (!param) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!find_port(port_id)) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        }
    });
}

int32_t sceVoiceDeletePort(uint32_t port_id) {
    WITH_PORT(port_id, {
        if (!find_port(port_id)) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            reset_port(&g_ports[port_id], false, 0);
        }
    });
}

static int32_t check_connection(uint32_t in_port_id, uint32_t out_port_id) {
    pthread_mutex_lock(&g_mutex);
    int32_t r = 0;
    if (!g_initialized) {
        r = ORBIS_VOICE_ERROR_NOT_INIT;
    } else if (in_port_id >= ORBIS_VOICE_MAX_PORT || out_port_id >= ORBIS_VOICE_MAX_PORT) {
        r = ORBIS_VOICE_ERROR_PORT_INVALID;
    } else if (!find_port(in_port_id) || !find_port(out_port_id)) {
        r = ORBIS_VOICE_ERROR_TOPOLOGY;
    }
    pthread_mutex_unlock(&g_mutex);
    return r;
}

int32_t sceVoiceConnectIPortToOPort(uint32_t in_port_id, uint32_t out_port_id) {
    return check_connection(in_port_id, out_port_id);
}

int32_t sceVoiceDisconnectIPortFromOPort(uint32_t in_port_id, uint32_t out_port_id) {
    return check_connection(in_port_id, out_port_id);
}

int32_t sceVoiceGetPortInfo(uint32_t port_id, OrbisVoicePortInfo *info) {
    WITH_PORT(port_id, {
        const VoicePort *p = find_port(port_id);
        if (!info) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!p) {
            r_ = ORBIS_VOICE_ERROR_TOPOLOGY;
        } else {
            info->port_type = p->type;
            info->state = 0;
            info->byte_count = 0; // nothing buffered
            info->frame_size = 1;
            info->edge_count = 0;
            info->reserved = 0;
        }
    });
}

int32_t sceVoiceGetPortAttr(uint32_t port_id, int32_t attr_id, void *value, uint32_t size) {
    WITH_PORT(port_id, {
        uint32_t expected_size = 0;
        int32_t required_type = ORBIS_VOICE_PORTTYPE_IN_DEVICE;
        switch (attr_id) {
        case ORBIS_VOICE_ATTR_1000:
        case ORBIS_VOICE_ATTR_1001:
        case ORBIS_VOICE_ATTR_2000:
        case ORBIS_VOICE_ATTR_2002: expected_size = 1; break;
        case ORBIS_VOICE_ATTR_1002:
        case ORBIS_VOICE_ATTR_2001: expected_size = 4; break;
        case ORBIS_VOICE_ATTR_1003:
            expected_size = 2;
            required_type = ORBIS_VOICE_PORTTYPE_OUT_DEVICE;
            break;
        default: break;
        }
        const VoicePort *p = find_port(port_id);
        if (!value || expected_size == 0 || size != expected_size) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else if (p->type != required_type) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else {
            // The voice library has no microphone of its own.
            const uint32_t result = attr_id == ORBIS_VOICE_ATTR_1002 ? ORBIS_AUDIO_IN_SILENT_STATE_DEVICE_NONE : 0;
            if (expected_size == 1) {
                *(uint8_t *)value = (uint8_t)result;
            } else if (expected_size == 2) {
                *(uint16_t *)value = (uint16_t)result;
            } else {
                *(uint32_t *)value = result;
            }
        }
    });
}

int32_t sceVoiceGetResourceInfo(OrbisVoiceResourceInfo *info) {
    int32_t r = if_initialized();
    if (r == 0) {
        if (!info) return ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        memset(info, 0, sizeof(*info));
    }
    return r;
}

int32_t sceVoiceGetBitRate(uint32_t port_id, uint32_t *bitrate) {
    WITH_PORT(port_id, {
        const VoicePort *p = find_port(port_id);
        if (!bitrate) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            *bitrate = p->bitrate;
        }
    });
}

int32_t sceVoiceSetBitRate(uint32_t port_id, uint32_t bitrate) {
    WITH_PORT(port_id, {
        VoicePort *p = find_port(port_id);
        if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            p->bitrate = bitrate;
        }
    });
}

int32_t sceVoiceGetMuteFlag(uint32_t port_id, bool *mute) {
    WITH_PORT(port_id, {
        const VoicePort *p = find_port(port_id);
        if (!mute) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            *mute = p->muted;
        }
    });
}

int32_t sceVoiceSetMuteFlag(uint32_t port_id, bool mute) {
    WITH_PORT(port_id, {
        VoicePort *p = find_port(port_id);
        if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            p->muted = mute;
        }
    });
}

int32_t sceVoiceSetMuteFlagAll(bool mute) {
    pthread_mutex_lock(&g_mutex);
    int32_t r = g_initialized ? 0 : ORBIS_VOICE_ERROR_NOT_INIT;
    if (r == 0) {
        for (int i = 0; i < ORBIS_VOICE_MAX_PORT; i++) g_ports[i].muted = mute;
    }
    pthread_mutex_unlock(&g_mutex);
    return r;
}

int32_t sceVoiceGetVolume(uint32_t port_id, float *volume) {
    WITH_PORT(port_id, {
        const VoicePort *p = find_port(port_id);
        if (!volume) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            *volume = p->volume;
        }
    });
}

int32_t sceVoiceSetVolume(uint32_t port_id, float volume) {
    WITH_PORT(port_id, {
        VoicePort *p = find_port(port_id);
        if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            p->volume = isfinite(volume) ? volume : 0.0f;
        }
    });
}

static int32_t existing_port(uint32_t port_id) {
    WITH_PORT(port_id, {
        if (!find_port(port_id)) r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
    });
}

int32_t sceVoicePausePort(uint32_t port_id) {
    return existing_port(port_id);
}

int32_t sceVoiceResumePort(uint32_t port_id) {
    return existing_port(port_id);
}

int32_t sceVoicePausePortAll(void) {
    return if_initialized();
}

int32_t sceVoiceResumePortAll(void) {
    return if_initialized();
}

int32_t sceVoiceResetPort(uint32_t port_id) {
    WITH_PORT(port_id, {
        VoicePort *p = find_port(port_id);
        if (!p) {
            r_ = ORBIS_VOICE_ERROR_PORT_INVALID;
        } else {
            reset_port(p, true, p->type);
        }
    });
}

int32_t sceVoiceReadFromOPort(uint32_t port_id, void *data, uint32_t *size) {
    WITH_PORT(port_id, {
        if (!data || !size) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else {
            *size = 0; // no voice data arrives
            const VoicePort *p = find_port(port_id);
            if (!p) {
                r_ = ORBIS_VOICE_ERROR_TOPOLOGY;
            } else if (p->type == ORBIS_VOICE_PORTTYPE_OUT_DEVICE) {
                r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
            } else if (p->type != ORBIS_VOICE_PORTTYPE_OUT_PCMAUDIO && p->type != ORBIS_VOICE_PORTTYPE_OUT_VOICE) {
                r_ = ORBIS_VOICE_ERROR_TOPOLOGY;
            }
        }
    });
}

int32_t sceVoiceWriteToIPort(uint32_t port_id, const void *data, uint32_t *size, int16_t frame_gaps) {
    WITH_PORT(port_id, {
        const VoicePort *p = find_port(port_id);
        if (!data || !size) {
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        } else if (!p) {
            *size = 0;
            r_ = ORBIS_VOICE_ERROR_TOPOLOGY;
        } else if (p->type != ORBIS_VOICE_PORTTYPE_IN_PCMAUDIO && p->type != ORBIS_VOICE_PORTTYPE_IN_VOICE) {
            *size = 0;
            r_ = ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
        }
        // Otherwise the data is taken whole (*size stays the bytes consumed) and goes nowhere.
    });
}

int32_t sceVoiceVADSetVersion(int32_t version) {
    return version == 0 || version == 1 ? 0 : ORBIS_VOICE_ERROR_ARGUMENT_INVALID;
}

// ---- Guest ABI ----

#define GPTR(type, addr) ((addr) ? (type)(ctx->mem_base + (addr)) : (type)NULL)
#define RET(v)                                      \
    do {                                            \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                              \
    } while (0)

static float xmm0_float(const GuestContext *ctx) {
    float v;
    memcpy(&v, &ctx->xmm[0], sizeof(v));
    return v;
}

void shim_sceVoiceInit(GuestContext *ctx) {
    RET(sceVoiceInit(GPTR(const OrbisVoiceInitParam *, ctx->rdi), (int32_t)ctx->rsi));
}

void shim_sceVoiceInitHQ(GuestContext *ctx) {
    RET(sceVoiceInitHQ(GPTR(const OrbisVoiceInitParam *, ctx->rdi), (int32_t)ctx->rsi));
}

void shim_sceVoiceEnd(GuestContext *ctx) {
    RET(sceVoiceEnd());
}

void shim_sceVoiceStart(GuestContext *ctx) {
    RET(sceVoiceStart(GPTR(const OrbisVoiceStartParam *, ctx->rdi)));
}

void shim_sceVoiceStop(GuestContext *ctx) {
    RET(sceVoiceStop());
}

void shim_sceVoiceCreatePort(GuestContext *ctx) {
    RET(sceVoiceCreatePort(GPTR(int32_t *, ctx->rdi), GPTR(const OrbisVoicePortParam *, ctx->rsi)));
}

void shim_sceVoiceUpdatePort(GuestContext *ctx) {
    RET(sceVoiceUpdatePort((uint32_t)ctx->rdi, GPTR(const OrbisVoicePortParam *, ctx->rsi)));
}

void shim_sceVoiceDeletePort(GuestContext *ctx) {
    RET(sceVoiceDeletePort((uint32_t)ctx->rdi));
}

void shim_sceVoiceConnectIPortToOPort(GuestContext *ctx) {
    RET(sceVoiceConnectIPortToOPort((uint32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceVoiceDisconnectIPortFromOPort(GuestContext *ctx) {
    RET(sceVoiceDisconnectIPortFromOPort((uint32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceVoiceGetPortInfo(GuestContext *ctx) {
    RET(sceVoiceGetPortInfo((uint32_t)ctx->rdi, GPTR(OrbisVoicePortInfo *, ctx->rsi)));
}

void shim_sceVoiceGetPortAttr(GuestContext *ctx) {
    RET(sceVoiceGetPortAttr((uint32_t)ctx->rdi, (int32_t)ctx->rsi, GPTR(void *, ctx->rdx), (uint32_t)ctx->rcx));
}

void shim_sceVoiceGetResourceInfo(GuestContext *ctx) {
    RET(sceVoiceGetResourceInfo(GPTR(OrbisVoiceResourceInfo *, ctx->rdi)));
}

void shim_sceVoiceGetBitRate(GuestContext *ctx) {
    RET(sceVoiceGetBitRate((uint32_t)ctx->rdi, GPTR(uint32_t *, ctx->rsi)));
}

void shim_sceVoiceSetBitRate(GuestContext *ctx) {
    RET(sceVoiceSetBitRate((uint32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceVoiceGetMuteFlag(GuestContext *ctx) {
    RET(sceVoiceGetMuteFlag((uint32_t)ctx->rdi, GPTR(bool *, ctx->rsi)));
}

void shim_sceVoiceSetMuteFlag(GuestContext *ctx) {
    RET(sceVoiceSetMuteFlag((uint32_t)ctx->rdi, (ctx->rsi & 0xff) != 0));
}

void shim_sceVoiceSetMuteFlagAll(GuestContext *ctx) {
    RET(sceVoiceSetMuteFlagAll((ctx->rdi & 0xff) != 0));
}

void shim_sceVoiceGetVolume(GuestContext *ctx) {
    RET(sceVoiceGetVolume((uint32_t)ctx->rdi, GPTR(float *, ctx->rsi)));
}

void shim_sceVoiceSetVolume(GuestContext *ctx) {
    RET(sceVoiceSetVolume((uint32_t)ctx->rdi, xmm0_float(ctx)));
}

void shim_sceVoicePausePort(GuestContext *ctx) {
    RET(sceVoicePausePort((uint32_t)ctx->rdi));
}

void shim_sceVoicePausePortAll(GuestContext *ctx) {
    RET(sceVoicePausePortAll());
}

void shim_sceVoiceResumePort(GuestContext *ctx) {
    RET(sceVoiceResumePort((uint32_t)ctx->rdi));
}

void shim_sceVoiceResumePortAll(GuestContext *ctx) {
    RET(sceVoiceResumePortAll());
}

void shim_sceVoiceResetPort(GuestContext *ctx) {
    RET(sceVoiceResetPort((uint32_t)ctx->rdi));
}

void shim_sceVoiceReadFromOPort(GuestContext *ctx) {
    RET(sceVoiceReadFromOPort((uint32_t)ctx->rdi, GPTR(void *, ctx->rsi), GPTR(uint32_t *, ctx->rdx)));
}

void shim_sceVoiceWriteToIPort(GuestContext *ctx) {
    RET(sceVoiceWriteToIPort((uint32_t)ctx->rdi, GPTR(const void *, ctx->rsi), GPTR(uint32_t *, ctx->rdx),
                             (int16_t)ctx->rcx));
}

void shim_sceVoiceVADSetVersion(GuestContext *ctx) {
    RET(sceVoiceVADSetVersion((int32_t)ctx->rdi));
}

// Voice activity detection tuning and the library's thread parameters have no effect without
// voice data; shadPS4 accepts them the same way.
PS4_HLE_STUB(sceVoiceVADAdjustment)
PS4_HLE_STUB(sceVoiceSetThreadsParams)
