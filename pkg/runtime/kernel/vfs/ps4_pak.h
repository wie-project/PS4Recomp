#ifndef PS4_PAK_H
#define PS4_PAK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pak_fd;
    uint64_t data_offset;
    uint64_t size;
    uint32_t compression;
} ps4_pak_entry_t;

// Initialize PAK provider by scanning app_root for .pak files
void ps4_pak_init(const char *app_root);

// Lookup a relative subpath (e.g. "engine/content/internationalization/icudt64l/lang/en.res")
// Returns true if found and fills out_entry.
bool ps4_pak_lookup(const char *subpath, ps4_pak_entry_t *out_entry);

// Compute parent locale path for CLDR/ICU resource bundle (.res) inheritance (e.g. "en_US.res" -> "en.res")
bool ps4_pak_get_parent_locale_path(const char *path, char *out_path, size_t out_sz);

// Check if a directory exists in any mounted pak archive
bool ps4_pak_dir_exists(const char *subpath);

// Cleanup all loaded pak archives
void ps4_pak_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_PAK_H
