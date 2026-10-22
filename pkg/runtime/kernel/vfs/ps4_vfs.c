#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

static char *g_app_root = NULL;
static pthread_mutex_t g_vfs_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    // In a standard macOS .app bundle: <Bundle>.app/Contents/MacOS/<exec>
    // Resources are placed in: <Bundle>.app/Contents/Resources
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
        // Verify hint actually contains game assets (not an empty leftover folder)
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
    pthread_mutex_unlock(&g_vfs_mutex);
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
    // If prefix already exists, update it
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

// Normalizes guest path components within a chroot mount.
// Eliminates '.', redundant slashes, and resolves '..' without escaping the mount root.
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
            // Cannot escape chroot root: if seg_count == 0, stay at root
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

// Case-insensitive lookup ported from shadps4 MntPoints::GetHostPath (fs.cpp:279-337).
// Traverses non-existing components using opendir/readdir with strcasecmp.
static int case_insensitive_lookup(const char *host_path, char *out_path, size_t out_sz) {
    if (!host_path || !out_path || out_sz == 0) return -EINVAL;

    // Fast path: if path already exists directly on host filesystem, return immediately
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

    // Collect non-existing path components from leaf to root without heap allocation
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
                // Root "/" is an anchor that exists
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

    // Anchor is in `curr` (or "/" if reached filesystem root)
    char resolved[1024];
    if (*curr) {
        strncpy(resolved, curr, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    } else {
        strncpy(resolved, "/", sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
    }

    // Walk down the stacked parts in reverse (from parent to leaf)
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

        // Case-insensitive directory scan (matching shadps4 search)
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
            // Keep original component name if no match found
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

    // 1. Check custom registered mount points first
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

    // 2. Default /app0, /hostapp, /data, or relative path handling
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
            // Relative path: in PS4, process working directory is /app0
            char norm_subpath[1024];
            normalize_chroot_subpath(guest_path, norm_subpath, sizeof(norm_subpath));
            if (norm_subpath[0]) {
                snprintf(raw_host, sizeof(raw_host), "%s/%s", root, norm_subpath);
            } else {
                snprintf(raw_host, sizeof(raw_host), "%s", root);
            }
        } else {
            // Absolute host path or unmounted system path
            strncpy(raw_host, guest_path, sizeof(raw_host) - 1);
            raw_host[sizeof(raw_host) - 1] = '\0';
        }
    }
    pthread_mutex_unlock(&g_vfs_mutex);

    // 3. Apply case-insensitive resolution on the host path
    return case_insensitive_lookup(raw_host, host_path, host_path_sz);
}

void ps4_vfs_destroy(void) {
    pthread_mutex_lock(&g_vfs_mutex);
    if (g_app_root) {
        free(g_app_root);
        g_app_root = NULL;
    }
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;
    pthread_mutex_unlock(&g_vfs_mutex);
}
