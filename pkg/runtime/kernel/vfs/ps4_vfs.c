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
    const char *env = getenv("PS4_APP_DIR");
    if (env && *env && access(env, F_OK) == 0) {
        return strdup(env);
    }

    // Check hint if provided and exists
    if (hint && *hint && access(hint, F_OK) == 0) {
        return strdup(hint);
    }

    // Check relative to executable
#if defined(__APPLE__)
    char exec_path[1024] = {0};
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) == 0) {
        char *dir = dirname(exec_path);
        char test_path[1024];

        // Check inside macOS .app bundle: <exe_dir>/../Resources
        snprintf(test_path, sizeof(test_path), "%s/../Resources", dir);
        if (access(test_path, F_OK) == 0) {
            return strdup(test_path);
        }

        // Check <exe_dir>/assets
        snprintf(test_path, sizeof(test_path), "%s/assets", dir);
        if (access(test_path, F_OK) == 0) {
            return strdup(dir);
        }

        // Check parent directory hierarchy up to 4 levels for assets/
        char cur[1024];
        strncpy(cur, dir, sizeof(cur) - 1);
        for (int i = 0; i < 4; i++) {
            char *parent = dirname(cur);
            snprintf(test_path, sizeof(test_path), "%s/assets", parent);
            if (access(test_path, F_OK) == 0) {
                return strdup(parent);
            }
            strncpy(cur, parent, sizeof(cur) - 1);
        }
    }
#endif

    // Check current working directory ./assets
    if (access("assets", F_OK) == 0) {
        return strdup(".");
    }

    if (hint && *hint) {
        return strdup(hint);
    }

    return strdup(".");
}

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
    pthread_mutex_unlock(&g_vfs_mutex);
}

int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    if (!guest_path || !host_path || host_path_sz == 0) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_vfs_mutex);
    const char *root = g_app_root ? g_app_root : ".";

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
    pthread_mutex_unlock(&g_vfs_mutex);
}
