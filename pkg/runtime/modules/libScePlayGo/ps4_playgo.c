#include "ps4_playgo.h"
#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define PLAYGO_MAGIC 0x6F676C70U // 'plgo'

#pragma pack(push, 1)
typedef struct chunk_t {
    uint32_t offset;
    uint32_t length;
} chunk_t;

typedef struct PlaygoHeader {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t image_count;
    uint16_t chunk_count;
    uint16_t mchunk_count;
    uint16_t scenario_count;
    uint32_t file_size;
    uint16_t default_scenario_id;
    uint16_t attrib;
    uint32_t sdk_version;
    uint16_t disc_count;
    uint16_t layer_bmp;
    uint8_t  reserved[32];
    char     content_id[128];
    chunk_t  chunk_attrs;
    chunk_t  chunk_mchunks;
    chunk_t  chunk_labels;
    chunk_t  mchunk_attrs;
    chunk_t  scenario_attrs;
    chunk_t  scenario_chunks;
    chunk_t  scenario_labels;
    chunk_t  inner_mchunk_attrs;
} PlaygoHeader;

typedef struct playgo_chunk_attr_entry_t {
    uint8_t  req_locus;
    uint8_t  reserved;
    uint16_t mchunk_count;
    uint32_t mchunks_offset;
    uint32_t label_offset;
    uint64_t language_mask;
} playgo_chunk_attr_entry_t;

typedef struct playgo_mchunk_attr_entry_t {
    uint8_t  image_disc_layer_no;
    uint8_t  flags;
    uint16_t reserved;
    uint32_t offset;
    uint64_t size;
} playgo_mchunk_attr_entry_t;
#pragma pack(pop)

typedef struct PlaygoChunkInfo {
    uint8_t req_locus;
    uint64_t language_mask;
    uint64_t total_size;
} PlaygoChunkInfo;

static const OrbisPlayGoHandle kValidPlayGoHandle = 1;

static bool g_playgo_initialized = false;
static uint32_t g_playgo_file_size = 0;
static uint16_t g_playgo_chunk_count = 0;
static PlaygoChunkInfo *g_playgo_chunks = NULL;
static OrbisPlayGoInstallSpeed g_playgo_speed = ORBIS_PLAYGO_INSTALL_SPEED_FULL;
static OrbisPlayGoLanguageMask g_playgo_lang_mask = 0xFFFFFFFFFFFFFFFFULL;

int32_t scePlayGoInitialize(const OrbisPlayGoInitParams *params) {
    if (!params || !params->bufAddr) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (params->bufSize < 0x200000) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_ALREADY_INITIALIZED;
    }

    g_playgo_file_size = 0;
    g_playgo_chunk_count = 0;
    if (g_playgo_chunks) {
        free(g_playgo_chunks);
        g_playgo_chunks = NULL;
    }

    // Attempt to locate and parse /app0/sce_sys/playgo-chunk.dat
    char resolved[1024];
    const char *playgo_path = "/app0/sce_sys/playgo-chunk.dat";
    if (ps4_vfs_resolve(playgo_path, resolved, sizeof(resolved)) == 0) {
        playgo_path = resolved;
    }

    FILE *f = fopen(playgo_path, "rb");
    if (!f) {
        // Try fallback to nested sce_sys path if present
        if (ps4_vfs_resolve("/app0/sce_sys/sce_sys/playgo-chunk.dat", resolved, sizeof(resolved)) == 0) {
            f = fopen(resolved, "rb");
        }
    }

    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (sz >= (long)sizeof(PlaygoHeader)) {
            uint8_t *data = (uint8_t *)malloc(sz);
            if (data && fread(data, 1, sz, f) == (size_t)sz) {
                const PlaygoHeader *hdr = (const PlaygoHeader *)data;
                if (hdr->magic == PLAYGO_MAGIC && hdr->chunk_count > 0) {
                    g_playgo_file_size = hdr->file_size;
                    g_playgo_chunk_count = hdr->chunk_count;
                    g_playgo_chunks = (PlaygoChunkInfo *)calloc(hdr->chunk_count, sizeof(PlaygoChunkInfo));

                    if (hdr->chunk_attrs.offset + hdr->chunk_attrs.length <= (uint32_t)sz &&
                        hdr->mchunk_attrs.offset + hdr->mchunk_attrs.length <= (uint32_t)sz &&
                        hdr->chunk_mchunks.offset + hdr->chunk_mchunks.length <= (uint32_t)sz) {
                        const playgo_chunk_attr_entry_t *attrs =
                            (const playgo_chunk_attr_entry_t *)(data + hdr->chunk_attrs.offset);
                        const uint16_t *mchunks_table =
                            (const uint16_t *)(data + hdr->chunk_mchunks.offset);
                        const playgo_mchunk_attr_entry_t *mchunk_attrs =
                            (const playgo_mchunk_attr_entry_t *)(data + hdr->mchunk_attrs.offset);

                        for (uint16_t i = 0; i < hdr->chunk_count; i++) {
                            g_playgo_chunks[i].req_locus = attrs[i].req_locus;
                            g_playgo_chunks[i].language_mask = attrs[i].language_mask;
                            uint64_t total_size = 0;
                            uint16_t mc = attrs[i].mchunk_count;
                            if (mc != 0 && (attrs[i].mchunks_offset + mc * sizeof(uint16_t) <= hdr->chunk_mchunks.length)) {
                                const uint16_t *mc_list = (const uint16_t *)((const uint8_t *)mchunks_table + attrs[i].mchunks_offset);
                                for (uint16_t j = 0; j < mc; j++) {
                                    uint16_t mchunk_id = mc_list[j];
                                    if (mchunk_id < hdr->mchunk_count) {
                                        total_size += mchunk_attrs[mchunk_id].size;
                                    }
                                }
                            }
                            g_playgo_chunks[i].total_size = total_size;
                        }
                    }
                }
            }
            if (data) free(data);
        }
        fclose(f);
    }

    g_playgo_initialized = true;
    g_playgo_speed = ORBIS_PLAYGO_INSTALL_SPEED_FULL;
    g_playgo_lang_mask = 0xFFFFFFFFFFFFFFFFULL;

    printf("[ps4-playgo] scePlayGoInitialize: parsed %u chunks (file_size=%u)\n",
           g_playgo_chunk_count, g_playgo_file_size);
    fflush(stdout);

    return 0;
}

int32_t scePlayGoTerminate(void) {
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    g_playgo_initialized = false;
    if (g_playgo_chunks) {
        free(g_playgo_chunks);
        g_playgo_chunks = NULL;
    }
    g_playgo_chunk_count = 0;
    g_playgo_file_size = 0;
    return 0;
}

int32_t scePlayGoOpen(OrbisPlayGoHandle *handle, const void *params) {
    if (!handle) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (params) {
        return ORBIS_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_playgo_file_size == 0) {
        return ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO;
    }

    *handle = kValidPlayGoHandle;
    return 0;
}

int32_t scePlayGoClose(OrbisPlayGoHandle handle) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    return 0;
}

int32_t scePlayGoGetProgress(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, OrbisPlayGoProgress *outProgress) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds || !outProgress) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_playgo_file_size == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID;
    }

    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t cid = chunkIds[i];
        if (cid < g_playgo_chunk_count) {
            total += g_playgo_chunks ? g_playgo_chunks[cid].total_size : 100000000ULL;
        } else {
            return ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID;
        }
    }

    outProgress->progressSize = total;
    outProgress->totalSize = total;
    return 0;
}

int32_t scePlayGoGetLocus(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, int8_t *outLocus) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds || !outLocus) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_playgo_file_size == 0) {
        return ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (chunkIds[i] < g_playgo_chunk_count) {
            outLocus[i] = (int8_t)ORBIS_PLAYGO_LOCUS_LOCAL_FAST;
        } else {
            outLocus[i] = (int8_t)ORBIS_PLAYGO_LOCUS_NOT_DOWNLOADED;
            return ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID;
        }
    }
    return 0;
}

int32_t scePlayGoGetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask *outMask) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outMask) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }

    *outMask = g_playgo_lang_mask;
    return 0;
}

int32_t scePlayGoSetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask mask) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }

    g_playgo_lang_mask = mask;
    return 0;
}

int32_t scePlayGoGetInstallSpeed(OrbisPlayGoHandle handle, int32_t *outSpeed) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outSpeed) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }

    *outSpeed = (int32_t)g_playgo_speed;
    return 0;
}

int32_t scePlayGoSetInstallSpeed(OrbisPlayGoHandle handle, int32_t speed) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (speed < 0 || speed > 2) {
        return ORBIS_PLAYGO_ERROR_INVALID_ARGUMENT;
    }

    g_playgo_speed = (OrbisPlayGoInstallSpeed)speed;
    return 0;
}

int32_t scePlayGoGetToDoList(OrbisPlayGoHandle handle, OrbisPlayGoToDo *outToDoList, uint32_t maxEntries, uint32_t *outNumEntries) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outNumEntries) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (outToDoList && maxEntries == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }

    *outNumEntries = 0; // nothing to do, all chunks local
    return 0;
}

int32_t scePlayGoGetEta(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count, OrbisPlayGoEta *outEta) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds || !outEta) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }

    for (uint32_t i = 0; i < count; i++) {
        outEta[i] = 0; // all is loaded
    }
    return 0;
}

int32_t scePlayGoPrefetch(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    return 0;
}

int32_t scePlayGoGetChunkId(OrbisPlayGoHandle handle, OrbisPlayGoChunkId *outChunkIdList, uint32_t numberOfEntries, uint32_t *outEntries) {
    if (handle != kValidPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outEntries) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (outChunkIdList && numberOfEntries == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_playgo_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }

    if (g_playgo_file_size == 0) {
        *outEntries = 0;
        return 0;
    }

    if (!outChunkIdList) {
        *outEntries = g_playgo_chunk_count;
        return 0;
    }

    if (numberOfEntries > g_playgo_chunk_count) {
        numberOfEntries = g_playgo_chunk_count;
    }

    for (uint32_t i = 0; i < numberOfEntries; i++) {
        outChunkIdList[i] = (OrbisPlayGoChunkId)i;
    }
    *outEntries = numberOfEntries;
    return 0;
}

// Guest ABI shims
void shim_scePlayGoInitialize(GuestContext *ctx) {
    uint64_t paramsGuest = ctx->rdi;
    const OrbisPlayGoInitParams *params = paramsGuest ? (const OrbisPlayGoInitParams *)(ctx->mem_base + paramsGuest) : NULL;
    int32_t ret = scePlayGoInitialize(params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoTerminate(GuestContext *ctx) {
    int32_t ret = scePlayGoTerminate();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoOpen(GuestContext *ctx) {
    uint64_t handleGuest = ctx->rdi;
    uint64_t paramsGuest = ctx->rsi;
    OrbisPlayGoHandle *handle = handleGuest ? (OrbisPlayGoHandle *)(ctx->mem_base + handleGuest) : NULL;
    const void *params = paramsGuest ? (const void *)(ctx->mem_base + paramsGuest) : NULL;

    int32_t ret = scePlayGoOpen(handle, params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoClose(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    int32_t ret = scePlayGoClose(handle);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetProgress(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;
    uint64_t outProgressGuest = ctx->rcx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;
    OrbisPlayGoProgress *outProgress = outProgressGuest ? (OrbisPlayGoProgress *)(ctx->mem_base + outProgressGuest) : NULL;

    int32_t ret = scePlayGoGetProgress(handle, chunkIds, count, outProgress);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetLocus(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;
    uint64_t outLocusGuest = ctx->rcx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;
    int8_t *outLocus = outLocusGuest ? (int8_t *)(ctx->mem_base + outLocusGuest) : NULL;

    int32_t ret = scePlayGoGetLocus(handle, chunkIds, count, outLocus);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetLanguageMask(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outMaskGuest = ctx->rsi;
    OrbisPlayGoLanguageMask *outMask = outMaskGuest ? (OrbisPlayGoLanguageMask *)(ctx->mem_base + outMaskGuest) : NULL;

    int32_t ret = scePlayGoGetLanguageMask(handle, outMask);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoSetLanguageMask(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    OrbisPlayGoLanguageMask mask = (OrbisPlayGoLanguageMask)ctx->rsi;

    int32_t ret = scePlayGoSetLanguageMask(handle, mask);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetInstallSpeed(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outSpeedGuest = ctx->rsi;
    int32_t *outSpeed = outSpeedGuest ? (int32_t *)(ctx->mem_base + outSpeedGuest) : NULL;

    int32_t ret = scePlayGoGetInstallSpeed(handle, outSpeed);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoSetInstallSpeed(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    int32_t speed = (int32_t)ctx->rsi;

    int32_t ret = scePlayGoSetInstallSpeed(handle, speed);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetToDoList(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outToDoGuest = ctx->rsi;
    uint32_t maxEntries = (uint32_t)ctx->rdx;
    uint64_t outNumGuest = ctx->rcx;

    OrbisPlayGoToDo *outToDo = outToDoGuest ? (OrbisPlayGoToDo *)(ctx->mem_base + outToDoGuest) : NULL;
    uint32_t *outNum = outNumGuest ? (uint32_t *)(ctx->mem_base + outNumGuest) : NULL;

    int32_t ret = scePlayGoGetToDoList(handle, outToDo, maxEntries, outNum);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetEta(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;
    uint64_t outEtaGuest = ctx->rcx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;
    OrbisPlayGoEta *outEta = outEtaGuest ? (OrbisPlayGoEta *)(ctx->mem_base + outEtaGuest) : NULL;

    int32_t ret = scePlayGoGetEta(handle, chunkIds, count, outEta);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoPrefetch(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t chunkIdsGuest = ctx->rsi;
    uint32_t count = (uint32_t)ctx->rdx;

    const OrbisPlayGoChunkId *chunkIds = chunkIdsGuest ? (const OrbisPlayGoChunkId *)(ctx->mem_base + chunkIdsGuest) : NULL;

    int32_t ret = scePlayGoPrefetch(handle, chunkIds, count);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoGetChunkId(GuestContext *ctx) {
    OrbisPlayGoHandle handle = (OrbisPlayGoHandle)ctx->rdi;
    uint64_t outChunkIdListGuest = ctx->rsi;
    uint32_t numberOfEntries = (uint32_t)ctx->rdx;
    uint64_t outEntriesGuest = ctx->rcx;

    OrbisPlayGoChunkId *outChunkIdList = outChunkIdListGuest ? (OrbisPlayGoChunkId *)(ctx->mem_base + outChunkIdListGuest) : NULL;
    uint32_t *outEntries = outEntriesGuest ? (uint32_t *)(ctx->mem_base + outEntriesGuest) : NULL;

    int32_t ret = scePlayGoGetChunkId(handle, outChunkIdList, numberOfEntries, outEntries);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
