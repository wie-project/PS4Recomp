#include "ps4_datx.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DATX_VERSION 4300u
#define DATX_COMPRESSION_LZ4HC 7u
#define DATX_CHUNK_SIZE 0x10000u
#define DATX_MAX_PATH 4096u
#define DATX_MARKER 0x12345678u

typedef struct {
    uint32_t hash;
    uint32_t volume;
    uint64_t offset;
    uint32_t size;
    char *path;
} DatxRecord;

typedef struct {
    int fd;
    uint32_t chunk_count;
    uint32_t *stored_sizes;
    uint64_t *chunk_offsets;
    uint8_t *cached_chunk_data;
    uint32_t cached_chunk;
    bool cache_valid;
} DatxVolume;

typedef struct {
    char *root;
    uint32_t hash_a;
    uint32_t hash_b;
    uint32_t chunk_size;
    uint32_t volume_count;
    size_t record_count;
    DatxRecord *records;
    uint64_t *volume_filetimes;
    DatxVolume *volumes;
    char **directories;
    size_t directory_count;
    bool loaded;
} DatxState;

static pthread_mutex_t g_datx_mutex = PTHREAD_MUTEX_INITIALIZER;
static DatxState g_datx;
static uint8_t *g_datx_compressed_chunk;
static size_t g_datx_compressed_capacity;

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t *p) {
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p + 4) << 32);
}

static bool pread_exact(int fd, void *buf, size_t size, uint64_t offset) {
    uint8_t *out = (uint8_t *)buf;
    size_t total = 0;
    while (total < size) {
        if (total > UINT64_MAX - offset ||
            offset + total > (uint64_t)INT64_MAX) {
            errno = EOVERFLOW;
            return false;
        }
        ssize_t got = pread(fd, out + total, size - total, (off_t)(offset + total));
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (got == 0) {
            errno = EIO;
            return false;
        }
        total += (size_t)got;
    }
    return true;
}

static bool normalize_path(const char *path, char out[DATX_MAX_PATH]) {
    if (!path) return false;
    while (*path == '/') path++;

    size_t len = 0;
    for (; *path; path++) {
        if (len + 1 >= DATX_MAX_PATH) return false;
        unsigned char c = (unsigned char)*path;
        if (c == '\\') c = '/';
        out[len++] = (char)tolower(c);
    }
    out[len] = '\0';
    return true;
}

static uint32_t datx_name_hash(const char *path, uint32_t seed_a, uint32_t seed_b) {
    uint32_t result = 0;
    uint32_t a = seed_a;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        uint32_t c = *p == '\\' ? '/' : (uint32_t)toupper(*p);
        a = (uint32_t)(a * seed_b) % 0x7ffffffeu;
        result = (uint32_t)(result * a + c) % 0x7fffffffu;
    }
    return result;
}

static bool bpe_expand(uint8_t token, const uint8_t *first, const uint8_t *second,
                       size_t half_size, char *out, size_t *out_len,
                       size_t out_capacity, unsigned depth) {
    if (depth > 32) return false;
    if (token < 0x80) {
        if (token == 0 || *out_len >= out_capacity) return false;
        out[(*out_len)++] = (char)token;
        return true;
    }
    size_t index = (size_t)(token - 0x80);
    if (index >= half_size) return false;
    return bpe_expand(first[index], first, second, half_size, out, out_len,
                      out_capacity, depth + 1) &&
           bpe_expand(second[index], first, second, half_size, out, out_len,
                      out_capacity, depth + 1);
}

static size_t find_record(const DatxState *state, uint32_t hash) {
    size_t lo = 0, hi = state->record_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (state->records[mid].hash < hash) lo = mid + 1;
        else hi = mid;
    }
    if (lo == state->record_count || state->records[lo].hash != hash) return SIZE_MAX;
    return lo;
}

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool append_directory(char ***dirs, size_t *count, size_t *capacity,
                             const char *path, size_t length) {
    if (length == 0) return true;
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 256;
        char **grown = (char **)realloc(*dirs, next * sizeof(**dirs));
        if (!grown) return false;
        *dirs = grown;
        *capacity = next;
    }
    char *dir = (char *)malloc(length + 1);
    if (!dir) return false;
    memcpy(dir, path, length);
    dir[length] = '\0';
    (*dirs)[(*count)++] = dir;
    return true;
}

static bool build_directories(DatxState *state) {
    size_t capacity = 0;
    for (size_t i = 0; i < state->record_count; i++) {
        const char *path = state->records[i].path;
        if (!path) return false;
        for (const char *slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/')) {
            if (!append_directory(&state->directories, &state->directory_count,
                                  &capacity, path, (size_t)(slash - path))) {
                return false;
            }
        }
    }

    qsort(state->directories, state->directory_count, sizeof(*state->directories),
          compare_strings);
    size_t unique = 0;
    for (size_t i = 0; i < state->directory_count; i++) {
        if (unique && strcmp(state->directories[unique - 1], state->directories[i]) == 0) {
            free(state->directories[i]);
            continue;
        }
        state->directories[unique++] = state->directories[i];
    }
    state->directory_count = unique;
    return true;
}

static void clear_state(DatxState *state) {
    if (!state) return;
    for (uint32_t i = 0; i < state->volume_count; i++) {
        DatxVolume *volume = state->volumes ? &state->volumes[i] : NULL;
        if (!volume) break;
        if (volume->fd >= 0) close(volume->fd);
        free(volume->stored_sizes);
        free(volume->chunk_offsets);
        free(volume->cached_chunk_data);
    }
    for (size_t i = 0; i < state->record_count; i++) {
        free(state->records ? state->records[i].path : NULL);
    }
    for (size_t i = 0; i < state->directory_count; i++) {
        free(state->directories[i]);
    }
    free(state->root);
    free(state->records);
    free(state->volume_filetimes);
    free(state->volumes);
    free(state->directories);
    memset(state, 0, sizeof(*state));
}

static bool parse_index(DatxState *state, const char *app_root) {
    char index_path[DATX_MAX_PATH];
    int path_len = snprintf(index_path, sizeof(index_path), "%s/game.ndx", app_root);
    if (path_len < 0 || (size_t)path_len >= sizeof(index_path)) {
        errno = ENAMETOOLONG;
        return false;
    }

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return false;
    }
    if (st.st_size < 64 || (uint64_t)st.st_size > SIZE_MAX) {
        close(fd);
        errno = EINVAL;
        return false;
    }

    size_t index_size = (size_t)st.st_size;
    uint8_t *index = (uint8_t *)malloc(index_size);
    if (!index) {
        close(fd);
        errno = ENOMEM;
        return false;
    }
    bool read_ok = pread_exact(fd, index, index_size, 0);
    close(fd);
    if (!read_ok) {
        free(index);
        return false;
    }

    if (memcmp(index, "DATX", 4) != 0 || read_u32le(index + 4) != DATX_VERSION ||
        read_u32le(index + 0x20) != DATX_COMPRESSION_LZ4HC ||
        read_u32le(index + 0x24) != DATX_CHUNK_SIZE) {
        fprintf(stderr, "[ps4-datx] Unsupported game.ndx header/version\n");
        free(index);
        errno = ENOTSUP;
        return false;
    }

    state->hash_a = read_u32le(index + 0x18);
    state->hash_b = read_u32le(index + 0x1c);
    state->volume_count = read_u32le(index + 0x30);
    state->record_count = read_u32le(index + 0x38);
    state->chunk_size = read_u32le(index + 0x24);
    if (state->volume_count == 0 || state->volume_count > 65536 ||
        state->record_count == 0 ||
        state->record_count > (index_size / 32) ||
        index[0x3c] == 0 || index[0x3c] > 128) {
        fprintf(stderr,
                "[ps4-datx] Invalid game.ndx counts: volumes=%u files=%zu dict=%u\n",
                state->volume_count, state->record_count, index[0x3c]);
        free(index);
        errno = EINVAL;
        return false;
    }

    size_t half_size = index[0x3c];
    size_t pos = 0x3d;
    size_t dictionary_bytes = half_size * 2;
    if (dictionary_bytes > index_size - pos) {
        free(index);
        errno = EINVAL;
        return false;
    }
    const uint8_t *dict_first = index + pos;
    const uint8_t *dict_second = dict_first + half_size;
    pos += dictionary_bytes;

    if (index_size - pos < 4) {
        free(index);
        errno = EINVAL;
        return false;
    }
    uint32_t names_size = read_u32le(index + pos);
    pos += 4;
    if (names_size > index_size - pos) {
        free(index);
        errno = EINVAL;
        return false;
    }
    const uint8_t *names = index + pos;
    pos += names_size;
    if (index_size - pos < 4 || read_u32le(index + pos) != DATX_MARKER) {
        free(index);
        errno = EINVAL;
        return false;
    }
    pos += 4;

    if (state->record_count > (index_size - pos) / 32) {
        free(index);
        errno = EINVAL;
        return false;
    }
    size_t records_pos = pos;
    size_t end_tag_pos = records_pos + state->record_count * 32;
    size_t times_pos = end_tag_pos + 4;
    if (times_pos > index_size || index_size - times_pos < 8 ||
        state->volume_count > (index_size - times_pos - 8) / 8 ||
        times_pos + (size_t)state->volume_count * 8 != index_size - 8 ||
        read_u32le(index + end_tag_pos) != DATX_MARKER ||
        read_u64le(index + index_size - 8) != index_size - 8) {
        fprintf(stderr,
                "[ps4-datx] Invalid game.ndx trailer/layout: index=%zu records=%zu "
                "volumes=%u times=%zu\n",
                index_size, state->record_count, state->volume_count, times_pos);
        free(index);
        errno = EINVAL;
        return false;
    }

    state->records = (DatxRecord *)calloc(state->record_count, sizeof(*state->records));
    state->volume_filetimes =
        (uint64_t *)calloc(state->volume_count, sizeof(*state->volume_filetimes));
    state->volumes = (DatxVolume *)calloc(state->volume_count, sizeof(*state->volumes));
    if (!state->records || !state->volume_filetimes || !state->volumes) {
        free(index);
        errno = ENOMEM;
        return false;
    }
    for (uint32_t i = 0; i < state->volume_count; i++) state->volumes[i].fd = -1;

    for (size_t i = 0; i < state->record_count; i++) {
        const uint8_t *record = index + records_pos + i * 32;
        uint64_t packed_location = read_u64le(record);
        DatxRecord *dst = &state->records[i];
        dst->volume = (uint32_t)(packed_location & 0xffffu);
        dst->offset = packed_location >> 16;
        dst->size = read_u32le(record + 8);
        dst->hash = read_u32le(record + 12);
        if (dst->volume >= state->volume_count || dst->hash > 0x7fffffffu ||
            (i && state->records[i - 1].hash >= dst->hash)) {
            fprintf(stderr,
                    "[ps4-datx] Invalid game.ndx record %zu: volume=%u hash=%u\n",
                    i, dst->volume, dst->hash);
            free(index);
            errno = EINVAL;
            return false;
        }
    }
    for (uint32_t i = 0; i < state->volume_count; i++) {
        state->volume_filetimes[i] = read_u64le(index + times_pos + (size_t)i * 8);
    }

    size_t name_pos = 0;
    for (size_t i = 0; i < state->record_count; i++) {
        char decoded[DATX_MAX_PATH];
        size_t decoded_len = 0;
        bool terminated = false;
        while (name_pos < names_size) {
            uint8_t token = names[name_pos++];
            if (token == 0) {
                terminated = true;
                break;
            }
            if (!bpe_expand(token, dict_first, dict_second, half_size, decoded,
                            &decoded_len, sizeof(decoded) - 1, 0)) {
                fprintf(stderr,
                        "[ps4-datx] Invalid BPE name %zu at encoded offset %zu\n",
                        i, name_pos);
                free(index);
                errno = EINVAL;
                return false;
            }
        }
        if (!terminated || decoded_len == 0) {
            fprintf(stderr,
                    "[ps4-datx] Unterminated/empty BPE name %zu at encoded offset %zu\n",
                    i, name_pos);
            free(index);
            errno = EINVAL;
            return false;
        }
        decoded[decoded_len] = '\0';
        char normalized[DATX_MAX_PATH];
        if (!normalize_path(decoded, normalized)) {
            free(index);
            errno = ENAMETOOLONG;
            return false;
        }

        uint32_t hash = datx_name_hash(normalized, state->hash_a, state->hash_b);
        size_t record_index = find_record(state, hash);
        if (record_index == SIZE_MAX || state->records[record_index].path) {
            fprintf(stderr,
                    "[ps4-datx] Name/hash mismatch for '%s' (hash=%u, name=%zu)\n",
                    normalized, hash, i);
            free(index);
            errno = EINVAL;
            return false;
        }
        state->records[record_index].path = strdup(normalized);
        if (!state->records[record_index].path) {
            free(index);
            errno = ENOMEM;
            return false;
        }
    }
    while (name_pos < names_size && names[name_pos] == 0) name_pos++;
    if (name_pos != names_size) {
        fprintf(stderr,
                "[ps4-datx] Encoded-name block has trailing bytes: consumed=%zu size=%u\n",
                name_pos, names_size);
        free(index);
        errno = EINVAL;
        return false;
    }
    free(index);

    for (size_t i = 0; i < state->record_count; i++) {
        if (!state->records[i].path) {
            fprintf(stderr, "[ps4-datx] No name found for index record %zu\n", i);
            errno = EINVAL;
            return false;
        }
    }
    if (!build_directories(state)) {
        errno = ENOMEM;
        return false;
    }
    state->root = strdup(app_root);
    if (!state->root) {
        errno = ENOMEM;
        return false;
    }
    return true;
}

int ps4_datx_init(const char *app_root) {
    if (!app_root || !*app_root) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&g_datx_mutex);
    if (g_datx.loaded) {
        pthread_mutex_unlock(&g_datx_mutex);
        return 1;
    }

    DatxState state = {0};
    if (!parse_index(&state, app_root)) {
        int saved = errno;
        clear_state(&state);
        pthread_mutex_unlock(&g_datx_mutex);
        if (saved == ENOENT) return 0;
        errno = saved;
        return -1;
    }

    size_t compressed_capacity =
        state.chunk_size + state.chunk_size / 255 + 64;
    uint8_t *compressed = (uint8_t *)malloc(compressed_capacity);
    if (!compressed) {
        clear_state(&state);
        pthread_mutex_unlock(&g_datx_mutex);
        errno = ENOMEM;
        return -1;
    }
    state.loaded = true;
    g_datx = state;
    g_datx_compressed_chunk = compressed;
    g_datx_compressed_capacity = compressed_capacity;
    printf("[ps4-datx] Loaded %s/game.ndx (files=%zu, volumes=%u)\n",
           app_root, g_datx.record_count, g_datx.volume_count);
    pthread_mutex_unlock(&g_datx_mutex);
    return 1;
}

void ps4_datx_destroy(void) {
    pthread_mutex_lock(&g_datx_mutex);
    clear_state(&g_datx);
    free(g_datx_compressed_chunk);
    g_datx_compressed_chunk = NULL;
    g_datx_compressed_capacity = 0;
    pthread_mutex_unlock(&g_datx_mutex);
}

bool ps4_datx_lookup(const char *subpath, ps4_datx_entry_t *out_entry) {
    if (!subpath) return false;
    char normalized[DATX_MAX_PATH];
    if (!normalize_path(subpath, normalized) || !*normalized) return false;

    pthread_mutex_lock(&g_datx_mutex);
    if (!g_datx.loaded) {
        pthread_mutex_unlock(&g_datx_mutex);
        return false;
    }
    uint32_t hash = datx_name_hash(normalized, g_datx.hash_a, g_datx.hash_b);
    size_t index = find_record(&g_datx, hash);
    if (index == SIZE_MAX || strcmp(g_datx.records[index].path, normalized) != 0) {
        pthread_mutex_unlock(&g_datx_mutex);
        return false;
    }
    if (out_entry) {
        out_entry->record_index = index;
        out_entry->size = g_datx.records[index].size;
    }
    pthread_mutex_unlock(&g_datx_mutex);
    return true;
}

bool ps4_datx_dir_exists(const char *subpath) {
    if (!subpath) return false;
    char normalized[DATX_MAX_PATH];
    if (!normalize_path(subpath, normalized)) return false;
    size_t path_len = strlen(normalized);
    while (path_len && normalized[path_len - 1] == '/') {
        normalized[--path_len] = '\0';
    }

    pthread_mutex_lock(&g_datx_mutex);
    if (!g_datx.loaded) {
        pthread_mutex_unlock(&g_datx_mutex);
        return false;
    }
    if (!*normalized) {
        pthread_mutex_unlock(&g_datx_mutex);
        return true;
    }
    size_t lo = 0, hi = g_datx.directory_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(g_datx.directories[mid], normalized);
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    bool found = lo < g_datx.directory_count &&
                 strcmp(g_datx.directories[lo], normalized) == 0;
    pthread_mutex_unlock(&g_datx_mutex);
    return found;
}

static bool lz4_decompress_block(const uint8_t *src, size_t src_size,
                                 uint8_t *dst, size_t dst_capacity,
                                 size_t *dst_size) {
    const uint8_t *in = src;
    const uint8_t *in_end = src + src_size;
    size_t out_len = 0;
    while (in < in_end) {
        uint8_t token = *in++;
        size_t literal_len = token >> 4;
        if (literal_len == 15) {
            uint8_t extension;
            do {
                if (in >= in_end || literal_len > SIZE_MAX - 255) return false;
                extension = *in++;
                literal_len += extension;
            } while (extension == 255);
        }
        if (literal_len > (size_t)(in_end - in) ||
            literal_len > dst_capacity - out_len) {
            return false;
        }
        memcpy(dst + out_len, in, literal_len);
        in += literal_len;
        out_len += literal_len;
        if (in == in_end) break;

        if ((size_t)(in_end - in) < 2) return false;
        size_t match_offset = (size_t)in[0] | ((size_t)in[1] << 8);
        in += 2;
        if (match_offset == 0 || match_offset > out_len) return false;

        size_t match_len = (token & 0x0f) + 4;
        if ((token & 0x0f) == 15) {
            uint8_t extension;
            do {
                if (in >= in_end || match_len > SIZE_MAX - 255) return false;
                extension = *in++;
                match_len += extension;
            } while (extension == 255);
        }
        if (match_len > dst_capacity - out_len) return false;
        for (size_t i = 0; i < match_len; i++) {
            dst[out_len + i] = dst[out_len + i - match_offset];
        }
        out_len += match_len;
    }
    *dst_size = out_len;
    return true;
}

static bool initialize_volume(uint32_t volume_index) {
    DatxVolume *volume = &g_datx.volumes[volume_index];
    if (volume->fd >= 0) return true;

    char path[DATX_MAX_PATH];
    int path_len = snprintf(path, sizeof(path), "%s/game%03u.dat",
                            g_datx.root, volume_index);
    if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return false;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;

    struct stat st;
    uint8_t header[36];
    if (fstat(fd, &st) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return false;
    }
    if (st.st_size < (off_t)sizeof(header)) {
        close(fd);
        errno = EINVAL;
        return false;
    }
    if (!pread_exact(fd, header, sizeof(header), 0)) {
        int saved = errno;
        close(fd);
        errno = saved;
        return false;
    }
    if (memcmp(header, "DATF", 4) != 0 ||
        read_u32le(header + 4) != DATX_VERSION ||
        read_u64le(header + 8) != g_datx.volume_filetimes[volume_index] ||
        memcmp(header + 32, "CHNK", 4) != 0) {
        close(fd);
        errno = EINVAL;
        return false;
    }

    uint32_t chunk_count = read_u32le(header + 24);
    uint32_t chunk_size = read_u32le(header + 28);
    if (chunk_size != g_datx.chunk_size ||
        chunk_count > ((uint64_t)st.st_size - sizeof(header)) / 4) {
        close(fd);
        errno = EINVAL;
        return false;
    }
    uint64_t table_bytes = (uint64_t)chunk_count * 4;
    uint64_t marker_offset = sizeof(header) + table_bytes;
    uint8_t marker[4];
    if (marker_offset + sizeof(marker) > (uint64_t)st.st_size ||
        !pread_exact(fd, marker, sizeof(marker), marker_offset) ||
        memcmp(marker, "DATA", 4) != 0) {
        int saved = errno ? errno : EINVAL;
        close(fd);
        errno = saved;
        return false;
    }

    uint32_t *sizes = (uint32_t *)calloc(chunk_count ? chunk_count : 1, sizeof(*sizes));
    uint64_t *offsets =
        (uint64_t *)calloc((size_t)chunk_count + 1, sizeof(*offsets));
    uint8_t *cache = (uint8_t *)malloc(g_datx.chunk_size);
    if (!sizes || !offsets || !cache) {
        free(sizes);
        free(offsets);
        free(cache);
        close(fd);
        errno = ENOMEM;
        return false;
    }
    if (table_bytes) {
        uint8_t *raw_sizes = (uint8_t *)malloc((size_t)table_bytes);
        if (!raw_sizes) {
            free(sizes);
            free(offsets);
            free(cache);
            close(fd);
            errno = ENOMEM;
            return false;
        }
        if (!pread_exact(fd, raw_sizes, (size_t)table_bytes, sizeof(header))) {
            int saved = errno;
            free(raw_sizes);
            free(sizes);
            free(offsets);
            free(cache);
            close(fd);
            errno = saved;
            return false;
        }
        for (uint32_t i = 0; i < chunk_count; i++) {
            sizes[i] = read_u32le(raw_sizes + (size_t)i * 4);
        }
        free(raw_sizes);
    }

    uint64_t data_offset = marker_offset + sizeof(marker);
    offsets[0] = data_offset;
    for (uint32_t i = 0; i < chunk_count; i++) {
        if (sizes[i] > g_datx_compressed_capacity ||
            offsets[i] > (uint64_t)st.st_size ||
            sizes[i] > (uint64_t)st.st_size - offsets[i]) {
            free(sizes);
            free(offsets);
            free(cache);
            close(fd);
            errno = EINVAL;
            return false;
        }
        offsets[i + 1] = offsets[i] + sizes[i];
    }

    volume->fd = fd;
    volume->chunk_count = chunk_count;
    volume->stored_sizes = sizes;
    volume->chunk_offsets = offsets;
    volume->cached_chunk_data = cache;
    volume->cache_valid = false;
    return true;
}

static bool load_chunk(uint32_t volume_index, uint32_t chunk_index) {
    DatxVolume *volume = &g_datx.volumes[volume_index];
    if (volume->cache_valid && volume->cached_chunk == chunk_index) return true;
    if (chunk_index >= volume->chunk_count) {
        errno = EIO;
        return false;
    }

    uint32_t stored_size = volume->stored_sizes[chunk_index];
    if (stored_size == 0) {
        memset(volume->cached_chunk_data, 0, g_datx.chunk_size);
    } else if (stored_size == g_datx.chunk_size) {
        if (!pread_exact(volume->fd, volume->cached_chunk_data,
                         g_datx.chunk_size,
                         volume->chunk_offsets[chunk_index])) {
            return false;
        }
    } else {
        if (stored_size < 4 || stored_size > g_datx_compressed_capacity) {
            errno = EIO;
            return false;
        }
        if (!pread_exact(volume->fd, g_datx_compressed_chunk, stored_size,
                         volume->chunk_offsets[chunk_index])) {
            return false;
        }
        uint32_t decoded_size = read_u32le(g_datx_compressed_chunk);
        size_t actual_size = 0;
        if (decoded_size != g_datx.chunk_size ||
            !lz4_decompress_block(g_datx_compressed_chunk + 4, stored_size - 4,
                                  volume->cached_chunk_data, g_datx.chunk_size,
                                  &actual_size) ||
            actual_size != g_datx.chunk_size) {
            errno = EIO;
            return false;
        }
    }
    volume->cached_chunk = chunk_index;
    volume->cache_valid = true;
    return true;
}

ssize_t ps4_datx_pread(const ps4_datx_entry_t *entry, void *buf, size_t count,
                       off_t offset) {
    if (!entry || (!buf && count) || offset < 0) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&g_datx_mutex);
    if (!g_datx.loaded || entry->record_index >= g_datx.record_count) {
        pthread_mutex_unlock(&g_datx_mutex);
        errno = EBADF;
        return -1;
    }

    const DatxRecord *record = &g_datx.records[entry->record_index];
    uint64_t position = (uint64_t)offset;
    if (position >= record->size || count == 0) {
        pthread_mutex_unlock(&g_datx_mutex);
        return 0;
    }
    size_t remaining = count;
    if ((uint64_t)remaining > record->size - position) {
        remaining = (size_t)(record->size - position);
    }
    uint8_t *out = (uint8_t *)buf;
    size_t total = 0;
    while (total < remaining) {
        if (record->offset > UINT64_MAX - position) {
            errno = EOVERFLOW;
            break;
        }
        uint64_t stream_position = record->offset + position;
        uint64_t chunk_index64 = stream_position / g_datx.chunk_size;
        if (chunk_index64 > UINT32_MAX) {
            errno = EOVERFLOW;
            break;
        }
        if (!initialize_volume(record->volume)) {
            break;
        }
        if (chunk_index64 >= g_datx.volumes[record->volume].chunk_count) {
            errno = EIO;
            break;
        }
        uint32_t chunk_index = (uint32_t)chunk_index64;
        size_t in_chunk = (size_t)(stream_position % g_datx.chunk_size);
        size_t amount = g_datx.chunk_size - in_chunk;
        if (amount > remaining - total) amount = remaining - total;
        if (!load_chunk(record->volume, chunk_index)) break;
        memcpy(out + total,
               g_datx.volumes[record->volume].cached_chunk_data + in_chunk,
               amount);
        total += amount;
        position += amount;
    }
    int saved = errno;
    pthread_mutex_unlock(&g_datx_mutex);
    if (total == 0 && remaining != 0 && saved != 0) {
        errno = saved;
        return -1;
    }
    return (ssize_t)total;
}
