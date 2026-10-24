#include "ps4_pak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/stat.h>

#define PAK_MAGIC 0x5A6F12E1

typedef struct {
    uint64_t hash;
    int pak_fd;
    uint64_t data_offset;
    uint64_t size;
    uint32_t compression;
} PakHashEntry;

typedef struct {
    uint64_t hash;
} PakDirEntry;

static pthread_mutex_t g_pak_mutex = PTHREAD_MUTEX_INITIALIZER;
static PakHashEntry *g_entries = NULL;
static size_t g_entry_count = 0;
static size_t g_entry_capacity = 0;

static PakDirEntry *g_dirs = NULL;
static size_t g_dir_count = 0;
static size_t g_dir_capacity = 0;

static int *g_open_pak_fds = NULL;
static size_t g_open_pak_count = 0;

static uint64_t fnv1a_64_lower(const char *str) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        char c = (char)tolower((unsigned char)*str);
        if (c == '\\') c = '/';
        hash ^= (uint64_t)(unsigned char)c;
        hash *= 1099511628211ULL;
        str++;
    }
    return hash;
}

static void hash_table_insert_file(uint64_t hash, int pak_fd, uint64_t data_offset, uint64_t size, uint32_t comp) {
    if (g_entry_count * 2 >= g_entry_capacity) {
        size_t new_cap = g_entry_capacity ? g_entry_capacity * 2 : 65536;
        PakHashEntry *new_table = (PakHashEntry *)calloc(new_cap, sizeof(PakHashEntry));
        if (!new_table) return;

        for (size_t i = 0; i < g_entry_capacity; i++) {
            if (g_entries[i].hash != 0) {
                size_t idx = g_entries[i].hash & (new_cap - 1);
                while (new_table[idx].hash != 0) {
                    idx = (idx + 1) & (new_cap - 1);
                }
                new_table[idx] = g_entries[i];
            }
        }
        free(g_entries);
        g_entries = new_table;
        g_entry_capacity = new_cap;
    }

    size_t idx = hash & (g_entry_capacity - 1);
    while (g_entries[idx].hash != 0 && g_entries[idx].hash != hash) {
        idx = (idx + 1) & (g_entry_capacity - 1);
    }
    if (g_entries[idx].hash == 0) {
        g_entry_count++;
    }
    g_entries[idx].hash = hash;
    g_entries[idx].pak_fd = pak_fd;
    g_entries[idx].data_offset = data_offset;
    g_entries[idx].size = size;
    g_entries[idx].compression = comp;
}

static void hash_table_insert_dir(uint64_t hash) {
    if (g_dir_count * 2 >= g_dir_capacity) {
        size_t new_cap = g_dir_capacity ? g_dir_capacity * 2 : 4096;
        PakDirEntry *new_table = (PakDirEntry *)calloc(new_cap, sizeof(PakDirEntry));
        if (!new_table) return;

        for (size_t i = 0; i < g_dir_capacity; i++) {
            if (g_dirs[i].hash != 0) {
                size_t idx = g_dirs[i].hash & (new_cap - 1);
                while (new_table[idx].hash != 0) {
                    idx = (idx + 1) & (new_cap - 1);
                }
                new_table[idx] = g_dirs[i];
            }
        }
        free(g_dirs);
        g_dirs = new_table;
        g_dir_capacity = new_cap;
    }

    size_t idx = hash & (g_dir_capacity - 1);
    while (g_dirs[idx].hash != 0 && g_dirs[idx].hash != hash) {
        idx = (idx + 1) & (g_dir_capacity - 1);
    }
    if (g_dirs[idx].hash == 0) {
        g_dir_count++;
        g_dirs[idx].hash = hash;
    }
}

static bool parse_pak_file(const char *pak_path) {
    int fd = open(pak_path, O_RDONLY);
    if (fd < 0) return false;

    off_t file_sz = lseek(fd, 0, SEEK_END);
    if (file_sz < 256) {
        close(fd);
        return false;
    }

    uint8_t tail[256];
    if (pread(fd, tail, 256, file_sz - 256) != 256) {
        close(fd);
        return false;
    }

    int magic_pos = -1;
    for (int i = 256 - 4; i >= 0; i--) {
        uint32_t val = (uint32_t)tail[i] | ((uint32_t)tail[i+1] << 8) |
                       ((uint32_t)tail[i+2] << 16) | ((uint32_t)tail[i+3] << 24);
        if (val == PAK_MAGIC) {
            magic_pos = i;
            break;
        }
    }

    if (magic_pos < 0 || magic_pos + 24 > 256) {
        close(fd);
        return false;
    }

    const uint8_t *footer = &tail[magic_pos];
    uint32_t ver = *(const uint32_t *)(footer + 4);
    uint64_t idx_off = *(const uint64_t *)(footer + 8);
    uint64_t idx_sz = *(const uint64_t *)(footer + 16);

    if (idx_off + idx_sz > (uint64_t)file_sz || idx_sz == 0) {
        close(fd);
        return false;
    }

    uint8_t *idx_buf = (uint8_t *)malloc(idx_sz);
    if (!idx_buf) {
        close(fd);
        return false;
    }

    if (pread(fd, idx_buf, idx_sz, idx_off) != (ssize_t)idx_sz) {
        free(idx_buf);
        close(fd);
        return false;
    }

    size_t pos = 0;
    if (pos + 4 > idx_sz) { free(idx_buf); close(fd); return false; }
    int32_t mlen = *(const int32_t *)(idx_buf + pos); pos += 4;
    size_t mbytes = (mlen > 0) ? (size_t)mlen : (size_t)(-mlen * 2);
    if (pos + mbytes + 4 + 8 + 4 > idx_sz) { free(idx_buf); close(fd); return false; }
    pos += mbytes; // skip mount point

    uint32_t entries_count = *(const uint32_t *)(idx_buf + pos); pos += 4;
    pos += 8; // skip path_hash_seed
    uint32_t has_phi = *(const uint32_t *)(idx_buf + pos); pos += 4;
    if (has_phi) {
        if (pos + 16 + 20 > idx_sz) { free(idx_buf); close(fd); return false; }
        pos += 16 + 20; // skip phi_off, phi_sz, phi_hash
    }

    if (pos + 4 > idx_sz) { free(idx_buf); close(fd); return false; }
    uint32_t has_fdi = *(const uint32_t *)(idx_buf + pos); pos += 4;
    if (!has_fdi || pos + 16 + 20 + 4 > idx_sz) {
        free(idx_buf);
        close(fd);
        return false;
    }

    uint64_t fdi_off = *(const uint64_t *)(idx_buf + pos); pos += 8;
    uint64_t fdi_sz = *(const uint64_t *)(idx_buf + pos); pos += 8;
    pos += 20; // skip fdi_hash
    uint32_t enc_sz = *(const uint32_t *)(idx_buf + pos); pos += 4;

    if (pos + enc_sz > idx_sz) {
        free(idx_buf);
        close(fd);
        return false;
    }

    const uint8_t *enc_buf = idx_buf + pos;

    uint8_t *fdi_buf = (uint8_t *)malloc(fdi_sz);
    if (!fdi_buf) {
        free(idx_buf);
        close(fd);
        return false;
    }

    if (pread(fd, fdi_buf, fdi_sz, fdi_off) != (ssize_t)fdi_sz) {
        free(fdi_buf);
        free(idx_buf);
        close(fd);
        return false;
    }

    // Save open fd in global list
    g_open_pak_fds = (int *)realloc(g_open_pak_fds, (g_open_pak_count + 1) * sizeof(int));
    g_open_pak_fds[g_open_pak_count++] = fd;

    // Parse DirectoryIndex
    size_t fdi_pos = 0;
    if (fdi_pos + 4 > fdi_sz) { free(fdi_buf); free(idx_buf); return false; }
    uint32_t num_dirs = *(const uint32_t *)(fdi_buf + fdi_pos); fdi_pos += 4;

    for (uint32_t d = 0; d < num_dirs && fdi_pos + 4 <= fdi_sz; d++) {
        int32_t dlen = *(const int32_t *)(fdi_buf + fdi_pos); fdi_pos += 4;
        char dname[512] = {0};
        if (dlen > 0) {
            size_t copy_len = ((size_t)dlen < sizeof(dname)) ? (size_t)dlen : sizeof(dname) - 1;
            if (fdi_pos + (size_t)dlen > fdi_sz) break;
            memcpy(dname, fdi_buf + fdi_pos, copy_len);
            dname[copy_len] = '\0';
            fdi_pos += (size_t)dlen;
        } else if (dlen < 0) {
            size_t u16_bytes = (size_t)(-dlen * 2);
            if (fdi_pos + u16_bytes > fdi_sz) break;
            // UTF-16 to ASCII
            size_t num_chars = (size_t)(-dlen);
            if (num_chars >= sizeof(dname)) num_chars = sizeof(dname) - 1;
            for (size_t c = 0; c < num_chars; c++) {
                dname[c] = (char)fdi_buf[fdi_pos + c * 2];
            }
            dname[num_chars] = '\0';
            fdi_pos += u16_bytes;
        }

        // Clean directory name
        size_t dn_len = strlen(dname);
        while (dn_len > 0 && (dname[dn_len - 1] == '\0' || dname[dn_len - 1] == '/')) {
            dname[--dn_len] = '\0';
        }
        const char *clean_dir = dname;
        while (*clean_dir == '/') clean_dir++;
        if (*clean_dir) {
            hash_table_insert_dir(fnv1a_64_lower(clean_dir));
        }

        if (fdi_pos + 4 > fdi_sz) break;
        uint32_t num_files = *(const uint32_t *)(fdi_buf + fdi_pos); fdi_pos += 4;

        for (uint32_t fi = 0; fi < num_files && fdi_pos + 4 <= fdi_sz; fi++) {
            int32_t flen = *(const int32_t *)(fdi_buf + fdi_pos); fdi_pos += 4;
            char fname[256] = {0};
            if (flen > 0) {
                size_t copy_len = ((size_t)flen < sizeof(fname)) ? (size_t)flen : sizeof(fname) - 1;
                if (fdi_pos + (size_t)flen > fdi_sz) break;
                memcpy(fname, fdi_buf + fdi_pos, copy_len);
                fname[copy_len] = '\0';
                fdi_pos += (size_t)flen;
            } else if (flen < 0) {
                size_t u16_bytes = (size_t)(-flen * 2);
                if (fdi_pos + u16_bytes > fdi_sz) break;
                size_t num_chars = (size_t)(-flen);
                if (num_chars >= sizeof(fname)) num_chars = sizeof(fname) - 1;
                for (size_t c = 0; c < num_chars; c++) {
                    fname[c] = (char)fdi_buf[fdi_pos + c * 2];
                }
                fname[num_chars] = '\0';
                fdi_pos += u16_bytes;
            }

            if (fdi_pos + 4 > fdi_sz) break;
            int32_t enc_off = *(const int32_t *)(fdi_buf + fdi_pos); fdi_pos += 4;

            if (enc_off >= 0 && (size_t)enc_off + 4 <= (size_t)enc_sz) {
                size_t epos = (size_t)enc_off;
                uint32_t bits = *(const uint32_t *)(enc_buf + epos); epos += 4;
                uint32_t comp_slot = (bits >> 23) & 0x3f;
                uint32_t block_sz = bits & 0x3f;
                if (block_sz == 0x3f) {
                    epos += 4;
                }

                uint64_t file_offset = 0;
                if (bits & (1U << 31)) {
                    if (epos + 4 > (size_t)enc_sz) continue;
                    file_offset = *(const uint32_t *)(enc_buf + epos); epos += 4;
                } else {
                    if (epos + 8 > (size_t)enc_sz) continue;
                    file_offset = *(const uint64_t *)(enc_buf + epos); epos += 8;
                }

                uint64_t uncomp_size = 0;
                if (bits & (1U << 30)) {
                    if (epos + 4 > (size_t)enc_sz) continue;
                    uncomp_size = *(const uint32_t *)(enc_buf + epos); epos += 4;
                } else {
                    if (epos + 8 > (size_t)enc_sz) continue;
                    uncomp_size = *(const uint64_t *)(enc_buf + epos); epos += 8;
                }

                // Construct full path
                char full_path[1024];
                if (*clean_dir) {
                    snprintf(full_path, sizeof(full_path), "%s/%s", clean_dir, fname);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s", fname);
                }

                // Header in pak payload is 53 bytes for uncompressed v11 entries
                uint64_t payload_offset = file_offset + 53;
                hash_table_insert_file(fnv1a_64_lower(full_path), fd, payload_offset, uncomp_size, comp_slot);
            }
        }
    }

    printf("[ps4-pak] Loaded index from %s (entries: %u)\n", pak_path, entries_count);
    free(fdi_buf);
    free(idx_buf);
    return true;
}

static void scan_dir_for_paks(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);

        size_t nlen = strlen(de->d_name);
        if (nlen > 4 && strcasecmp(de->d_name + nlen - 4, ".pak") == 0) {
            parse_pak_file(path);
        }
    }
    closedir(dir);
}

void ps4_pak_init(const char *app_root) {
    pthread_mutex_lock(&g_pak_mutex);
    if (g_entries != NULL) {
        pthread_mutex_unlock(&g_pak_mutex);
        return;
    }

    if (!app_root || !*app_root) app_root = ".";

    // Scan standard candidate directories for .pak files
    char test_dir[1024];

    // 1. <app_root>/phoenix/content/paks
    snprintf(test_dir, sizeof(test_dir), "%s/phoenix/content/paks", app_root);
    scan_dir_for_paks(test_dir);

    // 2. <app_root>/content/paks
    snprintf(test_dir, sizeof(test_dir), "%s/content/paks", app_root);
    scan_dir_for_paks(test_dir);

    // 3. <app_root>/paks
    snprintf(test_dir, sizeof(test_dir), "%s/paks", app_root);
    scan_dir_for_paks(test_dir);

    // 4. <app_root> directly
    scan_dir_for_paks(app_root);

    printf("[ps4-pak] Total indexed files across all paks: %zu\n", g_entry_count);
    pthread_mutex_unlock(&g_pak_mutex);
}

bool ps4_pak_lookup(const char *subpath, ps4_pak_entry_t *out_entry) {
    if (!subpath || !*subpath) return false;
    while (*subpath == '/') subpath++;

    pthread_mutex_lock(&g_pak_mutex);
    if (!g_entries || g_entry_count == 0) {
        pthread_mutex_unlock(&g_pak_mutex);
        return false;
    }

    uint64_t hash = fnv1a_64_lower(subpath);
    size_t idx = hash & (g_entry_capacity - 1);
    while (g_entries[idx].hash != 0) {
        if (g_entries[idx].hash == hash) {
            if (out_entry) {
                out_entry->pak_fd = g_entries[idx].pak_fd;
                out_entry->data_offset = g_entries[idx].data_offset;
                out_entry->size = g_entries[idx].size;
                out_entry->compression = g_entries[idx].compression;
            }
            pthread_mutex_unlock(&g_pak_mutex);
            return true;
        }
        idx = (idx + 1) & (g_entry_capacity - 1);
    }

    pthread_mutex_unlock(&g_pak_mutex);
    return false;
}

bool ps4_pak_dir_exists(const char *subpath) {
    if (!subpath || !*subpath) return false;
    while (*subpath == '/') subpath++;

    char clean[1024];
    strncpy(clean, subpath, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';
    size_t len = strlen(clean);
    while (len > 0 && clean[len - 1] == '/') {
        clean[--len] = '\0';
    }

    pthread_mutex_lock(&g_pak_mutex);
    if (!g_dirs || g_dir_count == 0) {
        pthread_mutex_unlock(&g_pak_mutex);
        return false;
    }

    uint64_t hash = fnv1a_64_lower(clean);
    size_t idx = hash & (g_dir_capacity - 1);
    while (g_dirs[idx].hash != 0) {
        if (g_dirs[idx].hash == hash) {
            pthread_mutex_unlock(&g_pak_mutex);
            return true;
        }
        idx = (idx + 1) & (g_dir_capacity - 1);
    }

    pthread_mutex_unlock(&g_pak_mutex);
    return false;
}

void ps4_pak_destroy(void) {
    pthread_mutex_lock(&g_pak_mutex);
    if (g_entries) {
        free(g_entries);
        g_entries = NULL;
    }
    g_entry_count = 0;
    g_entry_capacity = 0;

    if (g_dirs) {
        free(g_dirs);
        g_dirs = NULL;
    }
    g_dir_count = 0;
    g_dir_capacity = 0;

    for (size_t i = 0; i < g_open_pak_count; i++) {
        if (g_open_pak_fds[i] >= 0) {
            close(g_open_pak_fds[i]);
        }
    }
    if (g_open_pak_fds) {
        free(g_open_pak_fds);
        g_open_pak_fds = NULL;
    }
    g_open_pak_count = 0;

    pthread_mutex_unlock(&g_pak_mutex);
}
