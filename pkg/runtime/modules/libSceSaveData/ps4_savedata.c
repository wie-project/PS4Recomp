#include "ps4_savedata.h"
#include "ps4_sfo.h"
#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_SAVE_MEMORY_SLOTS 16
#define STANDARD_DIRNAME_SAVE_DATA_MEMORY "sce_sdmemory"
#define FILENAME_SAVE_DATA_MEMORY "memory.dat"

typedef struct SaveMemorySlot {
    bool active;
    uint32_t slot_id;
    int32_t user_id;
    char folder_path[1024];
    uint8_t *cache;
    size_t cache_size;
    bool dirty;
} SaveMemorySlot;

static int g_savedata_initialized = 0;
static pthread_mutex_t g_savedata_mutex = PTHREAD_MUTEX_INITIALIZER;
static SaveMemorySlot g_slots[MAX_SAVE_MEMORY_SLOTS];

static inline void *recomp_guest_to_host(const GuestContext *ctx, uint64_t gaddr) {
    if (!ctx || !ctx->mem_base || gaddr == 0) {
        return NULL;
    }
    if (gaddr >= (uintptr_t)ctx->mem_base && gaddr < (uintptr_t)ctx->mem_base + ctx->mem_size) {
        return (void *)gaddr;
    }
    if (gaddr < ctx->mem_size) {
        return (void *)(ctx->mem_base + gaddr);
    }
    return NULL;
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[1024];
    char *p = NULL;
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, len + 1);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void sanitize_filename(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.') {
            dst[j++] = (char)c;
        } else if (c == ':' || c == '/') {
            dst[j++] = '-';
        } else {
            dst[j++] = '_';
        }
    }
    dst[j] = '\0';
    // Trim trailing spaces
    while (j > 0 && dst[j - 1] == ' ') {
        dst[--j] = '\0';
    }
}

static void get_savedata_base_dir(char *out_path, size_t out_size) {
    const char *env_dir = getenv("PS4_SAVEDATA_DIR");
    if (env_dir && *env_dir) {
        snprintf(out_path, out_size, "%s", env_dir);
        return;
    }

    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(out_path, out_size, "%s/Library/Application Support/PS4Recomp/saves", home);
    } else {
        snprintf(out_path, out_size, "/tmp/PS4Recomp/saves");
    }
}

static void get_game_save_dir(char *out_path, size_t out_size) {
    char base_dir[1024];
    get_savedata_base_dir(base_dir, sizeof(base_dir));

    char title[128] = {0};
    char title_id[64] = {0};
    char sanitized_title[128] = {0};

    Ps4SfoContext sfo = {0};
    if (ps4_sfo_find_and_load(&sfo)) {
        ps4_sfo_get_string(&sfo, "TITLE", title, sizeof(title));
        ps4_sfo_get_string(&sfo, "TITLE_ID", title_id, sizeof(title_id));
        ps4_sfo_free(&sfo);
    }

    sanitize_filename(title, sanitized_title, sizeof(sanitized_title));

    if (sanitized_title[0] && title_id[0]) {
        snprintf(out_path, out_size, "%s/%s [%s]", base_dir, sanitized_title, title_id);
    } else if (sanitized_title[0]) {
        snprintf(out_path, out_size, "%s/%s", base_dir, sanitized_title);
    } else if (title_id[0]) {
        snprintf(out_path, out_size, "%s/%s", base_dir, title_id);
    } else {
        snprintf(out_path, out_size, "%s/DefaultGame", base_dir);
    }
}

static void get_slot_folder_path(int32_t user_id, uint32_t slot_id, char *out_path, size_t out_size) {
    char game_dir[1024];
    get_game_save_dir(game_dir, sizeof(game_dir));

    char slot_dir_name[64];
    if (slot_id == 0) {
        snprintf(slot_dir_name, sizeof(slot_dir_name), "%s", STANDARD_DIRNAME_SAVE_DATA_MEMORY);
    } else {
        snprintf(slot_dir_name, sizeof(slot_dir_name), "%s_%u", STANDARD_DIRNAME_SAVE_DATA_MEMORY, slot_id);
    }

    snprintf(out_path, out_size, "%s/user_%08x/%s", game_dir, (uint32_t)user_id, slot_dir_name);
}

static SaveMemorySlot *find_or_create_slot(uint32_t slot_id) {
    for (int i = 0; i < MAX_SAVE_MEMORY_SLOTS; i++) {
        if (g_slots[i].active && g_slots[i].slot_id == slot_id) {
            return &g_slots[i];
        }
    }
    for (int i = 0; i < MAX_SAVE_MEMORY_SLOTS; i++) {
        if (!g_slots[i].active) {
            g_slots[i].active = true;
            g_slots[i].slot_id = slot_id;
            return &g_slots[i];
        }
    }
    return NULL;
}

static SaveMemorySlot *find_slot(uint32_t slot_id) {
    for (int i = 0; i < MAX_SAVE_MEMORY_SLOTS; i++) {
        if (g_slots[i].active && g_slots[i].slot_id == slot_id) {
            return &g_slots[i];
        }
    }
    return NULL;
}

int32_t sceSaveDataInitialize(const void *param) {
    (void)param;
    pthread_mutex_lock(&g_savedata_mutex);
    g_savedata_initialized = 1;
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataInitialize2(const void *param) {
    return sceSaveDataInitialize(param);
}

int32_t sceSaveDataInitialize3(const void *param) {
    (void)param;
    pthread_mutex_lock(&g_savedata_mutex);
    g_savedata_initialized = 1;
    char game_dir[1024];
    get_game_save_dir(game_dir, sizeof(game_dir));
    fprintf(stderr, "[ps4-recomp] sceSaveDataInitialize3: SaveData initialized (save root: %s)\n", game_dir);
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataTerminate(void) {
    pthread_mutex_lock(&g_savedata_mutex);
    for (int i = 0; i < MAX_SAVE_MEMORY_SLOTS; i++) {
        if (g_slots[i].active) {
            if (g_slots[i].cache) {
                free(g_slots[i].cache);
                g_slots[i].cache = NULL;
            }
            g_slots[i].active = false;
        }
    }
    g_savedata_initialized = 0;
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSetupSaveDataMemory2(GuestContext *ctx,
                                       const OrbisSaveDataMemorySetup2 *setupParam,
                                       OrbisSaveDataMemorySetupResult *result) {
    (void)ctx;
    pthread_mutex_lock(&g_savedata_mutex);
    if (!g_savedata_initialized) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED;
    }
    if (!setupParam) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_PARAMETER;
    }

    uint32_t slot_id = setupParam->slotId;
    SaveMemorySlot *slot = find_or_create_slot(slot_id);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_INTERNAL;
    }

    slot->user_id = setupParam->userId;
    get_slot_folder_path(setupParam->userId, slot_id, slot->folder_path, sizeof(slot->folder_path));

    if (mkdir_p(slot->folder_path, 0755) != 0) {
        fprintf(stderr, "[ps4-recomp] WARN: Failed to create save directory: %s (errno=%d)\n",
                slot->folder_path, errno);
    }

    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", slot->folder_path, FILENAME_SAVE_DATA_MEMORY);

    size_t existed_size = 0;
    struct stat st;
    if (stat(file_path, &st) == 0 && S_ISREG(st.st_mode)) {
        existed_size = (size_t)st.st_size;
    }

    size_t alloc_size = existed_size > setupParam->memorySize ? existed_size : setupParam->memorySize;
    if (alloc_size == 0) {
        alloc_size = 64 * 1024; // Default minimum 64 KB
    }

    if (slot->cache) {
        free(slot->cache);
        slot->cache = NULL;
    }

    slot->cache = (uint8_t *)calloc(1, alloc_size);
    if (!slot->cache) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_INTERNAL;
    }
    slot->cache_size = alloc_size;
    slot->dirty = false;

    if (existed_size > 0) {
        FILE *f = fopen(file_path, "rb");
        if (f) {
            size_t read_bytes = fread(slot->cache, 1, existed_size, f);
            fclose(f);
            fprintf(stderr, "[ps4-recomp] sceSaveDataSetupSaveDataMemory2: Slot %u loaded %zu bytes from '%s'\n",
                    slot_id, read_bytes, file_path);
        }
    } else {
        fprintf(stderr, "[ps4-recomp] sceSaveDataSetupSaveDataMemory2: Slot %u created fresh at '%s' (allocated: %zu bytes)\n",
                slot_id, file_path, alloc_size);
    }

    if (result) {
        result->existedMemorySize = existed_size;
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataGetSaveDataMemory2(GuestContext *ctx,
                                     OrbisSaveDataMemoryGet2 *getParam) {
    pthread_mutex_lock(&g_savedata_mutex);
    if (!g_savedata_initialized) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED;
    }
    if (!getParam) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_PARAMETER;
    }

    uint32_t slot_id = getParam->slotId;
    SaveMemorySlot *slot = find_slot(slot_id);
    if (!slot || !slot->cache) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY;
    }

    if (getParam->data) {
        OrbisSaveDataMemoryData *data = (OrbisSaveDataMemoryData *)recomp_guest_to_host(ctx, getParam->data);
        if (data && data->buf) {
            void *dst_buf = recomp_guest_to_host(ctx, data->buf);
            if (dst_buf && data->bufSize > 0) {
                int64_t offset = data->offset;
                size_t read_size = data->bufSize;
                if (offset < 0) {
                    offset = 0;
                }
                if ((size_t)offset < slot->cache_size) {
                    if (offset + read_size > slot->cache_size) {
                        read_size = slot->cache_size - (size_t)offset;
                    }
                    memcpy(dst_buf, slot->cache + offset, read_size);
                } else {
                    memset(dst_buf, 0, read_size);
                }
            }
        }
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSetSaveDataMemory2(GuestContext *ctx,
                                     const OrbisSaveDataMemorySet2 *setParam) {
    pthread_mutex_lock(&g_savedata_mutex);
    if (!g_savedata_initialized) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED;
    }
    if (!setParam) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_PARAMETER;
    }

    uint32_t slot_id = setParam->slotId;
    SaveMemorySlot *slot = find_slot(slot_id);
    if (!slot || !slot->cache) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY;
    }

    if (setParam->data) {
        const OrbisSaveDataMemoryData *data = (const OrbisSaveDataMemoryData *)recomp_guest_to_host(ctx, setParam->data);
        uint32_t data_num = setParam->dataNum > 1 ? setParam->dataNum : 1;
        if (data) {
            for (uint32_t i = 0; i < data_num; i++) {
                if (data[i].buf && data[i].bufSize > 0) {
                    const void *src_buf = recomp_guest_to_host(ctx, data[i].buf);
                    int64_t offset = data[i].offset;
                    if (src_buf && offset >= 0) {
                        size_t required_size = (size_t)offset + data[i].bufSize;
                        if (required_size > slot->cache_size) {
                            uint8_t *new_cache = (uint8_t *)realloc(slot->cache, required_size);
                            if (new_cache) {
                                memset(new_cache + slot->cache_size, 0, required_size - slot->cache_size);
                                slot->cache = new_cache;
                                slot->cache_size = required_size;
                            }
                        }
                        if (required_size <= slot->cache_size) {
                            memcpy(slot->cache + offset, src_buf, data[i].bufSize);
                            slot->dirty = true;
                        }
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSyncSaveDataMemory(GuestContext *ctx,
                                     OrbisSaveDataMemorySync *syncParam) {
    (void)ctx;
    pthread_mutex_lock(&g_savedata_mutex);
    if (!g_savedata_initialized) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_NOT_INITIALIZED;
    }
    if (!syncParam) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_PARAMETER;
    }

    uint32_t slot_id = syncParam->slotId;
    SaveMemorySlot *slot = find_slot(slot_id);
    if (!slot || !slot->cache) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY;
    }

    char file_path[1024];
    char tmp_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", slot->folder_path, FILENAME_SAVE_DATA_MEMORY);
    snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", slot->folder_path, FILENAME_SAVE_DATA_MEMORY);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "[ps4-recomp] WARN: Failed to open save file for writing: '%s' (errno=%d)\n",
                tmp_path, errno);
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_INTERNAL;
    }

    size_t written = fwrite(slot->cache, 1, slot->cache_size, f);
    fflush(f);
    int fd = fileno(f);
    if (fd >= 0) {
        fsync(fd);
    }
    fclose(f);

    if (written == slot->cache_size) {
        rename(tmp_path, file_path);
        slot->dirty = false;
        fprintf(stderr, "[ps4-recomp] sceSaveDataSyncSaveDataMemory: Slot %u synced %zu bytes to '%s'\n",
                slot_id, slot->cache_size, file_path);
    } else {
        unlink(tmp_path);
        fprintf(stderr, "[ps4-recomp] WARN: Failed to write complete save data (%zu / %zu bytes)\n",
                written, slot->cache_size);
        pthread_mutex_unlock(&g_savedata_mutex);
        return ORBIS_SAVE_DATA_ERROR_INTERNAL;
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

// Shims
void shim_sceSaveDataInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataInitialize(
        ctx->rdi ? recomp_guest_to_host(ctx, ctx->rdi) : NULL
    );
    SHIM_RETURN();
}

void shim_sceSaveDataInitialize2(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataInitialize2(
        ctx->rdi ? recomp_guest_to_host(ctx, ctx->rdi) : NULL
    );
    SHIM_RETURN();
}

void shim_sceSaveDataInitialize3(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataInitialize3(
        ctx->rdi ? recomp_guest_to_host(ctx, ctx->rdi) : NULL
    );
    SHIM_RETURN();
}

void shim_sceSaveDataTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataTerminate();
    SHIM_RETURN();
}

void shim_sceSaveDataSetupSaveDataMemory2(GuestContext *ctx) {
    const OrbisSaveDataMemorySetup2 *setupParam =
        ctx->rdi ? (const OrbisSaveDataMemorySetup2 *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;
    OrbisSaveDataMemorySetupResult *result =
        ctx->rsi ? (OrbisSaveDataMemorySetupResult *)recomp_guest_to_host(ctx, ctx->rsi) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceSaveDataSetupSaveDataMemory2(ctx, setupParam, result);
    SHIM_RETURN();
}

void shim_sceSaveDataGetSaveDataMemory2(GuestContext *ctx) {
    OrbisSaveDataMemoryGet2 *getParam =
        ctx->rdi ? (OrbisSaveDataMemoryGet2 *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceSaveDataGetSaveDataMemory2(ctx, getParam);
    SHIM_RETURN();
}

void shim_sceSaveDataSetSaveDataMemory2(GuestContext *ctx) {
    const OrbisSaveDataMemorySet2 *setParam =
        ctx->rdi ? (const OrbisSaveDataMemorySet2 *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceSaveDataSetSaveDataMemory2(ctx, setParam);
    SHIM_RETURN();
}

void shim_sceSaveDataSyncSaveDataMemory(GuestContext *ctx) {
    OrbisSaveDataMemorySync *syncParam =
        ctx->rdi ? (OrbisSaveDataMemorySync *)recomp_guest_to_host(ctx, ctx->rdi) : NULL;

    ctx->rax = (uint64_t)(int64_t)sceSaveDataSyncSaveDataMemory(ctx, syncParam);
    SHIM_RETURN();
}
