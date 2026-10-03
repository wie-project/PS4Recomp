#include "ps4_http.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_http_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_http_next_id = 1;

static inline void *guest_to_host(const GuestContext *ctx, uint64_t addr) {
    if (!addr) return NULL;
    if (ctx && ctx->mem_base && addr < (1ULL << 39)) {
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

int32_t sceHttpInit(int32_t libnetMemId, int32_t libsslCtxId, uint64_t poolSize) {
    (void)libnetMemId;
    (void)libsslCtxId;
    (void)poolSize;
    return alloc_http_id();
}

int32_t sceHttpTerm(int32_t libhttpCtxId) {
    (void)libhttpCtxId;
    return 0;
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
