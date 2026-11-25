// SPDX-License-Identifier: GPL-2.0-or-later

// libScePlayGo, ported from shadPS4 (src/core/libraries/playgo, file_format/playgo_chunk).
// An installed game has every chunk locally, so PlayGo reports all chunks as fully downloaded;
// the chunk table and sizes come from /app0/sce_sys/playgo-chunk.dat. The PlayGo dialog is a
// common-dialog state machine that finishes as soon as it is updated, with the result that lets
// the game proceed.

#include "ps4_playgo.h"
#include "ps4_hle_stub.h"
#include "ps4_system_service.h"
#include "ps4_vfs.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PLAYGO_MAGIC 0x6F676C70U // 'plgo'

// ---- playgo-chunk.dat ----

#pragma pack(push, 1)
typedef struct PlaygoSection {
    uint32_t offset;
    uint32_t length;
} PlaygoSection;

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
    uint8_t reserved[32];
    char content_id[128];
    PlaygoSection chunk_attrs;
    PlaygoSection chunk_mchunks;
    PlaygoSection chunk_labels;
    PlaygoSection mchunk_attrs;
    PlaygoSection scenario_attrs;
    PlaygoSection scenario_chunks;
    PlaygoSection scenario_labels;
    PlaygoSection inner_mchunk_attrs;
} PlaygoHeader;

typedef struct PlaygoChunkAttr {
    uint8_t flag;
    uint8_t image_disc_layer_no;
    uint8_t req_locus;
    uint8_t unk[11];
    uint16_t mchunk_count;
    uint64_t language_mask;
    uint32_t mchunks_offset; // into chunk_mchunks
    uint32_t label_offset;   // into chunk_labels
} PlaygoChunkAttr;

typedef struct PlaygoMchunkAttr {
    uint64_t loc;  // offset:48, image_no at bits 56..59
    uint64_t size; // size:48
} PlaygoMchunkAttr;
#pragma pack(pop)

_Static_assert(sizeof(PlaygoHeader) == 0x100, "PlaygoHeader layout");
_Static_assert(sizeof(PlaygoChunkAttr) == 32, "playgo_chunk_attr_entry_t layout");
_Static_assert(sizeof(PlaygoMchunkAttr) == 16, "playgo_mchunk_attr_entry_t layout");

typedef struct PlaygoChunk {
    uint8_t req_locus;
    uint64_t language_mask;
    uint64_t total_size;
} PlaygoChunk;

static const OrbisPlayGoHandle kPlayGoHandle = 1;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized;
static uint32_t g_file_size; // header file_size: 0 when the game has no PlayGo data
static uint16_t g_chunk_count;
static PlaygoChunk *g_chunks;
static OrbisPlayGoInstallSpeed g_speed = ORBIS_PLAYGO_INSTALL_SPEED_FULL;
static int64_t g_speed_tick; // ms of the last SetInstallSpeed
static OrbisPlayGoLanguageMask g_lang_mask;

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool section_ok(const PlaygoSection *s, size_t size) {
    return (uint64_t)s->offset + s->length <= size;
}

// PlaygoFile::Open. The header counts as read once its magic matches; the chunk list needs the
// chunk sections to be inside the file.
static void parse_playgo(const uint8_t *data, size_t size) {
    if (size < sizeof(PlaygoHeader)) {
        return;
    }
    PlaygoHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != PLAYGO_MAGIC) {
        return;
    }
    g_file_size = hdr.file_size;
    if (!section_ok(&hdr.chunk_attrs, size) || !section_ok(&hdr.chunk_mchunks, size) ||
        !section_ok(&hdr.chunk_labels, size) || !section_ok(&hdr.mchunk_attrs, size) ||
        (uint64_t)hdr.chunk_count * sizeof(PlaygoChunkAttr) > hdr.chunk_attrs.length) {
        fprintf(stderr, "[ps4-playgo] Could not parse PlayGo file\n");
        return;
    }
    g_chunks = calloc(hdr.chunk_count ? hdr.chunk_count : 1, sizeof(PlaygoChunk));
    if (!g_chunks) {
        return;
    }
    g_chunk_count = hdr.chunk_count;
    const uint32_t mchunk_attr_count = hdr.mchunk_attrs.length / sizeof(PlaygoMchunkAttr);
    for (uint16_t i = 0; i < hdr.chunk_count; i++) {
        PlaygoChunkAttr a;
        memcpy(&a, data + hdr.chunk_attrs.offset + (size_t)i * sizeof(a), sizeof(a));
        g_chunks[i].req_locus = a.req_locus;
        g_chunks[i].language_mask = a.language_mask;
        uint64_t total = 0;
        if ((uint64_t)a.mchunks_offset + (uint64_t)a.mchunk_count * 2 <= hdr.chunk_mchunks.length) {
            for (uint16_t j = 0; j < a.mchunk_count; j++) {
                uint16_t id;
                memcpy(&id, data + hdr.chunk_mchunks.offset + a.mchunks_offset + (size_t)j * 2, 2);
                if (id < mchunk_attr_count) {
                    PlaygoMchunkAttr m;
                    memcpy(&m, data + hdr.mchunk_attrs.offset + (size_t)id * sizeof(m), sizeof(m));
                    total += m.size & 0xFFFFFFFFFFFFull;
                }
            }
        }
        g_chunks[i].total_size = total;
    }
}

static void load_playgo_file(void) {
    char path[1024];
    if (ps4_vfs_resolve("/app0/sce_sys/playgo-chunk.dat", path, sizeof(path)) != 0) {
        fprintf(stderr, "[ps4-playgo] Could not open PlayGo file\n");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ps4-playgo] Could not open PlayGo file\n");
        return;
    }
    uint8_t *data = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) == (size_t)size) {
            parse_playgo(data, (size_t)size);
        }
    }
    free(data);
    fclose(f);
}

// scePlayGoConvertLanguage: system language n is bit 63 - n.
static OrbisPlayGoLanguageMask convert_language(int32_t system_lang) {
    return system_lang >= 0 && system_lang < 48 ? 1ull << (63 - system_lang) : 0;
}

int32_t scePlayGoInitialize(const OrbisPlayGoInitParams *params) {
    if (!params || !params->bufAddr) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (params->bufSize < 0x200000) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    pthread_mutex_lock(&g_mutex);
    if (g_initialized) {
        pthread_mutex_unlock(&g_mutex);
        return ORBIS_PLAYGO_ERROR_ALREADY_INITIALIZED;
    }
    g_initialized = true;
    g_file_size = 0;
    g_chunk_count = 0;
    g_chunks = NULL;
    g_speed = ORBIS_PLAYGO_INSTALL_SPEED_FULL;
    g_speed_tick = 0;
    load_playgo_file();
    int32_t lang = 0;
    sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG, &lang);
    g_lang_mask = convert_language(lang);
    fprintf(stderr, "[ps4-playgo] scePlayGoInitialize: %u chunks (file_size=%u)\n", g_chunk_count, g_file_size);
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int32_t scePlayGoTerminate(void) {
    pthread_mutex_lock(&g_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_mutex);
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    g_initialized = false;
    free(g_chunks);
    g_chunks = NULL;
    g_chunk_count = 0;
    g_file_size = 0;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int32_t scePlayGoOpen(OrbisPlayGoHandle *handle, const void *params) {
    if (!handle) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (params) {
        return ORBIS_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_file_size == 0) {
        return ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO;
    }
    *handle = kPlayGoHandle;
    return 0;
}

int32_t scePlayGoClose(OrbisPlayGoHandle handle) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    return 0;
}

int32_t scePlayGoGetChunkId(OrbisPlayGoHandle handle, OrbisPlayGoChunkId *outChunkIdList, uint32_t numberOfEntries,
                            uint32_t *outEntries) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outEntries) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (outChunkIdList && numberOfEntries == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_file_size == 0) {
        *outEntries = 0;
        return 0;
    }
    if (!outChunkIdList) {
        *outEntries = g_chunk_count;
        return 0;
    }
    if (numberOfEntries > g_chunk_count) {
        numberOfEntries = g_chunk_count;
    }
    for (uint32_t i = 0; i < numberOfEntries; i++) {
        outChunkIdList[i] = (OrbisPlayGoChunkId)i;
    }
    *outEntries = numberOfEntries;
    return 0;
}

int32_t scePlayGoGetEta(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count,
                        OrbisPlayGoEta *outEta) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds || !outEta) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    *outEta = 0; // one estimate for all the chunks: everything is installed
    return 0;
}

int32_t scePlayGoGetInstallSpeed(OrbisPlayGoHandle handle, int32_t *outSpeed) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outSpeed) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    pthread_mutex_lock(&g_mutex);
    // A suspended installation resumes at trickle speed after 30 seconds.
    if (g_speed == ORBIS_PLAYGO_INSTALL_SPEED_SUSPENDED && now_ms() - g_speed_tick > 30 * 1000) {
        g_speed = ORBIS_PLAYGO_INSTALL_SPEED_TRICKLE;
    }
    *outSpeed = (int32_t)g_speed;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int32_t scePlayGoSetInstallSpeed(OrbisPlayGoHandle handle, int32_t speed) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (speed < ORBIS_PLAYGO_INSTALL_SPEED_SUSPENDED || speed > ORBIS_PLAYGO_INSTALL_SPEED_FULL) {
        return ORBIS_PLAYGO_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_mutex);
    g_speed = (OrbisPlayGoInstallSpeed)speed;
    g_speed_tick = now_ms();
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int32_t scePlayGoGetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask *outMask) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outMask) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    *outMask = g_lang_mask;
    return 0;
}

int32_t scePlayGoSetLanguageMask(OrbisPlayGoHandle handle, OrbisPlayGoLanguageMask mask) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    g_lang_mask = mask;
    return 0;
}

int32_t scePlayGoGetLocus(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count,
                          int8_t *outLocus) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds || !outLocus) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_file_size == 0) {
        return ORBIS_PLAYGO_ERROR_NOT_SUPPORT_PLAYGO;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (chunkIds[i] >= g_chunk_count) {
            outLocus[i] = ORBIS_PLAYGO_LOCUS_NOT_DOWNLOADED;
            return ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID;
        }
        outLocus[i] = ORBIS_PLAYGO_LOCUS_LOCAL_FAST;
    }
    return 0;
}

int32_t scePlayGoGetProgress(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count,
                             OrbisPlayGoProgress *outProgress) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds || !outProgress) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    if (g_file_size == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID;
    }
    outProgress->progressSize = 0;
    outProgress->totalSize = 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (chunkIds[i] >= g_chunk_count) {
            return ORBIS_PLAYGO_ERROR_BAD_CHUNK_ID;
        }
        total += g_chunks[chunkIds[i]].total_size;
    }
    outProgress->progressSize = total;
    outProgress->totalSize = total;
    return 0;
}

int32_t scePlayGoGetToDoList(OrbisPlayGoHandle handle, OrbisPlayGoToDo *outToDoList, uint32_t numberOfEntries,
                             uint32_t *outEntries) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!outToDoList || !outEntries) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (numberOfEntries == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    *outEntries = 0; // nothing left to download
    return 0;
}

int32_t scePlayGoSetToDoList(OrbisPlayGoHandle handle, const OrbisPlayGoToDo *todoList, uint32_t numberOfEntries) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!todoList) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (numberOfEntries == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    return 0; // every chunk is installed already
}

int32_t scePlayGoPrefetch(OrbisPlayGoHandle handle, const OrbisPlayGoChunkId *chunkIds, uint32_t count,
                          int32_t minimumLocus) {
    if (handle != kPlayGoHandle) {
        return ORBIS_PLAYGO_ERROR_BAD_HANDLE;
    }
    if (!chunkIds) {
        return ORBIS_PLAYGO_ERROR_BAD_POINTER;
    }
    if (count == 0) {
        return ORBIS_PLAYGO_ERROR_BAD_SIZE;
    }
    if (!g_initialized) {
        return ORBIS_PLAYGO_ERROR_NOT_INITIALIZED;
    }
    switch (minimumLocus) {
    case ORBIS_PLAYGO_LOCUS_NOT_DOWNLOADED:
    case ORBIS_PLAYGO_LOCUS_LOCAL_SLOW:
    case ORBIS_PLAYGO_LOCUS_LOCAL_FAST:
        return 0;
    default:
        return ORBIS_PLAYGO_ERROR_BAD_LOCUS;
    }
}

// ---- PlayGo dialog ----

static pthread_mutex_t g_playgo_dialog_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_playgo_dialog_status = 0; // COMMON_DIALOG_STATUS_NONE

int32_t scePlayGoDialogInitialize(void) {
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    int32_t rc = 0;
    if (g_playgo_dialog_status != 0) {
        rc = (int32_t)0x80B80004; // COMMON_DIALOG_ERROR_ALREADY_INITIALIZED
    } else {
        g_playgo_dialog_status = 1; // COMMON_DIALOG_STATUS_INITIALIZED
    }
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    return rc;
}

int32_t scePlayGoDialogTerminate(void) {
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    int32_t rc = 0;
    if (g_playgo_dialog_status == 0) {
        rc = (int32_t)0x80B80003; // COMMON_DIALOG_ERROR_NOT_INITIALIZED
    } else {
        g_playgo_dialog_status = 0;
    }
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    return rc;
}

int32_t scePlayGoDialogOpen(const void *param) {
    if (!param) {
        return (int32_t)0x80B8000D; // COMMON_DIALOG_ERROR_ARG_NULL
    }
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    int32_t rc = 0;
    if (g_playgo_dialog_status == 1 || g_playgo_dialog_status == 3) {
        g_playgo_dialog_status = 2; // COMMON_DIALOG_STATUS_RUNNING
    } else if (g_playgo_dialog_status == 0) {
        rc = (int32_t)0x80B80003; // COMMON_DIALOG_ERROR_NOT_INITIALIZED
    } else {
        rc = (int32_t)0x80B80006; // COMMON_DIALOG_ERROR_INVALID_STATE
    }
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    return rc;
}

int32_t scePlayGoDialogClose(void) {
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    int32_t rc = 0;
    if (g_playgo_dialog_status == 0) {
        rc = (int32_t)0x80B80003; // COMMON_DIALOG_ERROR_NOT_INITIALIZED
    } else if (g_playgo_dialog_status != 2) {
        rc = (int32_t)0x80B80007; // COMMON_DIALOG_ERROR_NOT_RUNNING
    } else {
        g_playgo_dialog_status = 3; // COMMON_DIALOG_STATUS_FINISHED
    }
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    return rc;
}

int32_t scePlayGoDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    if (g_playgo_dialog_status == 2) { // RUNNING -> FINISHED
        g_playgo_dialog_status = 3;
    }
    int32_t st = g_playgo_dialog_status;
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    return st;
}

int32_t scePlayGoDialogGetStatus(void) {
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    int32_t st = g_playgo_dialog_status;
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    return st;
}

int32_t scePlayGoDialogGetResult(const void *result) {
    if (!result) {
        return (int32_t)0x80B8000D; // COMMON_DIALOG_ERROR_ARG_NULL
    }
    pthread_mutex_lock(&g_playgo_dialog_mutex);
    int32_t st = g_playgo_dialog_status;
    pthread_mutex_unlock(&g_playgo_dialog_mutex);
    if (st == 0) {
        return (int32_t)0x80B80003; // COMMON_DIALOG_ERROR_NOT_INITIALIZED
    }
    if (st != 3) {
        return (int32_t)0x80B80005; // COMMON_DIALOG_ERROR_NOT_FINISHED
    }
    int32_t *res = (int32_t *)result;
    res[0] = 0;
    res[1] = 3; // allowed to proceed
    return 0;
}


// ---- Guest ABI ----

#define GPTR(type, addr) ((addr) ? (type)(ctx->mem_base + (addr)) : (type)NULL)
#define RET(v)                                      \
    do {                                            \
        ctx->rax = (uint64_t)(int64_t)(int32_t)(v); \
        SHIM_RETURN();                              \
    } while (0)

void shim_scePlayGoInitialize(GuestContext *ctx) {
    RET(scePlayGoInitialize(GPTR(const OrbisPlayGoInitParams *, ctx->rdi)));
}

void shim_scePlayGoTerminate(GuestContext *ctx) {
    RET(scePlayGoTerminate());
}

void shim_scePlayGoOpen(GuestContext *ctx) {
    RET(scePlayGoOpen(GPTR(OrbisPlayGoHandle *, ctx->rdi), GPTR(const void *, ctx->rsi)));
}

void shim_scePlayGoClose(GuestContext *ctx) {
    RET(scePlayGoClose((OrbisPlayGoHandle)ctx->rdi));
}

void shim_scePlayGoGetChunkId(GuestContext *ctx) {
    RET(scePlayGoGetChunkId((OrbisPlayGoHandle)ctx->rdi, GPTR(OrbisPlayGoChunkId *, ctx->rsi), (uint32_t)ctx->rdx,
                            GPTR(uint32_t *, ctx->rcx)));
}

void shim_scePlayGoGetEta(GuestContext *ctx) {
    RET(scePlayGoGetEta((OrbisPlayGoHandle)ctx->rdi, GPTR(const OrbisPlayGoChunkId *, ctx->rsi), (uint32_t)ctx->rdx,
                        GPTR(OrbisPlayGoEta *, ctx->rcx)));
}

void shim_scePlayGoGetInstallSpeed(GuestContext *ctx) {
    RET(scePlayGoGetInstallSpeed((OrbisPlayGoHandle)ctx->rdi, GPTR(int32_t *, ctx->rsi)));
}

void shim_scePlayGoSetInstallSpeed(GuestContext *ctx) {
    RET(scePlayGoSetInstallSpeed((OrbisPlayGoHandle)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_scePlayGoGetLanguageMask(GuestContext *ctx) {
    RET(scePlayGoGetLanguageMask((OrbisPlayGoHandle)ctx->rdi, GPTR(OrbisPlayGoLanguageMask *, ctx->rsi)));
}

void shim_scePlayGoSetLanguageMask(GuestContext *ctx) {
    RET(scePlayGoSetLanguageMask((OrbisPlayGoHandle)ctx->rdi, (OrbisPlayGoLanguageMask)ctx->rsi));
}

void shim_scePlayGoGetLocus(GuestContext *ctx) {
    RET(scePlayGoGetLocus((OrbisPlayGoHandle)ctx->rdi, GPTR(const OrbisPlayGoChunkId *, ctx->rsi), (uint32_t)ctx->rdx,
                          GPTR(int8_t *, ctx->rcx)));
}

void shim_scePlayGoGetProgress(GuestContext *ctx) {
    RET(scePlayGoGetProgress((OrbisPlayGoHandle)ctx->rdi, GPTR(const OrbisPlayGoChunkId *, ctx->rsi),
                             (uint32_t)ctx->rdx, GPTR(OrbisPlayGoProgress *, ctx->rcx)));
}

void shim_scePlayGoGetToDoList(GuestContext *ctx) {
    RET(scePlayGoGetToDoList((OrbisPlayGoHandle)ctx->rdi, GPTR(OrbisPlayGoToDo *, ctx->rsi), (uint32_t)ctx->rdx,
                             GPTR(uint32_t *, ctx->rcx)));
}

void shim_scePlayGoSetToDoList(GuestContext *ctx) {
    RET(scePlayGoSetToDoList((OrbisPlayGoHandle)ctx->rdi, GPTR(const OrbisPlayGoToDo *, ctx->rsi),
                             (uint32_t)ctx->rdx));
}

void shim_scePlayGoPrefetch(GuestContext *ctx) {
    RET(scePlayGoPrefetch((OrbisPlayGoHandle)ctx->rdi, GPTR(const OrbisPlayGoChunkId *, ctx->rsi), (uint32_t)ctx->rdx,
                          (int32_t)(int8_t)ctx->rcx));
}

// libSceDbgPlayGo: stubs in shadPS4 as well.
PS4_HLE_STUB(sceDbgPlayGoRequestNextChunk)
PS4_HLE_STUB(sceDbgPlayGoSnapshot)

void shim_scePlayGoDialogInitialize(GuestContext *ctx) {
    int32_t ret = scePlayGoDialogInitialize();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoDialogOpen(GuestContext *ctx) {
    uint64_t paramGuest = ctx->rdi;
    const void *param = paramGuest ? (const void *)(ctx->mem_base + paramGuest) : NULL;
    int32_t ret = scePlayGoDialogOpen(param);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoDialogUpdateStatus(GuestContext *ctx) {
    int32_t ret = scePlayGoDialogUpdateStatus();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoDialogGetResult(GuestContext *ctx) {
    uint64_t resultGuest = ctx->rdi;
    void *result = resultGuest ? (void *)(ctx->mem_base + resultGuest) : NULL;
    int32_t ret = scePlayGoDialogGetResult(result);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoDialogTerminate(GuestContext *ctx) {
    int32_t ret = scePlayGoDialogTerminate();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoDialogClose(GuestContext *ctx) {
    int32_t ret = scePlayGoDialogClose();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_scePlayGoDialogGetStatus(GuestContext *ctx) {
    int32_t ret = scePlayGoDialogGetStatus();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
