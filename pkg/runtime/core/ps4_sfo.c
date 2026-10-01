#include "ps4_sfo.h"
#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static uint16_t read_le16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t read_le32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool ps4_sfo_load_memory(Ps4SfoContext *sfo, const uint8_t *buf, size_t size) {
    if (!sfo || !buf || size < sizeof(PsfHeader)) {
        return false;
    }

    // Verify magic '\0PSF'
    if (buf[0] != 0x00 || buf[1] != 'P' || buf[2] != 'S' || buf[3] != 'F') {
        return false;
    }

    uint32_t key_table_offset = read_le32(buf + 8);
    uint32_t data_table_offset = read_le32(buf + 12);
    uint32_t entries_count = read_le32(buf + 16);

    if (key_table_offset > size || data_table_offset > size) {
        return false;
    }

    uint64_t entries_size = (uint64_t)entries_count * sizeof(PsfRawEntry);
    if (sizeof(PsfHeader) + entries_size > key_table_offset) {
        return false;
    }

    sfo->data = (uint8_t *)buf;
    sfo->size = size;
    sfo->key_table_offset = key_table_offset;
    sfo->data_table_offset = data_table_offset;
    sfo->entries_count = entries_count;
    sfo->is_allocated = false;
    return true;
}

bool ps4_sfo_load_file(Ps4SfoContext *sfo, const char *path) {
    if (!sfo || !path || *path == '\0') {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long file_size = ftell(f);
    if (file_size < (long)sizeof(PsfHeader) || file_size > 16 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    uint8_t *buffer = (uint8_t *)malloc((size_t)file_size);
    if (!buffer) {
        fclose(f);
        return false;
    }

    if (fread(buffer, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(buffer);
        fclose(f);
        return false;
    }
    fclose(f);

    if (!ps4_sfo_load_memory(sfo, buffer, (size_t)file_size)) {
        free(buffer);
        return false;
    }

    sfo->is_allocated = true;
    return true;
}

void ps4_sfo_free(Ps4SfoContext *sfo) {
    if (!sfo) return;
    if (sfo->is_allocated && sfo->data) {
        free(sfo->data);
    }
    memset(sfo, 0, sizeof(*sfo));
}

static const PsfRawEntry *find_entry(const Ps4SfoContext *sfo, const char *key) {
    if (!sfo || !sfo->data || !key) return NULL;

    const uint8_t *entries_base = sfo->data + sizeof(PsfHeader);
    const char *key_table = (const char *)(sfo->data + sfo->key_table_offset);

    for (uint32_t i = 0; i < sfo->entries_count; i++) {
        const PsfRawEntry *e = (const PsfRawEntry *)(entries_base + i * sizeof(PsfRawEntry));
        uint16_t key_off = read_le16(&e->key_offset);
        if (sfo->key_table_offset + key_off >= sfo->size) {
            continue;
        }

        const char *cur_key = key_table + key_off;
        // Verify key string bounds
        size_t max_key_len = sfo->size - (sfo->key_table_offset + key_off);
        if (strnlen(cur_key, max_key_len) == max_key_len) {
            continue; // Not null-terminated
        }

        if (strcmp(cur_key, key) == 0) {
            return e;
        }
    }
    return NULL;
}

bool ps4_sfo_get_int(const Ps4SfoContext *sfo, const char *key, int32_t *out_val) {
    if (!out_val) return false;
    const PsfRawEntry *e = find_entry(sfo, key);
    if (!e) return false;

    uint32_t data_off = read_le32(&e->data_offset);
    uint32_t param_len = read_le32(&e->param_len);

    if (param_len != 4) return false;
    if (sfo->data_table_offset + data_off + 4 > sfo->size) return false;

    *out_val = (int32_t)read_le32(sfo->data + sfo->data_table_offset + data_off);
    return true;
}

bool ps4_sfo_get_string(const Ps4SfoContext *sfo, const char *key, char *out_buf, size_t out_buf_sz) {
    if (!out_buf || out_buf_sz == 0) return false;
    const PsfRawEntry *e = find_entry(sfo, key);
    if (!e) return false;

    uint32_t data_off = read_le32(&e->data_offset);
    uint32_t param_len = read_le32(&e->param_len);

    if (sfo->data_table_offset + data_off + param_len > sfo->size) return false;

    const char *src = (const char *)(sfo->data + sfo->data_table_offset + data_off);
    size_t copy_len = param_len < out_buf_sz ? param_len : out_buf_sz - 1;
    memcpy(out_buf, src, copy_len);
    out_buf[copy_len] = '\0';
    return true;
}

bool ps4_sfo_find_and_load(Ps4SfoContext *sfo) {
    char test_path[1024];

    // 1. Through VFS
    const char *vfs_candidates[] = {
        "/app0/sce_sys/param.sfo",
        "/app0/sce_sys/sce_sys/param.sfo",
        "/app0/param.sfo",
    };
    for (size_t i = 0; i < sizeof(vfs_candidates) / sizeof(vfs_candidates[0]); i++) {
        if (ps4_vfs_resolve(vfs_candidates[i], test_path, sizeof(test_path)) == 0) {
            if (access(test_path, R_OK) == 0) {
                if (ps4_sfo_load_file(sfo, test_path)) {
                    return true;
                }
            }
        }
    }

    // 2. Relative to app_root
    const char *app_root = ps4_vfs_get_app_root();
    if (app_root && *app_root) {
        const char *rel_candidates[] = {
            "sce_sys/param.sfo",
            "sce_sys/sce_sys/param.sfo",
            "param.sfo",
        };
        for (size_t i = 0; i < sizeof(rel_candidates) / sizeof(rel_candidates[0]); i++) {
            snprintf(test_path, sizeof(test_path), "%s/%s", app_root, rel_candidates[i]);
            if (access(test_path, R_OK) == 0) {
                if (ps4_sfo_load_file(sfo, test_path)) {
                    return true;
                }
            }
        }
    }

    // 3. Fallback to current working directory
    const char *local_candidates[] = {
        "sce_sys/param.sfo",
        "sce_sys/sce_sys/param.sfo",
        "param.sfo",
        "assets/sce_sys/param.sfo",
    };
    for (size_t i = 0; i < sizeof(local_candidates) / sizeof(local_candidates[0]); i++) {
        if (access(local_candidates[i], R_OK) == 0) {
            if (ps4_sfo_load_file(sfo, local_candidates[i])) {
                return true;
            }
        }
    }

    return false;
}
