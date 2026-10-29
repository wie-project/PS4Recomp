// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PS4 libSceGnmDriver HLE implementation.
 * Provides AMD GCN (Liverpool) PM4 command buffer initialization, hardware state sequences,
 * ring buffer allocation, and GNM runtime driver routines.
 */

#include "ps4_gnmdriver.h"
#include "ps4_videoout.h"
#include "ps4_gnm_metal.h"
#include "ps4_equeue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <errno.h>

#define MAX_GNM_OWNERS    256
#define MAX_GNM_RESOURCES 4096

typedef struct GnmOwner {
    bool in_use;
    void *handle;
    char name[128];
} GnmOwner;

typedef struct GnmResource {
    bool in_use;
    void *res_handle;
    void *owner_handle;
    uint64_t addr;
    size_t size;
    char name[128];
    int res_type;
    uint64_t user_data;
    uint64_t shader_guid;
} GnmResource;

static pthread_mutex_t g_gnm_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_tessellation_ring_addr = 0;
static GnmOwner g_owners[MAX_GNM_OWNERS];
static GnmResource g_resources[MAX_GNM_RESOURCES];
static bool g_gnm_initialized = false;

// Canonical AMD GCN Liverpool hardware state init sequences
static const uint32_t g_initSequence350[] = {
    // IT_CLEAR_STATE preamble
    0xc0001200u, 0u,

    // Hardware init sequence
    0xc0017600u, 0x216u, 0xffffffffu,
    0xc0017600u, 0x217u, 0xffffffffu,
    0xc0017600u, 0x215u, 0u,
    0xc0016900u, 0x2f9u, 0x2du,
    0xc0016900u, 0x282u, 8u,
    0xc0016900u, 0x280u, 0x80008u,
    0xc0016900u, 0x281u, 0xffff0000u,
    0xc0016900u, 0x204u, 0u,
    0xc0016900u, 0x206u, 0x43fu,
    0xc0016900u, 0x83u,  0xffffu,
    0xc0016900u, 0x317u, 0x10u,
    0xc0016900u, 0x2fau, 0x3f800000u,
    0xc0016900u, 0x2fcu, 0x3f800000u,
    0xc0016900u, 0x2fbu, 0x3f800000u,
    0xc0016900u, 0x2fdu, 0x3f800000u,
    0xc0016900u, 0x202u, 0xcc0010u,
    0xc0016900u, 0x30eu, 0xffffffffu,
    0xc0016900u, 0x30fu, 0xffffffffu,
    0xc0002f00u, 1u,
    0xc0017600u, 7u,     0x1701ffu,
    0xc0017600u, 0x46u,  0x1701fdu,
    0xc0017600u, 0x87u,  0x1701ffu,
    0xc0017600u, 0xc7u,  0x1701fdu,
    0xc0017600u, 0x107u, 0x17u,
    0xc0017600u, 0x147u, 0x1701fdu,
    0xc0017600u, 0x47u,  0x1cu,
    0xc0016900u, 0x1b1u, 2u,
    0xc0016900u, 0x101u, 0u,
    0xc0016900u, 0x100u, 0xffffffffu,
    0xc0016900u, 0x103u, 0u,
    0xc0016900u, 0x284u, 0u,
    0xc0016900u, 0x290u, 0u,
    0xc0016900u, 0x2aeu, 0u,
    0xc0016900u, 0x102u, 0u,
    0xc0016900u, 0x292u, 0u,
    0xc0016900u, 0x293u, 0x6020000u,
    0xc0016900u, 0x2f8u, 0u,
    0xc0016900u, 0x2deu, 0x1e9u,
    0xc0036900u, 0x295u, 0x100u, 0x100u, 4u,
    0xc0017900u, 0x200u, 0xe0000000u,
    0xc0016900u, 0x2aau, 0xffu,
};

static const uint32_t g_ctxInitSequence400[] = {
    0xc0012800u, 0x80000000u, 0x80000000u,
    0xc0001200u, 0u,
    0xc0016900u, 0x2f9u, 0x2du,
    0xc0016900u, 0x282u, 8u,
    0xc0016900u, 0x280u, 0x80008u,
    0xc0016900u, 0x281u, 0xffff0000u,
    0xc0016900u, 0x204u, 0u,
    0xc0016900u, 0x206u, 0x43fu,
    0xc0016900u, 0x83u,  0xffffu,
    0xc0016900u, 0x317u, 0x10u,
    0xc0016900u, 0x2fau, 0x3f800000u,
    0xc0016900u, 0x2fcu, 0x3f800000u,
    0xc0016900u, 0x2fbu, 0x3f800000u,
    0xc0016900u, 0x2fdu, 0x3f800000u,
    0xc0016900u, 0x202u, 0xcc0010u,
    0xc0016900u, 0x30eu, 0xffffffffu,
    0xc0016900u, 0x30fu, 0xffffffffu,
    0xc0002f00u, 1u,
    0xc0016900u, 0x1b1u, 2u,
    0xc0016900u, 0x101u, 0u,
    0xc0016900u, 0x100u, 0xffffffffu,
    0xc0016900u, 0x103u, 0u,
    0xc0016900u, 0x284u, 0u,
    0xc0016900u, 0x290u, 0u,
    0xc0016900u, 0x2aeu, 0u,
    0xc0016900u, 0x102u, 0u,
    0xc0016900u, 0x292u, 0u,
    0xc0016900u, 0x293u, 0x6020000u,
    0xc0016900u, 0x2f8u, 0u,
    0xc0016900u, 0x2deu, 0x1e9u,
    0xc0036900u, 0x295u, 0x100u, 0x100u, 4u,
    0xc0016900u, 0x2aau, 0xffu,
    0xc09e1000u, 0u,
};

static inline void *recomp_guest_to_host(const GuestContext *ctx, uint64_t gaddr) {
    if (!ctx || !ctx->mem_base || gaddr == 0) return NULL;
    if (gaddr >= (uintptr_t)ctx->mem_base && gaddr < (uintptr_t)ctx->mem_base + ctx->mem_size) {
        return (void *)gaddr;
    }
    if (gaddr < ctx->mem_size) {
        return (void *)(ctx->mem_base + gaddr);
    }
    return NULL;
}

static inline uint32_t *write_trailing_nop(uint32_t *cmdbuf, uint32_t data_block_size) {
    if (data_block_size == 0) return cmdbuf;
    *cmdbuf = PM4_TYPE3_HEADER(PM4_IT_NOP, data_block_size - 1, 0, 0);
    memset(cmdbuf + 1, 0, data_block_size * sizeof(uint32_t));
    return cmdbuf + data_block_size + 1;
}

static inline uint32_t *clear_context_state(uint32_t *cmdbuf) {
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_CLEAR_STATE, 0, 0, 0);
    cmdbuf[1] = 0;
    return cmdbuf + 2;
}

static void gnm_remember_ctx(GuestContext *ctx);

void ps4_gnmdriver_init(GuestContext *ctx) {
    gnm_remember_ctx(ctx);
    pthread_mutex_lock(&g_gnm_mutex);
    if (!g_gnm_initialized) {
        memset(g_owners, 0, sizeof(g_owners));
        memset(g_resources, 0, sizeof(g_resources));
        g_gnm_initialized = true;
    }

    if (g_tessellation_ring_addr == 0 && ctx) {
        GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
        g_tessellation_ring_addr = recomp_vm_alloc_named_aligned(
            proc, GNM_TESSELLATION_RING_SIZE, 0x10000, PROT_READ | PROT_WRITE, 0, "gnm_tess_ring"
        );
        printf("[ps4-gnm] Allocated tessellation factor ring buffer at guest VAddr 0x%llx (size: %u MB)\n",
               (unsigned long long)g_tessellation_ring_addr, GNM_TESSELLATION_RING_SIZE / (1024 * 1024));
    }
    pthread_mutex_unlock(&g_gnm_mutex);
}

void ps4_gnmdriver_destroy(void) {
    ps4_gnm_metal_destroy();
    pthread_mutex_lock(&g_gnm_mutex);
    memset(g_owners, 0, sizeof(g_owners));
    memset(g_resources, 0, sizeof(g_resources));
    g_tessellation_ring_addr = 0;
    g_gnm_initialized = false;
    pthread_mutex_unlock(&g_gnm_mutex);
}

uint64_t sceGnmGetTheTessellationFactorRingBufferBaseAddress(void) {
    pthread_mutex_lock(&g_gnm_mutex);
    uint64_t addr = g_tessellation_ring_addr;
    pthread_mutex_unlock(&g_gnm_mutex);
    return addr;
}

int32_t sceGnmGetOffChipTessellationBufferSize(void) {
    return (int32_t)GNM_TESSELLATION_RING_SIZE;
}

uint32_t sceGnmGetGpuCoreClockFrequency(void) {
    return 800u; // Liverpool GPU base clock: 800 MHz
}

int32_t sceGnmAreSubmitsAllowed(void) {
    return 1;
}

int32_t sceGnmGetNumTcaUnits(void) {
    return 2;
}

int32_t sceGnmLogicalTcaUnitToPhysical(uint32_t tca) {
    return (int32_t)tca;
}

int32_t sceGnmLogicalCuIndexToPhysicalCuIndex(uint32_t cu) {
    return (int32_t)cu;
}

int32_t sceGnmLogicalCuMaskToPhysicalCuMask(int64_t unk, int32_t logical_cu_mask) {
    (void)unk;
    return logical_cu_mask;
}

void sceGnmFlushGarlic(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

int32_t sceGnmGetShaderStatus(void) {
    return ORBIS_GNM_OK;
}

uint32_t sceGnmGetProtectionFaultTimeStamp(void) {
    return 0;
}

int32_t sceGnmGetEqTimeStamp(void) {
    return 0;
}

int32_t sceGnmGetGpuBlockStatus(void) {
    return 0;
}

int32_t sceGnmGetGpuInfoStatus(void) {
    return 0;
}

int32_t sceGnmGetPhysicalCounterFromVirtualized(void) {
    return 0;
}

// Hardware state initialization implementations
uint32_t sceGnmDrawInitDefaultHardwareState(uint32_t *cmdbuf, uint32_t size) {
    return sceGnmDrawInitDefaultHardwareState350(cmdbuf, size);
}

uint32_t sceGnmDrawInitDefaultHardwareState175(uint32_t *cmdbuf, uint32_t size) {
    return sceGnmDrawInitDefaultHardwareState350(cmdbuf, size);
}

uint32_t sceGnmDrawInitDefaultHardwareState200(uint32_t *cmdbuf, uint32_t size) {
    return sceGnmDrawInitDefaultHardwareState350(cmdbuf, size);
}

uint32_t sceGnmDrawInitDefaultHardwareState350(uint32_t *cmdbuf, uint32_t size) {
    if (!cmdbuf || size < PM4_HW_INIT_PACKET_SIZE) return 0;
    uint32_t *cur = cmdbuf;
    uint32_t *cmdbuf_end = cmdbuf + PM4_HW_INIT_PACKET_SIZE;

    cur = clear_context_state(cur);
    size_t seq_words = sizeof(g_initSequence350) / sizeof(g_initSequence350[0]);
    // Skip the 2 preamble words of g_initSequence350 since ClearContextState was just written
    if (seq_words > 2) {
        memcpy(cur, &g_initSequence350[2], (seq_words - 2) * sizeof(uint32_t));
        cur += (seq_words - 2);
    }

    if (cur < cmdbuf_end - 1) {
        uint32_t left = (uint32_t)(cmdbuf_end - cur - 1);
        write_trailing_nop(cur, left);
    }
    return PM4_HW_INIT_PACKET_SIZE;
}

uint32_t sceGnmDrawInitToDefaultContextState(uint32_t *cmdbuf, uint32_t size) {
    return sceGnmDrawInitToDefaultContextState400(cmdbuf, size);
}

uint32_t sceGnmDrawInitToDefaultContextState400(uint32_t *cmdbuf, uint32_t size) {
    if (!cmdbuf || size != PM4_CTX_INIT_PACKET_SIZE_400) return 0;
    size_t seq_words = sizeof(g_ctxInitSequence400) / sizeof(g_ctxInitSequence400[0]);
    memcpy(cmdbuf, g_ctxInitSequence400, seq_words * sizeof(uint32_t));
    if (size > seq_words) {
        memset(cmdbuf + seq_words, 0, (size - seq_words) * sizeof(uint32_t));
    }
    return PM4_CTX_INIT_PACKET_SIZE_400;
}

uint32_t sceGnmDispatchInitDefaultHardwareState(uint32_t *cmdbuf, uint32_t size) {
    if (!cmdbuf || size < 4) return 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, size - 2, 1, 0);
    memset(cmdbuf + 1, 0, (size - 1) * sizeof(uint32_t));
    return size;
}

int32_t sceGnmResetVgtControl(uint32_t *cmdbuf, uint32_t size) {
    if (!cmdbuf || size < 3) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    cmdbuf[1] = 0x2deu; // VGT_CONTROL
    cmdbuf[2] = 0x1e9u;
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetVgtControl(uint32_t *cmdbuf, uint32_t size, uint32_t vgt_control) {
    if (!cmdbuf || size < 3) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    cmdbuf[1] = 0x2deu; // VGT_CONTROL
    cmdbuf[2] = vgt_control;
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetGsRingSizes(uint32_t *cmdbuf, uint32_t size, uint32_t ring_sizes) {
    if (!cmdbuf || size < 3) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    cmdbuf[1] = 0x284u; // VGT_GS_VERTEX_REUSE
    cmdbuf[2] = ring_sizes;
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetWaveLimitMultipliers(uint32_t *cmdbuf, uint32_t size, uint32_t mult) {
    if (!cmdbuf || size < 3) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 1, 0, 0);
    cmdbuf[1] = 0x215u; // SPI_WAVE_LIMIT_MULT
    cmdbuf[2] = mult;
    return ORBIS_GNM_OK;
}

// Markers
int32_t sceGnmInsertDingDongMarker(uint32_t *cmdbuf, uint32_t size) {
    if (!cmdbuf || size != 4) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, 2, 0, 0);
    cmdbuf[1] = 0;
    cmdbuf[2] = 0;
    cmdbuf[3] = 0;
    return ORBIS_GNM_OK;
}

int32_t sceGnmInsertPopMarker(uint32_t *cmdbuf, uint32_t size) {
    if (!cmdbuf || size != 6) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, 4, 0, 0);
    cmdbuf[1] = 2; // DebugMarkerPop
    cmdbuf[2] = 0;
    cmdbuf[3] = 0;
    cmdbuf[4] = 0;
    cmdbuf[5] = 0;
    return ORBIS_GNM_OK;
}

int32_t sceGnmInsertPushMarker(uint32_t *cmdbuf, uint32_t size, const char *marker) {
    if (!cmdbuf || !marker) return -1;
    size_t len = strlen(marker);
    uint32_t packet_size = (uint32_t)(((len + 8) >> 2) + ((len + 0xc) >> 3) * 2);
    if (packet_size + 2 != size) return -1;

    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, packet_size, 0, 0);
    cmdbuf[1] = 1; // DebugMarkerPush
    char *payload = (char *)(&cmdbuf[2]);
    memcpy(payload, marker, len + 1);
    memset(payload + len + 1, 0, (packet_size * 4) - (len + 1));
    return ORBIS_GNM_OK;
}

int32_t sceGnmInsertPushColorMarker(uint32_t *cmdbuf, uint32_t size, const char *marker, uint32_t color) {
    if (!cmdbuf || !marker) return -1;
    size_t len = strlen(marker);
    uint32_t packet_size = (uint32_t)(((len + 0xc) >> 2) + ((len + 0x10) >> 3) * 2);
    if (packet_size + 2 != size) return -1;

    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, packet_size, 0, 0);
    cmdbuf[1] = 3; // DebugColorMarkerPush
    char *payload = (char *)(&cmdbuf[2]);
    memcpy(payload, marker, len + 1);
    uint32_t *color_ptr = (uint32_t *)(payload + len + 1 + 8);
    *color_ptr = color;
    return ORBIS_GNM_OK;
}

int32_t sceGnmInsertSetMarker(uint32_t *cmdbuf, uint32_t size, const char *marker) {
    if (!cmdbuf || !marker) return -1;
    size_t len = strlen(marker);
    uint32_t packet_size = (uint32_t)(((len + 8) >> 2) + ((len + 0xc) >> 3) * 2);
    if (packet_size + 2 != size) return -1;

    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, packet_size, 0, 0);
    cmdbuf[1] = 4; // DebugSetMarker
    char *payload = (char *)(&cmdbuf[2]);
    memcpy(payload, marker, len + 1);
    memset(payload + len + 1, 0, (packet_size * 4) - (len + 1));
    return ORBIS_GNM_OK;
}

int32_t sceGnmInsertSetColorMarker(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmInsertThreadTraceMarker(void) {
    return ORBIS_GNM_OK;
}

// Waits (on the command processor) for the VideoOut flip label of display buffer buf_idx to be 0
// (shadPS4 gnmdriver.cpp). The GPU flip of a buffer sets its label to 1; the label is cleared when
// the flip after it completes, i.e. once another buffer has been flipped to (see ps4_videoout.c).
int32_t sceGnmInsertWaitFlipDone(GuestContext *ctx, uint32_t *cmdbuf, uint32_t size, int32_t vo_handle,
                                 uint32_t buf_idx) {
    // shadPS4 returns -1 unless size is exactly 7 and asserts on a bad handle. The packet is 7
    // dwords whatever the space, so a larger size is accepted (only the first 7 are written); a
    // bad handle or buffer index returns -1 like a bad size instead of stopping the process.
    if (!cmdbuf || size < 7 || buf_idx >= PS4_VIDEO_OUT_MAX_BUFFERS) return -1;
    uint64_t labels = 0;
    if (sceVideoOutGetBufferLabelAddress(ctx, vo_handle, &labels) < 0) return -1;
    const uint64_t label = labels + (uint64_t)buf_idx * sizeof(uint64_t);
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_WAIT_REG_MEM, 5, 0, 0);
    cmdbuf[1] = 0x13; // ENGINE_ME | MEM_SPACE_MEMORY | FUNCTION_EQUAL
    cmdbuf[2] = (uint32_t)label & ~3u;
    cmdbuf[3] = (uint32_t)(label >> 32);
    cmdbuf[4] = 0;          // reference
    cmdbuf[5] = 0xFFFFFFFF; // mask
    cmdbuf[6] = 10;         // poll interval
    return ORBIS_GNM_OK;
}

// Resource registration & ownership
int32_t sceGnmRegisterOwner(void *handle, const char *name) {
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_OWNERS; i++) {
        if (!g_owners[i].in_use) {
            g_owners[i].in_use = true;
            g_owners[i].handle = handle;
            if (name) {
                strncpy(g_owners[i].name, name, sizeof(g_owners[i].name) - 1);
            }
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmRegisterResource(void *res_handle, void *owner_handle, const void *addr, size_t size, const char *name, int res_type, uint64_t user_data) {
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (!g_resources[i].in_use) {
            g_resources[i].in_use = true;
            g_resources[i].res_handle = res_handle;
            g_resources[i].owner_handle = owner_handle;
            g_resources[i].addr = (uint64_t)(uintptr_t)addr;
            g_resources[i].size = size;
            g_resources[i].res_type = res_type;
            g_resources[i].user_data = user_data;
            if (name) {
                strncpy(g_resources[i].name, name, sizeof(g_resources[i].name) - 1);
            }
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmUnregisterResource(void *res_handle) {
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            g_resources[i].in_use = false;
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmUnregisterOwnerAndResources(void *owner_handle) {
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].owner_handle == owner_handle) {
            g_resources[i].in_use = false;
        }
    }
    for (int i = 0; i < MAX_GNM_OWNERS; i++) {
        if (g_owners[i].in_use && g_owners[i].handle == owner_handle) {
            g_owners[i].in_use = false;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_OK;
}

int32_t sceGnmUnregisterAllResourcesForOwner(void *owner_handle) {
    return sceGnmUnregisterOwnerAndResources(owner_handle);
}

int32_t sceGnmSetResourceUserData(void *res_handle, uint64_t user_data) {
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            g_resources[i].user_data = user_data;
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmGetResourceUserData(void *res_handle, uint64_t *user_data) {
    if (!user_data) return ORBIS_GNM_ERROR_INVALID_ARGS;
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            *user_data = g_resources[i].user_data;
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmGetResourceBaseAddressAndSizeInBytes(void *res_handle, uint64_t *base_addr, uint64_t *size) {
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            if (base_addr) *base_addr = g_resources[i].addr;
            if (size) *size = g_resources[i].size;
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmGetResourceName(void *res_handle, char *name, size_t max_len) {
    if (!name || max_len == 0) return ORBIS_GNM_ERROR_INVALID_ARGS;
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            strncpy(name, g_resources[i].name, max_len - 1);
            name[max_len - 1] = '\0';
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmGetResourceType(void *res_handle, int *res_type) {
    if (!res_type) return ORBIS_GNM_ERROR_INVALID_ARGS;
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            *res_type = g_resources[i].res_type;
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmGetOwnerName(void *owner_handle, char *name, size_t max_len) {
    if (!name || max_len == 0) return ORBIS_GNM_ERROR_INVALID_ARGS;
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_OWNERS; i++) {
        if (g_owners[i].in_use && g_owners[i].handle == owner_handle) {
            strncpy(name, g_owners[i].name, max_len - 1);
            name[max_len - 1] = '\0';
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmGetResourceShaderGuid(void *res_handle, uint64_t *guid) {
    if (!guid) return ORBIS_GNM_ERROR_INVALID_ARGS;
    pthread_mutex_lock(&g_gnm_mutex);
    for (int i = 0; i < MAX_GNM_RESOURCES; i++) {
        if (g_resources[i].in_use && g_resources[i].res_handle == res_handle) {
            *guid = g_resources[i].shader_guid;
            pthread_mutex_unlock(&g_gnm_mutex);
            return ORBIS_GNM_OK;
        }
    }
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmFindResourcesPublic(void) {
    return ORBIS_GNM_ERROR_FAILURE;
}

int32_t sceGnmQueryResourceRegistrationUserMemoryRequirements(size_t *required_size) {
    if (required_size) *required_size = 0x10000;
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetResourceRegistrationUserMemory(void *addr, size_t size) {
    (void)addr;
    (void)size;
    return ORBIS_GNM_OK;
}

int32_t sceGnmRegisterGdsResource(void) {
    return ORBIS_GNM_OK;
}

// Workloads & DingDong
int32_t sceGnmBeginWorkload(uint32_t workload_stream, uint64_t *workload) {
    (void)workload_stream;
    if (workload) *workload = 1;
    return ORBIS_GNM_OK;
}

int32_t sceGnmEndWorkload(uint64_t workload) {
    return (workload != 0) ? 0 : 2;
}

int32_t sceGnmCreateWorkloadStream(uint64_t param1, uint32_t *workload_stream) {
    (void)param1;
    if (workload_stream) *workload_stream = 1;
    return ORBIS_GNM_OK;
}

int32_t sceGnmDestroyWorkloadStream(void) {
    return ORBIS_GNM_OK;
}

// ---------------------------------------------------------------------------
// Compute (ASC/MEC) queues. sceGnmMapComputeQueue returns a 1-based virtual queue id
// (vqid); sceGnmDingDong(vqid, next_offs_dw) tells the CP that the ring now holds packets
// up to next_offs_dw. Packets between the previous and the new offset are processed
// synchronously (labels / RELEASE_MEM interrupts), mirroring shadPS4's asc_queues logic.
// ---------------------------------------------------------------------------
#define GNM_MAX_COMPUTE_QUEUES (PS4_GNM_NUM_COMPUTE_PIPES * PS4_GNM_NUM_QUEUES_PER_PIPE)

typedef struct GnmComputeQueue {
    bool in_use;
    uint32_t pipe_id;
    uint32_t queue_id;
    uint64_t ring_base_addr;   // guest address
    uint32_t ring_size_dw;
    uint32_t *read_ptr;        // host pointer into guest memory (may be NULL)
    uint32_t next_offs_dw;     // offset (dw) up to which packets were consumed
} GnmComputeQueue;

static pthread_mutex_t g_gnm_compute_mutex = PTHREAD_MUTEX_INITIALIZER;
static GnmComputeQueue g_compute_queues[GNM_MAX_COMPUTE_QUEUES];
static GuestContext *g_gnm_proc_ctx = NULL; // process-wide context (mem_base/mem_size)

static void gnm_remember_ctx(GuestContext *ctx) {
    if (!ctx) return;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    __atomic_store_n(&g_gnm_proc_ctx, proc, __ATOMIC_RELEASE);
}

// Returns nonzero when the queue stopped at a WAIT_REG_MEM; the renderer then advances the
// read pointer itself as the packets complete.
static int gnm_process_ring_span(GuestContext *ctx, GnmComputeQueue *q, uint32_t from_dw, uint32_t size_dw) {
    if (!ctx || !ctx->mem_base || size_dw == 0) return 0;
    uint64_t addr = q->ring_base_addr + (uint64_t)from_dw * 4u;
    if (addr == 0 || addr >= ctx->mem_size || (uint64_t)size_dw * 4u > ctx->mem_size - addr) return 0;
    const uint32_t *words = (const uint32_t *)(ctx->mem_base + addr);
    return ps4_gnm_metal_process_compute_packets(ctx, words, size_dw, q->pipe_id, (uint32_t)(q - g_compute_queues), q->read_ptr, q->ring_size_dw, from_dw);
}

void ps4_gnm_dingdong(GuestContext *ctx, uint32_t gnm_vqid, uint32_t next_offs_dw) {
    if (gnm_vqid == 0 || gnm_vqid > GNM_MAX_COMPUTE_QUEUES) return;
    if (ctx) {
        gnm_remember_ctx(ctx);
    } else {
        ctx = __atomic_load_n(&g_gnm_proc_ctx, __ATOMIC_ACQUIRE);
    }
    if (!ctx) return;

    static bool s_logged_first_dingdong = false;

    pthread_mutex_lock(&g_gnm_compute_mutex);
    GnmComputeQueue *q = &g_compute_queues[gnm_vqid - 1];
    if (!q->in_use || q->ring_size_dw == 0) {
        pthread_mutex_unlock(&g_gnm_compute_mutex);
        return;
    }
    if (next_offs_dw > q->ring_size_dw) next_offs_dw %= q->ring_size_dw;

    uint32_t offs = q->next_offs_dw;
    int waiting = 0;
    if (next_offs_dw != offs) {
        if (!s_logged_first_dingdong) {
            s_logged_first_dingdong = true;
            fprintf(stderr, "[ps4-gnm] first DingDong: vqid=%u pipe=%u queue=%u offs %u -> %u (ring %u dw)\n",
                    gnm_vqid, q->pipe_id, q->queue_id, offs, next_offs_dw, q->ring_size_dw);
        }
        if (next_offs_dw < offs && next_offs_dw != 0) {
            // Submission wrapped around the end of the ring: process the tail first.
            waiting |= gnm_process_ring_span(ctx, q, offs, q->ring_size_dw - offs);
            offs = 0;
        }
        uint32_t end = next_offs_dw ? next_offs_dw : q->ring_size_dw;
        if (end > offs) {
            waiting |= gnm_process_ring_span(ctx, q, offs, end - offs);
        }
        q->next_offs_dw = next_offs_dw;
        if (q->read_ptr && !waiting) {
            __atomic_store_n(q->read_ptr, next_offs_dw % q->ring_size_dw, __ATOMIC_SEQ_CST);
        }
    }
    pthread_mutex_unlock(&g_gnm_compute_mutex);
}

void sceGnmDingDong(uint32_t gnm_vqid, uint32_t next_offs_dw) {
    ps4_gnm_dingdong(NULL, gnm_vqid, next_offs_dw);
}

void sceGnmDingDongForWorkload(uint32_t gnm_vqid, uint32_t next_offs_dw, uint64_t workload_id) {
    (void)workload_id;
    ps4_gnm_dingdong(NULL, gnm_vqid, next_offs_dw);
}

// Compute queues & events
int32_t sceGnmMapComputeQueue(uint32_t pipe_id, uint32_t queue_id, uint64_t ring_base_addr, uint32_t ring_size_dw, uint32_t *read_ptr_addr) {
    if (pipe_id >= PS4_GNM_NUM_COMPUTE_PIPES) return ORBIS_GNM_ERROR_COMPUTEQUEUE_INVALID_PIPE_ID;
    if (queue_id >= PS4_GNM_NUM_QUEUES_PER_PIPE) return ORBIS_GNM_ERROR_COMPUTEQUEUE_INVALID_QUEUE_ID;
    if ((ring_base_addr % 256) != 0) return ORBIS_GNM_ERROR_COMPUTEQUEUE_INVALID_RING_BASE_ADDR;
    if (ring_size_dw == 0 || (ring_size_dw & (ring_size_dw - 1)) != 0) return ORBIS_GNM_ERROR_COMPUTEQUEUE_INVALID_RING_SIZE;
    if (((uintptr_t)read_ptr_addr % 4) != 0) return ORBIS_GNM_ERROR_COMPUTEQUEUE_INVALID_READ_PTR_ADDR;

    pthread_mutex_lock(&g_gnm_compute_mutex);
    int slot = -1;
    for (int i = 0; i < (int)GNM_MAX_COMPUTE_QUEUES; i++) {
        if (g_compute_queues[i].in_use && g_compute_queues[i].pipe_id == pipe_id && g_compute_queues[i].queue_id == queue_id) {
            slot = i; // remap of the same hardware queue
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < (int)GNM_MAX_COMPUTE_QUEUES; i++) {
            if (!g_compute_queues[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_gnm_compute_mutex);
        return ORBIS_GNM_ERROR_COMPUTEQUEUE_INTERNAL;
    }
    GnmComputeQueue *q = &g_compute_queues[slot];
    q->in_use = true;
    q->pipe_id = pipe_id;
    q->queue_id = queue_id;
    q->ring_base_addr = ring_base_addr;
    q->ring_size_dw = ring_size_dw;
    q->read_ptr = read_ptr_addr;
    q->next_offs_dw = 0;
    // drop waiting submissions of the slot's previous mapping before the new one is published
    // (lock order compute -> CP state, as in DingDong)
    ps4_gnm_metal_reset_compute_queue((uint32_t)slot);
    if (read_ptr_addr) *read_ptr_addr = 0;
    pthread_mutex_unlock(&g_gnm_compute_mutex);

    int32_t vqid = slot + 1; // DingDong expects [1..64]
    printf("[ps4-gnm] ASC pipe %u queue %u mapped to vqid %d (ring 0x%llx, %u dw)\n",
           pipe_id, queue_id, vqid, (unsigned long long)ring_base_addr, ring_size_dw);
    return vqid;
}

int32_t sceGnmMapComputeQueueWithPriority(uint32_t pipe_id, uint32_t queue_id, uint64_t ring_base_addr, uint32_t ring_size_dw, uint32_t *read_ptr_addr, uint32_t pipePriority) {
    (void)pipePriority;
    return sceGnmMapComputeQueue(pipe_id, queue_id, ring_base_addr, ring_size_dw, read_ptr_addr);
}

int32_t sceGnmUnmapComputeQueue(uint32_t pipe_id, uint32_t queue_id) {
    pthread_mutex_lock(&g_gnm_compute_mutex);
    for (int i = 0; i < (int)GNM_MAX_COMPUTE_QUEUES; i++) {
        if (g_compute_queues[i].in_use && g_compute_queues[i].pipe_id == pipe_id && g_compute_queues[i].queue_id == queue_id) {
            ps4_gnm_metal_reset_compute_queue((uint32_t)i);
            memset(&g_compute_queues[i], 0, sizeof(g_compute_queues[i]));
        }
    }
    pthread_mutex_unlock(&g_gnm_compute_mutex);
    return ORBIS_GNM_OK;
}

int32_t sceGnmComputeWaitSemaphore(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmComputeWaitOnAddress(uint32_t *cmdbuf, uint32_t size, uint64_t addr, uint32_t mask, uint32_t cmp_func, uint32_t ref) {
    (void)addr;
    (void)mask;
    (void)cmp_func;
    (void)ref;
    if (!cmdbuf || size < 7) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_WAIT_REG_MEM, 5, 1, 0);
    memset(cmdbuf + 1, 0, 6 * sizeof(uint32_t));
    return ORBIS_GNM_OK;
}

int32_t sceGnmGetLastWaitedAddress(void) {
    return 0;
}

// ---------------------------------------------------------------------------
// GPU interrupt -> equeue plumbing (EVFILT_GRAPHICS_CORE).
// sceGnmAddEqEvent(eq, id, udata) registers interest in interrupt `id`
// (0x40 = GfxEop, 0..6 = Compute0..6 RelMem). When the PM4 processor raises that interrupt
// (EVENT_WRITE_EOP / RELEASE_MEM with int_sel != 0), an event with ident = id,
// filter = EVFILT_GRAPHICS_CORE, data = id (the event type, see sceGnmGetEqEventType)
// and the registered udata is triggered on every registered queue.
// ---------------------------------------------------------------------------
#define GNM_MAX_EQ_REGS 64

typedef struct GnmEqReg {
    bool in_use;
    OrbisKernelEqueue eq;
    uint64_t id;
    uint64_t udata; // opaque guest value, copied back verbatim into the guest event
} GnmEqReg;

static pthread_mutex_t g_gnm_eq_mutex = PTHREAD_MUTEX_INITIALIZER;
static GnmEqReg g_eq_regs[GNM_MAX_EQ_REGS];

int32_t sceGnmAddEqEvent(uint64_t eq, uint64_t id, void *udata) {
    OrbisKernelEqueue q = (OrbisKernelEqueue)(int32_t)eq;
    if (!ps4_equeue_exists(q)) {
        return (int32_t)ORBIS_KERNEL_ERROR_EBADF;
    }

    pthread_mutex_lock(&g_gnm_eq_mutex);
    int slot = -1;
    for (int i = 0; i < GNM_MAX_EQ_REGS; i++) {
        if (g_eq_regs[i].in_use && g_eq_regs[i].eq == q && g_eq_regs[i].id == id) {
            slot = i; // re-add updates udata (kqueue EV_ADD semantics)
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < GNM_MAX_EQ_REGS; i++) {
            if (!g_eq_regs[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_gnm_eq_mutex);
        fprintf(stderr, "[ps4-gnm] sceGnmAddEqEvent: registration table full\n");
        return (int32_t)0x8002000c; // ORBIS_KERNEL_ERROR_ENOMEM
    }
    g_eq_regs[slot].in_use = true;
    g_eq_regs[slot].eq = q;
    g_eq_regs[slot].id = id;
    g_eq_regs[slot].udata = (uint64_t)(uintptr_t)udata;
    pthread_mutex_unlock(&g_gnm_eq_mutex);

    printf("[ps4-gnm] sceGnmAddEqEvent(eq=%d, id=0x%llx, udata=0x%llx)\n",
           q, (unsigned long long)id, (unsigned long long)(uintptr_t)udata);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDeleteEqEvent(uint64_t eq, uint64_t id) {
    OrbisKernelEqueue q = (OrbisKernelEqueue)(int32_t)eq;
    if (!ps4_equeue_exists(q)) {
        return (int32_t)ORBIS_KERNEL_ERROR_EBADF;
    }
    pthread_mutex_lock(&g_gnm_eq_mutex);
    for (int i = 0; i < GNM_MAX_EQ_REGS; i++) {
        if (g_eq_regs[i].in_use && g_eq_regs[i].eq == q && g_eq_regs[i].id == id) {
            memset(&g_eq_regs[i], 0, sizeof(g_eq_regs[i]));
        }
    }
    pthread_mutex_unlock(&g_gnm_eq_mutex);
    ps4_equeue_remove_events(q, id, ORBIS_KERNEL_EVFILT_GRAPHICS_CORE);
    return ORBIS_GNM_OK;
}

int32_t sceGnmGetEqEventType(const void *ev) {
    // shadPS4: returns sceKernelGetEventData(ev); the trigger stores the event type there.
    if (!ev) return 0;
    return (int32_t)((const OrbisKernelEvent *)ev)->data;
}

void ps4_gnm_signal_interrupt(uint32_t irq_id) {
    GnmEqReg hits[GNM_MAX_EQ_REGS];
    int n = 0;
    pthread_mutex_lock(&g_gnm_eq_mutex);
    for (int i = 0; i < GNM_MAX_EQ_REGS; i++) {
        if (g_eq_regs[i].in_use && g_eq_regs[i].id == (uint64_t)irq_id) {
            hits[n++] = g_eq_regs[i];
        }
    }
    pthread_mutex_unlock(&g_gnm_eq_mutex);

    for (int i = 0; i < n; i++) {
        ps4_equeue_trigger_event(hits[i].eq, hits[i].id, ORBIS_KERNEL_EVFILT_GRAPHICS_CORE,
                                 (int64_t)hits[i].id, (void *)(uintptr_t)hits[i].udata);
    }
}

// Diagnostics & validation
int32_t sceGnmValidateGetVersion(void) {
    return 0;
}

bool sceGnmValidateOnSubmitEnabled(void) {
    return false;
}

int32_t sceGnmValidateGetDiagnosticInfo(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateGetDiagnostics(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateResetState(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateDisableDiagnostics(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateDisableDiagnostics2(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateCommandBuffers(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateDrawCommandBuffers(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidateDispatchCommandBuffers(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceGnmValidationRegisterMemoryCheckCallback(void) {
    return ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED;
}

int32_t sceRazorIsLoaded(void) {
    return 0;
}

int32_t sceRazorCaptureImmediate(void) {
    return ORBIS_GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED;
}

int32_t sceRazorCpuJobManagerSequence(void) {
    return 0;
}

int32_t sceRazorCpuJobManagerJob(void) {
    return 0;
}

int32_t sceRazorCpuJobManagerDispatch(void) {
    return 0;
}

bool sceGnmDriverTraceInProgress(void) {
    return false;
}

bool sceGnmDriverCaptureInProgress(void) {
    return false;
}

int32_t sceGnmDriverTriggerCapture(void) {
    return ORBIS_GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED;
}

bool sceGnmIsUserPaEnabled(void) {
    return false;
}

int32_t sceGnmDebugHardwareStatus(void) {
    return 0;
}

int32_t sceGnmSetupMipStatsReport(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmDisableMipStatsReport(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmRequestMipStatsReportAndReset(void) {
    return ORBIS_GNM_OK;
}

// Command Buffer Processing & Submission
static uint64_t g_submitted_frames = 0;

static int32_t ps4_gnm_process_command_buffers(GuestContext *ctx, uint32_t count,
                                               const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                               const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes) {
    if (!ctx || count == 0) return ORBIS_GNM_OK;
    return ps4_gnm_metal_process_command_buffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes);
}

int32_t sceGnmSubmitCommandBuffers(GuestContext *ctx, uint32_t count,
                                   const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                   const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes) {
    if (count != 0) {
        if (!dcb_gpu_addrs || !dcb_sizes_in_bytes) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
        if (ccb_sizes_in_bytes && !ccb_gpu_addrs) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        if (dcb_sizes_in_bytes[i] == 0 || dcb_sizes_in_bytes[i] > 0x3ffffc) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
        if (ccb_sizes_in_bytes && ccb_sizes_in_bytes[i] > 0x3ffffc) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
    }

    pthread_mutex_lock(&g_gnm_mutex);
    ps4_gnm_process_command_buffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes);
    pthread_mutex_unlock(&g_gnm_mutex);

    return ORBIS_GNM_OK;
}

int32_t sceGnmSubmitCommandBuffersForWorkload(GuestContext *ctx, uint32_t workload, uint32_t count,
                                              const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                              const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes) {
    (void)workload;
    return sceGnmSubmitCommandBuffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes);
}

int32_t sceGnmSubmitAndFlipCommandBuffers(GuestContext *ctx, uint32_t count,
                                          const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                          const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes,
                                          uint32_t vo_handle, uint32_t buf_idx, uint32_t flip_mode, int64_t flip_arg) {
    if (count != 0) {
        if (!dcb_gpu_addrs || !dcb_sizes_in_bytes) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
        if (ccb_sizes_in_bytes && !ccb_gpu_addrs) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        if (dcb_sizes_in_bytes[i] == 0 || dcb_sizes_in_bytes[i] > 0x3ffffc) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
        if (ccb_sizes_in_bytes && ccb_sizes_in_bytes[i] > 0x3ffffc) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
    }

    pthread_mutex_lock(&g_gnm_mutex);
    ps4_gnm_process_command_buffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes);
    g_submitted_frames++;
    pthread_mutex_unlock(&g_gnm_mutex);

    // Present frame & signal flip event via libSceVideoOut
    int32_t flip_res = ps4_videoout_submit_flip(ctx, (int32_t)vo_handle, (int32_t)buf_idx, flip_mode, flip_arg, 1);
    if (flip_res != 0) {
        return (flip_res == -ENOSPC) ? (int32_t)0x80d11081 : flip_res;
    }

    return ORBIS_GNM_OK;
}

int32_t sceGnmSubmitAndFlipCommandBuffersForWorkload(GuestContext *ctx, uint32_t workload, uint32_t count,
                                                     const uint64_t *dcb_gpu_addrs, const uint32_t *dcb_sizes_in_bytes,
                                                     const uint64_t *ccb_gpu_addrs, const uint32_t *ccb_sizes_in_bytes,
                                                     uint32_t vo_handle, uint32_t buf_idx, uint32_t flip_mode, int64_t flip_arg) {
    (void)workload;
    return sceGnmSubmitAndFlipCommandBuffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes, vo_handle, buf_idx, flip_mode, flip_arg);
}

int32_t sceGnmSubmitDone(void) {
    pthread_mutex_lock(&g_gnm_mutex);
    g_submitted_frames++;
    pthread_mutex_unlock(&g_gnm_mutex);
    return ORBIS_GNM_OK;
}

int32_t sceGnmRequestFlipAndSubmitDone(GuestContext *ctx, uint64_t gpu_addr, uint32_t gpu_addr_size_in_bytes, uint32_t vo_handle, uint32_t buf_idx, uint32_t flip_mode, int64_t flip_arg) {
    return sceGnmRequestFlipAndSubmitDoneForWorkload(ctx, gpu_addr, gpu_addr, gpu_addr_size_in_bytes, vo_handle, buf_idx, flip_mode, flip_arg);
}

int32_t sceGnmRequestFlipAndSubmitDoneForWorkload(GuestContext *ctx, uint64_t workload, uint64_t gpu_addr, uint32_t gpu_addr_size_in_bytes, uint32_t vo_handle, uint32_t buf_idx, uint32_t flip_mode, int64_t flip_arg) {
    (void)workload;

    if (gpu_addr != 0 && gpu_addr_size_in_bytes > 0) {
        // Same limit as sceGnmSubmitCommandBuffers / SubmitAndFlipCommandBuffers.
        if (gpu_addr_size_in_bytes > 0x3ffffc) {
            return ORBIS_GNM_ERROR_INVALID_VALUE;
        }
        // ps4_gnmdriver_init takes g_gnm_mutex itself (and is idempotent), so it runs before
        // the lock is taken here.
        ps4_gnmdriver_init(ctx);
        pthread_mutex_lock(&g_gnm_mutex);
        ps4_gnm_process_command_buffers(ctx, 1, &gpu_addr, &gpu_addr_size_in_bytes, NULL, NULL);
        pthread_mutex_unlock(&g_gnm_mutex);
    }

    sceGnmSubmitDone();

    int32_t flip_res = ps4_videoout_submit_flip(ctx, (int32_t)vo_handle, (int32_t)buf_idx, flip_mode, flip_arg, 1);
    if (flip_res != 0) {
        return (flip_res == -ENOSPC) ? (int32_t)0x80d11081 : flip_res;
    }
    return ORBIS_GNM_OK;
}

// Shims
void shim_sceGnmGetTheTessellationFactorRingBufferBaseAddress(GuestContext *ctx) {
    if (g_tessellation_ring_addr == 0 && ctx) {
        ps4_gnmdriver_init(ctx);
    }
    ctx->rax = sceGnmGetTheTessellationFactorRingBufferBaseAddress();
    SHIM_RETURN();
}

void shim_sceGnmGetOffChipTessellationBufferSize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetOffChipTessellationBufferSize();
    SHIM_RETURN();
}

void shim_sceGnmGetGpuCoreClockFrequency(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceGnmGetGpuCoreClockFrequency();
    SHIM_RETURN();
}

void shim_sceGnmAreSubmitsAllowed(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmAreSubmitsAllowed();
    SHIM_RETURN();
}

void shim_sceGnmGetNumTcaUnits(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetNumTcaUnits();
    SHIM_RETURN();
}

void shim_sceGnmLogicalTcaUnitToPhysical(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmLogicalTcaUnitToPhysical((uint32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceGnmLogicalCuIndexToPhysicalCuIndex(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmLogicalCuIndexToPhysicalCuIndex((uint32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceGnmLogicalCuMaskToPhysicalCuMask(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmLogicalCuMaskToPhysicalCuMask((int64_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmFlushGarlic(GuestContext *ctx) {
    (void)ctx;
    sceGnmFlushGarlic();
    SHIM_RETURN();
}

void shim_sceGnmGetShaderStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetShaderStatus();
    SHIM_RETURN();
}

void shim_sceGnmGetProtectionFaultTimeStamp(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceGnmGetProtectionFaultTimeStamp();
    SHIM_RETURN();
}

void shim_sceGnmGetEqTimeStamp(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetEqTimeStamp();
    SHIM_RETURN();
}

void shim_sceGnmGetGpuBlockStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetGpuBlockStatus();
    SHIM_RETURN();
}

void shim_sceGnmGetGpuInfoStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetGpuInfoStatus();
    SHIM_RETURN();
}

void shim_sceGnmGetPhysicalCounterFromVirtualized(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmGetPhysicalCounterFromVirtualized();
    SHIM_RETURN();
}

void shim_sceGnmDrawInitDefaultHardwareState(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDrawInitDefaultHardwareState(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDrawInitDefaultHardwareState175(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDrawInitDefaultHardwareState175(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDrawInitDefaultHardwareState200(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDrawInitDefaultHardwareState200(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDrawInitDefaultHardwareState350(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDrawInitDefaultHardwareState350(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDrawInitToDefaultContextState(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDrawInitToDefaultContextState(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDrawInitToDefaultContextState400(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDrawInitToDefaultContextState400(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDispatchInitDefaultHardwareState(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)sceGnmDispatchInitDefaultHardwareState(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmResetVgtControl(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmResetVgtControl(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmSetVgtControl(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetVgtControl(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceGnmSetGsRingSizes(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetGsRingSizes(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceGnmSetWaveLimitMultipliers(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetWaveLimitMultipliers(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceGnmInsertDingDongMarker(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertDingDongMarker(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmInsertPopMarker(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertPopMarker(cmdbuf, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmInsertPushMarker(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const char *marker = (const char *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertPushMarker(cmdbuf, (uint32_t)ctx->rsi, marker);
    SHIM_RETURN();
}

void shim_sceGnmInsertPushColorMarker(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const char *marker = (const char *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertPushColorMarker(cmdbuf, (uint32_t)ctx->rsi, marker, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmInsertSetMarker(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const char *marker = (const char *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertSetMarker(cmdbuf, (uint32_t)ctx->rsi, marker);
    SHIM_RETURN();
}

void shim_sceGnmInsertSetColorMarker(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertSetColorMarker();
    SHIM_RETURN();
}

void shim_sceGnmInsertThreadTraceMarker(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertThreadTraceMarker();
    SHIM_RETURN();
}

void shim_sceGnmInsertWaitFlipDone(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmInsertWaitFlipDone(ctx, cmdbuf, (uint32_t)ctx->rsi, (int32_t)ctx->rdx,
                                                           (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmRegisterOwner(GuestContext *ctx) {
    void *handle = recomp_guest_to_host(ctx, ctx->rdi);
    const char *name = (const char *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmRegisterOwner(handle, name);
    SHIM_RETURN();
}

void shim_sceGnmRegisterResource(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    void *owner_handle = recomp_guest_to_host(ctx, ctx->rsi);
    const void *addr = recomp_guest_to_host(ctx, ctx->rdx);
    size_t size = (size_t)ctx->rcx;
    const char *name = (const char *)recomp_guest_to_host(ctx, ctx->r8);
    int res_type = (int)ctx->r9;
    uint64_t user_data = 0;
    if (ctx->rsp && ctx->mem_base) {
        user_data = *(uint64_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmRegisterResource(res_handle, owner_handle, addr, size, name, res_type, user_data);
    SHIM_RETURN();
}

void shim_sceGnmUnregisterResource(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmUnregisterResource(res_handle);
    SHIM_RETURN();
}

void shim_sceGnmUnregisterOwnerAndResources(GuestContext *ctx) {
    void *owner_handle = recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmUnregisterOwnerAndResources(owner_handle);
    SHIM_RETURN();
}

void shim_sceGnmUnregisterAllResourcesForOwner(GuestContext *ctx) {
    void *owner_handle = recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmUnregisterAllResourcesForOwner(owner_handle);
    SHIM_RETURN();
}

void shim_sceGnmSetResourceUserData(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetResourceUserData(res_handle, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmGetResourceUserData(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    uint64_t *user_data = (uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetResourceUserData(res_handle, user_data);
    SHIM_RETURN();
}

void shim_sceGnmGetResourceBaseAddressAndSizeInBytes(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    uint64_t *base_addr = (uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    uint64_t *size = (uint64_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetResourceBaseAddressAndSizeInBytes(res_handle, base_addr, size);
    SHIM_RETURN();
}

void shim_sceGnmGetResourceName(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    char *name = (char *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetResourceName(res_handle, name, (size_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceGnmGetResourceType(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    int *res_type = (int *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetResourceType(res_handle, res_type);
    SHIM_RETURN();
}

void shim_sceGnmGetOwnerName(GuestContext *ctx) {
    void *owner_handle = recomp_guest_to_host(ctx, ctx->rdi);
    char *name = (char *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetOwnerName(owner_handle, name, (size_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceGnmGetResourceShaderGuid(GuestContext *ctx) {
    void *res_handle = recomp_guest_to_host(ctx, ctx->rdi);
    uint64_t *guid = (uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetResourceShaderGuid(res_handle, guid);
    SHIM_RETURN();
}

void shim_sceGnmFindResourcesPublic(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmFindResourcesPublic();
    SHIM_RETURN();
}

void shim_sceGnmQueryResourceRegistrationUserMemoryRequirements(GuestContext *ctx) {
    size_t *required_size = (size_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmQueryResourceRegistrationUserMemoryRequirements(required_size);
    SHIM_RETURN();
}

void shim_sceGnmSetResourceRegistrationUserMemory(GuestContext *ctx) {
    void *addr = recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetResourceRegistrationUserMemory(addr, (size_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmRegisterGdsResource(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmRegisterGdsResource();
    SHIM_RETURN();
}

void shim_sceGnmBeginWorkload(GuestContext *ctx) {
    uint64_t *workload = (uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmBeginWorkload((uint32_t)ctx->rdi, workload);
    SHIM_RETURN();
}

void shim_sceGnmEndWorkload(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmEndWorkload(ctx->rdi);
    SHIM_RETURN();
}

void shim_sceGnmCreateWorkloadStream(GuestContext *ctx) {
    uint32_t *workload_stream = (uint32_t *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceGnmCreateWorkloadStream(ctx->rdi, workload_stream);
    SHIM_RETURN();
}

void shim_sceGnmDestroyWorkloadStream(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDestroyWorkloadStream();
    SHIM_RETURN();
}

void shim_sceGnmDingDong(GuestContext *ctx) {
    ps4_gnm_dingdong(ctx, (uint32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmDingDongForWorkload(GuestContext *ctx) {
    ps4_gnm_dingdong(ctx, (uint32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmMapComputeQueue(GuestContext *ctx) {
    gnm_remember_ctx(ctx);
    uint32_t *read_ptr = (uint32_t *)recomp_guest_to_host(ctx, ctx->r8);
    ctx->rax = (uint64_t)(int64_t)sceGnmMapComputeQueue((uint32_t)ctx->rdi, (uint32_t)ctx->rsi, ctx->rdx, (uint32_t)ctx->rcx, read_ptr);
    SHIM_RETURN();
}

void shim_sceGnmMapComputeQueueWithPriority(GuestContext *ctx) {
    gnm_remember_ctx(ctx);
    uint32_t *read_ptr = (uint32_t *)recomp_guest_to_host(ctx, ctx->r8);
    ctx->rax = (uint64_t)(int64_t)sceGnmMapComputeQueueWithPriority((uint32_t)ctx->rdi, (uint32_t)ctx->rsi, ctx->rdx, (uint32_t)ctx->rcx, read_ptr, (uint32_t)ctx->r9);
    SHIM_RETURN();
}

void shim_sceGnmUnmapComputeQueue(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmUnmapComputeQueue((uint32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmComputeWaitSemaphore(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmComputeWaitSemaphore();
    SHIM_RETURN();
}

void shim_sceGnmComputeWaitOnAddress(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    uint32_t cmp_func = (uint32_t)ctx->r9;
    uint32_t ref = 0;
    if (ctx->rsp && ctx->mem_base) {
        ref = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmComputeWaitOnAddress(cmdbuf, (uint32_t)ctx->rsi, ctx->rdx, (uint32_t)ctx->rcx, cmp_func, ref);
    SHIM_RETURN();
}

void shim_sceGnmGetLastWaitedAddress(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmGetLastWaitedAddress();
    SHIM_RETURN();
}

void shim_sceGnmAddEqEvent(GuestContext *ctx) {
    // udata is returned verbatim to the guest in the event: keep it a guest value.
    void *udata = (void *)(uintptr_t)ctx->rdx;
    ctx->rax = (uint64_t)(int64_t)sceGnmAddEqEvent(ctx->rdi, ctx->rsi, udata);
    SHIM_RETURN();
}

void shim_sceGnmDeleteEqEvent(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGnmDeleteEqEvent(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceGnmGetEqEventType(GuestContext *ctx) {
    const void *ev = recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmGetEqEventType(ev);
    SHIM_RETURN();
}

void shim_sceGnmValidateGetVersion(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateGetVersion();
    SHIM_RETURN();
}

void shim_sceGnmValidateOnSubmitEnabled(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(sceGnmValidateOnSubmitEnabled() ? 1 : 0);
    SHIM_RETURN();
}

void shim_sceGnmValidateGetDiagnosticInfo(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateGetDiagnosticInfo();
    SHIM_RETURN();
}

void shim_sceGnmValidateGetDiagnostics(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateGetDiagnostics();
    SHIM_RETURN();
}

void shim_sceGnmValidateResetState(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateResetState();
    SHIM_RETURN();
}

void shim_sceGnmValidateDisableDiagnostics(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateDisableDiagnostics();
    SHIM_RETURN();
}

void shim_sceGnmValidateDisableDiagnostics2(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateDisableDiagnostics2();
    SHIM_RETURN();
}

void shim_sceGnmValidateCommandBuffers(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateCommandBuffers();
    SHIM_RETURN();
}

void shim_sceGnmValidateDrawCommandBuffers(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateDrawCommandBuffers();
    SHIM_RETURN();
}

void shim_sceGnmValidateDispatchCommandBuffers(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidateDispatchCommandBuffers();
    SHIM_RETURN();
}

void shim_sceGnmValidationRegisterMemoryCheckCallback(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmValidationRegisterMemoryCheckCallback();
    SHIM_RETURN();
}

void shim_sceRazorIsLoaded(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceRazorIsLoaded();
    SHIM_RETURN();
}

void shim_sceRazorCaptureImmediate(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceRazorCaptureImmediate();
    SHIM_RETURN();
}

void shim_sceRazorCpuJobManagerSequence(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceRazorCpuJobManagerSequence();
    SHIM_RETURN();
}

void shim_sceRazorCpuJobManagerJob(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceRazorCpuJobManagerJob();
    SHIM_RETURN();
}

void shim_sceRazorCpuJobManagerDispatch(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceRazorCpuJobManagerDispatch();
    SHIM_RETURN();
}

void shim_sceGnmDriverTraceInProgress(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(sceGnmDriverTraceInProgress() ? 1 : 0);
    SHIM_RETURN();
}

void shim_sceGnmDriverCaptureInProgress(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(sceGnmDriverCaptureInProgress() ? 1 : 0);
    SHIM_RETURN();
}

void shim_sceGnmDriverTriggerCapture(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDriverTriggerCapture();
    SHIM_RETURN();
}

void shim_sceGnmIsUserPaEnabled(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(sceGnmIsUserPaEnabled() ? 1 : 0);
    SHIM_RETURN();
}

void shim_sceGnmDebugHardwareStatus(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDebugHardwareStatus();
    SHIM_RETURN();
}

void shim_sceGnmSetupMipStatsReport(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmSetupMipStatsReport();
    SHIM_RETURN();
}

void shim_sceGnmDisableMipStatsReport(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDisableMipStatsReport();
    SHIM_RETURN();
}

void shim_sceGnmRequestMipStatsReportAndReset(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmRequestMipStatsReportAndReset();
    SHIM_RETURN();
}

static const uint32_t indirect_sgpr_offsets[] = { 0u, 0u, 0x4cu, 0u, 0xccu, 0u, 0x14cu };

// Draw and Dispatch commands implementation
int32_t sceGnmDrawIndex(uint32_t *cmdbuf, uint32_t size, uint32_t index_count, uint64_t index_addr, uint32_t flags, uint32_t type) {
    (void)type;
    if (!cmdbuf || size != 10 || index_addr == 0 || (index_addr & 1) != 0 || (flags & 0x1ffffffe) != 0) {
        return -1;
    }
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDEX_2, 4, 0, 0);
    cmdbuf[1] = index_count;
    cmdbuf[2] = (uint32_t)(index_addr & 0xffffffffu);
    cmdbuf[3] = (uint32_t)(index_addr >> 32);
    cmdbuf[4] = index_count;
    cmdbuf[5] = 0;
    write_trailing_nop(cmdbuf + 6, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndexAuto(uint32_t *cmdbuf, uint32_t size, uint32_t index_count, uint32_t flags) {
    if (!cmdbuf || size != 7 || (flags & 0x1ffffffe) != 0) {
        return -1;
    }
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDEX_AUTO, 1, 0, 0); // count = body dwords - 1
    cmdbuf[1] = index_count;
    cmdbuf[2] = 2u;
    write_trailing_nop(cmdbuf + 3, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndexOffset(uint32_t *cmdbuf, uint32_t size, uint32_t index_offset, uint32_t index_count, uint32_t flags) {
    if (!cmdbuf || size != 9) return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDEX_OFFSET_2, 3, 0, pred);
    cmdbuf[1] = index_count;
    cmdbuf[2] = index_offset;
    cmdbuf[3] = index_count;
    cmdbuf[4] = 0;
    write_trailing_nop(cmdbuf + 5, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndexIndirect(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags) {
    if (!cmdbuf || size != 9 || shader_stage >= 7 || vertex_sgpr_offset >= 0x10 || instance_sgpr_offset >= 0x10) return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDEX_INDIRECT, 3, 0, pred);
    uint32_t sgpr_offset = indirect_sgpr_offsets[shader_stage];
    cmdbuf[1] = data_offset;
    cmdbuf[2] = (vertex_sgpr_offset == 0) ? 0 : (vertex_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[3] = (instance_sgpr_offset == 0) ? 0 : (instance_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[4] = 0;
    write_trailing_nop(cmdbuf + 5, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndirect(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags) {
    if (!cmdbuf || size != 9 || shader_stage >= 7 || vertex_sgpr_offset >= 0x10 || instance_sgpr_offset >= 0x10) return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDIRECT, 3, 0, pred);
    uint32_t sgpr_offset = indirect_sgpr_offsets[shader_stage];
    cmdbuf[1] = data_offset;
    cmdbuf[2] = (vertex_sgpr_offset == 0) ? 0 : (vertex_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[3] = (instance_sgpr_offset == 0) ? 0 : (instance_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[4] = 2u;
    write_trailing_nop(cmdbuf + 5, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndexIndirectCountMulti(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t max_count, uint64_t count_addr, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags) {
    if (!cmdbuf || size != 16 || vertex_sgpr_offset >= 0x10 || instance_sgpr_offset >= 0x10) return -1;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_NOP, 1, 0, 0);
    cmdbuf[1] = 0;
    cmdbuf[2] = 0;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[3] = PM4_TYPE3_HEADER(0x3a, 8, 0, pred);
    uint32_t sgpr_offset = (shader_stage < 7) ? indirect_sgpr_offsets[shader_stage] : 0;
    cmdbuf[4] = data_offset;
    cmdbuf[5] = (vertex_sgpr_offset == 0) ? 0 : (vertex_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[6] = (instance_sgpr_offset == 0) ? 0 : (instance_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[7] = (count_addr != 0 ? 1u : 0u) << 30;
    cmdbuf[8] = max_count;
    *(uint64_t *)(&cmdbuf[9]) = count_addr;
    cmdbuf[11] = 20; // sizeof(DrawIndexedIndirectArgs)
    cmdbuf[12] = 0;
    write_trailing_nop(cmdbuf + 13, 2);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndirectCountMulti(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndexIndirectMulti(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t max_count, uint32_t shader_stage, uint32_t vertex_sgpr_offset, uint32_t instance_sgpr_offset, uint32_t flags) {
    if (!cmdbuf || size != 11 || vertex_sgpr_offset >= 0x10 || instance_sgpr_offset >= 0x10) return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDEX_INDIRECT_MULTI, 5, 0, pred);
    uint32_t sgpr_offset = (shader_stage < 7) ? indirect_sgpr_offsets[shader_stage] : 0;
    cmdbuf[1] = data_offset;
    cmdbuf[2] = (vertex_sgpr_offset == 0) ? 0 : (vertex_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[3] = (instance_sgpr_offset == 0) ? 0 : (instance_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[4] = max_count;
    cmdbuf[5] = 20;
    cmdbuf[6] = 0;
    write_trailing_nop(cmdbuf + 7, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawIndexMultiInstanced(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmDrawOpaqueAuto(void) {
    return ORBIS_GNM_OK;
}

int32_t sceGnmDispatchDirect(uint32_t *cmdbuf, uint32_t size, uint32_t threads_x, uint32_t threads_y, uint32_t threads_z, uint32_t flags) {
    if (!cmdbuf || size != 9) return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DISPATCH_DIRECT, 3, 1, pred);
    cmdbuf[1] = threads_x;
    cmdbuf[2] = threads_y;
    cmdbuf[3] = threads_z;
    cmdbuf[4] = (flags & 0x18) + 1;
    write_trailing_nop(cmdbuf + 5, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDispatchIndirect(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t flags) {
    if (!cmdbuf || size != 7) return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DISPATCH_INDIRECT, 1, 1, pred);
    cmdbuf[1] = data_offset;
    cmdbuf[2] = (flags & 0x18) + 1;
    write_trailing_nop(cmdbuf + 3, 3);
    return ORBIS_GNM_OK;
}

int32_t sceGnmDispatchIndirectOnMec(uint32_t *cmdbuf, uint32_t size, uint64_t args, uint32_t modifier) {
    if (!cmdbuf || size != 8 || args == 0 || (args & 3u) != 0) return -1;
    cmdbuf[0] = 0xc0021602u | (modifier & 1u);
    *(uint64_t *)(&cmdbuf[1]) = args;
    cmdbuf[3] = (modifier & 0x18) | 1u;
    cmdbuf[4] = 0xc0021000u;
    cmdbuf[5] = 0;
    return ORBIS_GNM_OK;
}

// Shader binding commands implementation
int32_t sceGnmSetVsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *vs_regs, uint32_t shader_modifier) {
    if (!cmdbuf || size <= 0x1c || !vs_regs || vs_regs[1] != 0 || (shader_modifier & 0xfcfffc3f) != 0) return -1;
    uint32_t var = (shader_modifier == 0) ? vs_regs[2] : (vs_regs[2] & 0xfcfffc3f) | shader_modifier;
    uint32_t *c = cmdbuf;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x48u; // SPI_SHADER_PGM_LO_VS
    *c++ = vs_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x4au; // SPI_SHADER_PGM_RSRC1_VS
    *c++ = var;
    *c++ = vs_regs[3];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x207u; // PA_CL_VS_OUT_CNTL
    *c++ = vs_regs[6];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x1b1u; // SPI_VS_OUT_CONFIG
    *c++ = vs_regs[4];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x1c3u; // SPI_SHADER_POS_FORMAT
    *c++ = vs_regs[5];

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmUpdateVsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *vs_regs, uint32_t shader_modifier) {
    return sceGnmSetVsShader(cmdbuf, size, vs_regs, shader_modifier);
}

int32_t sceGnmSetEmbeddedVsShader(uint32_t *cmdbuf, uint32_t size, uint32_t shader_id, uint32_t shader_modifier) {
    (void)shader_id;
    (void)shader_modifier;
    if (!cmdbuf || size <= 0x1c) return -1;
    write_trailing_nop(cmdbuf, size - 1);
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetPsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs) {
    return sceGnmSetPsShader350(cmdbuf, size, ps_regs);
}

int32_t sceGnmSetPsShader350(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs) {
    if (!cmdbuf || size <= 0x27) return -1;
    uint32_t *c = cmdbuf;

    if (!ps_regs) {
        *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
        *c++ = 8u; // SPI_SHADER_PGM_LO_PS
        *c++ = 0u;
        *c++ = 0u;

        *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
        *c++ = 0x203u; // DB_SHADER_CONTROL
        *c++ = 0u;

        *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
        *c++ = 0x8fu; // CB_SHADER_MASK
        *c++ = 0xfu;

        write_trailing_nop(c, 0x1d);
        return ORBIS_GNM_OK;
    }

    if (ps_regs[1] != 0) return -1;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 8u; // SPI_SHADER_PGM_LO_PS
    *c++ = ps_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 10u; // SPI_SHADER_PGM_RSRC1_PS / RSRC2_PS
    *c++ = ps_regs[2];
    *c++ = ps_regs[3];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 2, 0, 0);
    *c++ = 0x1c4u; // SPI_SHADER_Z_FORMAT / COL_FORMAT
    *c++ = ps_regs[4];
    *c++ = ps_regs[5];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 2, 0, 0);
    *c++ = 0x1b3u; // SPI_PS_INPUT_ENA / INPUT_ADDR
    *c++ = ps_regs[6];
    *c++ = ps_regs[7];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x1b6u; // SPI_PS_IN_CONTROL
    *c++ = ps_regs[8];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x1b8u; // SPI_BARYC_CNTL
    *c++ = ps_regs[9];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x203u; // DB_SHADER_CONTROL
    *c++ = ps_regs[10];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x8fu; // CB_SHADER_MASK
    *c++ = ps_regs[11];

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmUpdatePsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs) {
    return sceGnmSetPsShader350(cmdbuf, size, ps_regs);
}

int32_t sceGnmUpdatePsShader350(uint32_t *cmdbuf, uint32_t size, const uint32_t *ps_regs) {
    return sceGnmSetPsShader350(cmdbuf, size, ps_regs);
}

int32_t sceGnmSetEmbeddedPsShader(uint32_t *cmdbuf, uint32_t size, uint32_t shader_id, uint32_t shader_modifier) {
    (void)shader_id;
    (void)shader_modifier;
    if (!cmdbuf || size <= 0x27) return -1;
    write_trailing_nop(cmdbuf, size - 1);
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetGsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *gs_regs) {
    if (!cmdbuf || size < 0x1d || !gs_regs || gs_regs[1] != 0) return -1;
    uint32_t *c = cmdbuf;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x88u; // SPI_SHADER_PGM_LO_GS
    *c++ = gs_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x8au; // SPI_SHADER_PGM_RSRC1_GS / RSRC2_GS
    *c++ = gs_regs[2];
    *c++ = gs_regs[3];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x2e5u; // VGT_STRMOUT_CONFIG
    *c++ = gs_regs[4];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x29bu; // VGT_GS_OUT_PRIM_TYPE
    *c++ = gs_regs[5];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x2e4u; // VGT_GS_INSTANCE_CNT
    *c++ = gs_regs[6];

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmUpdateGsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *gs_regs) {
    return sceGnmSetGsShader(cmdbuf, size, gs_regs);
}

int32_t sceGnmSetHsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *hs_regs, uint32_t param4) {
    if (!cmdbuf || size < 0x1e || !hs_regs || hs_regs[1] != 0) return -1;
    uint32_t *c = cmdbuf;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x108u; // SPI_SHADER_PGM_LO_HS
    *c++ = hs_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x10au; // SPI_SHADER_PGM_RSRC1_HS / RSRC2_HS
    *c++ = hs_regs[2];
    *c++ = hs_regs[3];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 2, 0, 0);
    *c++ = 0x286u; // VGT_HOS_MAX_TESS_LEVEL / MIN_TESS_LEVEL
    *c++ = hs_regs[5];
    *c++ = hs_regs[6];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x2dbu; // VGT_TF_PARAM
    *c++ = hs_regs[4];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_CONTEXT_REG, 1, 0, 0);
    *c++ = 0x2d6u; // VGT_LS_HS_CONFIG
    *c++ = param4;

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmUpdateHsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *hs_regs, uint32_t ls_hs_config) {
    return sceGnmSetHsShader(cmdbuf, size, hs_regs, ls_hs_config);
}

int32_t sceGnmSetEsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *es_regs, uint32_t shader_modifier) {
    if (!cmdbuf || size < 0x14 || !es_regs || es_regs[1] != 0 || (shader_modifier & 0xfcfffc3f) != 0) return -1;
    uint32_t var = (shader_modifier == 0) ? es_regs[2] : (es_regs[2] & 0xfcfffc3f) | shader_modifier;
    uint32_t *c = cmdbuf;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0xc8u; // SPI_SHADER_PGM_LO_ES
    *c++ = es_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0xcau; // SPI_SHADER_PGM_RSRC1_ES
    *c++ = var;
    *c++ = es_regs[3];

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetLsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *ls_regs, uint32_t shader_modifier) {
    if (!cmdbuf || size < 0x17 || !ls_regs || ls_regs[1] != 0) return -1;
    uint32_t mask = ((shader_modifier & 0xfffffc3f) == 0) ? 0xfffffc3f : 0xfcfffc3f;
    if (shader_modifier & mask) return -1;
    uint32_t var = (shader_modifier == 0) ? ls_regs[2] : (ls_regs[2] & mask) | shader_modifier;
    uint32_t *c = cmdbuf;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x148u; // SPI_SHADER_PGM_LO_LS
    *c++ = ls_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 0, 0);
    *c++ = 0x14au; // SPI_SHADER_PGM_RSRC1_LS / RSRC2_LS
    *c++ = var;
    *c++ = ls_regs[3];

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmSetCsShader(uint32_t *cmdbuf, uint32_t size, const uint32_t *cs_regs) {
    return sceGnmSetCsShaderWithModifier(cmdbuf, size, cs_regs, 0);
}

int32_t sceGnmSetCsShaderWithModifier(uint32_t *cmdbuf, uint32_t size, const uint32_t *cs_regs, uint32_t modifier) {
    if (!cmdbuf || size <= 0x18 || !cs_regs || cs_regs[1] != 0 || (modifier & 0xfffffc3fu) != 0) return -1;
    uint32_t rsrc1 = (modifier == 0) ? cs_regs[2] : (cs_regs[2] & 0xfffffc3fu) | modifier;
    uint32_t *c = cmdbuf;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 1, 0);
    *c++ = 0x20cu; // COMPUTE_PGM_LO / COMPUTE_PGM_HI
    *c++ = cs_regs[0];
    *c++ = 0u;

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 2, 1, 0);
    *c++ = 0x212u; // COMPUTE_PGM_RSRC1 / COMPUTE_PGM_RSRC2
    *c++ = rsrc1;
    *c++ = cs_regs[3];

    *c++ = PM4_TYPE3_HEADER(PM4_IT_SET_SH_REG, 3, 1, 0);
    *c++ = 0x207u; // COMPUTE_NUM_THREAD_X / Y / Z
    *c++ = cs_regs[4];
    *c++ = cs_regs[5];
    *c++ = cs_regs[6];

    write_trailing_nop(c, 11);
    return ORBIS_GNM_OK;
}

int32_t sceGnmGetShaderProgramBaseAddress(void) {
    return ORBIS_GNM_OK;
}

// Shims for Draw, Dispatch, and Shaders
void shim_sceGnmDrawIndex(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndex(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndexAuto(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndexAuto(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndexOffset(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndexOffset(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx, (uint32_t)ctx->r8);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndexIndirect(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    uint32_t flags = 0;
    if (ctx->rsp && ctx->mem_base) {
        flags = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndexIndirect(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9, flags);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndirect(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    uint32_t flags = 0;
    if (ctx->rsp && ctx->mem_base) {
        flags = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndirect(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9, flags);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndexIndirectCountMulti(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    uint32_t vertex_sgpr = 0;
    uint32_t instance_sgpr = 0;
    uint32_t flags = 0;
    if (ctx->rsp && ctx->mem_base) {
        vertex_sgpr = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
        instance_sgpr = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 16);
        flags = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 24);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndexIndirectCountMulti(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx, ctx->r8, (uint32_t)ctx->r9, vertex_sgpr, instance_sgpr, flags);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndirectCountMulti(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndirectCountMulti();
    SHIM_RETURN();
}

void shim_sceGnmDrawIndexIndirectMulti(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    uint32_t instance_sgpr = 0;
    uint32_t flags = 0;
    if (ctx->rsp && ctx->mem_base) {
        instance_sgpr = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
        flags = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 16);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndexIndirectMulti(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9, instance_sgpr, flags);
    SHIM_RETURN();
}

void shim_sceGnmDrawIndexMultiInstanced(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndexMultiInstanced();
    SHIM_RETURN();
}

void shim_sceGnmDrawOpaqueAuto(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawOpaqueAuto();
    SHIM_RETURN();
}

void shim_sceGnmDispatchDirect(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmDispatchDirect(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9);
    SHIM_RETURN();
}

void shim_sceGnmDispatchIndirect(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmDispatchIndirect(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmDispatchIndirectOnMec(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmDispatchIndirectOnMec(cmdbuf, (uint32_t)ctx->rsi, ctx->rdx, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetVsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *vs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetVsShader(cmdbuf, (uint32_t)ctx->rsi, vs_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmUpdateVsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *vs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmUpdateVsShader(cmdbuf, (uint32_t)ctx->rsi, vs_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetEmbeddedVsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetEmbeddedVsShader(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetPsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *ps_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetPsShader(cmdbuf, (uint32_t)ctx->rsi, ps_regs);
    SHIM_RETURN();
}

void shim_sceGnmSetPsShader350(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *ps_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetPsShader350(cmdbuf, (uint32_t)ctx->rsi, ps_regs);
    SHIM_RETURN();
}

void shim_sceGnmUpdatePsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *ps_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmUpdatePsShader(cmdbuf, (uint32_t)ctx->rsi, ps_regs);
    SHIM_RETURN();
}

void shim_sceGnmUpdatePsShader350(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *ps_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmUpdatePsShader350(cmdbuf, (uint32_t)ctx->rsi, ps_regs);
    SHIM_RETURN();
}

void shim_sceGnmSetEmbeddedPsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetEmbeddedPsShader(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetGsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *gs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetGsShader(cmdbuf, (uint32_t)ctx->rsi, gs_regs);
    SHIM_RETURN();
}

void shim_sceGnmUpdateGsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *gs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmUpdateGsShader(cmdbuf, (uint32_t)ctx->rsi, gs_regs);
    SHIM_RETURN();
}

void shim_sceGnmSetHsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *hs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetHsShader(cmdbuf, (uint32_t)ctx->rsi, hs_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmUpdateHsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *hs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmUpdateHsShader(cmdbuf, (uint32_t)ctx->rsi, hs_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetEsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *es_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetEsShader(cmdbuf, (uint32_t)ctx->rsi, es_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetLsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *ls_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetLsShader(cmdbuf, (uint32_t)ctx->rsi, ls_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmSetCsShader(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *cs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetCsShader(cmdbuf, (uint32_t)ctx->rsi, cs_regs);
    SHIM_RETURN();
}

void shim_sceGnmSetCsShaderWithModifier(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    const uint32_t *cs_regs = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)(int64_t)sceGnmSetCsShaderWithModifier(cmdbuf, (uint32_t)ctx->rsi, cs_regs, (uint32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceGnmGetShaderProgramBaseAddress(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmGetShaderProgramBaseAddress();
    SHIM_RETURN();
}

// The guest thread that submits command buffers feeds the GPU; if the scheduler
// starves it the GPU idles. Raise it once, like the encoder thread.
// PS4_GNM_CP_QOS=0 disables this.
static void gnm_raise_submit_thread_qos(void) {
    static __thread int done;
    if (done) return;
    done = 1;
    const char *e = getenv("PS4_GNM_CP_QOS");
    if (e && e[0] == '0') return;
    int err = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (err != 0) {
        printf("[gnm] raising the submit thread to USER_INTERACTIVE failed: error %d\n", err);
        return;
    }
    char name[64] = {0};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    printf("[gnm] submit thread '%s' raised to USER_INTERACTIVE\n", name[0] ? name : "unnamed");
}

void shim_sceGnmSubmitCommandBuffers(GuestContext *ctx) {
    gnm_raise_submit_thread_qos();
    uint32_t count = (uint32_t)ctx->rdi;
    const uint64_t *dcb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    const uint32_t *dcb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    const uint64_t *ccb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->rcx);
    const uint32_t *ccb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->r8);
    ctx->rax = (uint64_t)(int64_t)sceGnmSubmitCommandBuffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes);
    SHIM_RETURN();
}

void shim_sceGnmSubmitCommandBuffersForWorkload(GuestContext *ctx) {
    gnm_raise_submit_thread_qos();
    uint32_t workload = (uint32_t)ctx->rdi;
    uint32_t count = (uint32_t)ctx->rsi;
    const uint64_t *dcb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->rdx);
    const uint32_t *dcb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rcx);
    const uint64_t *ccb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->r8);
    const uint32_t *ccb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->r9);
    ctx->rax = (uint64_t)(int64_t)sceGnmSubmitCommandBuffersForWorkload(ctx, workload, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes);
    SHIM_RETURN();
}

void shim_sceGnmSubmitAndFlipCommandBuffers(GuestContext *ctx) {
    gnm_raise_submit_thread_qos();
    uint32_t count = (uint32_t)ctx->rdi;
    const uint64_t *dcb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->rsi);
    const uint32_t *dcb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rdx);
    const uint64_t *ccb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->rcx);
    const uint32_t *ccb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->r8);
    uint32_t vo_handle = (uint32_t)ctx->r9;
    uint32_t buf_idx = 0;
    uint32_t flip_mode = 0;
    int64_t flip_arg = 0;
    if (ctx->rsp && ctx->mem_base) {
        buf_idx = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
        flip_mode = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 16);
        flip_arg = *(int64_t *)recomp_guest_to_host(ctx, ctx->rsp + 24);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmSubmitAndFlipCommandBuffers(ctx, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes, vo_handle, buf_idx, flip_mode, flip_arg);
    SHIM_RETURN();
}

void shim_sceGnmSubmitAndFlipCommandBuffersForWorkload(GuestContext *ctx) {
    gnm_raise_submit_thread_qos();
    uint32_t workload = (uint32_t)ctx->rdi;
    uint32_t count = (uint32_t)ctx->rsi;
    const uint64_t *dcb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->rdx);
    const uint32_t *dcb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->rcx);
    const uint64_t *ccb_gpu_addrs = (const uint64_t *)recomp_guest_to_host(ctx, ctx->r8);
    const uint32_t *ccb_sizes_in_bytes = (const uint32_t *)recomp_guest_to_host(ctx, ctx->r9);
    uint32_t vo_handle = 0;
    uint32_t buf_idx = 0;
    uint32_t flip_mode = 0;
    int64_t flip_arg = 0;
    if (ctx->rsp && ctx->mem_base) {
        vo_handle = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
        buf_idx = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 16);
        flip_mode = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 24);
        flip_arg = *(int64_t *)recomp_guest_to_host(ctx, ctx->rsp + 32);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmSubmitAndFlipCommandBuffersForWorkload(ctx, workload, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes, vo_handle, buf_idx, flip_mode, flip_arg);
    SHIM_RETURN();
}

void shim_sceGnmSubmitDone(GuestContext *ctx) {
    (void)ctx;
    ctx->rax = (uint64_t)(int64_t)sceGnmSubmitDone();
    SHIM_RETURN();
}

void shim_sceGnmRequestFlipAndSubmitDone(GuestContext *ctx) {
    uint64_t gpu_addr = ctx->rdi;
    uint32_t gpu_addr_size_in_bytes = (uint32_t)ctx->rsi;
    uint32_t vo_handle = (uint32_t)ctx->rdx;
    uint32_t buf_idx = (uint32_t)ctx->rcx;
    uint32_t flip_mode = (uint32_t)ctx->r8;
    int64_t flip_arg = (int64_t)ctx->r9;
    ctx->rax = (uint64_t)(int64_t)sceGnmRequestFlipAndSubmitDone(ctx, gpu_addr, gpu_addr_size_in_bytes, vo_handle, buf_idx, flip_mode, flip_arg);
    SHIM_RETURN();
}

void shim_sceGnmRequestFlipAndSubmitDoneForWorkload(GuestContext *ctx) {
    uint64_t workload = ctx->rdi;
    uint64_t gpu_addr = ctx->rsi;
    uint32_t gpu_addr_size_in_bytes = (uint32_t)ctx->rdx;
    uint32_t vo_handle = (uint32_t)ctx->rcx;
    uint32_t buf_idx = (uint32_t)ctx->r8;
    uint32_t flip_mode = (uint32_t)ctx->r9;
    int64_t flip_arg = 0;
    if (ctx->rsp && ctx->mem_base) {
        flip_arg = *(int64_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmRequestFlipAndSubmitDoneForWorkload(ctx, workload, gpu_addr, gpu_addr_size_in_bytes, vo_handle, buf_idx, flip_mode, flip_arg);
    SHIM_RETURN();
}

// sceGnmDrawIndirectMulti(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset,
//     uint32_t max_count, uint32_t shader_stage, uint32_t vertex_sgpr_offset,
//     uint32_t instance_sgpr_offset, uint32_t flags)
// Emits DRAW_INDIRECT_MULTI (6 payload dwords) followed by a 3 dword NOP, like the
// system library (11 dwords total).
int32_t sceGnmDrawIndirectMulti(uint32_t *cmdbuf, uint32_t size, uint32_t data_offset, uint32_t max_count,
                                uint32_t shader_stage, uint32_t vertex_sgpr_offset,
                                uint32_t instance_sgpr_offset, uint32_t flags) {
    if (!cmdbuf || size != 11 || shader_stage >= 7 || vertex_sgpr_offset >= 0x10 || instance_sgpr_offset >= 0x10)
        return -1;
    uint32_t pred = (flags & 1) ? 1 : 0;
    cmdbuf[0] = PM4_TYPE3_HEADER(PM4_IT_DRAW_INDIRECT_MULTI, 5, 0, pred);
    uint32_t sgpr_offset = indirect_sgpr_offsets[shader_stage];
    cmdbuf[1] = data_offset;
    cmdbuf[2] = (vertex_sgpr_offset == 0) ? 0 : (vertex_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[3] = (instance_sgpr_offset == 0) ? 0 : (instance_sgpr_offset & 0xffffu) + sgpr_offset;
    cmdbuf[4] = max_count;
    cmdbuf[5] = 16; // sizeof(DrawIndirectArgs)
    cmdbuf[6] = 2;  // draw initiator: auto index
    write_trailing_nop(cmdbuf + 7, 3);
    return ORBIS_GNM_OK;
}

void shim_sceGnmDrawIndirectMulti(GuestContext *ctx) {
    uint32_t *cmdbuf = (uint32_t *)recomp_guest_to_host(ctx, ctx->rdi);
    uint32_t instance_sgpr = 0;
    uint32_t flags = 0;
    if (ctx->rsp && ctx->mem_base) {
        instance_sgpr = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 8);
        flags = *(uint32_t *)recomp_guest_to_host(ctx, ctx->rsp + 16);
    }
    ctx->rax = (uint64_t)(int64_t)sceGnmDrawIndirectMulti(cmdbuf, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx,
                                                          (uint32_t)ctx->rcx, (uint32_t)ctx->r8, (uint32_t)ctx->r9,
                                                          instance_sgpr, flags);
    SHIM_RETURN();
}

// ---------------------------------------------------------------------------------
// Two libSceGnmDriver entry points the NID database has no names for. shadPS4 calls them
// Func_4774D83BB4DDBF9A (NID R3TYO7Tdv5o) and Func_BADE7B4C199140DD (NID ut57TBmRQN0) and
// registers them under the sub-library "libSceGnmWaitFreeSubmit".
//
// Uncharted 4 calls them as an on/off pair: function 0x176aca0 flips a flag and calls
// ut57TBmRQN0 when the flag was set, R3TYO7Tdv5o otherwise; R3TYO7Tdv5o is also called once,
// unconditionally, at boot (0x22000b, with no arguments).
//
// Class N (documented no-op). The console would switch a driver mode, but nothing the guest
// can observe depends on it here:
//  * The result is never used. Disassembly of the recompiled call sites of both functions
//    (0x22000b, 0x207568, 0x207750, 0x176acc0, 0x176accb): none reads EAX/RAX afterwards;
//    the next instruction overwrites it (a call, a load into RAX, XOR EAX / MOV AL, 1 in
//    the toggle) or ignores it (a store, a LEA).
//  * The host never waits for free ring space: sceGnmSubmitCommandBuffers (above) hands the
//    command buffers to ps4_gnm_process_command_buffers inside the call, under g_gnm_mutex,
//    with no ring-capacity wait anywhere, so there is no wait such a mode could gate.
// Both return SCE_OK, as shadPS4 does (its stubs only log).
// ---------------------------------------------------------------------------------
void shim_Func_4774D83BB4DDBF9A(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim_Func_BADE7B4C199140DD(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}
