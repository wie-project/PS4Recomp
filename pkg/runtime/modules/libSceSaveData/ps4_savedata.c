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
#include <dirent.h>
#include <ftw.h>
#include <time.h>

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

typedef struct SdMountSlot {
    bool active;
    int32_t user_id;
    char dir_name[SD_DIRNAME_SIZE];
    char mount_point[SD_MOUNTPT_SIZE];
    char host_path[1024];
    uint64_t blocks;
    bool read_only;
} SdMountSlot;

static int g_savedata_initialized = 0;
static pthread_mutex_t g_savedata_mutex = PTHREAD_MUTEX_INITIALIZER;
static SaveMemorySlot g_slots[MAX_SAVE_MEMORY_SLOTS];
static SdMountSlot g_sd_mounts[SD_MAX_MOUNTS];

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

static void get_app_title_and_id(char *out_title, size_t title_size, char *out_title_id, size_t id_size) {
    if (out_title && title_size > 0) out_title[0] = '\0';
    if (out_title_id && id_size > 0) out_title_id[0] = '\0';

    Ps4SfoContext sfo = {0};
    if (ps4_sfo_find_and_load(&sfo)) {
        if (out_title) ps4_sfo_get_string(&sfo, "TITLE", out_title, title_size);
        if (out_title_id) ps4_sfo_get_string(&sfo, "TITLE_ID", out_title_id, id_size);
        ps4_sfo_free(&sfo);
    }
}

static void get_game_save_dir(char *out_path, size_t out_size) {
    char base_dir[1024];
    get_savedata_base_dir(base_dir, sizeof(base_dir));

    char title[128] = {0};
    char title_id[64] = {0};
    char sanitized_title[128] = {0};

    get_app_title_and_id(title, sizeof(title), title_id, sizeof(title_id));
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

// Check if a valid save exists (presence of param.sfo or param.bin)
static bool sd_save_exists(const char *dir_path) {
    char sfo_path[1200];
    snprintf(sfo_path, sizeof(sfo_path), "%s/%s", dir_path, SD_PARAM_SFO);
    if (access(sfo_path, F_OK) == 0) return true;
    char bin_path[1200];
    snprintf(bin_path, sizeof(bin_path), "%s/%s", dir_path, SD_PARAM_FILE);
    if (access(bin_path, F_OK) == 0) return true;
    return false;
}

// SFO writing logic
#pragma pack(push, 1)
typedef struct {
    uint8_t magic[4];
    uint32_t version;
    uint32_t key_table_offset;
    uint32_t data_table_offset;
    uint32_t entries_count;
} PsfHeader;

typedef struct {
    uint16_t key_offset;
    uint16_t param_fmt;
    uint32_t param_len;
    uint32_t param_max_len;
    uint32_t data_offset;
} PsfRawEntry;
#pragma pack(pop)

static void write_le16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

static void write_le64(uint8_t *p, uint64_t val) {
    write_le32(p, (uint32_t)(val & 0xFFFFFFFF));
    write_le32(p + 4, (uint32_t)((val >> 32) & 0xFFFFFFFF));
}

// Generate a valid, standard PS4 PSF 1.1 param.sfo file for save data
static bool sd_write_sfo(const char *sfo_path, const SdParam *param, const char *dir_name, uint64_t blocks) {
    char title_id[64] = "CUSA00000";
    get_app_title_and_id(NULL, 0, title_id, sizeof(title_id));
    if (title_id[0] == '\0') {
        snprintf(title_id, sizeof(title_id), "CUSA00000");
    }

    const char *title = (param && param->title[0]) ? param->title : "Saved Data";
    const char *sub_title = (param && param->subTitle[0]) ? param->subTitle : "";
    const char *detail = (param && param->detail[0]) ? param->detail : "";
    uint32_t user_param = param ? param->userParam : 0;
    const char *dir = (dir_name && dir_name[0]) ? dir_name : "save";

    // 8 entries sorted alphabetically by key:
    // 0: ACCOUNT_ID          (BINARY, max 8)
    // 1: DETAIL              (STRING, max 1024)
    // 2: MAINTITLE           (STRING, max 128)
    // 3: SAVEDATA_BLOCKS     (BINARY, max 8)
    // 4: SAVEDATA_DIRECTORY  (STRING, max 32)
    // 5: SAVEDATA_LIST_PARAM (INT,    max 4)
    // 6: SUBTITLE            (STRING, max 128)
    // 7: TITLE_ID            (STRING, max 16)

    static const char keys_blob[] =
        "ACCOUNT_ID\0"
        "DETAIL\0"
        "MAINTITLE\0"
        "SAVEDATA_BLOCKS\0"
        "SAVEDATA_DIRECTORY\0"
        "SAVEDATA_LIST_PARAM\0"
        "SUBTITLE\0"
        "TITLE_ID\0";

    static const uint16_t key_offsets[8] = { 0, 11, 18, 28, 44, 63, 83, 92 };
    static const uint16_t fmts[8] = {
        0x0004, 0x0204, 0x0204, 0x0004,
        0x0204, 0x0404, 0x0204, 0x0204
    };
    static const uint32_t max_lens[8] = {
        8, 1024, 128, 8,
        32, 4, 128, 16
    };

    uint32_t lens[8];
    lens[0] = 8;
    lens[1] = (uint32_t)(strlen(detail) + 1);
    lens[2] = (uint32_t)(strlen(title) + 1);
    lens[3] = 8;
    lens[4] = (uint32_t)(strlen(dir) + 1);
    lens[5] = 4;
    lens[6] = (uint32_t)(strlen(sub_title) + 1);
    lens[7] = (uint32_t)(strlen(title_id) + 1);

    uint32_t data_offsets[8];
    uint32_t curr_data_off = 0;
    for (int i = 0; i < 8; i++) {
        data_offsets[i] = curr_data_off;
        curr_data_off += max_lens[i];
    }

    uint32_t header_size = sizeof(PsfHeader); // 20
    uint32_t entries_size = 8 * sizeof(PsfRawEntry); // 128
    uint32_t key_table_offset = header_size + entries_size; // 148
    uint32_t keys_size = sizeof(keys_blob); // 101
    uint32_t data_table_offset = (key_table_offset + keys_size + 3) & ~3u; // 252
    uint32_t total_size = data_table_offset + curr_data_off; // 252 + 1348 = 1600

    uint8_t *buf = (uint8_t *)calloc(1, total_size);
    if (!buf) return false;

    // Header
    buf[0] = 0x00; buf[1] = 'P'; buf[2] = 'S'; buf[3] = 'F';
    write_le32(buf + 4, 0x00000101);
    write_le32(buf + 8, key_table_offset);
    write_le32(buf + 12, data_table_offset);
    write_le32(buf + 16, 8);

    // Entries
    for (int i = 0; i < 8; i++) {
        uint8_t *e = buf + header_size + i * sizeof(PsfRawEntry);
        write_le16(e + 0, key_offsets[i]);
        write_le16(e + 2, fmts[i]);
        write_le32(e + 4, lens[i]);
        write_le32(e + 8, max_lens[i]);
        write_le32(e + 12, data_offsets[i]);
    }

    // Key table
    memcpy(buf + key_table_offset, keys_blob, keys_size);

    // Data table
    uint8_t *data_base = buf + data_table_offset;
    // 0: ACCOUNT_ID (8 zero bytes)
    memset(data_base + data_offsets[0], 0, 8);
    // 1: DETAIL
    snprintf((char *)(data_base + data_offsets[1]), max_lens[1], "%s", detail);
    // 2: MAINTITLE
    snprintf((char *)(data_base + data_offsets[2]), max_lens[2], "%s", title);
    // 3: SAVEDATA_BLOCKS
    write_le64(data_base + data_offsets[3], blocks);
    // 4: SAVEDATA_DIRECTORY
    snprintf((char *)(data_base + data_offsets[4]), max_lens[4], "%s", dir);
    // 5: SAVEDATA_LIST_PARAM
    write_le32(data_base + data_offsets[5], user_param);
    // 6: SUBTITLE
    snprintf((char *)(data_base + data_offsets[6]), max_lens[6], "%s", sub_title);
    // 7: TITLE_ID
    snprintf((char *)(data_base + data_offsets[7]), max_lens[7], "%s", title_id);

    FILE *f = fopen(sfo_path, "wb");
    if (!f) {
        free(buf);
        return false;
    }
    size_t written = fwrite(buf, 1, total_size, f);
    fclose(f);
    free(buf);
    return written == total_size;
}

static bool sd_load_param(const char *dir_path, SdParam *out) {
    memset(out, 0, sizeof(*out));
    char sfo_path[1200];
    snprintf(sfo_path, sizeof(sfo_path), "%s/%s", dir_path, SD_PARAM_SFO);
    Ps4SfoContext sfo = {0};
    if (ps4_sfo_load_file(&sfo, sfo_path)) {
        ps4_sfo_get_string(&sfo, "MAINTITLE", out->title, sizeof(out->title));
        ps4_sfo_get_string(&sfo, "SUBTITLE", out->subTitle, sizeof(out->subTitle));
        ps4_sfo_get_string(&sfo, "DETAIL", out->detail, sizeof(out->detail));
        int32_t up = 0;
        if (ps4_sfo_get_int(&sfo, "SAVEDATA_LIST_PARAM", &up)) {
            out->userParam = (uint32_t)up;
        }
        struct stat st;
        if (stat(sfo_path, &st) == 0) {
            out->mtime = (int64_t)st.st_mtime;
        }
        ps4_sfo_free(&sfo);
        return true;
    }

    // Fallback to legacy param.bin
    char bin_path[1200];
    snprintf(bin_path, sizeof(bin_path), "%s/%s", dir_path, SD_PARAM_FILE);
    FILE *f = fopen(bin_path, "rb");
    if (!f) return false;
    size_t got = fread(out, 1, sizeof(*out), f);
    fclose(f);
    return got == sizeof(*out);
}

static int sd_store_param(const char *dir_path, const SdParam *param, const char *dir_name, uint64_t blocks) {
    char sys_path[1200];
    snprintf(sys_path, sizeof(sys_path), "%s/sce_sys", dir_path);
    mkdir_p(sys_path, 0755);

    char sfo_path[1200];
    snprintf(sfo_path, sizeof(sfo_path), "%s/%s", dir_path, SD_PARAM_SFO);
    sd_write_sfo(sfo_path, param, dir_name, blocks);

    // Also write legacy param.bin for backwards compatibility
    char bin_path[1200];
    snprintf(bin_path, sizeof(bin_path), "%s/%s", dir_path, SD_PARAM_FILE);
    FILE *f = fopen(bin_path, "wb");
    if (f) {
        fwrite(param, 1, sizeof(*param), f);
        fclose(f);
    }
    return 0;
}

static void sd_copy_default_icon_if_needed(const char *dir_path) {
    char dst_icon[1200];
    snprintf(dst_icon, sizeof(dst_icon), "%s/%s", dir_path, SD_ICON_FILE);
    if (access(dst_icon, F_OK) == 0) return;

    char host_src[1024];
    if (ps4_vfs_resolve("/app0/sce_sys/save_data.png", host_src, sizeof(host_src)) != 0 &&
        ps4_vfs_resolve("/app0/sce_sys/icon0.png", host_src, sizeof(host_src)) != 0) {
        return;
    }

    FILE *in = fopen(host_src, "rb");
    if (!in) return;
    FILE *out = fopen(dst_icon, "wb");
    if (!out) {
        fclose(in);
        return;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
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
    if (!sd_save_exists(path)) {
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

// Unified mount function
static int32_t sd_mount_internal(GuestContext *ctx, int32_t userId, const char *dir_name,
                                 uint64_t blocks, uint32_t mountMode, SdMountResult *result) {
    if (!g_savedata_initialized) {
        return SD_ERR_NOT_INIT;
    }
    if (!dir_name || !result) {
        return SD_ERR_PARAMETER;
    }

    bool is_ro = (mountMode & SD_MOUNT_MODE_RDONLY) != 0;
    bool create = (mountMode & SD_MOUNT_MODE_CREATE) != 0;
    bool create_if_not_exist = (mountMode & SD_MOUNT_MODE_CREATE2) != 0;
    bool copy_icon = (mountMode & SD_MOUNT_MODE_COPY_ICON) != 0;

    pthread_mutex_lock(&g_savedata_mutex);
    if (sd_find_mount_by_dir(userId, dir_name)) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_BUSY;
    }

    char host_path[1024];
    sd_dir_path(userId, dir_name, host_path, sizeof(host_path));
    bool exists = sd_save_exists(host_path);
    uint32_t status = SD_MOUNT_STATUS_NOTHING;

    if (!exists) {
        if (!create && !create_if_not_exist) {
            pthread_mutex_unlock(&g_savedata_mutex);
            fprintf(stderr, "[ps4-savedata] Mount: user=%d dir='%s' mode=0x%x -> NOT_FOUND\n",
                    userId, dir_name, mountMode);
            return SD_ERR_NOT_FOUND;
        }

        char sys_path[1100];
        snprintf(sys_path, sizeof(sys_path), "%s/sce_sys", host_path);
        if (mkdir_p(sys_path, 0755) != 0) {
            fprintf(stderr, "[ps4-savedata] Mount: cannot create '%s' (errno=%d)\n", host_path, errno);
            pthread_mutex_unlock(&g_savedata_mutex);
            return SD_ERR_INTERNAL;
        }

        uint64_t max_blocks = (blocks >= SD_BLOCKS_MIN && blocks <= SD_BLOCKS_MAX) ? blocks : SD_DEFAULT_BLOCKS;
        SdParam default_param;
        memset(&default_param, 0, sizeof(default_param));
        char app_title[128] = "Saved Data";
        get_app_title_and_id(app_title, sizeof(app_title), NULL, 0);
        snprintf(default_param.title, sizeof(default_param.title), "%s", app_title[0] ? app_title : "Saved Data");
        default_param.mtime = (int64_t)time(NULL);

        sd_store_param(host_path, &default_param, dir_name, max_blocks);
        if (copy_icon) {
            sd_copy_default_icon_if_needed(host_path);
        }
        status = SD_MOUNT_STATUS_CREATED;
    } else if (create && !create_if_not_exist) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_EXISTS;
    }

    int slot_idx = -1;
    for (int i = 0; i < SD_MAX_MOUNTS; i++) {
        if (!g_sd_mounts[i].active) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_MOUNT_FULL;
    }

    SdMountSlot *slot = &g_sd_mounts[slot_idx];
    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->user_id = userId;
    slot->blocks = (blocks >= SD_BLOCKS_MIN && blocks <= SD_BLOCKS_MAX) ? blocks : SD_DEFAULT_BLOCKS;
    slot->read_only = is_ro;
    snprintf(slot->dir_name, sizeof(slot->dir_name), "%s", dir_name);
    snprintf(slot->mount_point, sizeof(slot->mount_point), "/savedata%d", slot_idx);
    snprintf(slot->host_path, sizeof(slot->host_path), "%s", host_path);
    ps4_vfs_mount(slot->mount_point, slot->host_path);

    memset(result, 0, sizeof(*result));
    snprintf(result->mountPoint.data, sizeof(result->mountPoint.data), "%s", slot->mount_point);
    result->requiredBlocks = 0;
    result->mountStatus = status;
    pthread_mutex_unlock(&g_savedata_mutex);

    fprintf(stderr, "[ps4-savedata] Mount: user=%d dir='%s' mode=0x%x -> %s (%s)%s\n",
            userId, dir_name, mountMode, slot->mount_point, host_path,
            status == SD_MOUNT_STATUS_CREATED ? " [created]" : "");
    return 0;
}

int32_t sceSaveDataMount(GuestContext *ctx, const SdMount *mount, SdMountResult *result) {
    if (!mount || !result) return SD_ERR_PARAMETER;
    char dir_name[SD_DIRNAME_SIZE];
    if (!sd_copy_dirname(ctx, mount->dirName, dir_name)) return SD_ERR_PARAMETER;
    return sd_mount_internal(ctx, mount->userId, dir_name, mount->blocks, mount->mountMode, result);
}

int32_t sceSaveDataMount2(GuestContext *ctx, const SdMount2 *mount, SdMountResult *result) {
    if (!mount || !result) return SD_ERR_PARAMETER;
    char dir_name[SD_DIRNAME_SIZE];
    if (!sd_copy_dirname(ctx, mount->dirName, dir_name)) return SD_ERR_PARAMETER;
    return sd_mount_internal(ctx, mount->userId, dir_name, mount->blocks, mount->mountMode, result);
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

int32_t sceSaveDataUmountWithBackup(GuestContext *ctx, uint64_t mount_point_addr) {
    return sceSaveDataUmount(ctx, mount_point_addr);
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
    if (slot->read_only) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_INTERNAL;
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
    int rc = sd_store_param(slot->host_path, &p, slot->dir_name, slot->blocks);
    pthread_mutex_unlock(&g_savedata_mutex);
    return rc == 0 ? 0 : SD_ERR_INTERNAL;
}

int32_t sceSaveDataGetParam(GuestContext *ctx, uint64_t mount_point_addr, uint32_t param_type,
                            uint64_t buf_addr, uint64_t buf_size, uint64_t got_size_addr) {
    if (!mount_point_addr || !buf_addr) return SD_ERR_PARAMETER;
    void *buf = recomp_guest_to_host(ctx, buf_addr);
    if (!buf) return SD_ERR_PARAMETER;

    pthread_mutex_lock(&g_savedata_mutex);
    SdMountSlot *slot = sd_find_mount_by_point(ctx, mount_point_addr);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_MOUNTED;
    }

    SdParam p;
    sd_load_param(slot->host_path, &p);

    size_t copied = 0;
    switch (param_type) {
    case SD_PARAM_ALL: {
        size_t n = buf_size < sizeof(p) ? (size_t)buf_size : sizeof(p);
        memcpy(buf, &p, n);
        copied = sizeof(p);
        break;
    }
    case SD_PARAM_TITLE: {
        size_t len = strlen(p.title);
        size_t n = buf_size < len + 1 ? (size_t)buf_size : len + 1;
        if (n > 0) {
            memcpy(buf, p.title, n - 1);
            ((char *)buf)[n - 1] = '\0';
        }
        copied = len + 1;
        break;
    }
    case SD_PARAM_SUB_TITLE: {
        size_t len = strlen(p.subTitle);
        size_t n = buf_size < len + 1 ? (size_t)buf_size : len + 1;
        if (n > 0) {
            memcpy(buf, p.subTitle, n - 1);
            ((char *)buf)[n - 1] = '\0';
        }
        copied = len + 1;
        break;
    }
    case SD_PARAM_DETAIL: {
        size_t len = strlen(p.detail);
        size_t n = buf_size < len + 1 ? (size_t)buf_size : len + 1;
        if (n > 0) {
            memcpy(buf, p.detail, n - 1);
            ((char *)buf)[n - 1] = '\0';
        }
        copied = len + 1;
        break;
    }
    case SD_PARAM_USER_PARAM: {
        if (buf_size < sizeof(uint32_t)) {
            pthread_mutex_unlock(&g_savedata_mutex);
            return SD_ERR_PARAMETER;
        }
        memcpy(buf, &p.userParam, sizeof(uint32_t));
        copied = sizeof(uint32_t);
        break;
    }
    case SD_PARAM_MTIME: {
        if (buf_size < sizeof(int64_t)) {
            pthread_mutex_unlock(&g_savedata_mutex);
            return SD_ERR_PARAMETER;
        }
        memcpy(buf, &p.mtime, sizeof(int64_t));
        copied = sizeof(int64_t);
        break;
    }
    default:
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_PARAMETER;
    }

    if (got_size_addr) {
        size_t *got = (size_t *)recomp_guest_to_host(ctx, got_size_addr);
        if (got) *got = copied;
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataGetMountInfo(GuestContext *ctx, uint64_t mount_point_addr, SdMountInfo *info) {
    if (!mount_point_addr || !info) return SD_ERR_PARAMETER;
    pthread_mutex_lock(&g_savedata_mutex);
    SdMountSlot *slot = sd_find_mount_by_point(ctx, mount_point_addr);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_MOUNTED;
    }

    uint64_t used_blocks = sd_dir_used_blocks(slot->host_path);
    uint64_t total_blocks = slot->blocks ? slot->blocks : SD_DEFAULT_BLOCKS;
    memset(info, 0, sizeof(*info));
    info->blocks = total_blocks;
    info->freeBlocks = total_blocks > used_blocks ? total_blocks - used_blocks : 0;
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
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
    return (f && put == size) ? 0 : SD_ERR_INTERNAL;
}

int32_t sceSaveDataLoadIcon(GuestContext *ctx, uint64_t mount_point_addr, SdIcon *icon) {
    if (!mount_point_addr || !icon || !icon->buf || icon->bufSize == 0) return SD_ERR_PARAMETER;
    void *dst = recomp_guest_to_host(ctx, icon->buf);
    if (!dst) return SD_ERR_PARAMETER;

    pthread_mutex_lock(&g_savedata_mutex);
    SdMountSlot *slot = sd_find_mount_by_point(ctx, mount_point_addr);
    if (!slot) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_MOUNTED;
    }

    char icon_path[1200];
    snprintf(icon_path, sizeof(icon_path), "%s/%s", slot->host_path, SD_ICON_FILE);
    FILE *f = fopen(icon_path, "rb");
    if (!f) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return SD_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t read_bytes = 0;
    if (sz > 0) {
        size_t to_read = (size_t)sz < icon->bufSize ? (size_t)sz : icon->bufSize;
        read_bytes = fread(dst, 1, to_read, f);
        icon->dataSize = (uint64_t)sz;
    } else {
        icon->dataSize = 0;
    }
    fclose(f);
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
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
            if (name[0] == '.' || strncmp(name, "sce_", 4) == 0) continue;
            if (strlen(name) >= SD_DIRNAME_SIZE) continue;
            if (!sd_match(pattern, name)) continue;
            char path[1100];
            snprintf(path, sizeof(path), "%s/%s", user_dir, name);
            if (!sd_save_exists(path)) continue;

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

// SaveDataMemory slot helpers
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

static int32_t save_data_memory_read(uint32_t slot_id, void *dst_buf, size_t buf_size, int64_t offset) {
    SaveMemorySlot *slot = find_slot(slot_id);
    if (!slot || !slot->cache) {
        return ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY;
    }
    if (offset < 0) offset = 0;
    size_t read_size = buf_size;
    if ((size_t)offset < slot->cache_size) {
        if (offset + read_size > slot->cache_size) {
            read_size = slot->cache_size - (size_t)offset;
        }
        memcpy(dst_buf, slot->cache + offset, read_size);
    } else {
        memset(dst_buf, 0, read_size);
    }
    return 0;
}

static int32_t save_data_memory_write(uint32_t slot_id, const void *src_buf, size_t buf_size, int64_t offset) {
    SaveMemorySlot *slot = find_slot(slot_id);
    if (!slot || !slot->cache) {
        return ORBIS_SAVE_DATA_ERROR_MEMORY_NOT_READY;
    }
    if (offset < 0) offset = 0;
    size_t required_size = (size_t)offset + buf_size;
    if (required_size > slot->cache_size) {
        uint8_t *new_cache = (uint8_t *)realloc(slot->cache, required_size);
        if (new_cache) {
            memset(new_cache + slot->cache_size, 0, required_size - slot->cache_size);
            slot->cache = new_cache;
            slot->cache_size = required_size;
        }
    }
    if (required_size <= slot->cache_size) {
        memcpy(slot->cache + offset, src_buf, buf_size);
        slot->dirty = true;
    }
    return 0;
}

// Lifecycle functions
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
    for (int i = 0; i < SD_MAX_MOUNTS; i++) {
        if (g_sd_mounts[i].active) {
            ps4_vfs_unmount(g_sd_mounts[i].mount_point);
            g_sd_mounts[i].active = false;
        }
    }
    g_savedata_initialized = 0;
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSetupSaveDataMemory2(GuestContext *ctx,
                                       const OrbisSaveDataMemorySetup2 *setupParam,
                                       OrbisSaveDataMemorySetupResult *result) {
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
        alloc_size = 64 * 1024;
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
        // Write default param.sfo
        SdParam p;
        memset(&p, 0, sizeof(p));
        char app_title[128] = "Saved Data";
        get_app_title_and_id(app_title, sizeof(app_title), NULL, 0);
        snprintf(p.title, sizeof(p.title), "%s", app_title[0] ? app_title : "Saved Data");
        if (setupParam->initParam) {
            const SdParam *init_p = (const SdParam *)recomp_guest_to_host(ctx, setupParam->initParam);
            if (init_p) memcpy(&p, init_p, sizeof(p));
        }
        char slot_dir[64];
        if (slot_id == 0) snprintf(slot_dir, sizeof(slot_dir), "%s", STANDARD_DIRNAME_SAVE_DATA_MEMORY);
        else snprintf(slot_dir, sizeof(slot_dir), "%s_%u", STANDARD_DIRNAME_SAVE_DATA_MEMORY, slot_id);
        sd_store_param(slot->folder_path, &p, slot_dir, (alloc_size + SD_BLOCK_SIZE - 1) / SD_BLOCK_SIZE);

        fprintf(stderr, "[ps4-recomp] sceSaveDataSetupSaveDataMemory2: Slot %u created fresh at '%s' (allocated: %zu bytes)\n",
                slot_id, file_path, alloc_size);
    }

    if (result) {
        result->existedMemorySize = existed_size;
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataGetSaveDataMemory2(GuestContext *ctx, OrbisSaveDataMemoryGet2 *getParam) {
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
    if (getParam->data) {
        OrbisSaveDataMemoryData *data = (OrbisSaveDataMemoryData *)recomp_guest_to_host(ctx, getParam->data);
        if (data && data->buf && data->bufSize > 0) {
            void *dst_buf = recomp_guest_to_host(ctx, data->buf);
            if (dst_buf) {
                int32_t rc = save_data_memory_read(slot_id, dst_buf, data->bufSize, data->offset);
                if (rc != 0) {
                    pthread_mutex_unlock(&g_savedata_mutex);
                    return rc;
                }
            }
        }
    }
    if (getParam->param) {
        SdParam *dst_p = (SdParam *)recomp_guest_to_host(ctx, getParam->param);
        SaveMemorySlot *slot = find_slot(slot_id);
        if (dst_p && slot) {
            sd_load_param(slot->folder_path, dst_p);
        }
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSetSaveDataMemory2(GuestContext *ctx, const OrbisSaveDataMemorySet2 *setParam) {
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
    if (setParam->data) {
        const OrbisSaveDataMemoryData *data = (const OrbisSaveDataMemoryData *)recomp_guest_to_host(ctx, setParam->data);
        uint32_t data_num = setParam->dataNum > 1 ? setParam->dataNum : 1;
        if (data) {
            for (uint32_t i = 0; i < data_num; i++) {
                if (data[i].buf && data[i].bufSize > 0) {
                    const void *src_buf = recomp_guest_to_host(ctx, data[i].buf);
                    if (src_buf && data[i].offset >= 0) {
                        int32_t rc = save_data_memory_write(slot_id, src_buf, data[i].bufSize, data[i].offset);
                        if (rc != 0) {
                            pthread_mutex_unlock(&g_savedata_mutex);
                            return rc;
                        }
                    }
                }
            }
        }
    }
    if (setParam->param) {
        const SdParam *src_p = (const SdParam *)recomp_guest_to_host(ctx, setParam->param);
        SaveMemorySlot *slot = find_slot(slot_id);
        if (src_p && slot) {
            char slot_dir[64];
            if (slot_id == 0) snprintf(slot_dir, sizeof(slot_dir), "%s", STANDARD_DIRNAME_SAVE_DATA_MEMORY);
            else snprintf(slot_dir, sizeof(slot_dir), "%s_%u", STANDARD_DIRNAME_SAVE_DATA_MEMORY, slot_id);
            sd_store_param(slot->folder_path, src_p, slot_dir, (slot->cache_size + SD_BLOCK_SIZE - 1) / SD_BLOCK_SIZE);
        }
    }

    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataSyncSaveDataMemory(GuestContext *ctx, OrbisSaveDataMemorySync *syncParam) {
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

int32_t sceSaveDataSetupSaveDataMemory(GuestContext *ctx, int32_t userId, size_t memorySize, uint64_t param_addr) {
    OrbisSaveDataMemorySetup2 setupParam;
    memset(&setupParam, 0, sizeof(setupParam));
    setupParam.userId = userId;
    setupParam.memorySize = memorySize;
    setupParam.slotId = 0;
    setupParam.initParam = param_addr;

    OrbisSaveDataMemorySetupResult result;
    memset(&result, 0, sizeof(result));

    return sceSaveDataSetupSaveDataMemory2(ctx, &setupParam, &result);
}

int32_t sceSaveDataGetSaveDataMemory(GuestContext *ctx, int32_t userId, uint64_t buf_addr, size_t bufSize, int64_t offset) {
    (void)userId;
    if (!buf_addr || bufSize == 0) return ORBIS_SAVE_DATA_ERROR_PARAMETER;
    void *dst = recomp_guest_to_host(ctx, buf_addr);
    if (!dst) return ORBIS_SAVE_DATA_ERROR_PARAMETER;

    pthread_mutex_lock(&g_savedata_mutex);
    int32_t rc = save_data_memory_read(0, dst, bufSize, offset);
    pthread_mutex_unlock(&g_savedata_mutex);
    return rc;
}

int32_t sceSaveDataSetSaveDataMemory(GuestContext *ctx, int32_t userId, uint64_t buf_addr, size_t bufSize, int64_t offset) {
    (void)userId;
    if (!buf_addr || bufSize == 0) return ORBIS_SAVE_DATA_ERROR_PARAMETER;
    const void *src = recomp_guest_to_host(ctx, buf_addr);
    if (!src) return ORBIS_SAVE_DATA_ERROR_PARAMETER;

    pthread_mutex_lock(&g_savedata_mutex);
    int32_t rc = save_data_memory_write(0, src, bufSize, offset);
    pthread_mutex_unlock(&g_savedata_mutex);
    return rc;
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

void shim_sceSaveDataMount(GuestContext *ctx) {
    const SdMount *mount = (const SdMount *)recomp_guest_to_host(ctx, ctx->rdi);
    SdMountResult *result = (SdMountResult *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataMount(ctx, mount, result);
    SHIM_RETURN();
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

void shim_sceSaveDataUmountWithBackup(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataUmountWithBackup(ctx, ctx->rdi);
    SHIM_RETURN();
}

void shim_sceSaveDataSetParam(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataSetParam(ctx, ctx->rdi, (uint32_t)ctx->rsi, ctx->rdx, ctx->rcx);
    SHIM_RETURN();
}

void shim_sceSaveDataGetParam(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataGetParam(ctx, ctx->rdi, (uint32_t)ctx->rsi, ctx->rdx, ctx->rcx, ctx->r8);
    SHIM_RETURN();
}

void shim_sceSaveDataSaveIcon(GuestContext *ctx) {
    const SdIcon *icon = (const SdIcon *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataSaveIcon(ctx, ctx->rdi, icon);
    SHIM_RETURN();
}

void shim_sceSaveDataLoadIcon(GuestContext *ctx) {
    SdIcon *icon = (SdIcon *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataLoadIcon(ctx, ctx->rdi, icon);
    SHIM_RETURN();
}

void shim_sceSaveDataGetMountInfo(GuestContext *ctx) {
    SdMountInfo *info = (SdMountInfo *)recomp_guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceSaveDataGetMountInfo(ctx, ctx->rdi, info);
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

void shim_sceSaveDataSetupSaveDataMemory(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataSetupSaveDataMemory(
        ctx, (int32_t)ctx->rdi, (size_t)ctx->rsi, ctx->rdx
    );
    SHIM_RETURN();
}

void shim_sceSaveDataGetSaveDataMemory(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataGetSaveDataMemory(
        ctx, (int32_t)ctx->rdi, ctx->rsi, (size_t)ctx->rdx, (int64_t)ctx->rcx
    );
    SHIM_RETURN();
}

void shim_sceSaveDataSetSaveDataMemory(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataSetSaveDataMemory(
        ctx, (int32_t)ctx->rdi, ctx->rsi, (size_t)ctx->rdx, (int64_t)ctx->rcx
    );
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
