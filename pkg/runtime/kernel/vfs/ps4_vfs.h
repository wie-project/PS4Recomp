#ifndef PS4_VFS_H
#define PS4_VFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize VFS with host directory path corresponding to /app0
void ps4_vfs_init(const char *app_root);

// Returns the current app root path
const char *ps4_vfs_get_app_root(void);

// Register a custom mount point: guest_prefix (e.g. "/temp0" or "/addcont0") -> host_path
int ps4_vfs_mount(const char *guest_prefix, const char *host_path);

// Unregister a mount point
int ps4_vfs_unmount(const char *guest_prefix);

// Resolves a guest path (e.g. /app0/assets/images/logo.png) to host filesystem path
// Returns 0 on success, or negative errno on error.
int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz);

// Cleanup VFS state
void ps4_vfs_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_VFS_H
