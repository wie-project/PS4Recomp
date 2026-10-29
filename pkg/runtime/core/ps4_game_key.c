// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_game_key.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TITLE_ID_MAX 31

static bool valid_title_id(const char *s) {
    if (!s || !*s) return false;
    size_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  c == '-' || c == '_';
        if (!ok || n >= TITLE_ID_MAX) return false;
    }
    return true;
}

bool ps4_settings_key(const char *title_id, const char *fallback, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    const char *src = valid_title_id(title_id) ? title_id : fallback;
    if (!src || !*src) {
        out[0] = '\0';
        return false;
    }
    int n = snprintf(out, out_size, "%s", src);
    return n > 0 && (size_t)n < out_size;
}

bool ps4_game_key_for_root(const char *title_id, const char *app_root, char *out, size_t out_size) {
    char base[256] = "default";
    if (app_root) {
        size_t end = strlen(app_root);
        while (end > 0 && app_root[end - 1] == '/') end--;
        size_t start = end;
        while (start > 0 && app_root[start - 1] != '/') start--;
        if (end > start && end - start < sizeof(base)) {
            memcpy(base, app_root + start, end - start);
            base[end - start] = '\0';
        }
    }
    return ps4_settings_key(title_id, base, out, out_size);
}

bool ps4_find_save_dir_by_title_id(const char *base_dir, const char *title_id, char *out, size_t out_size) {
    if (!base_dir || !*base_dir || !title_id || !*title_id || !out || out_size == 0) return false;
    char suffix[96];
    int sn = snprintf(suffix, sizeof(suffix), " [%s]", title_id);
    if (sn <= 0 || (size_t)sn >= sizeof(suffix)) return false;
    size_t suffix_len = (size_t)sn;

    DIR *d = opendir(base_dir);
    if (!d) return false;
    char best[1024];
    best[0] = '\0';
    struct timespec best_mtime = {0, 0};
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len <= suffix_len || memcmp(e->d_name + len - suffix_len, suffix, suffix_len) != 0) continue;
        char path[1024];
        int pn = snprintf(path, sizeof(path), "%s/%s", base_dir, e->d_name);
        if (pn <= 0 || (size_t)pn >= sizeof(path)) continue;
        struct stat st;
        if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        // Newest wins; equal times fall back to the greater name so the pick is deterministic.
        bool newer = st.st_mtimespec.tv_sec != best_mtime.tv_sec
                         ? st.st_mtimespec.tv_sec > best_mtime.tv_sec
                         : st.st_mtimespec.tv_nsec != best_mtime.tv_nsec
                               ? st.st_mtimespec.tv_nsec > best_mtime.tv_nsec
                               : strcmp(path, best) > 0;
        if (best[0] == '\0' || newer) {
            memcpy(best, path, (size_t)pn + 1);
            best_mtime = st.st_mtimespec;
        }
    }
    closedir(d);
    if (best[0] == '\0') return false;
    int n = snprintf(out, out_size, "%s", best);
    return n > 0 && (size_t)n < out_size;
}
