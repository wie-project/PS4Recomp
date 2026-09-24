#ifndef PS4_VFS_H
#define PS4_VFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize VFS with host directory path corresponding to /app0
void ps4_vfs_init(const char *app_root);

// Resolves a guest path (e.g. /app0/assets/images/logo.png) to host filesystem path
// Returns 0 on success, or negative errno on error.
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz);

// Cleanup VFS state
void ps4_vfs_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_VFS_H
