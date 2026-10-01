#ifndef PS4_SFO_H
#define PS4_SFO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PSF_MAGIC_NUM 0x00505346 // "\0PSF" in big-endian, or bytes {0x00, 'P', 'S', 'F'}

#define PSF_FMT_BINARY 0x0004
#define PSF_FMT_STRING 0x0204
#define PSF_FMT_INT    0x0404

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t key_table_offset;
    uint32_t data_table_offset;
    uint32_t entries_count;
    bool is_allocated;
} Ps4SfoContext;

// Load and parse SFO file from filesystem
bool ps4_sfo_load_file(Ps4SfoContext *sfo, const char *path);

// Load and parse SFO from memory buffer
bool ps4_sfo_load_memory(Ps4SfoContext *sfo, const uint8_t *buf, size_t size);

// Free any allocated resources
void ps4_sfo_free(Ps4SfoContext *sfo);

// Query integer parameter (returns true if found and valid integer)
bool ps4_sfo_get_int(const Ps4SfoContext *sfo, const char *key, int32_t *out_val);

// Query string parameter (returns true if found and copies null-terminated string)
bool ps4_sfo_get_string(const Ps4SfoContext *sfo, const char *key, char *out_buf, size_t out_buf_sz);

// Find and load param.sfo from standard application paths
bool ps4_sfo_find_and_load(Ps4SfoContext *sfo);

#ifdef __cplusplus
}
#endif

#endif // PS4_SFO_H
