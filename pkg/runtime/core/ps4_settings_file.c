// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_settings_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SETTINGS_FILE_MAX (64 * 1024)

static const char *const kOwned[] = {"lang", "pro", "upscale", "hud"};
#define NOWNED 4

static int owned_value(const Ps4SettingsFile *s, int k) {
    switch (k) {
    case 0: return s->lang;
    case 1: return s->pro != 0;
    case 2: return s->upscale != 0;
    default: return s->hud != 0;
    }
}

// The index in kOwned of the line's key (the text before '='), or -1.
static int owned_key(const char *line, size_t len) {
    const char *eq = memchr(line, '=', len);
    if (!eq) return -1;
    const size_t klen = (size_t)(eq - line);
    for (int k = 0; k < NOWNED; k++) {
        if (strlen(kOwned[k]) == klen && memcmp(line, kOwned[k], klen) == 0) return k;
    }
    return -1;
}

bool ps4_settings_file_read(const char *path, Ps4SettingsFile *s) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    s->metal = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        int v;
        if (sscanf(line, "%63[^=]=%d", key, &v) != 2) continue;
        if (!strcmp(key, "lang")) s->lang = v;
        else if (!strcmp(key, "pro")) s->pro = v != 0;
        else if (!strcmp(key, "upscale")) s->upscale = v != 0;
        else if (!strcmp(key, "hud")) s->hud = v != 0;
        else if (!strcmp(key, "metal")) s->metal = (v == 3 || v == 4) ? v : 0;
    }
    fclose(f);
    return true;
}

bool ps4_settings_file_write(const char *path, const Ps4SettingsFile *s) {
    // Static: the settings are written once, from the startup dialog on the main thread.
    static char old[SETTINGS_FILE_MAX];
    size_t n = 0;
    FILE *in = fopen(path, "r");
    if (in) {
        n = fread(old, 1, sizeof(old), in);
        const bool too_big = n == sizeof(old) && fgetc(in) != EOF;
        const bool err = ferror(in) != 0;
        fclose(in);
        if (too_big || err) return false;
    }
    char tmp[2100];
    int tn = snprintf(tmp, sizeof(tmp), "%s.%d.tmp", path, (int)getpid());
    if (tn <= 0 || (size_t)tn >= sizeof(tmp)) return false;
    FILE *out = fopen(tmp, "w");
    if (!out) return false;
    bool written[NOWNED] = {false, false, false, false};
    for (size_t pos = 0; pos < n;) {
        const char *line = old + pos;
        const char *nl = memchr(line, '\n', n - pos);
        const size_t len = nl ? (size_t)(nl - line) : n - pos;
        pos += len + (nl ? 1 : 0);
        const int k = owned_key(line, len);
        if (k < 0) {
            fwrite(line, 1, len, out);
            fputc('\n', out);
        } else if (!written[k]) {
            fprintf(out, "%s=%d\n", kOwned[k], owned_value(s, k));
            written[k] = true;
        }
    }
    for (int k = 0; k < NOWNED; k++) {
        if (!written[k]) fprintf(out, "%s=%d\n", kOwned[k], owned_value(s, k));
    }
    bool ok = fflush(out) == 0 && fsync(fileno(out)) == 0;
    ok = fclose(out) == 0 && ok;
    if (ok && rename(tmp, path) == 0) return true;
    unlink(tmp);
    return false;
}
