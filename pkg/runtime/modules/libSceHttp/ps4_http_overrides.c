// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHttp host overrides (shadPS4 network/http.cpp, http_host_overrides.cpp): a JSON object
// that sends connections for chosen endpoints elsewhere, e.g. to a private server.
//
//   {
//     "api.example.com":             "http://localhost:8080",
//     "discovery.example.com:5300":  "localhost:8081",
//     "https://api.example.com:443": "http://localhost:8082",
//     "*":                           "http://localhost:8080"
//   }
//
// The most specific key wins: "scheme://host:port", then "host:port", then "host", then "*".
// Keys starting with '_' are comments. A value is "host", "host:port", "http(s)://host" or
// "http(s)://host:port": without a scheme the connection keeps its own; without a port it keeps
// its own, except that a scheme change moves the scheme's default port (80 <-> 443).
//
// The file is <data>/host_overrides/<TITLE_ID>.json for the running title when it exists,
// otherwise PS4_HTTP_HOST_OVERRIDES_JSON or <data>/host_overrides.json, where <data> is
// PS4RECOMP_DATA_DIR or ~/Library/Application Support/PS4Recomp. It is read once.
#include "ps4_http.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct HostOverride {
    char *key;
    char scheme[8]; // "http", "https" or "" to keep the connection's
    char *host;
    uint16_t port; // 0 keeps the connection's
} HostOverride;

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static HostOverride *g_entries;
static size_t g_entry_count;

// ---------------------------------------------------------------------------------------------
// Minimal JSON reader: the top-level object's string members; other values are skipped.

typedef struct Json {
    const char *p;
    const char *end;
} Json;

static void json_ws(Json *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int hex4(const char *s) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        const char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

// Reads a string (at '"'); *out is a heap copy (UTF-8) when out is not NULL.
static bool json_string(Json *j, char **out) {
    if (j->p >= j->end || *j->p != '"') return false;
    j->p++;
    const size_t cap = (size_t)(j->end - j->p) + 1;
    char *s = out ? malloc(cap) : NULL;
    if (out && !s) return false;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->end) break;
            c = *j->p++;
            switch (c) {
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                if (j->end - j->p < 4) goto fail;
                const int cp = hex4(j->p);
                if (cp < 0) goto fail;
                j->p += 4;
                if (s) {
                    if (cp < 0x80) {
                        s[n++] = (char)cp;
                    } else if (cp < 0x800) {
                        s[n++] = (char)(0xC0 | (cp >> 6));
                        s[n++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        s[n++] = (char)(0xE0 | (cp >> 12));
                        s[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        s[n++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
                continue;
            }
            default: break; // '"', '\\', '/'
            }
        }
        if (s) s[n++] = c;
    }
    if (j->p >= j->end) goto fail;
    j->p++;
    if (s) {
        s[n] = 0;
        *out = s;
    }
    return true;
fail:
    free(s);
    return false;
}

static bool json_skip_value(Json *j, int depth) {
    json_ws(j);
    if (j->p >= j->end || depth > 64) return false;
    const char c = *j->p;
    if (c == '"') return json_string(j, NULL);
    if (c == '{' || c == '[') {
        const char close = c == '{' ? '}' : ']';
        j->p++;
        json_ws(j);
        if (j->p < j->end && *j->p == close) {
            j->p++;
            return true;
        }
        for (;;) {
            if (c == '{') {
                json_ws(j);
                if (!json_string(j, NULL)) return false;
                json_ws(j);
                if (j->p >= j->end || *j->p != ':') return false;
                j->p++;
            }
            if (!json_skip_value(j, depth + 1)) return false;
            json_ws(j);
            if (j->p >= j->end) return false;
            if (*j->p == ',') {
                j->p++;
                continue;
            }
            if (*j->p != close) return false;
            j->p++;
            return true;
        }
    }
    // A number, true, false or null.
    const char *start = j->p;
    while (j->p < j->end && (isalnum((unsigned char)*j->p) || *j->p == '-' || *j->p == '+' || *j->p == '.')) j->p++;
    return j->p > start;
}

// ---------------------------------------------------------------------------------------------

static bool parse_target(const char *value, HostOverride *out) {
    const char *v = value;
    const char *scheme_end = strstr(v, "://");
    if (scheme_end) {
        const size_t len = (size_t)(scheme_end - v);
        if (len == 4 && strncasecmp(v, "http", 4) == 0) snprintf(out->scheme, sizeof(out->scheme), "http");
        else if (len == 5 && strncasecmp(v, "https", 5) == 0) snprintf(out->scheme, sizeof(out->scheme), "https");
        v = scheme_end + 3; // an unknown scheme keeps the connection's
    }
    const char *colon = strchr(v, ':');
    if (colon) {
        out->host = strndup(v, (size_t)(colon - v));
        char *endp = NULL;
        const unsigned long p = strtoul(colon + 1, &endp, 10);
        if (endp != colon + 1 && p > 0 && p <= 65535) out->port = (uint16_t)p;
    } else {
        out->host = strdup(v);
    }
    return out->host != NULL;
}

static void add_entry(const char *key, const char *value) {
    HostOverride e;
    memset(&e, 0, sizeof(e));
    if (!parse_target(value, &e)) return;
    e.key = strdup(key);
    HostOverride *entries = realloc(g_entries, (g_entry_count + 1) * sizeof(*entries));
    if (!e.key || !entries) {
        free(e.key);
        free(e.host);
        if (entries) g_entries = entries;
        return;
    }
    g_entries = entries;
    g_entries[g_entry_count++] = e;
}

static void parse_overrides(const char *text, size_t len, const char *source) {
    Json j = {text, text + len};
    json_ws(&j);
    if (j.p >= j.end || *j.p != '{') {
        fprintf(stderr, "[libSceHttp] %s: host overrides must be a JSON object\n", source);
        return;
    }
    j.p++;
    json_ws(&j);
    if (j.p < j.end && *j.p == '}') return;
    for (;;) {
        char *key = NULL;
        json_ws(&j);
        if (!json_string(&j, &key)) break;
        json_ws(&j);
        if (j.p >= j.end || *j.p != ':') {
            free(key);
            break;
        }
        j.p++;
        json_ws(&j);
        if (j.p < j.end && *j.p == '"') {
            char *value = NULL;
            if (!json_string(&j, &value)) {
                free(key);
                break;
            }
            if (key[0] != '_' && value[0]) add_entry(key, value);
            free(value);
        } else if (!json_skip_value(&j, 0)) {
            free(key);
            break;
        }
        free(key);
        json_ws(&j);
        if (j.p < j.end && *j.p == ',') {
            j.p++;
            continue;
        }
        if (j.p < j.end && *j.p == '}') return;
        break;
    }
    fprintf(stderr, "[libSceHttp] %s: malformed JSON, %zu host overrides read\n", source, g_entry_count);
}

static void data_dir(char *out, size_t size) {
    const char *d = getenv("PS4RECOMP_DATA_DIR");
    if (d && *d) {
        snprintf(out, size, "%s", d);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(out, size, "%s/Library/Application Support/PS4Recomp", home && *home ? home : ".");
}

static bool valid_title_id(const char *id) {
    if (strlen(id) != 9) return false;
    for (int i = 0; i < 4; i++) {
        if (!isupper((unsigned char)id[i])) return false;
    }
    for (int i = 4; i < 9; i++) {
        if (!isdigit((unsigned char)id[i])) return false;
    }
    return true;
}

static void load_overrides(void) {
    char dir[1024];
    data_dir(dir, sizeof(dir));
    char path[1280] = {0};
    char title[64] = {0};
    ps4_http_title_id(title, sizeof(title));
    FILE *f = NULL;
    if (valid_title_id(title)) {
        snprintf(path, sizeof(path), "%s/host_overrides/%s.json", dir, title);
        f = fopen(path, "rb");
    }
    if (!f) {
        const char *env = getenv("PS4_HTTP_HOST_OVERRIDES_JSON");
        if (env && *env) {
            snprintf(path, sizeof(path), "%s", env);
        } else {
            snprintf(path, sizeof(path), "%s/host_overrides.json", dir);
        }
        f = fopen(path, "rb");
    }
    if (!f) return;
    char *text = NULL;
    size_t len = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (text = malloc((size_t)n))) len = fread(text, 1, (size_t)n, f);
    }
    fclose(f);
    if (text) parse_overrides(text, len, path);
    free(text);
    fprintf(stderr, "[libSceHttp] %zu host overrides from %s\n", g_entry_count, path);
}

static const HostOverride *lookup(const char *key) {
    for (size_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0) return &g_entries[i];
    }
    return NULL;
}

bool ps4_http_apply_host_override(char *scheme, size_t scheme_size, char **host, uint16_t *port, bool *is_secure) {
    pthread_once(&g_once, load_overrides);
    if (g_entry_count == 0) return false;
    char key[600];
    snprintf(key, sizeof(key), "%s://%s:%u", scheme, *host, (unsigned)*port);
    const HostOverride *e = lookup(key);
    if (!e) {
        snprintf(key, sizeof(key), "%s:%u", *host, (unsigned)*port);
        e = lookup(key);
    }
    if (!e) e = lookup(*host);
    if (!e) e = lookup("*");
    if (!e) return false;

    char *new_host = strdup(e->host);
    if (!new_host) return false;
    char orig_scheme[8];
    snprintf(orig_scheme, sizeof(orig_scheme), "%s", scheme);
    const uint16_t orig_port = *port;
    fprintf(stderr, "[libSceHttp] host override: %s://%s:%u -> ", orig_scheme, *host, (unsigned)orig_port);
    free(*host);
    *host = new_host;
    if (e->scheme[0]) {
        snprintf(scheme, scheme_size, "%s", e->scheme);
        *is_secure = strcmp(e->scheme, "https") == 0;
    }
    if (e->port != 0) {
        *port = e->port;
    } else if (e->scheme[0] && strcmp(e->scheme, orig_scheme) != 0) {
        if (strcmp(orig_scheme, "https") == 0 && *port == 443) *port = 80;
        else if (strcmp(orig_scheme, "http") == 0 && *port == 80) *port = 443;
    }
    fprintf(stderr, "%s://%s:%u\n", scheme, *host, (unsigned)*port);
    return true;
}
