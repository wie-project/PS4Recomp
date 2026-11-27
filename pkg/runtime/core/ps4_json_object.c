// SPDX-License-Identifier: GPL-2.0-or-later

// Minimal JSON reader for the runtime's small configuration files; see ps4_json_object.h.

#include "ps4_json_object.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>


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

// A number, true, false or null at j (after whitespace); *out is a heap copy of its text.
static bool json_literal(Json *j, char **out) {
    const char *start = j->p;
    while (j->p < j->end && (isalnum((unsigned char)*j->p) || *j->p == '-' || *j->p == '+' || *j->p == '.')) j->p++;
    if (j->p == start) return false;
    *out = strndup(start, (size_t)(j->p - start));
    return *out != NULL;
}

int ps4_json_object_members(const char *text, size_t len, Ps4JsonMemberFn member, void *user) {
    Json j = {text, text + len};
    int count = 0;
    json_ws(&j);
    if (j.p >= j.end || *j.p != '{') return -1;
    j.p++;
    json_ws(&j);
    if (j.p < j.end && *j.p == '}') return 0;
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
        char *value = NULL;
        bool ok;
        Ps4JsonKind kind = PS4_JSON_STRING;
        if (j.p < j.end && *j.p == '"') {
            ok = json_string(&j, &value);
        } else if (j.p < j.end && (*j.p == '{' || *j.p == '[')) {
            ok = json_skip_value(&j, 0);
        } else {
            kind = PS4_JSON_LITERAL;
            ok = json_literal(&j, &value);
        }
        if (ok && value) {
            member(user, key, kind, value);
            count++;
        }
        free(value);
        free(key);
        if (!ok) break;
        json_ws(&j);
        if (j.p < j.end && *j.p == ',') {
            j.p++;
            continue;
        }
        if (j.p < j.end && *j.p == '}') return count;
        break;
    }
    return -1 - count;
}
