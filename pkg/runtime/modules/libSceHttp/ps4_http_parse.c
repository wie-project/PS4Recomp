// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHttp string functions after shadPS4 network/http.cpp: the status-line and response
// header parsers and the URI helpers (parse, build, escape, unescape, merge, sweep path). They
// work on host pointers; the shims in ps4_http.c translate guest addresses.
#include "ps4_http.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static inline bool is_ascii_digit_char(char c) {
    const unsigned char uc = (unsigned char)c;
    return uc >= '0' && uc <= '9';
}

int32_t sceHttpParseStatusLine(const char *statusLine, uint64_t lineLen, int32_t *httpMajorVer, int32_t *httpMinorVer,
                               int32_t *responseCode, const char **reasonPhrase, uint64_t *phraseLen) {
    if (!statusLine) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    if (!httpMajorVer || !httpMinorVer || !responseCode || !reasonPhrase || !phraseLen) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE;
    }

    *httpMajorVer = 0;
    *httpMinorVer = 0;

    if (lineLen < 8) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    if (memcmp(statusLine, "HTTP/", 5) != 0) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    if (!is_ascii_digit_char(statusLine[5])) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }

    // Major version digits. The reads stay inside lineLen (shadPS4 reads the bytes unchecked).
    uint64_t index = 7;
    {
        unsigned char ch = (unsigned char)statusLine[5];
        while (ch < 0x80 && (ch >= '0' && ch <= '9')) {
            *httpMajorVer = *httpMajorVer * 10 + (statusLine[index - 2] - '0');
            const char next = (index - 1 < lineLen) ? statusLine[index - 1] : 0;
            index++;
            if ((unsigned char)next >= 0x80) {
                return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
            }
            ch = (unsigned char)next;
        }
    }
    if (index - 2 >= lineLen || statusLine[index - 2] != '.') {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    if (index - 1 >= lineLen || !is_ascii_digit_char(statusLine[index - 1])) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }

    // Minor version digits.
    {
        unsigned char ch = (unsigned char)statusLine[index - 1];
        while (ch < 0x80 && (ch >= '0' && ch <= '9')) {
            *httpMinorVer = *httpMinorVer * 10 + (statusLine[index - 1] - '0');
            const char next = (index < lineLen) ? statusLine[index] : 0;
            index++;
            if ((unsigned char)next >= 0x80) {
                return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
            }
            ch = (unsigned char)next;
        }
    }
    if (index - 1 >= lineLen || statusLine[index - 1] != ' ') {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    if (lineLen < index || (lineLen - index) < 3) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    for (int i = 0; i < 3; ++i) {
        if (!is_ascii_digit_char(statusLine[index + i])) {
            return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
        }
    }
    *responseCode =
        (statusLine[index] - '0') * 100 + (statusLine[index + 1] - '0') * 10 + (statusLine[index + 2] - '0');

    const char *phraseStart = statusLine + index + 3;
    const uint64_t maxScan = lineLen - index - 3;
    for (uint64_t scanLen = 0; scanLen <= maxScan; ++scanLen) {
        if (phraseStart[scanLen] != '\n') {
            continue;
        }
        *reasonPhrase = phraseStart;
        if (scanLen == 0) {
            *phraseLen = 0;
        } else {
            *phraseLen = scanLen - (phraseStart[scanLen - 1] == '\r' ? 1 : 0);
        }
        return (int32_t)((uint64_t)(phraseStart - statusLine) + scanLen + 1);
    }
    return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
}

static bool is_ascii_space(unsigned char c) {
    return c < 0x80 && isspace(c) != 0;
}

int32_t sceHttpParseResponseHeader(const char *header, uint64_t headerLen, const char *fieldStr, const char **fieldValue,
                                   uint64_t *valueLen) {
    if (!header) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
    }
    if (!fieldStr || !fieldValue || !valueLen) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE;
    }
    const uint64_t fieldStrLen = strnlen(fieldStr, 0xfff);

    bool atLineStart = true;
    uint64_t valueOffset = 0;
    bool found = false;
    if (headerLen != 0) {
        uint64_t cur = 0;
        for (;;) {
            if (atLineStart && !is_ascii_space((unsigned char)header[cur])) {
                if (fieldStrLen < headerLen - cur && strncasecmp(fieldStr, header + cur, fieldStrLen) == 0 &&
                    header[cur + fieldStrLen] == ':') {
                    valueOffset = cur + fieldStrLen + 1;
                    found = true;
                    break;
                }
            }
            atLineStart = (header[cur] == '\n');
            cur++;
            if (cur >= headerLen) break;
        }
    }
    if (!found) {
        return ORBIS_HTTP_ERROR_PARSE_HTTP_NOT_FOUND;
    }

    while (valueOffset < headerLen) {
        const unsigned char c = (unsigned char)header[valueOffset];
        if (!is_ascii_space(c)) break;
        if (c == '\n') {
            // Only whitespace up to the end of the line: the value scan starts past the '\n'.
            valueOffset++;
            break;
        }
        valueOffset++;
    }

    const uint64_t valueStart = valueOffset;
    uint64_t scan = valueOffset;
    uint64_t lineEnd = valueOffset;   // one past the '\n' of the value's last line
    uint64_t lengthEnd = valueOffset; // end of the value without the trailing '\r'
    if (valueOffset < headerLen) {
        while (scan < headerLen) {
            while (scan < headerLen && header[scan] != '\n') scan++;
            if (scan >= headerLen) {
                lengthEnd = headerLen;
                lineEnd = headerLen;
                break;
            }
            const bool sawCR = (scan > valueStart && header[scan - 1] == '\r');
            const uint64_t afterLF = scan + 1;
            if (afterLF < headerLen && (header[afterLF] == ' ' || header[afterLF] == '\t')) {
                scan = afterLF; // folded continuation line
                continue;
            }
            lineEnd = afterLF;
            lengthEnd = sawCR ? (scan - 1) : scan;
            break;
        }
    } else {
        lineEnd = headerLen;
        lengthEnd = headerLen;
    }

    const uint64_t finalLen = (lengthEnd > valueStart) ? (lengthEnd - valueStart) : 0;
    if (finalLen == 0) {
        *fieldValue = NULL;
        *valueLen = 0;
        return 0;
    }
    *fieldValue = header + valueStart;
    *valueLen = finalLen;
    return (int32_t)lineEnd;
}

// The default port of a scheme by its case-insensitive prefix, as libhttp matches it ("TTP" is
// in the console's table as well), 0 for others or for schemes longer than 0x20.
static uint16_t scheme_default_port(const char *scheme, size_t len) {
    if (len > 0x20) return 0;
    if (len >= 5 && strncasecmp(scheme, "HTTPS", 5) == 0) return 443;
    if (len >= 4 && strncasecmp(scheme, "HTTP", 4) == 0) return 80;
    if (len >= 3 && strncasecmp(scheme, "TTP", 3) == 0) return 80;
    return 0;
}

typedef struct UriBuf {
    char *data;
    size_t len;
    size_t cap;
} UriBuf;

static bool uribuf_append(UriBuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1) cap *= 2;
        char *p = realloc(b->data, cap);
        if (!p) return false;
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static bool uribuf_str(UriBuf *b, const char *s) {
    return uribuf_append(b, s, strlen(s));
}

int32_t sceHttpUriBuild(char *out, uint64_t *require, uint64_t prepare, const OrbisHttpUriElement *srcElement,
                        uint32_t option) {
    if (!srcElement) {
        return ORBIS_HTTP_ERROR_INVALID_URL;
    }
    if (!out && !require) {
        return ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    const char *scheme = srcElement->scheme ? srcElement->scheme : "";
    const char *username = srcElement->username ? srcElement->username : "";
    const char *password = srcElement->password ? srcElement->password : "";
    const char *hostname = srcElement->hostname ? srcElement->hostname : "";
    const char *path = srcElement->path ? srcElement->path : "";
    const char *query = srcElement->query ? srcElement->query : "";
    const char *fragment = srcElement->fragment ? srcElement->fragment : "";
    const size_t scheme_len = strlen(scheme);
    const bool is_mailto = scheme_len == 6 && memcmp(scheme, "mailto", 6) == 0;

    UriBuf b = {0};
    bool ok = uribuf_append(&b, "", 0);
    if ((option & ORBIS_HTTP_URI_BUILD_WITH_SCHEME) && scheme_len) {
        ok = ok && uribuf_append(&b, scheme, scheme_len) && uribuf_append(&b, ":", 1);
    }
    if (!srcElement->opaque) {
        ok = ok && uribuf_append(&b, "//", 2);
    }
    const bool has_user = (option & ORBIS_HTTP_URI_BUILD_WITH_USERNAME) && username[0];
    const bool has_pass = (option & ORBIS_HTTP_URI_BUILD_WITH_PASSWORD) && password[0];
    if (has_user) ok = ok && uribuf_str(&b, username);
    if (has_pass) ok = ok && uribuf_append(&b, ":", 1) && uribuf_str(&b, password);
    if (has_user || has_pass) ok = ok && uribuf_append(&b, "@", 1);
    if ((option & ORBIS_HTTP_URI_BUILD_WITH_HOSTNAME) && hostname[0]) ok = ok && uribuf_str(&b, hostname);
    // The port only when it is not the scheme's default.
    if ((option & ORBIS_HTTP_URI_BUILD_WITH_PORT) && srcElement->port != 0) {
        const uint16_t def = scheme_default_port(scheme, scheme_len);
        const bool skip = (def != 0 || is_mailto) && def == srcElement->port;
        if (!skip) {
            char port[8];
            const int n = snprintf(port, sizeof(port), ":%u", (unsigned)srcElement->port);
            ok = ok && uribuf_append(&b, port, (size_t)n);
        }
    }
    if ((option & ORBIS_HTTP_URI_BUILD_WITH_PATH) && path[0]) ok = ok && uribuf_str(&b, path);
    if ((option & ORBIS_HTTP_URI_BUILD_WITH_QUERY) && query[0]) ok = ok && uribuf_str(&b, query);
    if ((option & ORBIS_HTTP_URI_BUILD_WITH_FRAGMENT) && fragment[0]) ok = ok && uribuf_str(&b, fragment);
    if (!ok) {
        free(b.data);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }

    const size_t need = b.len + 1;
    if (require) *require = need;
    int32_t rc = 0;
    if (out) {
        if (prepare < need) {
            rc = ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        } else {
            memcpy(out, b.data, need);
        }
    }
    free(b.data);
    return rc;
}

static bool is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~';
}

int32_t sceHttpUriEscape(char *out, uint64_t *require, uint64_t prepare, const char *in) {
    if (!in) {
        return ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    uint64_t needed = 1;
    for (const char *s = in; *s; s++) needed += is_unreserved((unsigned char)*s) ? 1 : 3;
    if (require) *require = needed;
    if (!out) return 0;
    if (prepare < needed) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;

    static const char hex_chars[] = "0123456789ABCDEF";
    char *dst = out;
    for (const char *s = in; *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (is_unreserved(c)) {
            *dst++ = (char)c;
        } else {
            *dst++ = '%';
            *dst++ = hex_chars[(c >> 4) & 0x0F];
            *dst++ = hex_chars[c & 0x0F];
        }
    }
    *dst = '\0';
    return 0;
}

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static bool is_percent_sequence(const char *s) {
    return s[0] == '%' && s[1] != '\0' && s[2] != '\0' && is_hex(s[1]) && is_hex(s[2]);
}

int32_t sceHttpUriUnescape(char *out, uint64_t *require, uint64_t prepare, const char *in) {
    if (!in) {
        return ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    uint64_t needed = 1;
    for (const char *s = in; *s; needed++) s += is_percent_sequence(s) ? 3 : 1;
    if (require) *require = needed;
    if (!out) return 0;
    if (prepare < needed) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;

    char *dst = out;
    const char *s = in;
    while (*s) {
        if (is_percent_sequence(s)) {
            *dst++ = (char)((hex_value(s[1]) << 4) | hex_value(s[2]));
            s += 3;
        } else {
            *dst++ = *s++;
        }
    }
    *dst = '\0';
    return 0;
}

int32_t sceHttpUriSweepPath(char *dst, const char *src, uint64_t srcSize) {
    if (srcSize == 0) return 0;
    if (!dst || !src) return ORBIS_HTTP_ERROR_INVALID_VALUE;

    if (src[0] != '/') {
        const uint64_t copyLen = srcSize - 1;
        memmove(dst, src, copyLen);
        dst[copyLen] = '\0';
        return 0;
    }
    dst[0] = '/';
    dst[1] = '\0';
    if (srcSize - 1u <= 1) return 0;

    uint64_t srcPos = 1;
    char *segmentEnd = dst;
    while (srcPos < srcSize - 1u) {
        if (src[srcPos] == '.') {
            if (src[srcPos + 1] == '/') { // "./"
                srcPos += 2;
                continue;
            }
            if (src[srcPos + 1] == '.' && src[srcPos + 2] == '/') { // "../": drop the last segment
                char *newSegmentEnd = dst;
                if (segmentEnd != dst) {
                    *segmentEnd = '\0';
                    char *prevSlash = strrchr(dst, '/');
                    if (!prevSlash) {
                        newSegmentEnd = NULL;
                    } else {
                        prevSlash[1] = '\0';
                        newSegmentEnd = prevSlash;
                    }
                }
                srcPos += 3;
                segmentEnd = newSegmentEnd;
                if (!segmentEnd) return 0;
                continue;
            }
        }
        const char *segmentStart = src + srcPos;
        const char *nextSlash = strchr(segmentStart, '/');
        const uint64_t remaining = srcSize - srcPos - 1u;
        uint64_t copyLen;
        if (!nextSlash) {
            copyLen = remaining;
        } else {
            const uint64_t segLen = (uint64_t)(nextSlash + 1 - segmentStart);
            copyLen = segLen <= remaining ? segLen : remaining;
        }
        memcpy(segmentEnd + 1, segmentStart, copyLen);
        segmentEnd[copyLen + 1] = '\0';
        segmentEnd += copyLen;
        srcPos += copyLen;
    }
    return 0;
}

static bool is_userinfo_punct(unsigned char c) {
    switch (c) {
    case 0x21: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a:
    case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x3a: case 0x3b: case 0x3d: case 0x5f:
    case 0x7e:
        return true;
    default:
        return false;
    }
}

int32_t sceHttpUriParse(OrbisHttpUriElement *out, const char *srcUri, void *pool, uint64_t *require, uint64_t prepare) {
    if (!srcUri) {
        return ORBIS_HTTP_ERROR_INVALID_URL;
    }
    const bool writeOutput = out && pool;
    if (!writeOutput && !require) {
        return ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    if (writeOutput) memset(out, 0, sizeof(*out));
    char *poolBytes = (char *)pool;

    // Scheme: up to 0x20 characters [A-Za-z0-9+-.] starting with a letter, then ':'.
    bool hasScheme = false;
    uint64_t schemeLen = 0;
    {
        uint64_t i = 0;
        while (i < 0x20 && srcUri[i]) {
            if (srcUri[i] == ':') break;
            const unsigned char c = (unsigned char)srcUri[i];
            if (!isalnum(c) && c != '+' && c != '-' && c != '.') break;
            i++;
        }
        if (i > 0 && srcUri[i] == ':' && isalpha((unsigned char)srcUri[0])) {
            hasScheme = true;
            schemeLen = i;
        }
    }

    uint64_t poolUsed;
    uint64_t inputOffset;
    if (hasScheme) {
        if (writeOutput) {
            if (prepare < schemeLen + 1) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
            memcpy(poolBytes, srcUri, schemeLen);
            poolBytes[schemeLen] = '\0';
            out->scheme = poolBytes;
        }
        poolUsed = schemeLen + 1;
        inputOffset = schemeLen + 1;
    } else {
        if (writeOutput) {
            if (prepare < 2) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
            poolBytes[0] = '\0';
            out->scheme = poolBytes;
        }
        poolUsed = 1;
        inputOffset = 0;
    }

    int slashCount = 0;
    for (const char *p = srcUri + inputOffset; *p == '/'; p++) slashCount++;
    if (slashCount >= 2) {
        inputOffset += 2;
    } else if (writeOutput) {
        out->opaque = true;
    }

    // Userinfo: scan to '@' (the first ':' splits user and password).
    const char *authStart = srcUri + inputOffset;
    uint64_t scanPos = 0, colonPos = 0, atPos = 0;
    bool seenColon = false, seenAt = false;
    for (;;) {
        const unsigned char c = (unsigned char)authStart[scanPos];
        if (c == 0) break;
        if (c == '@') {
            seenAt = true;
            atPos = scanPos;
            break;
        }
        if (!seenColon && c == ':') {
            seenColon = true;
            colonPos = scanPos;
        } else {
            if ((signed char)c < 0) break;
            if (!isalnum(c) && !is_userinfo_punct(c)) break;
        }
        scanPos++;
    }

    char *userDest = poolBytes + poolUsed;
    uint64_t inputAdvance = 0;
    if (seenAt) {
        uint64_t passOffset, passLen, userLen;
        if (seenColon) {
            userLen = colonPos;
            passOffset = colonPos + 1;
            passLen = atPos - passOffset;
        } else {
            userLen = atPos;
            passOffset = atPos + 1;
            passLen = 0;
        }
        if (writeOutput) {
            if (prepare - poolUsed < passOffset + passLen + 1) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
            memcpy(userDest, authStart, userLen);
            userDest[userLen] = '\0';
            memcpy(userDest + passOffset, authStart + passOffset, passLen);
            userDest[passOffset + passLen] = '\0';
            out->username = userDest;
            out->password = userDest + passOffset;
        }
        poolUsed += passOffset + passLen + 1;
        inputAdvance = atPos + 1;
    } else {
        if (writeOutput) {
            if (prepare - poolUsed < 2) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
            userDest[0] = '\0';
            userDest[1] = '\0';
            out->username = userDest;
            out->password = userDest + 1;
        }
        poolUsed += 2;
    }
    inputOffset += inputAdvance;

    // Host: [A-Za-z0-9-._], or an IPv6 literal in brackets (stored without them).
    char *hostDest = poolBytes + poolUsed;
    const char *hostStart = srcUri + inputOffset;
    const char firstHostChar = hostStart[0];
    uint64_t hostScanLen = 0;
    uint64_t storedHostLen = 0;
    if (firstHostChar == '.') {
        hostScanLen = 0;
        storedHostLen = 0;
    } else if (firstHostChar == '[') {
        hostScanLen = 1;
        for (;;) {
            if (hostScanLen == 0xff) return ORBIS_HTTP_ERROR_INVALID_URL;
            const unsigned char c = (unsigned char)hostStart[hostScanLen];
            if (c == 0 || (signed char)c < 0 || c == ']') break;
            if (!isalnum(c) && c != '-' && c != '.' && c != '_' && c != ':') break;
            hostScanLen++;
        }
        if (hostStart[hostScanLen] != ']') return ORBIS_HTTP_ERROR_INVALID_URL;
        storedHostLen = hostScanLen - 1;
        hostScanLen++;
    } else {
        for (;;) {
            if (hostScanLen == 0xff) return ORBIS_HTTP_ERROR_INVALID_URL;
            const unsigned char c = (unsigned char)hostStart[hostScanLen];
            if (c == 0 || (signed char)c < 0) break;
            if (!isalnum(c) && c != '-' && c != '.' && c != '_') break;
            hostScanLen++;
        }
        storedHostLen = hostScanLen;
    }
    if (writeOutput) {
        if (prepare - poolUsed < storedHostLen + 1) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        memcpy(hostDest, firstHostChar == '[' ? hostStart + 1 : hostStart, storedHostLen);
        hostDest[storedHostLen] = '\0';
        out->hostname = hostDest;
    }
    poolUsed += storedHostLen + 1;
    inputOffset += hostScanLen;

    // Port: up to 5 digits after ':', followed by the end or '/'.
    bool hasExplicitPort = false;
    uint16_t portValue = 0;
    if (srcUri[inputOffset] == ':') {
        inputOffset++;
        const char *digits = srcUri + inputOffset;
        uint64_t digitsLen = 0;
        uint32_t port32 = 0;
        while (digitsLen < 5 && isdigit((unsigned char)digits[digitsLen])) {
            port32 = port32 * 10 + (uint32_t)(digits[digitsLen] - '0');
            digitsLen++;
        }
        if (port32 > 0x10000) return ORBIS_HTTP_ERROR_INVALID_URL;
        const char afterPort = digits[digitsLen];
        if (afterPort != '\0' && afterPort != '/') return ORBIS_HTTP_ERROR_INVALID_URL;
        if (digitsLen > 0) {
            hasExplicitPort = true;
            portValue = (uint16_t)port32;
        }
        inputOffset += digitsLen;
    }
    if (writeOutput) {
        if (hasExplicitPort) {
            out->port = portValue;
        } else if (out->scheme) {
            out->port = scheme_default_port(out->scheme, strlen(out->scheme));
        }
    }

    // Path up to '?' or '#', normalized by sceHttpUriSweepPath.
    char *pathDest = poolBytes + poolUsed;
    const char *pathStart = srcUri + inputOffset;
    uint64_t pathLen = 0;
    while (pathStart[pathLen] && pathStart[pathLen] != '?' && pathStart[pathLen] != '#') {
        if (pathLen >= 0x3fff) return ORBIS_HTTP_ERROR_INVALID_URL;
        pathLen++;
    }
    if (writeOutput) {
        if (prepare - poolUsed < pathLen + 1) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        char *tmp = malloc(pathLen + 1);
        if (!tmp) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        memcpy(tmp, pathStart, pathLen);
        tmp[pathLen] = '\0';
        memcpy(pathDest, tmp, pathLen + 1);
        sceHttpUriSweepPath(pathDest, tmp, pathLen + 1);
        free(tmp);
        out->path = pathDest;
    }
    poolUsed += pathLen + 1;
    inputOffset += pathLen;

    // Query with its leading '?'.
    char *queryDest = poolBytes + poolUsed;
    uint64_t queryLen = 0;
    if (srcUri[inputOffset] == '?') {
        queryLen = 1;
        while (srcUri[inputOffset + queryLen] && srcUri[inputOffset + queryLen] != '#') {
            if (queryLen >= 0x3fff) return ORBIS_HTTP_ERROR_INVALID_URL;
            queryLen++;
        }
    }
    if (writeOutput) {
        if (prepare - poolUsed < queryLen + 1) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        memcpy(queryDest, srcUri + inputOffset, queryLen);
        queryDest[queryLen] = '\0';
        out->query = queryDest;
    }
    poolUsed += queryLen + 1;
    inputOffset += queryLen;

    // Fragment with its leading '#'.
    char *fragDest = poolBytes + poolUsed;
    uint64_t fragLen = 0;
    if (srcUri[inputOffset] == '#') {
        uint64_t i = 1;
        while (srcUri[inputOffset + i]) {
            if (i >= 0x3fff) return ORBIS_HTTP_ERROR_INVALID_URL;
            i++;
        }
        fragLen = i;
    }
    if (writeOutput) {
        if (prepare - poolUsed < fragLen + 1) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        memcpy(fragDest, srcUri + inputOffset, fragLen);
        fragDest[fragLen] = '\0';
        out->fragment = fragDest;
    }
    poolUsed += fragLen + 1;

    if (require) *require = poolUsed;
    return 0;
}

// Replaces the last segment of dest (after its last '/') with src, or dest entirely when src is
// an absolute path (shadPS4 NormalizeAndAppendPath).
static void normalize_and_append_path(char *dest, const char *src) {
    char *lastSlash = strrchr(dest, '/');
    if (!lastSlash) {
        const size_t length = strlen(dest);
        dest[length] = '/';
        dest[length + 1] = '\0';
    } else {
        lastSlash[1] = '\0';
    }
    if (*src == '/') dest[0] = '\0';
    const size_t length = strnlen(dest, 0x3fff);
    strncat(dest, src, 0x3fff - length);
}

int32_t sceHttpUriMerge(char *mergedUrl, const char *url, const char *relativeUri, uint64_t *require, uint64_t prepare,
                        uint32_t option) {
    if (option != 0 || !url || !relativeUri) {
        return ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    uint64_t localSizeBaseUrl = 0, localSizeRelativeUri = 0;
    int32_t rc = sceHttpUriParse(NULL, url, NULL, &localSizeBaseUrl, 0);
    if (rc < 0) return rc;
    rc = sceHttpUriParse(NULL, relativeUri, NULL, &localSizeRelativeUri, 0);
    if (rc < 0) return rc;

    uint64_t baseUrlLength = strnlen(url, 0x3fff);
    uint64_t relativeUriLength = strnlen(relativeUri, 0x3fff);
    const uint64_t requiredLength = localSizeBaseUrl + 2 + (relativeUriLength + baseUrlLength) * 2;
    if (require) *require = requiredLength;
    if (!mergedUrl) return 0;
    if (prepare < requiredLength) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;

    // The output buffer doubles as the parse pool, past room for both inputs.
    const uint64_t totalLength = strnlen(url, 0x3fff);
    baseUrlLength = strnlen(relativeUri, 0x3fff);
    uint64_t combinedLength = totalLength + 1 + baseUrlLength;
    relativeUriLength = prepare - combinedLength;

    OrbisHttpUriElement parsed;
    rc = sceHttpUriParse(&parsed, relativeUri, mergedUrl + totalLength + baseUrlLength + 1, &localSizeRelativeUri,
                         relativeUriLength);
    if (rc < 0) return rc;
    if (!parsed.opaque) {
        strncpy(mergedUrl, relativeUri, requiredLength);
        if (require) *require = strnlen(relativeUri, 0x3fff) + 1;
        return 0;
    }

    rc = sceHttpUriParse(&parsed, url, mergedUrl + totalLength + baseUrlLength + 1, &localSizeBaseUrl,
                         relativeUriLength);
    if (rc < 0) return rc;
    combinedLength += localSizeBaseUrl;
    strncpy(mergedUrl + combinedLength, parsed.path, prepare - combinedLength);
    normalize_and_append_path(mergedUrl + combinedLength, relativeUri);
    parsed.path = mergedUrl + combinedLength;

    rc = sceHttpUriBuild(mergedUrl, NULL, prepare - (baseUrlLength + totalLength) - 1, &parsed, 0x3f);
    return rc >= 0 ? 0 : rc;
}
