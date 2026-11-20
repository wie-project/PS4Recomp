#ifndef PS4_DATX_H
#define PS4_DATX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t record_index;
    uint64_t size;
} ps4_datx_entry_t;

// Loads DriveClub's read-only game.ndx index when present in app_root.
// Returns 1 when loaded, 0 when game.ndx is absent, or -1 on an invalid index/error.
int ps4_datx_init(const char *app_root);
void ps4_datx_destroy(void);

bool ps4_datx_lookup(const char *subpath, ps4_datx_entry_t *out_entry);
bool ps4_datx_dir_exists(const char *subpath);
ssize_t ps4_datx_pread(const ps4_datx_entry_t *entry, void *buf, size_t count,
                       off_t offset);

#ifdef __cplusplus
}
#endif

#endif
