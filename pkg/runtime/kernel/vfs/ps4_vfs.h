#ifndef PS4_VFS_H
#define PS4_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

// Base threshold for virtual archive file descriptors
#define PS4_VFS_VIRTUAL_FD_BASE 10000

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

// Check if an fd is a virtual file descriptor managed by VFS (e.g. backed by PAK)
bool ps4_vfs_is_virtual_fd(int fd);

// Unified VFS operations handling both host filesystem and virtual archive files
int ps4_vfs_open(const char *guest_path, int host_flags, mode_t mode);
int ps4_vfs_close(int fd);
ssize_t ps4_vfs_read(int fd, void *buf, size_t count);
ssize_t ps4_vfs_pread(int fd, void *buf, size_t count, off_t offset);
off_t ps4_vfs_lseek(int fd, off_t offset, int whence);
int ps4_vfs_fstat(int fd, struct stat *st);
int ps4_vfs_stat(const char *guest_path, struct stat *st);
int ps4_vfs_lstat(const char *guest_path, struct stat *st);
int ps4_vfs_access(const char *guest_path, int mode);
void *ps4_vfs_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);

// Cleanup VFS state
void ps4_vfs_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_VFS_H
