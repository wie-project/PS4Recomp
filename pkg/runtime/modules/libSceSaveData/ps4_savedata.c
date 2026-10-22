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

    const char *data_dir = getenv("PS4RECOMP_DATA_DIR");
    if (data_dir && *data_dir) {
        snprintf(out_path, out_size, "%s/saves", data_dir);
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

//=============================================================================
// Directory-based save data: sceSaveDataMount2 / Umount / DirNameSearch /
// SetParam / SaveIcon / Delete.
//
// Save directories live under the same host root as the SaveDataMemory slots:
//   <save root>/<Title [TITLEID]>/user_<uid>/<dirName>/
// (default save root: ~/Library/Application Support/PS4Recomp/saves, override
// with PS4_SAVEDATA_DIR). Never inside the game folder. A mounted directory is
// exposed to the guest through the VFS as "/savedataN".
// Struct layouts and error codes follow shadPS4 (core/libraries/save_data).
//=============================================================================
#include <dirent.h>
#include <ftw.h>
#include <time.h>

#define SD_ERR_PARAMETER     ((int32_t)0x809F0000)
#define SD_ERR_BUSY          ((int32_t)0x809F0003)
#define SD_ERR_NOT_MOUNTED   ((int32_t)0x809F0004)
#define SD_ERR_EXISTS        ((int32_t)0x809F0007)
#define SD_ERR_NOT_FOUND     ((int32_t)0x809F0008)
#define SD_ERR_INTERNAL      ((int32_t)0x809F000F)
#define SD_ERR_MOUNT_FULL    ((int32_t)0x809F0010)

#define SD_MOUNT_MODE_RDONLY  0x01u
#define SD_MOUNT_MODE_RDWR    0x02u
#define SD_MOUNT_MODE_CREATE  0x04u
#define SD_MOUNT_MODE_CREATE2 0x20u

#define SD_MOUNT_STATUS_CREATED 1u

#define SD_DIRNAME_SIZE   32
#define SD_MOUNTPT_SIZE   16
#define SD_MAX_MOUNTS     16
#define SD_BLOCK_SIZE     32768ULL
#define SD_DEFAULT_BLOCKS 32768ULL /* 1 GiB */
#define SD_PARAM_FILE     "sce_sys/param.bin"
#define SD_ICON_FILE      "sce_sys/icon0.png"

typedef struct SdDirName { char data[SD_DIRNAME_SIZE]; } SdDirName;
typedef struct SdMountPoint { char data[SD_MOUNTPT_SIZE]; } SdMountPoint;

typedef struct SdMount2 {
    int32_t userId;
    int32_t _pad0;
    uint64_t dirName;      // const SdDirName*
    uint64_t blocks;
    uint32_t mountMode;    // offset 0x18 (no systemBlocks field in SceSaveDataMount2)
    uint8_t _reserved[32];
    int32_t _pad1;
} SdMount2;                // 64 bytes

typedef struct SdMountResult {
    SdMountPoint mountPoint;
    uint64_t requiredBlocks;
    uint32_t _unused;
    uint32_t mountStatus;
    uint8_t _reserved[28];
    int32_t _pad0;
} SdMountResult;           // 64 bytes

typedef struct SdParam {
    char title[128];
    char subTitle[128];
    char detail[1024];
    uint32_t userParam;
    int32_t _pad0;
    int64_t mtime;
    uint8_t _reserved[32];
} SdParam;                 // 1328 bytes

typedef struct SdIcon {
    uint64_t buf;
    uint64_t bufSize;
    uint64_t dataSize;
    uint8_t _reserved[32];
} SdIcon;

typedef struct SdDelete {
    int32_t userId;
    int32_t _pad0;
    uint64_t titleId;
    uint64_t dirName;
    uint32_t _unused;
    uint8_t _reserved[32];
    int32_t _pad1;
} SdDelete;

typedef struct SdDirNameSearchCond {
    int32_t userId;
    int32_t _pad0;
    uint64_t titleId;
    uint64_t dirName;      // pattern, '%' = any run, '_' = any char
    uint32_t key;
    uint32_t order;
    uint8_t _reserved[32];
} SdDirNameSearchCond;

typedef struct SdDirNameSearchResult {
    uint32_t hitNum;
    int32_t _pad0;
    uint64_t dirNames;     // SdDirName*
    uint32_t dirNamesNum;
    uint32_t setNum;       // SDK >= 1.7
    uint64_t params;       // SdParam*          (SDK >= 1.7)
    uint64_t infos;        // SdSearchInfo*     (SDK >= 2.5)
    uint8_t _reserved[12];
    int32_t _pad1;
} SdDirNameSearchResult;

typedef struct SdSearchInfo {
    uint64_t blocks;
    uint64_t freeBlocks;
    uint8_t _reserved[32];
} SdSearchInfo;

enum { SD_PARAM_ALL = 0, SD_PARAM_TITLE = 1, SD_PARAM_SUB_TITLE = 2,
       SD_PARAM_DETAIL = 3, SD_PARAM_USER_PARAM = 4, SD_PARAM_MTIME = 5 };
enum { SD_SORT_DIRNAME = 0, SD_SORT_USER_PARAM = 1, SD_SORT_BLOCKS = 2,
       SD_SORT_MTIME = 3, SD_SORT_FREE_BLOCKS = 5 };

typedef struct SdMountSlot {
    bool active;
    int32_t user_id;
    char dir_name[SD_DIRNAME_SIZE];
    char mount_point[SD_MOUNTPT_SIZE];
    char host_path[1024];
} SdMountSlot;

static SdMountSlot g_sd_mounts[SD_MAX_MOUNTS];

#define SD_LOG_ONCE(msg)                                                       \
    do {                                                                       \
        static int warned_;                                                    \
        if (!warned_) {                                                        \
            warned_ = 1;                                                       \
            fprintf(stderr, "[ps4-savedata] %s\n", msg);                       \
        }                                                                      \
    } while (0)

static bool sd_copy_dirname(const GuestContext *ctx, uint64_t gaddr, char out[SD_DIRNAME_SIZE]) {
    const char *src = (const char *)recomp_guest_to_host(ctx, gaddr);
    if (!src) return false;
    size_t n = strnlen(src, SD_DIRNAME_SIZE - 1);
    if (n == 0) return false;
    memcpy(out, src, n);
    out[n] = '\0';
    if (strchr(out, '/') || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) return false;
    return true;
}

static void sd_user_dir(int32_t user_id, char *out, size_t out_size) {
    char game_dir[1024];
    get_game_save_dir(game_dir, sizeof(game_dir));
    snprintf(out, out_size, "%s/user_%08x", game_dir, (uint32_t)user_id);
}

static void sd_dir_path(int32_t user_id, const char *dir_name, char *out, size_t out_size) {
    char user_dir[1024];
    sd_user_dir(user_id, user_dir, sizeof(user_dir));
    snprintf(out, out_size, "%s/%s", user_dir, dir_name);
}

static bool sd_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static SdMountSlot *sd_find_mount_by_point(const GuestContext *ctx, uint64_t gaddr) {
    const char *mp = (const char *)recomp_guest_to_host(ctx, gaddr);
    if (!mp) return NULL;
    char name[SD_MOUNTPT_SIZE];
    size_t n = strnlen(mp, SD_MOUNTPT_SIZE - 1);
    memcpy(name, mp, n);
    name[n] = '\0';
    for (int i = 0; i < SD_MAX_MOUNTS; i++) {
        if (g_sd_mounts[i].active && strcmp(g_sd_mounts[i].mount_point, name) == 0) {
            return &g_sd_mounts[i];
        }
    }
    return NULL;
}

static SdMountSlot *sd_find_mount_by_dir(int32_t user_id, const char *dir_name) {
    for (int i = 0; i < SD_MAX_MOUNTS; i++) {
        if (g_sd_mounts[i].active && g_sd_mounts[i].user_id == user_id &&
            strcmp(g_sd_mounts[i].dir_name, dir_name) == 0) {
            return &g_sd_mounts[i];
        }
    }
    return NULL;
}

static bool sd_load_param(const char *dir_path, SdParam *out) {
    memset(out, 0, sizeof(*out));
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", dir_path, SD_PARAM_FILE);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t got = fread(out, 1, sizeof(*out), f);
    fclose(f);
    if (got != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

static int sd_store_param(const char *dir_path, const SdParam *param) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/sce_sys", dir_path);
    mkdir_p(path, 0755);
    snprintf(path, sizeof(path), "%s/%s", dir_path, SD_PARAM_FILE);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t put = fwrite(param, 1, sizeof(*param), f);
    fclose(f);
    return put == sizeof(*param) ? 0 : -1;
}

static uint64_t g_sd_du_total;
static int sd_du_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)path; (void)ftw;
    if (flag == FTW_F) g_sd_du_total += (uint64_t)st->st_size;
    return 0;
}

static uint64_t sd_dir_used_blocks(const char *dir_path) {
    g_sd_du_total = 0;
    nftw(dir_path, sd_du_cb, 16, FTW_PHYS);
    return (g_sd_du_total + SD_BLOCK_SIZE - 1) / SD_BLOCK_SIZE;
}

_Static_assert(sizeof(SdParam) == sizeof(Ps4SaveDataParam), "param layouts differ");

static int64_t g_sd_du_newest;
static int sd_du_mtime_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)path; (void)ftw;
    if (flag == FTW_F) {
        g_sd_du_total += (uint64_t)st->st_size;
        if ((int64_t)st->st_mtime > g_sd_du_newest) g_sd_du_newest = (int64_t)st->st_mtime;
    }
    return 0;
}

bool ps4_savedata_read_dir_info(int32_t user_id, const char *dir_name,
                                Ps4SaveDataParam *out_param, uint64_t *out_bytes) {
    if (!dir_name || !*dir_name || strchr(dir_name, '/') || strlen(dir_name) >= SD_DIRNAME_SIZE)
        return false;
    pthread_mutex_lock(&g_savedata_mutex);
    char path[1024];
    sd_dir_path(user_id, dir_name, path, sizeof(path));
    if (!sd_is_dir(path)) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return false;
    }
    SdParam p;
    sd_load_param(path, &p);
    g_sd_du_total = 0;
    g_sd_du_newest = 0;
    nftw(path, sd_du_mtime_cb, 16, FTW_PHYS);
    if (p.mtime == 0) p.mtime = g_sd_du_newest;
    if (out_param) memcpy(out_param, &p, sizeof(p));
    if (out_bytes) *out_bytes = g_sd_du_total;
    pthread_mutex_unlock(&g_savedata_mutex);
    return true;
}

static int sd_rm_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)st; (void)ftw;
    if (flag == FTW_DP || flag == FTW_D) return rmdir(path);
    return unlink(path);
}

// SQL-LIKE style match: '%' matches any run of characters, '_' matches one.
static bool sd_match(const char *pat, const char *s) {
    if (*pat == '\0') return *s == '\0';
    if (*pat == '%') {
        for (;;) {
            if (sd_match(pat + 1, s)) return true;
            if (*s == '\0') return false;
            s++;
        }
    }
    if (*s == '\0') return false;
    if (*pat == '_' || *pat == *s) return sd_match(pat + 1, s + 1);
    return false;
}

int32_t sceSaveDataMount2(GuestContext *ctx, const SdMount2 *mount, SdMountResult *result) {
    if (!mount || !result) return SD_ERR_PARAMETER;
    char dir_name[SD_DIRNAME_SIZE];
    if (!sd_copy_dirname(ctx, mount->dirName, dir_name)) return SD_ERR_PARAMETER;

    uint32_t mode = mount->mountMode;
    bool create = (mode & (SD_MOUNT_MODE_CREATE | SD_MOUNT_MODE_CREATE2)) != 0;

    pthread_mutex_lock(&g_savedata_mutex);
    if (sd_find_mount_by_dir(mount->userId, dir_name)) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_BUSY;
    }

    char host_path[1024];
    sd_dir_path(mount->userId, dir_name, host_path, sizeof(host_path));
    bool exists = sd_is_dir(host_path);
    uint32_t status = 0;

    if (!exists) {
        if (!create) {
            pthread_mutex_unlock(&g_savedata_mutex);
            fprintf(stderr, "[ps4-savedata] Mount2: user=%d dir='%s' mode=0x%x -> NOT_FOUND\n",
                    mount->userId, dir_name, mode);
            return SD_ERR_NOT_FOUND;
        }
        char sys_path[1100];
        snprintf(sys_path, sizeof(sys_path), "%s/sce_sys", host_path);
        if (mkdir_p(sys_path, 0755) != 0) {
            fprintf(stderr, "[ps4-savedata] Mount2: cannot create '%s' (errno=%d)\n", host_path, errno);
            pthread_mutex_unlock(&g_savedata_mutex);
            return SD_ERR_INTERNAL;
        }
        status = SD_MOUNT_STATUS_CREATED;
    } else if ((mode & SD_MOUNT_MODE_CREATE) && !(mode & SD_MOUNT_MODE_CREATE2)) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_EXISTS;
    }

    int slot_idx = -1;
    for (int i = 0; i < SD_MAX_MOUNTS; i++) {
        if (!g_sd_mounts[i].active) { slot_idx = i; break; }
    }
    if (slot_idx < 0) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_MOUNT_FULL;
    }

    SdMountSlot *slot = &g_sd_mounts[slot_idx];
    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->user_id = mount->userId;
    snprintf(slot->dir_name, sizeof(slot->dir_name), "%s", dir_name);
    snprintf(slot->mount_point, sizeof(slot->mount_point), "/savedata%d", slot_idx);
    snprintf(slot->host_path, sizeof(slot->host_path), "%s", host_path);
    ps4_vfs_mount(slot->mount_point, slot->host_path);

    memset(result, 0, sizeof(*result));
    snprintf(result->mountPoint.data, sizeof(result->mountPoint.data), "%s", slot->mount_point);
    result->requiredBlocks = 0;
    result->mountStatus = status;
    pthread_mutex_unlock(&g_savedata_mutex);

    fprintf(stderr, "[ps4-savedata] Mount2: user=%d dir='%s' mode=0x%x -> %s (%s)%s\n",
            mount->userId, dir_name, mode, slot->mount_point, host_path,
            status == SD_MOUNT_STATUS_CREATED ? " [created]" : "");
    return 0;
}

int32_t sceSaveDataUmount(GuestContext *ctx, uint64_t mount_point_addr) {
    if (!mount_point_addr) return SD_ERR_PARAMETER;
    pthread_mutex_lock(&g_savedata_mutex);
    SdMountSlot *slot = sd_find_mount_by_point(ctx, mount_point_addr);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_MOUNTED;
    }
    ps4_vfs_unmount(slot->mount_point);
    fprintf(stderr, "[ps4-savedata] Umount: %s (dir='%s')\n", slot->mount_point, slot->dir_name);
    memset(slot, 0, sizeof(*slot));
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSetParam(GuestContext *ctx, uint64_t mount_point_addr, uint32_t param_type,
                            uint64_t buf_addr, uint64_t buf_size) {
    if (!mount_point_addr) return SD_ERR_PARAMETER;
    const uint8_t *buf = (const uint8_t *)recomp_guest_to_host(ctx, buf_addr);
    if (!buf) return SD_ERR_PARAMETER;

    pthread_mutex_lock(&g_savedata_mutex);
    SdMountSlot *slot = sd_find_mount_by_point(ctx, mount_point_addr);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_MOUNTED;
    }
    SdParam p;
    sd_load_param(slot->host_path, &p);
    size_t n;
    switch (param_type) {
    case SD_PARAM_ALL:
        n = buf_size < sizeof(p) ? (size_t)buf_size : sizeof(p);
        memcpy(&p, buf, n);
        p.title[sizeof(p.title) - 1] = '\0';
        p.subTitle[sizeof(p.subTitle) - 1] = '\0';
        p.detail[sizeof(p.detail) - 1] = '\0';
        p.mtime = (int64_t)time(NULL);
        break;
    case SD_PARAM_TITLE:
        n = buf_size < sizeof(p.title) - 1 ? (size_t)buf_size : sizeof(p.title) - 1;
        memset(p.title, 0, sizeof(p.title));
        memcpy(p.title, buf, strnlen((const char *)buf, n));
        break;
    case SD_PARAM_SUB_TITLE:
        n = buf_size < sizeof(p.subTitle) - 1 ? (size_t)buf_size : sizeof(p.subTitle) - 1;
        memset(p.subTitle, 0, sizeof(p.subTitle));
        memcpy(p.subTitle, buf, strnlen((const char *)buf, n));
        break;
    case SD_PARAM_DETAIL:
        n = buf_size < sizeof(p.detail) - 1 ? (size_t)buf_size : sizeof(p.detail) - 1;
        memset(p.detail, 0, sizeof(p.detail));
        memcpy(p.detail, buf, strnlen((const char *)buf, n));
        break;
    case SD_PARAM_USER_PARAM:
        if (buf_size < sizeof(uint32_t)) {
            pthread_mutex_unlock(&g_savedata_mutex);
            return SD_ERR_PARAMETER;
        }
        memcpy(&p.userParam, buf, sizeof(uint32_t));
        break;
    case SD_PARAM_MTIME:
        if (buf_size < sizeof(int64_t)) {
            pthread_mutex_unlock(&g_savedata_mutex);
            return SD_ERR_PARAMETER;
        }
        memcpy(&p.mtime, buf, sizeof(int64_t));
        break;
    default:
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_PARAMETER;
    }
    if (p.mtime == 0) p.mtime = (int64_t)time(NULL);
    int rc = sd_store_param(slot->host_path, &p);
    pthread_mutex_unlock(&g_savedata_mutex);
    SD_LOG_ONCE("sceSaveDataSetParam: params stored in sce_sys/param.bin");
    return rc == 0 ? 0 : SD_ERR_INTERNAL;
}

int32_t sceSaveDataSaveIcon(GuestContext *ctx, uint64_t mount_point_addr, const SdIcon *icon) {
    if (!mount_point_addr || !icon) return SD_ERR_PARAMETER;
    const void *data = recomp_guest_to_host(ctx, icon->buf);
    uint64_t size = icon->dataSize ? icon->dataSize : icon->bufSize;
    if (!data || size == 0) return SD_ERR_PARAMETER;
    if (icon->bufSize && size > icon->bufSize) size = icon->bufSize;

    pthread_mutex_lock(&g_savedata_mutex);
    SdMountSlot *slot = sd_find_mount_by_point(ctx, mount_point_addr);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_MOUNTED;
    }
    char path[1200];
    snprintf(path, sizeof(path), "%s/sce_sys", slot->host_path);
    mkdir_p(path, 0755);
    snprintf(path, sizeof(path), "%s/%s", slot->host_path, SD_ICON_FILE);
    FILE *f = fopen(path, "wb");
    size_t put = 0;
    if (f) {
        put = fwrite(data, 1, (size_t)size, f);
        fclose(f);
    }
    pthread_mutex_unlock(&g_savedata_mutex);
    SD_LOG_ONCE("sceSaveDataSaveIcon: icon stored in sce_sys/icon0.png");
    return (f && put == size) ? 0 : SD_ERR_INTERNAL;
}

int32_t sceSaveDataDelete(GuestContext *ctx, const SdDelete *del) {
    if (!del) return SD_ERR_PARAMETER;
    char dir_name[SD_DIRNAME_SIZE];
    if (!sd_copy_dirname(ctx, del->dirName, dir_name)) return SD_ERR_PARAMETER;

    pthread_mutex_lock(&g_savedata_mutex);
    if (sd_find_mount_by_dir(del->userId, dir_name)) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_BUSY;
    }
    char host_path[1024];
    sd_dir_path(del->userId, dir_name, host_path, sizeof(host_path));
    if (!sd_is_dir(host_path)) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_FOUND;
    }
    int rc = nftw(host_path, sd_rm_cb, 16, FTW_DEPTH | FTW_PHYS);
    pthread_mutex_unlock(&g_savedata_mutex);
    fprintf(stderr, "[ps4-savedata] Delete: user=%d dir='%s' -> %s\n", del->userId, dir_name,
            rc == 0 ? "ok" : "failed");
    return rc == 0 ? 0 : SD_ERR_INTERNAL;
}

typedef struct SdSearchEntry {
    char name[SD_DIRNAME_SIZE];
    SdParam param;
    uint64_t used_blocks;
} SdSearchEntry;

static uint32_t g_sd_sort_key;
static uint32_t g_sd_sort_order;

static int sd_entry_cmp(const void *a, const void *b) {
    const SdSearchEntry *x = (const SdSearchEntry *)a;
    const SdSearchEntry *y = (const SdSearchEntry *)b;
    int r = 0;
    switch (g_sd_sort_key) {
    case SD_SORT_USER_PARAM:
        r = (x->param.userParam > y->param.userParam) - (x->param.userParam < y->param.userParam);
        break;
    case SD_SORT_BLOCKS:
    case SD_SORT_FREE_BLOCKS: {
        // All dirs have the same total size, so free blocks sort inversely to used blocks.
        int c = (x->used_blocks > y->used_blocks) - (x->used_blocks < y->used_blocks);
        r = (g_sd_sort_key == SD_SORT_FREE_BLOCKS) ? -c : 0;
        break;
    }
    case SD_SORT_MTIME:
        r = (x->param.mtime > y->param.mtime) - (x->param.mtime < y->param.mtime);
        break;
    default:
        break;
    }
    if (r == 0) r = strcmp(x->name, y->name);
    return g_sd_sort_order == 1 ? -r : r;
}

static bool sd_guest_ptr_ok(const GuestContext *ctx, uint64_t gaddr, size_t bytes) {
    return gaddr != 0 && (gaddr & 3) == 0 && gaddr < ctx->mem_size && bytes <= ctx->mem_size - gaddr;
}

int32_t sceSaveDataDirNameSearch(GuestContext *ctx, const SdDirNameSearchCond *cond,
                                 SdDirNameSearchResult *result) {
    if (!cond || !result) return SD_ERR_PARAMETER;

    char pattern[SD_DIRNAME_SIZE] = "%";
    if (cond->dirName) {
        const char *p = (const char *)recomp_guest_to_host(ctx, cond->dirName);
        if (p && *p) {
            size_t n = strnlen(p, SD_DIRNAME_SIZE - 1);
            memcpy(pattern, p, n);
            pattern[n] = '\0';
        }
    }

    pthread_mutex_lock(&g_savedata_mutex);
    char user_dir[1024];
    sd_user_dir(cond->userId, user_dir, sizeof(user_dir));

    SdSearchEntry *entries = NULL;
    size_t count = 0, cap = 0;
    DIR *d = opendir(user_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *name = de->d_name;
            if (name[0] == '.' || strncmp(name, "sce_", 4) == 0) continue; // skip SaveDataMemory dirs
            if (strlen(name) >= SD_DIRNAME_SIZE) continue;
            if (!sd_match(pattern, name)) continue;
            char path[1100];
            snprintf(path, sizeof(path), "%s/%s", user_dir, name);
            struct stat st;
            if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            if (count == cap) {
                size_t ncap = cap ? cap * 2 : 16;
                SdSearchEntry *ne = (SdSearchEntry *)realloc(entries, ncap * sizeof(*ne));
                if (!ne) break;
                entries = ne;
                cap = ncap;
            }
            SdSearchEntry *e = &entries[count++];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", name);
            if (!sd_load_param(path, &e->param)) {
                e->param.mtime = (int64_t)st.st_mtime;
            }
            e->used_blocks = sd_dir_used_blocks(path);
        }
        closedir(d);
    }

    g_sd_sort_key = cond->key;
    g_sd_sort_order = cond->order;
    if (count > 1) qsort(entries, count, sizeof(*entries), sd_entry_cmp);

    result->hitNum = (uint32_t)count;
    uint32_t set = 0;
    SdDirName *names = (SdDirName *)recomp_guest_to_host(ctx, result->dirNames);
    if (names && result->dirNamesNum > 0) {
        set = (uint32_t)count < result->dirNamesNum ? (uint32_t)count : result->dirNamesNum;
        // params/infos are newer-SDK fields; only touch them when the pointer looks valid.
        SdParam *params = NULL;
        SdSearchInfo *infos = NULL;
        if (set > 0 && sd_guest_ptr_ok(ctx, result->params, (size_t)set * sizeof(SdParam)))
            params = (SdParam *)(ctx->mem_base + result->params);
        if (set > 0 && sd_guest_ptr_ok(ctx, result->infos, (size_t)set * sizeof(SdSearchInfo)))
            infos = (SdSearchInfo *)(ctx->mem_base + result->infos);
        for (uint32_t i = 0; i < set; i++) {
            memset(&names[i], 0, sizeof(names[i]));
            snprintf(names[i].data, sizeof(names[i].data), "%s", entries[i].name);
            if (params) params[i] = entries[i].param;
            if (infos) {
                memset(&infos[i], 0, sizeof(infos[i]));
                infos[i].blocks = SD_DEFAULT_BLOCKS;
                infos[i].freeBlocks = entries[i].used_blocks < SD_DEFAULT_BLOCKS
                                          ? SD_DEFAULT_BLOCKS - entries[i].used_blocks : 0;
            }
        }
    }
    result->setNum = set;
    pthread_mutex_unlock(&g_savedata_mutex);
    free(entries);

    fprintf(stderr, "[ps4-savedata] DirNameSearch: user=%d pattern='%s' -> %zu hit(s)\n",
            cond->userId, pattern, count);
    return 0;
}

void shim_sceSaveDataMount2(GuestContext *ctx) {
    const SdMount2 *mount = (const SdMount2 *)recomp_guest_to_host(ctx, ctx->rdi);
    SdMountResult *result = (SdMountResult *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataMount2(ctx, mount, result);
    SHIM_RETURN();
}

void shim_sceSaveDataUmount(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataUmount(ctx, ctx->rdi);
    SHIM_RETURN();
}

void shim_sceSaveDataSetParam(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataSetParam(ctx, ctx->rdi, (uint32_t)ctx->rsi, ctx->rdx, ctx->rcx);
    SHIM_RETURN();
}

void shim_sceSaveDataSaveIcon(GuestContext *ctx) {
    const SdIcon *icon = (const SdIcon *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataSaveIcon(ctx, ctx->rdi, icon);
    SHIM_RETURN();
}

void shim_sceSaveDataDelete(GuestContext *ctx) {
    const SdDelete *del = (const SdDelete *)recomp_guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataDelete(ctx, del);
    SHIM_RETURN();
}

void shim_sceSaveDataDirNameSearch(GuestContext *ctx) {
    const SdDirNameSearchCond *cond = (const SdDirNameSearchCond *)recomp_guest_to_host(ctx, ctx->rdi);
    SdDirNameSearchResult *result = (SdDirNameSearchResult *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataDirNameSearch(ctx, cond, result);
    SHIM_RETURN();
}
