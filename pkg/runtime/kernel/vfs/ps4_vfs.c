#include "ps4_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

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

int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    if (!guest_path || !host_path || host_path_sz == 0) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_vfs_mutex);
    const char *root = g_app_root ? g_app_root : ".";

    // 1. Check custom registered mount points first
    for (int i = 0; i < g_mount_count; i++) {
        if (!g_mounts[i].active) continue;
        size_t plen = strlen(g_mounts[i].guest_prefix);
        if (strncmp(guest_path, g_mounts[i].guest_prefix, plen) == 0 &&
            (guest_path[plen] == '/' || guest_path[plen] == '\0')) {
            const char *subpath = guest_path + plen;
            while (*subpath == '/') {
                subpath++;
            }
            int written;
            if (*subpath) {
                written = snprintf(host_path, host_path_sz, "%s/%s", g_mounts[i].host_path, subpath);
            } else {
                written = snprintf(host_path, host_path_sz, "%s", g_mounts[i].host_path);
            }
            pthread_mutex_unlock(&g_vfs_mutex);
            if (written < 0 || (size_t)written >= host_path_sz) {
                return -ENAMETOOLONG;
            }
            return 0;
        }
    }

    // 2. Default /app0 handling
    if (strncmp(guest_path, "/app0", 5) == 0 && (guest_path[5] == '/' || guest_path[5] == '\0')) {
        const char *subpath = guest_path + 5;
        while (*subpath == '/') {
            subpath++;
        }

        int written;
        if (*subpath) {
            written = snprintf(host_path, host_path_sz, "%s/%s", root, subpath);
        } else {
            written = snprintf(host_path, host_path_sz, "%s", root);
        }

        pthread_mutex_unlock(&g_vfs_mutex);
        if (written < 0 || (size_t)written >= host_path_sz) {
            return -ENAMETOOLONG;
        }
        return 0;
    }

    // 3. Default /data handling
    if (strncmp(guest_path, "/data", 5) == 0 && (guest_path[5] == '/' || guest_path[5] == '\0')) {
        const char *subpath = guest_path + 5;
        while (*subpath == '/') {
            subpath++;
        }
        int written = snprintf(host_path, host_path_sz, "%s/data/%s", root, subpath);
        pthread_mutex_unlock(&g_vfs_mutex);
        if (written < 0 || (size_t)written >= host_path_sz) {
            return -ENAMETOOLONG;
        }
        return 0;
    }

    pthread_mutex_unlock(&g_vfs_mutex);

    // Direct path copy
    if (strlen(guest_path) >= host_path_sz) {
        return -ENAMETOOLONG;
    }
    strncpy(host_path, guest_path, host_path_sz - 1);
    host_path[host_path_sz - 1] = '\0';
    return 0;
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
