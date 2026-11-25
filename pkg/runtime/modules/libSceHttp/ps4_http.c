#include "ps4_http.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_http_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_http_next_id = 1;

static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < ctx->mem_size) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

static int32_t alloc_http_id(void) {
    pthread_mutex_lock(&g_http_mutex);
    int32_t id = g_http_next_id++;
    pthread_mutex_unlock(&g_http_mutex);
    return id;
}

// Live library contexts (sceHttpInit results) and whether sceHttpsLoadCert was called on them.
#define HTTP_MAX_CTX 256
static struct {
    int32_t id;
    bool custom_ca;
} g_http_ctx[HTTP_MAX_CTX]; // id 0 = free slot; protected by g_http_mutex

int32_t sceHttpInit(int32_t libnetMemId, int32_t libsslCtxId, uint64_t poolSize) {
    (void)libnetMemId;
    (void)libsslCtxId;
    (void)poolSize;
    int32_t id = alloc_http_id();
    bool stored = false;
    pthread_mutex_lock(&g_http_mutex);
    for (int i = 0; i < HTTP_MAX_CTX; i++) {
        if (g_http_ctx[i].id == 0) {
            g_http_ctx[i].id = id;
            g_http_ctx[i].custom_ca = false;
            stored = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_http_mutex);
    // All slots used: the console's answer when its context pool is exhausted.
    return stored ? id : (int32_t)0x80431022; // ORBIS_HTTP_ERROR_OUT_OF_MEMORY
}

int32_t sceHttpTerm(int32_t libhttpCtxId) {
    pthread_mutex_lock(&g_http_mutex);
    for (int i = 0; i < HTTP_MAX_CTX; i++) {
        if (g_http_ctx[i].id == libhttpCtxId) g_http_ctx[i].id = 0;
    }
    pthread_mutex_unlock(&g_http_mutex);
    return 0;
}

// int sceHttpsLoadCert(int libhttpCtxId, int caCertNum, const void **caList, const void *cert,
//                      const void *privKey)
//
// Registers client certificates / CA certificates for the HTTPS requests of a library context.
// This runtime's libSceHttp never opens a network connection (requests fail with a network
// error), so there is no TLS handshake the certificates could be used in; what remains
// observable is the call's validation and the per-context record that custom CAs were
// supplied (ps4_http_ctx_has_custom_ca). No runtime path reads that flag today: the request
// functions never reach a TLS layer, so it is state for a future network implementation. Checks as in shadPS4
// (libraries/network/http.cpp sceHttpsLoadCert): no initialised library is BEFORE_INIT, an
// unknown context id is INVALID_ID. The certificate blobs are not parsed.
int32_t sceHttpsLoadCert(int32_t libhttpCtxId, int32_t caCertNum, const void **caList, const void *cert,
                         const void *privKey) {
    (void)caCertNum;
    (void)caList;
    (void)cert;
    (void)privKey;
    int32_t rc = (int32_t)0x80431001; // ORBIS_HTTP_ERROR_BEFORE_INIT
    pthread_mutex_lock(&g_http_mutex);
    for (int i = 0; i < HTTP_MAX_CTX; i++) {
        if (g_http_ctx[i].id == 0) continue;
        rc = (int32_t)ORBIS_HTTP_ERROR_INVALID_ID; // at least one live context: only the id can be wrong
        break;
    }
    for (int i = 0; i < HTTP_MAX_CTX; i++) {
        if (g_http_ctx[i].id != 0 && g_http_ctx[i].id == libhttpCtxId) {
            g_http_ctx[i].custom_ca = true;
            rc = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_http_mutex);
    return rc;
}

bool ps4_http_ctx_has_custom_ca(int32_t libhttpCtxId) {
    bool r = false;
    pthread_mutex_lock(&g_http_mutex);
    for (int i = 0; i < HTTP_MAX_CTX; i++) {
        if (g_http_ctx[i].id != 0 && g_http_ctx[i].id == libhttpCtxId) r = g_http_ctx[i].custom_ca;
    }
    pthread_mutex_unlock(&g_http_mutex);
    return r;
}

int32_t sceHttpCreateTemplate(int32_t libhttpCtxId, const char *userAgent, int32_t httpVer, int32_t isAutoProxyConf) {
    (void)libhttpCtxId;
    (void)userAgent;
    (void)httpVer;
    (void)isAutoProxyConf;
    return alloc_http_id();
}

int32_t sceHttpDeleteTemplate(int32_t tmplId) {
    (void)tmplId;
    return 0;
}

int32_t sceHttpCreateConnectionWithURL(int32_t tmplId, const char *url, bool enableKeepalive) {
    (void)tmplId;
    (void)url;
    (void)enableKeepalive;
    return alloc_http_id();
}

int32_t sceHttpDeleteConnection(int32_t connId) {
    (void)connId;
    return 0;
}

int32_t sceHttpCreateRequest(int32_t connId, int32_t method, const char *path, uint64_t contentLength) {
    (void)connId;
    (void)method;
    (void)path;
    (void)contentLength;
    return alloc_http_id();
}

int32_t sceHttpSetAutoRedirect(int32_t id, int32_t isEnable) {
    (void)id;
    (void)isEnable;
    return 0;
}

static inline bool is_ascii_digit_char(char c) {
    unsigned char uc = (unsigned char)c;
    return uc >= '0' && uc <= '9';
}

int32_t sceHttpParseStatusLine(const char *statusLine, uint64_t lineLen, int32_t *httpMajorVer,
                               int32_t *httpMinorVer, int32_t *responseCode,
                               const char **reasonPhrase, uint64_t *phraseLen) {
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

    uint64_t index = 7;
    {
        unsigned char ch = (unsigned char)statusLine[5];
        while (ch < 0x80 && (ch >= '0' && ch <= '9')) {
            *httpMajorVer = *httpMajorVer * 10 + (statusLine[index - 2] - '0');
            char next = (index - 1 < lineLen) ? statusLine[index - 1] : 0;
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

    {
        unsigned char ch = (unsigned char)statusLine[index - 1];
        while (ch < 0x80 && (ch >= '0' && ch <= '9')) {
            *httpMinorVer = *httpMinorVer * 10 + (statusLine[index - 1] - '0');
            char next = (index < lineLen) ? statusLine[index] : 0;
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
    *responseCode = (statusLine[index] - '0') * 100 + (statusLine[index + 1] - '0') * 10 +
                    (statusLine[index + 2] - '0');

    const char *phraseStart = statusLine + index + 3;
    uint64_t remaining = lineLen - index;
    uint64_t maxScan = remaining - 3;
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
        uint64_t bytesConsumed = (uint64_t)(phraseStart - statusLine) + scanLen + 1;
        return (int32_t)bytesConsumed;
    }

    return ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE;
}

int32_t sceHttpCreateRequestWithURL(int32_t connId, int32_t method, const char *url, uint64_t contentLength) {
    (void)connId;
    (void)method;
    (void)url;
    (void)contentLength;
    return alloc_http_id();
}

int32_t sceHttpCreateRequestWithURL2(int32_t connId, const char *method, const char *url, uint64_t contentLength) {
    (void)connId;
    (void)method;
    (void)url;
    (void)contentLength;
    return alloc_http_id();
}

int32_t sceHttpDeleteRequest(int32_t reqId) {
    (void)reqId;
    return 0;
}

int32_t sceHttpSendRequest(int32_t reqId, const void *postData, uint64_t size) {
    (void)reqId;
    (void)postData;
    (void)size;
    // Authentic offline status: network connection unavailable
    return (int32_t)ORBIS_HTTP_ERROR_NETWORK;
}

int32_t sceHttpAbortRequest(int32_t reqId) {
    (void)reqId;
    return 0;
}

int32_t sceHttpWaitRequest(OrbisHttpEpollHandle eh, OrbisHttpNBEvent *nbev, int32_t maxevents, int32_t timeout) {
    (void)eh;
    (void)nbev;
    (void)maxevents;
    (void)timeout;
    return (int32_t)ORBIS_HTTP_ERROR_NETWORK;
}

int32_t sceHttpReadData(int32_t reqId, void *data, uint64_t size) {
    (void)reqId;
    (void)data;
    (void)size;
    return 0; // 0 bytes read
}

int32_t sceHttpGetStatusCode(int32_t reqId, int32_t *statusCode) {
    (void)reqId;
    if (statusCode) {
        *statusCode = 0;
    }
    return (int32_t)ORBIS_HTTP_ERROR_NETWORK;
}

int32_t sceHttpGetResponseContentLength(int32_t reqId, int32_t *result, uint64_t *contentLength) {
    (void)reqId;
    if (result) {
        *result = 0;
    }
    if (contentLength) {
        *contentLength = 0;
    }
    return (int32_t)ORBIS_HTTP_ERROR_NETWORK;
}

int32_t sceHttpGetAllResponseHeaders(int32_t reqId, char **header, uint64_t *headerSize) {
    (void)reqId;
    if (header) {
        *header = NULL;
    }
    if (headerSize) {
        *headerSize = 0;
    }
    return (int32_t)ORBIS_HTTP_ERROR_NETWORK;
}

int32_t sceHttpAddRequestHeader(int32_t id, const char *name, const char *value, int32_t mode) {
    (void)id;
    (void)name;
    (void)value;
    (void)mode;
    return 0;
}

int32_t sceHttpSetNonblock(int32_t id, int32_t isEnable) {
    (void)id;
    (void)isEnable;
    return 0;
}

int32_t sceHttpGetLastErrno(int32_t reqId, int32_t *errNum) {
    (void)reqId;
    if (errNum) {
        *errNum = 0;
    }
    return 0;
}

int32_t sceHttpUriParse(OrbisHttpUriElement *out, const char *srcUri, void *pool, uint64_t *require, uint64_t prepare) {
    (void)pool;
    (void)require;
    (void)prepare;
    if (!out || !srcUri) {
        return (int32_t)0x804311fe; // INVALID_VALUE
    }
    memset(out, 0, sizeof(OrbisHttpUriElement));
    return 0;
}

int32_t sceHttpUriBuild(char *out, uint64_t *require, uint64_t prepare, const OrbisHttpUriElement *srcElement, uint32_t option) {
    (void)prepare;
    (void)srcElement;
    (void)option;
    if (!out) {
        if (require) *require = 1;
        return 0;
    }
    out[0] = '\0';
    if (require) *require = 1;
    return 0;
}

int32_t sceHttpCreateEpoll(int32_t libhttpCtxId, OrbisHttpEpollHandle *eh) {
    (void)libhttpCtxId;
    if (eh) {
        *eh = (OrbisHttpEpollHandle)(uintptr_t)alloc_http_id();
    }
    return 0;
}

int32_t sceHttpSetEpoll(int32_t id, OrbisHttpEpollHandle eh, void *userArg) {
    (void)id;
    (void)eh;
    (void)userArg;
    return 0;
}

int32_t sceHttpDestroyEpoll(int32_t libhttpCtxId, OrbisHttpEpollHandle eh) {
    (void)libhttpCtxId;
    (void)eh;
    return 0;
}

static inline bool is_http_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

int32_t sceHttpUriEscape(char *out, uint64_t *require, uint64_t prepare, const char *in) {
    if (!in) {
        return (int32_t)0x80431102; // ORBIS_HTTP_ERROR_INVALID_VALUE
    }

    uint64_t needed = 0;
    const char *src = in;
    while (*src) {
        unsigned char c = (unsigned char)(*src);
        if (is_http_unreserved(c)) {
            needed++;
        } else {
            needed += 3; // %XX
        }
        src++;
    }
    needed++; // null terminator

    if (require) {
        *require = needed;
    }

    if (!out) {
        return 0; // ORBIS_OK
    }

    if (prepare < needed) {
        return (int32_t)0x80431001; // ORBIS_HTTP_ERROR_OUT_OF_MEMORY
    }

    static const char hex_chars[] = "0123456789ABCDEF";
    src = in;
    char *dst = out;
    while (*src) {
        unsigned char c = (unsigned char)(*src);
        if (is_http_unreserved(c)) {
            *dst++ = *src;
        } else {
            *dst++ = '%';
            *dst++ = hex_chars[(c >> 4) & 0x0F];
            *dst++ = hex_chars[c & 0x0F];
        }
        src++;
    }
    *dst = '\0';
    return 0;
}

int32_t sceHttpCreateConnection(int32_t tmplId, const char *serverName, const char *scheme, uint16_t port, int32_t isEnableKeepalive) {
    (void)tmplId;
    (void)serverName;
    (void)scheme;
    (void)port;
    (void)isEnableKeepalive;
    return alloc_http_id();
}

int32_t sceHttpCreateRequest2(int32_t connId, const char *method, const char *path, uint64_t contentLength) {
    (void)connId;
    (void)method;
    (void)path;
    (void)contentLength;
    return alloc_http_id();
}

int32_t sceHttpSetConnectTimeOut(int32_t id, uint32_t usec) {
    (void)id;
    (void)usec;
    return 0;
}

int32_t sceHttpSetSendTimeOut(int32_t id, uint32_t usec) {
    (void)id;
    (void)usec;
    return 0;
}

int32_t sceHttpSetRecvTimeOut(int32_t id, uint32_t usec) {
    (void)id;
    (void)usec;
    return 0;
}

int32_t sceHttpSetRequestContentLength(int32_t id, uint64_t contentLength) {
    (void)id;
    (void)contentLength;
    return 0;
}

int32_t sceHttpSetChunkedTransferEnabled(int32_t id, int32_t isEnable) {
    (void)id;
    (void)isEnable;
    return 0;
}

int32_t sceHttpSetAuthEnabled(int32_t id, int32_t isEnable) {
    (void)id;
    (void)isEnable;
    return 0;
}

int32_t sceHttpCookieFlush(int32_t libhttpCtxId) {
    (void)libhttpCtxId;
    return 0;
}

int32_t sceHttpSetRedirectCallback(int32_t id, void *cbfunc, void *userArg) {
    (void)id;
    (void)cbfunc;
    (void)userArg;
    return 0;
}

int32_t sceHttpGetMemoryPoolStats(int32_t libhttpCtxId, void *currentStat) {
    (void)libhttpCtxId;
    (void)currentStat;
    return 0;
}

int32_t sceHttpsEnableOption(int32_t id, uint32_t sslFlags) {
    (void)id;
    (void)sslFlags;
    return 0;
}

int32_t sceHttpsDisableOption(int32_t id, uint32_t sslFlags) {
    (void)id;
    (void)sslFlags;
    return 0;
}

int32_t sceHttpsSetSslCallback(int32_t id, void *cbfunc, void *userArg) {
    (void)id;
    (void)cbfunc;
    (void)userArg;
    return 0;
}

// libSceHttp2
int32_t sceHttp2Init(int32_t net_id, int32_t ssl_id, uint64_t pool_size, int32_t max_requests) {
    (void)net_id;
    (void)ssl_id;
    (void)pool_size;
    (void)max_requests;
    return alloc_http_id();
}

int32_t sceHttp2Term(int32_t ctx_id) {
    (void)ctx_id;
    return 0;
}

static pthread_mutex_t g_companion_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_companion_inited = false;
static bool g_companion_started = false;
static void *g_companion_cb = NULL;
static void *g_companion_userdata = NULL;

int32_t sceCompanionHttpdInitialize2(const void *param) {
    (void)param;
    pthread_mutex_lock(&g_companion_mutex);
    if (g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_ALREADY_INITIALIZED;
    }
    g_companion_inited = true;
    g_companion_started = false;
    g_companion_cb = NULL;
    g_companion_userdata = NULL;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdTerminate(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    g_companion_inited = false;
    g_companion_started = false;
    g_companion_cb = NULL;
    g_companion_userdata = NULL;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdStart(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    if (g_companion_started) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_ALREADY_STARTED;
    }
    g_companion_started = true;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdStop(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    if (!g_companion_started) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_STARTED;
    }
    g_companion_started = false;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdGetEvent(OrbisCompanionHttpdEvent *event) {
    if (!event) {
        return ORBIS_COMPANION_HTTPD_ERROR_INVALID_PARAM;
    }
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    memset(event, 0, sizeof(*event));
    event->event = ORBIS_COMPANION_HTTPD_EVENT_DISCONNECT;
    pthread_mutex_unlock(&g_companion_mutex);
    return ORBIS_COMPANION_HTTPD_ERROR_NO_EVENT;
}

int32_t sceCompanionHttpdRegisterRequestCallback2(void *cb, void *userData) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    g_companion_cb = cb;
    g_companion_userdata = userData;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceCompanionHttpdUnregisterRequestCallback(void) {
    pthread_mutex_lock(&g_companion_mutex);
    if (!g_companion_inited) {
        pthread_mutex_unlock(&g_companion_mutex);
        return ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    }
    g_companion_cb = NULL;
    g_companion_userdata = NULL;
    pthread_mutex_unlock(&g_companion_mutex);
    return 0;
}

int32_t sceHttpSetResolveTimeOut(int32_t id, uint32_t timeout) {
    if (id <= 0) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_lock(&g_http_mutex);
    if (id >= g_http_next_id) {
        pthread_mutex_unlock(&g_http_mutex);
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_unlock(&g_http_mutex);
    (void)timeout;
    return 0;
}

int32_t sceHttpSetResolveRetry(int32_t id, int32_t retry) {
    if (retry < 0) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    if (id <= 0) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_lock(&g_http_mutex);
    if (id >= g_http_next_id) {
        pthread_mutex_unlock(&g_http_mutex);
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_unlock(&g_http_mutex);
    return 0;
}

int32_t sceHttpSetCookieEnabled(int32_t id, int32_t enabled) {
    if (id <= 0) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_lock(&g_http_mutex);
    if (id >= g_http_next_id) {
        pthread_mutex_unlock(&g_http_mutex);
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_unlock(&g_http_mutex);
    (void)enabled;
    return 0;
}

int32_t sceHttpsGetCaList(int32_t id, OrbisHttpsCaList *list) {
    if (id <= 0) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    if (!list) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    pthread_mutex_lock(&g_http_mutex);
    bool valid = false;
    for (int i = 0; i < HTTP_MAX_CTX; i++) {
        if (g_http_ctx[i].id == id) {
            valid = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_http_mutex);
    if (!valid) {
        return (int32_t)ORBIS_HTTP_ERROR_INVALID_ID;
    }
    list->certs = NULL;
    list->certsNum = 0;
    return 0;
}

// Shims
void shim_sceHttpInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpInit((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint64_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceHttpTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpTerm((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpCreateTemplate(GuestContext *ctx) {
    const char *ua = ctx->rsi ? (const char *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpCreateTemplate(
        (int32_t)ctx->rdi, ua, (int32_t)ctx->rdx, (int32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttpDeleteTemplate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDeleteTemplate((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpCreateConnectionWithURL(GuestContext *ctx) {
    const char *url = ctx->rsi ? (const char *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpCreateConnectionWithURL((int32_t)ctx->rdi, url, (bool)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceHttpDeleteConnection(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDeleteConnection((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpCreateRequest(GuestContext *ctx) {
    const char *path = ctx->rdx ? (const char *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpCreateRequest(
        (int32_t)ctx->rdi, (int32_t)ctx->rsi, path, (uint64_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttpSetAutoRedirect(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetAutoRedirect((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpParseStatusLine(GuestContext *ctx) {
    const char *statusLine = (const char *)guest_to_host(ctx, ctx->rdi);
    uint64_t lineLen = ctx->rsi;
    int32_t *httpMajorVer = (int32_t *)guest_to_host(ctx, ctx->rdx);
    int32_t *httpMinorVer = (int32_t *)guest_to_host(ctx, ctx->rcx);
    int32_t *responseCode = (int32_t *)guest_to_host(ctx, ctx->r8);
    const char **reasonPhrase = (const char **)guest_to_host(ctx, ctx->r9);
    uint64_t *phraseLen = NULL;
    if (ctx->rsp && ctx->mem_base && (ctx->rsp + 8) < ctx->mem_size) {
        uint64_t stack_val = *(uint64_t *)(ctx->mem_base + ctx->rsp + 8);
        phraseLen = (uint64_t *)guest_to_host(ctx, stack_val);
    }
    ctx->rax = (uint64_t)(int64_t)sceHttpParseStatusLine(statusLine, lineLen, httpMajorVer,
                                                         httpMinorVer, responseCode,
                                                         reasonPhrase, phraseLen);
    SHIM_RETURN();
}

void shim_sceHttpCreateRequestWithURL(GuestContext *ctx) {
    const char *url = ctx->rdx ? (const char *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpCreateRequestWithURL(
        (int32_t)ctx->rdi, (int32_t)ctx->rsi, url, (uint64_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttpCreateRequestWithURL2(GuestContext *ctx) {
    const char *method = ctx->rsi ? (const char *)(ctx->mem_base + ctx->rsi) : NULL;
    const char *url = ctx->rdx ? (const char *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpCreateRequestWithURL2(
        (int32_t)ctx->rdi, method, url, (uint64_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttpDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDeleteRequest((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpSendRequest(GuestContext *ctx) {
    const void *data = ctx->rsi ? (const void *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpSendRequest((int32_t)ctx->rdi, data, (uint64_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceHttpAbortRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpAbortRequest((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpWaitRequest(GuestContext *ctx) {
    OrbisHttpNBEvent *ev = ctx->rsi ? (OrbisHttpNBEvent *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpWaitRequest(
        (OrbisHttpEpollHandle)ctx->rdi, ev, (int32_t)ctx->rdx, (int32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttpReadData(GuestContext *ctx) {
    void *buf = ctx->rsi ? (void *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpReadData((int32_t)ctx->rdi, buf, (uint64_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceHttpGetStatusCode(GuestContext *ctx) {
    int32_t *sc = ctx->rsi ? (int32_t *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpGetStatusCode((int32_t)ctx->rdi, sc);
    SHIM_RETURN();
}

void shim_sceHttpGetResponseContentLength(GuestContext *ctx) {
    int32_t *rc = ctx->rsi ? (int32_t *)(ctx->mem_base + ctx->rsi) : NULL;
    uint64_t *len = ctx->rdx ? (uint64_t *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpGetResponseContentLength(
        (int32_t)ctx->rdi, rc, len);
    SHIM_RETURN();
}

void shim_sceHttpGetAllResponseHeaders(GuestContext *ctx) {
    char **headers = ctx->rsi ? (char **)(ctx->mem_base + ctx->rsi) : NULL;
    uint64_t *len = ctx->rdx ? (uint64_t *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpGetAllResponseHeaders(
        (int32_t)ctx->rdi, headers, len);
    SHIM_RETURN();
}

void shim_sceHttpAddRequestHeader(GuestContext *ctx) {
    const char *name = ctx->rsi ? (const char *)(ctx->mem_base + ctx->rsi) : NULL;
    const char *value = ctx->rdx ? (const char *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpAddRequestHeader(
        (int32_t)ctx->rdi, name, value, (int32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttpSetNonblock(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetNonblock((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpGetLastErrno(GuestContext *ctx) {
    int32_t *err = ctx->rsi ? (int32_t *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpGetLastErrno((int32_t)ctx->rdi, err);
    SHIM_RETURN();
}

void shim_sceHttpUriParse(GuestContext *ctx) {
    OrbisHttpUriElement *el = ctx->rdi ? (OrbisHttpUriElement *)(ctx->mem_base + ctx->rdi) : NULL;
    const char *uri = ctx->rsi ? (const char *)(ctx->mem_base + ctx->rsi) : NULL;
    void *pool = ctx->rdx ? (void *)(ctx->mem_base + ctx->rdx) : NULL;
    uint64_t *psize = ctx->rcx ? (uint64_t *)(ctx->mem_base + ctx->rcx) : NULL;
    ctx->rax = (uint64_t)sceHttpUriParse(
        el, uri, pool, psize, (uint64_t)ctx->r8);
    SHIM_RETURN();
}

void shim_sceHttpUriBuild(GuestContext *ctx) {
    char *buf = ctx->rdi ? (char *)(ctx->mem_base + ctx->rdi) : NULL;
    uint64_t *req = ctx->rsi ? (uint64_t *)(ctx->mem_base + ctx->rsi) : NULL;
    const OrbisHttpUriElement *el = ctx->rcx ? (const OrbisHttpUriElement *)(ctx->mem_base + ctx->rcx) : NULL;
    ctx->rax = (uint64_t)sceHttpUriBuild(
        buf, req, (uint64_t)ctx->rdx, el, (uint32_t)ctx->r8);
    SHIM_RETURN();
}

void shim_sceHttpCreateEpoll(GuestContext *ctx) {
    OrbisHttpEpollHandle *eh = ctx->rsi ? (OrbisHttpEpollHandle *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)sceHttpCreateEpoll((int32_t)ctx->rdi, eh);
    SHIM_RETURN();
}

void shim_sceHttpSetEpoll(GuestContext *ctx) {
    void *uarg = ctx->rdx ? (void *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)sceHttpSetEpoll((int32_t)ctx->rdi, (OrbisHttpEpollHandle)ctx->rsi, uarg);
    SHIM_RETURN();
}

void shim_sceHttpDestroyEpoll(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDestroyEpoll((int32_t)ctx->rdi, (OrbisHttpEpollHandle)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttp2Init(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttp2Init((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint64_t)ctx->rdx, (int32_t)ctx->rcx);
    SHIM_RETURN();
}

void shim_sceHttp2Term(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttp2Term((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpUriEscape(GuestContext *ctx) {
    char *out = (char *)guest_to_host(ctx, ctx->rdi);
    uint64_t *require = (uint64_t *)guest_to_host(ctx, ctx->rsi);
    uint64_t prepare = ctx->rdx;
    const char *in = (const char *)guest_to_host(ctx, ctx->rcx);
    ctx->rax = (uint64_t)sceHttpUriEscape(out, require, prepare, in);
    SHIM_RETURN();
}

void shim_sceHttpCreateConnection(GuestContext *ctx) {
    int32_t tmplId = (int32_t)ctx->rdi;
    const char *serverName = (const char *)guest_to_host(ctx, ctx->rsi);
    const char *scheme = (const char *)guest_to_host(ctx, ctx->rdx);
    uint16_t port = (uint16_t)ctx->rcx;
    int32_t isEnableKeepalive = (int32_t)ctx->r8;
    ctx->rax = (uint64_t)sceHttpCreateConnection(tmplId, serverName, scheme, port, isEnableKeepalive);
    SHIM_RETURN();
}

void shim_sceHttpCreateRequest2(GuestContext *ctx) {
    int32_t connId = (int32_t)ctx->rdi;
    const char *method = (const char *)guest_to_host(ctx, ctx->rsi);
    const char *path = (const char *)guest_to_host(ctx, ctx->rdx);
    uint64_t contentLength = ctx->rcx;
    ctx->rax = (uint64_t)sceHttpCreateRequest2(connId, method, path, contentLength);
    SHIM_RETURN();
}

void shim_sceHttpSetConnectTimeOut(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetConnectTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetSendTimeOut(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetSendTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetRecvTimeOut(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetRecvTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetRequestContentLength(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetRequestContentLength((int32_t)ctx->rdi, (uint64_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetChunkedTransferEnabled(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetChunkedTransferEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetAuthEnabled(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetAuthEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpCookieFlush(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpCookieFlush((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceHttpSetRedirectCallback(GuestContext *ctx) {
    void *cbfunc = (void *)guest_to_host(ctx, ctx->rsi);
    void *userArg = (void *)guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)sceHttpSetRedirectCallback((int32_t)ctx->rdi, cbfunc, userArg);
    SHIM_RETURN();
}

void shim_sceHttpGetMemoryPoolStats(GuestContext *ctx) {
    void *currentStat = (void *)guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)sceHttpGetMemoryPoolStats((int32_t)ctx->rdi, currentStat);
    SHIM_RETURN();
}

void shim_sceHttpsLoadCert(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceHttpsLoadCert((int32_t)ctx->rdi, (int32_t)ctx->rsi, (const void **)guest_to_host(ctx, ctx->rdx),
                                                   guest_to_host(ctx, ctx->rcx), guest_to_host(ctx, ctx->r8));
    SHIM_RETURN();
}

void shim_sceHttpsEnableOption(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpsEnableOption((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpsDisableOption(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpsDisableOption((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpsSetSslCallback(GuestContext *ctx) {
    void *cbfunc = (void *)guest_to_host(ctx, ctx->rsi);
    void *userArg = (void *)guest_to_host(ctx, ctx->rdx);
    ctx->rax = (uint64_t)sceHttpsSetSslCallback((int32_t)ctx->rdi, cbfunc, userArg);
    SHIM_RETURN();
}



void shim_sceCompanionHttpdInitialize2(GuestContext *ctx) {
    const void *param = (const void *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdInitialize2(param);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdTerminate();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdStart(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdStart();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdStop(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdStop();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdGetEvent(GuestContext *ctx) {
    OrbisCompanionHttpdEvent *event = (OrbisCompanionHttpdEvent *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdGetEvent(event);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdRegisterRequestCallback2(GuestContext *ctx) {
    void *cb = NULL;
    void *userData = NULL;
    if (ctx->rdi > 0x10000) {
        cb = (void *)guest_to_host(ctx, ctx->rdi);
        userData = (void *)guest_to_host(ctx, ctx->rsi);
    } else {
        cb = (void *)guest_to_host(ctx, ctx->rsi);
        userData = (void *)guest_to_host(ctx, ctx->rdx);
    }
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdRegisterRequestCallback2(cb, userData);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdUnregisterRequestCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdUnregisterRequestCallback();
    SHIM_RETURN();
}

// No second-screen client ever connects (there is no companion app server), so the request
// callbacks never run; they are kept as registered.
static uint64_t g_companion_body_cb, g_companion_body_param;

int32_t sceCompanionHttpdInitialize(void) {
    return sceCompanionHttpdInitialize2(NULL);
}

int32_t sceCompanionHttpdRegisterRequestCallback(void *cb, void *userData) {
    return sceCompanionHttpdRegisterRequestCallback2(cb, userData);
}

int32_t sceCompanionHttpdRegisterRequestBodyReceptionCallback(uint64_t cb, uint64_t param) {
    pthread_mutex_lock(&g_companion_mutex);
    int32_t r = 0;
    if (!g_companion_inited) {
        r = ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED;
    } else {
        g_companion_body_cb = cb;
        g_companion_body_param = param;
    }
    pthread_mutex_unlock(&g_companion_mutex);
    return r;
}

int32_t sceCompanionHttpdUnregisterRequestBodyReceptionCallback(void) {
    return sceCompanionHttpdRegisterRequestBodyReceptionCallback(0, 0);
}

void shim_sceCompanionHttpdInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdInitialize();
    SHIM_RETURN();
}

void shim_sceCompanionHttpdRegisterRequestCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdRegisterRequestCallback((void *)(uintptr_t)ctx->rdi,
                                                                           (void *)(uintptr_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdRegisterRequestBodyReceptionCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdRegisterRequestBodyReceptionCallback(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceCompanionHttpdUnregisterRequestBodyReceptionCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionHttpdUnregisterRequestBodyReceptionCallback();
    SHIM_RETURN();
}

// Responses are only built inside request callbacks, and the second-screen status and user
// queries have no client to report: stubs in shadPS4 as well.
PS4_HLE_STUB(sceCompanionHttpdAddHeader)
PS4_HLE_STUB(sceCompanionHttpdSetBody)
PS4_HLE_STUB(sceCompanionHttpdSetStatus)
PS4_HLE_STUB(sceCompanionHttpdGet2ndScreenStatus)
PS4_HLE_STUB(sceCompanionHttpdGetUserId)
PS4_HLE_STUB(sceCompanionHttpdOptParamInitialize)

void shim_sceHttpSetResolveTimeOut(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceHttpSetResolveTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetResolveRetry(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceHttpSetResolveRetry((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpSetCookieEnabled(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceHttpSetCookieEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi);
    SHIM_RETURN();
}

void shim_sceHttpsGetCaList(GuestContext *ctx) {
    OrbisHttpsCaList *list = (OrbisHttpsCaList *)guest_to_host(ctx, ctx->rsi);
    ctx->rax = (uint64_t)(int64_t)sceHttpsGetCaList((int32_t)ctx->rdi, list);
    SHIM_RETURN();
}