#include "ps4_vfs.h"
#include "ps4_pak.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

static char *g_app_root = NULL;
static pthread_mutex_t g_vfs_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_VIRTUAL_FILES 256
typedef struct {
    int active;
    int pak_fd;
    uint64_t data_offset;
    uint64_t file_size;
    off_t cur_offset;
    char path[512];
} VirtualPakFile;

static VirtualPakFile g_vfiles[MAX_VIRTUAL_FILES];
static pthread_mutex_t g_vfile_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *discover_app_root(const char *hint) {
    // 1. Environment variable override
    const char *env = getenv("PS4_APP_DIR");
    if (env && *env && access(env, F_OK) == 0) {
        char resolved[1024];
        if (realpath(env, resolved) != NULL) {
            return strdup(resolved);
        }
        return strdup(env);
    }

#if defined(__APPLE__)
    // 2. Check if running inside macOS .app bundle: <exe_dir>/../Resources
    char exec_path[1024] = {0};
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) == 0) {
        char canonical_exec[1024];
        if (realpath(exec_path, canonical_exec) != NULL) {
            char dir_buf[1024];
            strncpy(dir_buf, canonical_exec, sizeof(dir_buf) - 1);
            dir_buf[sizeof(dir_buf) - 1] = '\0';
            char *dir = dirname(dir_buf);

            // Check inside macOS .app bundle: <exe_dir>/../Resources
            char test_path[1024];
            snprintf(test_path, sizeof(test_path), "%s/../Resources", dir);
            char canonical_res[1024];
            if (realpath(test_path, canonical_res) != NULL && access(canonical_res, F_OK) == 0) {
                return strdup(canonical_res);
            }

            // Check <exe_dir>/assets
            snprintf(test_path, sizeof(test_path), "%s/assets", dir);
            if (access(test_path, F_OK) == 0) {
                if (realpath(dir, canonical_res) != NULL) {
                    return strdup(canonical_res);
                }
                return strdup(dir);
            }

            // Check parent directory hierarchy up to 4 levels for assets/
            char cur[1024];
            strncpy(cur, dir, sizeof(cur) - 1);
            cur[sizeof(cur) - 1] = '\0';
            for (int i = 0; i < 4; i++) {
                char *parent = dirname(cur);
                snprintf(test_path, sizeof(test_path), "%s/assets", parent);
                if (access(test_path, F_OK) == 0) {
                    if (realpath(parent, canonical_res) != NULL) {
                        return strdup(canonical_res);
                    }
                    return strdup(parent);
                }
                strncpy(cur, parent, sizeof(cur) - 1);
                cur[sizeof(cur) - 1] = '\0';
            }
        }
    }
#endif

    // 3. Check compile-time hint if provided and contains valid assets
    if (hint && *hint && access(hint, F_OK) == 0) {
        char check_path[1024];
        snprintf(check_path, sizeof(check_path), "%s/Media", hint);
        int has_media = (access(check_path, F_OK) == 0);
        snprintf(check_path, sizeof(check_path), "%s/sce_sys", hint);
        int has_sce_sys = (access(check_path, F_OK) == 0);
        snprintf(check_path, sizeof(check_path), "%s/param.sfo", hint);
        int has_sfo = (access(check_path, F_OK) == 0);

        if (has_media || has_sce_sys || has_sfo) {
            char canonical_hint[1024];
            if (realpath(hint, canonical_hint) != NULL) {
                return strdup(canonical_hint);
            }
            return strdup(hint);
        }
    }

    // 4. Check current working directory ./assets or ./Media
    if (access("assets", F_OK) == 0 || access("Media", F_OK) == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            return strdup(cwd);
        }
        return strdup(".");
    }

    // 5. Fallback to hint if provided, otherwise "."
    if (hint && *hint && access(hint, F_OK) == 0) {
        char canonical_hint[1024];
        if (realpath(hint, canonical_hint) != NULL) {
            return strdup(canonical_hint);
        }
        return strdup(hint);
    }

    return strdup(".");
}

#define MAX_VFS_MOUNTS 32

typedef struct {
    char guest_prefix[64];
    char host_path[1024];
    int active;
} VfsMount;

static VfsMount g_mounts[MAX_VFS_MOUNTS];
static int g_mount_count = 0;

void ps4_vfs_init(const char *app_root) {
    pthread_mutex_lock(&g_vfs_mutex);
    if (g_app_root) {
        free(g_app_root);
        g_app_root = NULL;
    }

    g_app_root = discover_app_root(app_root);

    // Strip trailing slash if present
    if (g_app_root) {
        size_t len = strlen(g_app_root);
        while (len > 1 && g_app_root[len - 1] == '/') {
            g_app_root[len - 1] = '\0';
            len--;
        }
    }

    printf("[ps4-vfs] Application root: %s\n", g_app_root ? g_app_root : "(null)");

    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;

    pthread_mutex_lock(&g_vfile_mutex);
    memset(g_vfiles, 0, sizeof(g_vfiles));
    pthread_mutex_unlock(&g_vfile_mutex);

    pthread_mutex_unlock(&g_vfs_mutex);

    // Initialize universal PAK provider
    ps4_pak_init(g_app_root);
}

const char *ps4_vfs_get_app_root(void) {
    pthread_mutex_lock(&g_vfs_mutex);
    const char *root = g_app_root ? g_app_root : ".";
    pthread_mutex_unlock(&g_vfs_mutex);
    return root;
}

int ps4_vfs_mount(const char *guest_prefix, const char *host_path) {
    if (!guest_prefix || !host_path || *guest_prefix == '\0' || *host_path == '\0') {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_vfs_mutex);
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].guest_prefix, guest_prefix) == 0) {
            strncpy(g_mounts[i].host_path, host_path, sizeof(g_mounts[i].host_path) - 1);
            g_mounts[i].host_path[sizeof(g_mounts[i].host_path) - 1] = '\0';
            pthread_mutex_unlock(&g_vfs_mutex);
            return 0;
        }
    }

    if (g_mount_count >= MAX_VFS_MOUNTS) {
        pthread_mutex_unlock(&g_vfs_mutex);
        return -ENOMEM;
    }

    VfsMount *m = &g_mounts[g_mount_count++];
    strncpy(m->guest_prefix, guest_prefix, sizeof(m->guest_prefix) - 1);
    m->guest_prefix[sizeof(m->guest_prefix) - 1] = '\0';
    strncpy(m->host_path, host_path, sizeof(m->host_path) - 1);
    m->host_path[sizeof(m->host_path) - 1] = '\0';
    m->active = 1;

    pthread_mutex_unlock(&g_vfs_mutex);
    return 0;
}

int ps4_vfs_unmount(const char *guest_prefix) {
    if (!guest_prefix) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_vfs_mutex);
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].guest_prefix, guest_prefix) == 0) {
            g_mounts[i].active = 0;
            pthread_mutex_unlock(&g_vfs_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_vfs_mutex);
    return -ENOENT;
}

static void normalize_chroot_subpath(const char *subpath, char *out, size_t out_sz) {
    if (!subpath || !out || out_sz == 0) return;
    out[0] = '\0';

    char buf[1024];
    size_t slen = strlen(subpath);
    if (slen >= sizeof(buf)) {
        slen = sizeof(buf) - 1;
    }
    memcpy(buf, subpath, slen);
    buf[slen] = '\0';

    const char *segs[64];
    int seg_count = 0;

    char *saveptr = NULL;
    char *token = strtok_r(buf, "/", &saveptr);
    while (token) {
        if (strcmp(token, ".") == 0 || *token == '\0') {
            // Ignore current directory
        } else if (strcmp(token, "..") == 0) {
            if (seg_count > 0) {
                seg_count--;
            }
        } else {
            if (seg_count < 64) {
                segs[seg_count++] = token;
            }
        }
        token = strtok_r(NULL, "/", &saveptr);
    }

    size_t written = 0;
    for (int i = 0; i < seg_count; i++) {
        size_t part_len = strlen(segs[i]);
        if (written + part_len + 2 >= out_sz) break;
        if (written > 0) {
            out[written++] = '/';
        }
        memcpy(out + written, segs[i], part_len);
        written += part_len;
        out[written] = '\0';
    }
    out[written] = '\0';
}

static int case_insensitive_lookup(const char *host_path, char *out_path, size_t out_sz) {
    if (!host_path || !out_path || out_sz == 0) return -EINVAL;

    if (access(host_path, F_OK) == 0) {
        if (strlen(host_path) >= out_sz) return -ENAMETOOLONG;
        strncpy(out_path, host_path, out_sz - 1);
        out_path[out_sz - 1] = '\0';
        return 0;
    }

    char temp[1024];
    size_t hlen = strlen(host_path);
    if (hlen >= sizeof(temp)) return -ENAMETOOLONG;
    memcpy(temp, host_path, hlen + 1);

    const char *parts[64];
    int num_parts = 0;

    char *curr = temp;
    while (*curr) {
        if (access(curr, F_OK) == 0) {
            break;
        }
        char *slash = strrchr(curr, '/');
        if (!slash || slash == curr) {
            if (slash == curr) {
                if (num_parts < 64) parts[num_parts++] = slash + 1;
                *slash = '\0';
            } else {
                if (num_parts < 64) parts[num_parts++] = curr;
                curr[0] = '\0';
            }
            break;
        }
        if (num_parts < 64) parts[num_parts++] = slash + 1;
        *slash = '\0';
    }

    char resolved[1024];
    if (*curr) {
        strncpy(resolved, curr, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    } else {
        strncpy(resolved, "/", sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    for (int i = num_parts - 1; i >= 0; i--) {
        const char *part = parts[i];
        if (!part || *part == '\0') {
            continue;
        }

        char test_path[1024];
        int is_root = (strcmp(resolved, "/") == 0);
        snprintf(test_path, sizeof(test_path), "%s%s%s", resolved, is_root ? "" : "/", part);
        if (access(test_path, F_OK) == 0) {
            strncpy(resolved, test_path, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
            continue;
        }

        DIR *dir = opendir(resolved);
        int matched = 0;
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                if (strcasecmp(de->d_name, part) == 0) {
                    snprintf(test_path, sizeof(test_path), "%s%s%s", resolved, is_root ? "" : "/", de->d_name);
                    strncpy(resolved, test_path, sizeof(resolved) - 1);
                    resolved[sizeof(resolved) - 1] = '\0';
                    matched = 1;
                    break;
                }
            }
            closedir(dir);
        }

        if (!matched) {
            snprintf(test_path, sizeof(test_path), "%s%s%s", resolved, is_root ? "" : "/", part);
            strncpy(resolved, test_path, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
        }
    }

    if (strlen(resolved) >= out_sz) return -ENAMETOOLONG;
    strncpy(out_path, resolved, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    return 0;
}

int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    if (!guest_path || !host_path || host_path_sz == 0) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_vfs_mutex);
    const char *root = g_app_root ? g_app_root : ".";
    char raw_host[1024];
    raw_host[0] = '\0';

    for (int i = 0; i < g_mount_count; i++) {
        if (!g_mounts[i].active) continue;
        size_t plen = strlen(g_mounts[i].guest_prefix);
        if (strncmp(guest_path, g_mounts[i].guest_prefix, plen) == 0 &&
            (guest_path[plen] == '/' || guest_path[plen] == '\0')) {
            const char *subpath = guest_path + plen;
            while (*subpath == '/') subpath++;

            char norm_subpath[1024];
            normalize_chroot_subpath(subpath, norm_subpath, sizeof(norm_subpath));
            if (norm_subpath[0]) {
                snprintf(raw_host, sizeof(raw_host), "%s/%s", g_mounts[i].host_path, norm_subpath);
            } else {
                snprintf(raw_host, sizeof(raw_host), "%s", g_mounts[i].host_path);
            }
            break;
        }
    }

    if (raw_host[0] == '\0') {
        if (strncmp(guest_path, "/app0", 5) == 0 && (guest_path[5] == '/' || guest_path[5] == '\0')) {
            const char *subpath = guest_path + 5;
            while (*subpath == '/') subpath++;

            char norm_subpath[1024];
            normalize_chroot_subpath(subpath, norm_subpath, sizeof(norm_subpath));
            if (norm_subpath[0]) {
                snprintf(raw_host, sizeof(raw_host), "%s/%s", root, norm_subpath);
            } else {
                snprintf(raw_host, sizeof(raw_host), "%s", root);
            }
        } else if (strncmp(guest_path, "/hostapp", 8) == 0 && (guest_path[8] == '/' || guest_path[8] == '\0')) {
            const char *subpath = guest_path + 8;
            while (*subpath == '/') subpath++;

            char norm_subpath[1024];
            normalize_chroot_subpath(subpath, norm_subpath, sizeof(norm_subpath));
            if (norm_subpath[0]) {
                snprintf(raw_host, sizeof(raw_host), "%s/%s", root, norm_subpath);
            } else {
                snprintf(raw_host, sizeof(raw_host), "%s", root);
            }
        } else if (strncmp(guest_path, "/data", 5) == 0 && (guest_path[5] == '/' || guest_path[5] == '\0')) {
            const char *subpath = guest_path + 5;
            while (*subpath == '/') subpath++;

            char norm_subpath[1024];
            normalize_chroot_subpath(subpath, norm_subpath, sizeof(norm_subpath));
            if (norm_subpath[0]) {
                snprintf(raw_host, sizeof(raw_host), "%s/data/%s", root, norm_subpath);
            } else {
                snprintf(raw_host, sizeof(raw_host), "%s/data", root);
            }
        } else if (guest_path[0] != '/') {
            char norm_subpath[1024];
            normalize_chroot_subpath(guest_path, norm_subpath, sizeof(norm_subpath));
            if (norm_subpath[0]) {
                snprintf(raw_host, sizeof(raw_host), "%s/%s", root, norm_subpath);
            } else {
                snprintf(raw_host, sizeof(raw_host), "%s", root);
            }
        } else {
            strncpy(raw_host, guest_path, sizeof(raw_host) - 1);
            raw_host[sizeof(raw_host) - 1] = '\0';
        }
    }
    pthread_mutex_unlock(&g_vfs_mutex);

    return case_insensitive_lookup(raw_host, host_path, host_path_sz);
}

static const char *get_app0_subpath(const char *guest_path) {
    if (!guest_path) return NULL;
    if (strncmp(guest_path, "/app0/", 6) == 0) {
        return guest_path + 6;
    }
    if (strcmp(guest_path, "/app0") == 0) {
        return "";
    }
    if (strncmp(guest_path, "/hostapp/", 9) == 0) {
        return guest_path + 9;
    }
    if (guest_path[0] != '/') {
        return guest_path;
    }
    return NULL;
}

bool ps4_vfs_is_virtual_fd(int fd) {
    return (fd >= PS4_VFS_VIRTUAL_FD_BASE && fd < PS4_VFS_VIRTUAL_FD_BASE + MAX_VIRTUAL_FILES);
}

int ps4_vfs_open(const char *guest_path, int host_flags, mode_t mode) {
    if (!guest_path) {
        errno = EFAULT;
        return -1;
    }

    char resolved[1024];
    if (ps4_vfs_resolve(guest_path, resolved, sizeof(resolved)) == 0) {
        if (access(resolved, F_OK) == 0) {
            return open(resolved, host_flags, mode);
        }
    }

    // Path not on physical disk. Check universal PAK archive provider
    const char *subpath = get_app0_subpath(guest_path);
    if (subpath) {
        ps4_pak_entry_t entry;
        if (ps4_pak_lookup(subpath, &entry)) {
            // PAK archives are strictly read-only
            if ((host_flags & O_ACCMODE) != O_RDONLY) {
                errno = EROFS;
                return -1;
            }

            pthread_mutex_lock(&g_vfile_mutex);
            int slot = -1;
            for (int i = 0; i < MAX_VIRTUAL_FILES; i++) {
                if (!g_vfiles[i].active) {
                    slot = i;
                    break;
                }
            }

            if (slot < 0) {
                pthread_mutex_unlock(&g_vfile_mutex);
                errno = ENFILE;
                return -1;
            }

            g_vfiles[slot].active = 1;
            g_vfiles[slot].pak_fd = entry.pak_fd;
            g_vfiles[slot].data_offset = entry.data_offset;
            g_vfiles[slot].file_size = entry.size;
            g_vfiles[slot].cur_offset = 0;
            strncpy(g_vfiles[slot].path, guest_path, sizeof(g_vfiles[slot].path) - 1);
            pthread_mutex_unlock(&g_vfile_mutex);

            int vfd = PS4_VFS_VIRTUAL_FD_BASE + slot;
            printf("[ps4-vfs] Virtual open: '%s' -> vfd=%d (pak_offset=0x%llx, size=%llu)\n",
                   guest_path, vfd, (unsigned long long)entry.data_offset, (unsigned long long)entry.size);
            return vfd;
        }
    }

    errno = ENOENT;
    return -1;
}

int ps4_vfs_close(int fd) {
    if (ps4_vfs_is_virtual_fd(fd)) {
        int slot = fd - PS4_VFS_VIRTUAL_FD_BASE;
        pthread_mutex_lock(&g_vfile_mutex);
        if (slot >= 0 && slot < MAX_VIRTUAL_FILES && g_vfiles[slot].active) {
            g_vfiles[slot].active = 0;
            pthread_mutex_unlock(&g_vfile_mutex);
            return 0;
        }
        pthread_mutex_unlock(&g_vfile_mutex);
        errno = EBADF;
        return -1;
    }
    return close(fd);
}

ssize_t ps4_vfs_read(int fd, void *buf, size_t count) {
    if (ps4_vfs_is_virtual_fd(fd)) {
        int slot = fd - PS4_VFS_VIRTUAL_FD_BASE;
        pthread_mutex_lock(&g_vfile_mutex);
        if (slot < 0 || slot >= MAX_VIRTUAL_FILES || !g_vfiles[slot].active) {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EBADF;
            return -1;
        }
        VirtualPakFile *vf = &g_vfiles[slot];
        if ((uint64_t)vf->cur_offset >= vf->file_size) {
            pthread_mutex_unlock(&g_vfile_mutex);
            return 0; // EOF
        }

        size_t to_read = count;
        if ((uint64_t)vf->cur_offset + to_read > vf->file_size) {
            to_read = (size_t)(vf->file_size - (uint64_t)vf->cur_offset);
        }

        ssize_t ret = pread(vf->pak_fd, buf, to_read, (off_t)(vf->data_offset + (uint64_t)vf->cur_offset));
        if (ret > 0) {
            vf->cur_offset += ret;
        }
        pthread_mutex_unlock(&g_vfile_mutex);
        return ret;
    }
    return read(fd, buf, count);
}

ssize_t ps4_vfs_pread(int fd, void *buf, size_t count, off_t offset) {
    if (ps4_vfs_is_virtual_fd(fd)) {
        int slot = fd - PS4_VFS_VIRTUAL_FD_BASE;
        pthread_mutex_lock(&g_vfile_mutex);
        if (slot < 0 || slot >= MAX_VIRTUAL_FILES || !g_vfiles[slot].active) {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EBADF;
            return -1;
        }
        VirtualPakFile *vf = &g_vfiles[slot];
        if (offset < 0 || (uint64_t)offset >= vf->file_size) {
            pthread_mutex_unlock(&g_vfile_mutex);
            return 0;
        }

        size_t to_read = count;
        if ((uint64_t)offset + to_read > vf->file_size) {
            to_read = (size_t)(vf->file_size - (uint64_t)offset);
        }

        ssize_t ret = pread(vf->pak_fd, buf, to_read, (off_t)(vf->data_offset + (uint64_t)offset));
        pthread_mutex_unlock(&g_vfile_mutex);
        return ret;
    }
    return pread(fd, buf, count, offset);
}

off_t ps4_vfs_lseek(int fd, off_t offset, int whence) {
    if (ps4_vfs_is_virtual_fd(fd)) {
        int slot = fd - PS4_VFS_VIRTUAL_FD_BASE;
        pthread_mutex_lock(&g_vfile_mutex);
        if (slot < 0 || slot >= MAX_VIRTUAL_FILES || !g_vfiles[slot].active) {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EBADF;
            return -1;
        }
        VirtualPakFile *vf = &g_vfiles[slot];
        off_t new_off = vf->cur_offset;
        if (whence == SEEK_SET) {
            new_off = offset;
        } else if (whence == SEEK_CUR) {
            new_off += offset;
        } else if (whence == SEEK_END) {
            new_off = (off_t)vf->file_size + offset;
        } else {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EINVAL;
            return -1;
        }

        if (new_off < 0) {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EINVAL;
            return -1;
        }

        vf->cur_offset = new_off;
        pthread_mutex_unlock(&g_vfile_mutex);
        return new_off;
    }
    return lseek(fd, offset, whence);
}

int ps4_vfs_fstat(int fd, struct stat *st) {
    if (!st) {
        errno = EFAULT;
        return -1;
    }
    if (ps4_vfs_is_virtual_fd(fd)) {
        int slot = fd - PS4_VFS_VIRTUAL_FD_BASE;
        pthread_mutex_lock(&g_vfile_mutex);
        if (slot < 0 || slot >= MAX_VIRTUAL_FILES || !g_vfiles[slot].active) {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EBADF;
            return -1;
        }
        VirtualPakFile *vf = &g_vfiles[slot];
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = (off_t)vf->file_size;
        st->st_blksize = 4096;
        st->st_blocks = (off_t)((vf->file_size + 511) / 512);
        pthread_mutex_unlock(&g_vfile_mutex);
        return 0;
    }
    return fstat(fd, st);
}

int ps4_vfs_stat(const char *guest_path, struct stat *st) {
    if (!guest_path || !st) {
        errno = EFAULT;
        return -1;
    }
    char resolved[1024];
    if (ps4_vfs_resolve(guest_path, resolved, sizeof(resolved)) == 0) {
        if (stat(resolved, st) == 0) {
            return 0;
        }
    }

    const char *subpath = get_app0_subpath(guest_path);
    if (subpath) {
        ps4_pak_entry_t entry;
        if (ps4_pak_lookup(subpath, &entry)) {
            memset(st, 0, sizeof(*st));
            st->st_mode = S_IFREG | 0644;
            st->st_nlink = 1;
            st->st_size = (off_t)entry.size;
            st->st_blksize = 4096;
            st->st_blocks = (off_t)((entry.size + 511) / 512);
            return 0;
        }
        if (ps4_pak_dir_exists(subpath)) {
            memset(st, 0, sizeof(*st));
            st->st_mode = S_IFDIR | 0755;
            st->st_nlink = 2;
            st->st_size = 4096;
            st->st_blksize = 4096;
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

int ps4_vfs_lstat(const char *guest_path, struct stat *st) {
    return ps4_vfs_stat(guest_path, st);
}

int ps4_vfs_access(const char *guest_path, int mode) {
    if (!guest_path) {
        errno = EFAULT;
        return -1;
    }
    char resolved[1024];
    if (ps4_vfs_resolve(guest_path, resolved, sizeof(resolved)) == 0) {
        if (access(resolved, mode) == 0) {
            return 0;
        }
    }

    const char *subpath = get_app0_subpath(guest_path);
    if (subpath) {
        if (ps4_pak_lookup(subpath, NULL) || ps4_pak_dir_exists(subpath)) {
            if (mode & W_OK) {
                errno = EACCES;
                return -1;
            }
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

void *ps4_vfs_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
    if (ps4_vfs_is_virtual_fd(fd)) {
        int slot = fd - PS4_VFS_VIRTUAL_FD_BASE;
        pthread_mutex_lock(&g_vfile_mutex);
        if (slot < 0 || slot >= MAX_VIRTUAL_FILES || !g_vfiles[slot].active) {
            pthread_mutex_unlock(&g_vfile_mutex);
            errno = EBADF;
            return MAP_FAILED;
        }
        VirtualPakFile *vf = &g_vfiles[slot];
        void *mem = mmap(addr, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (mem == MAP_FAILED) {
            pthread_mutex_unlock(&g_vfile_mutex);
            return MAP_FAILED;
        }
        if (offset >= 0 && (uint64_t)offset < vf->file_size) {
            size_t to_read = len;
            if ((uint64_t)offset + to_read > vf->file_size) {
                to_read = (size_t)(vf->file_size - (uint64_t)offset);
            }
            pread(vf->pak_fd, mem, to_read, (off_t)(vf->data_offset + (uint64_t)offset));
        }
        pthread_mutex_unlock(&g_vfile_mutex);
        mprotect(mem, len, prot);
        return mem;
    }
    return mmap(addr, len, prot, flags, fd, offset);
}

void ps4_vfs_destroy(void) {
    pthread_mutex_lock(&g_vfs_mutex);
    if (g_app_root) {
        free(g_app_root);
        g_app_root = NULL;
    }
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;

    pthread_mutex_lock(&g_vfile_mutex);
    memset(g_vfiles, 0, sizeof(g_vfiles));
    pthread_mutex_unlock(&g_vfile_mutex);

    pthread_mutex_unlock(&g_vfs_mutex);

    ps4_pak_destroy();
}
