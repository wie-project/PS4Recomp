// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHttp2 after shadPS4 network/http2.cpp, which runs HTTP/2 requests through libSceHttp:
// sceHttp2Init / Term, the templates, the request headers, sending, reading and the response
// getters forward to their libSceHttp counterparts. libSceHttp2 has no connections, so
// sceHttp2CreateRequestWithURL opens a libSceHttp connection for the URL (keep-alive) and
// creates the request on it; the request remembers its connection, which
// sceHttp2DeleteRequest deletes with it. The functions shadPS4 leaves as stubs are stubs here
// too (end of file).
#include "ps4_http.h"
#include "ps4_hle_stub.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct Http2RequestConnection {
    int32_t req_id;
    int32_t conn_id;
} Http2RequestConnection;

static pthread_mutex_t g_http2_mutex = PTHREAD_MUTEX_INITIALIZER;
static Http2RequestConnection *g_req_conns; // guarded by g_http2_mutex
static size_t g_req_conn_count;
static size_t g_req_conn_cap;

// Records req -> conn (g_http2_mutex held), replacing an older entry for a reused request id.
static bool track(int32_t req_id, int32_t conn_id) {
    for (size_t i = 0; i < g_req_conn_count; i++) {
        if (g_req_conns[i].req_id == req_id) {
            g_req_conns[i].conn_id = conn_id;
            return true;
        }
    }
    if (g_req_conn_count == g_req_conn_cap) {
        const size_t cap = g_req_conn_cap ? g_req_conn_cap * 2 : 16;
        Http2RequestConnection *p = realloc(g_req_conns, cap * sizeof(*p));
        if (!p) return false;
        g_req_conns = p;
        g_req_conn_cap = cap;
    }
    g_req_conns[g_req_conn_count++] = (Http2RequestConnection){req_id, conn_id};
    return true;
}

int32_t ps4_http2_connection_of(int32_t reqId) {
    int32_t conn = 0;
    pthread_mutex_lock(&g_http2_mutex);
    for (size_t i = 0; i < g_req_conn_count; i++) {
        if (g_req_conns[i].req_id == reqId) conn = g_req_conns[i].conn_id;
    }
    pthread_mutex_unlock(&g_http2_mutex);
    return conn;
}

int32_t sceHttp2Init(int32_t netId, int32_t sslId, uint64_t poolSize, int32_t maxRequests) {
    (void)maxRequests;
    return sceHttpInit(netId, sslId, poolSize);
}

int32_t sceHttp2Term(int32_t ctxId) {
    return sceHttpTerm(ctxId);
}

int32_t sceHttp2CreateTemplate(int32_t ctxId, const char *userAgent, int32_t httpVer, int32_t autoProxyConf) {
    return sceHttpCreateTemplate(ctxId, userAgent, httpVer, autoProxyConf);
}

int32_t sceHttp2DeleteTemplate(int32_t tmplId) {
    return sceHttpDeleteTemplate(tmplId);
}

int32_t sceHttp2CreateRequestWithURL(int32_t tmplId, const char *method, const char *url, uint64_t contentLength) {
    pthread_mutex_lock(&g_http2_mutex);
    const int32_t conn_id = sceHttpCreateConnectionWithURL(tmplId, url, true);
    if (conn_id < 0) {
        pthread_mutex_unlock(&g_http2_mutex);
        return conn_id;
    }
    const int32_t req_id = sceHttpCreateRequestWithURL2(conn_id, method, url, contentLength);
    if (req_id < 0) {
        sceHttpDeleteConnection(conn_id);
        pthread_mutex_unlock(&g_http2_mutex);
        return req_id;
    }
    if (!track(req_id, conn_id)) {
        sceHttpDeleteRequest(req_id);
        sceHttpDeleteConnection(conn_id);
        pthread_mutex_unlock(&g_http2_mutex);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }
    pthread_mutex_unlock(&g_http2_mutex);
    return req_id;
}

int32_t sceHttp2DeleteRequest(int32_t reqId) {
    pthread_mutex_lock(&g_http2_mutex);
    const int32_t rc = sceHttpDeleteRequest(reqId);
    if (rc < 0) {
        pthread_mutex_unlock(&g_http2_mutex);
        return rc;
    }
    int32_t conn_id = 0;
    for (size_t i = 0; i < g_req_conn_count; i++) {
        if (g_req_conns[i].req_id == reqId) {
            conn_id = g_req_conns[i].conn_id;
            g_req_conns[i] = g_req_conns[--g_req_conn_count];
            break;
        }
    }
    pthread_mutex_unlock(&g_http2_mutex);
    if (conn_id == 0) return 0; // not created by sceHttp2CreateRequestWithURL
    return sceHttpDeleteConnection(conn_id);
}

int32_t sceHttp2AbortRequest(int32_t reqId) {
    return sceHttpAbortRequest(reqId);
}

int32_t sceHttp2AddRequestHeader(int32_t id, const char *name, const char *value, uint32_t mode) {
    return sceHttpAddRequestHeader(id, name, value, (int32_t)mode);
}

int32_t sceHttp2GetAllResponseHeaders(GuestContext *ctx, int32_t reqId, uint64_t *header, uint64_t *headerSize) {
    return sceHttpGetAllResponseHeaders(ctx, reqId, header, headerSize);
}

int32_t sceHttp2GetResponseContentLength(int32_t reqId, int32_t *result, uint64_t *contentLength) {
    return sceHttpGetResponseContentLength(reqId, result, contentLength);
}

int32_t sceHttp2GetStatusCode(int32_t reqId, int32_t *statusCode) {
    return sceHttpGetStatusCode(reqId, statusCode);
}

int32_t sceHttp2ReadData(int32_t reqId, void *data, uint64_t size) {
    return sceHttpReadData(reqId, data, size);
}

int32_t sceHttp2SendRequest(int32_t reqId, const void *data, uint64_t size) {
    return sceHttpSendRequest(reqId, data, size);
}

int32_t sceHttp2SetRequestContentLength(int32_t reqId, uint64_t contentLength) {
    return sceHttpSetRequestContentLength(reqId, contentLength);
}

// ---------------------------------------------------------------------------------------------
// Shims

static inline void *g2h(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

#define RET(expr)                                  \
    do {                                           \
        ctx->rax = (uint64_t)(int64_t)(expr);      \
        SHIM_RETURN();                             \
    } while (0)

void shim_sceHttp2Init(GuestContext *ctx) {
    RET(sceHttp2Init((int32_t)ctx->rdi, (int32_t)ctx->rsi, ctx->rdx, (int32_t)ctx->rcx));
}

void shim_sceHttp2Term(GuestContext *ctx) {
    RET(sceHttp2Term((int32_t)ctx->rdi));
}

void shim_sceHttp2CreateTemplate(GuestContext *ctx) {
    RET(sceHttp2CreateTemplate((int32_t)ctx->rdi, (const char *)g2h(ctx, ctx->rsi), (int32_t)ctx->rdx,
                               (int32_t)ctx->rcx));
}

void shim_sceHttp2DeleteTemplate(GuestContext *ctx) {
    RET(sceHttp2DeleteTemplate((int32_t)ctx->rdi));
}

void shim_sceHttp2CreateRequestWithURL(GuestContext *ctx) {
    RET(sceHttp2CreateRequestWithURL((int32_t)ctx->rdi, (const char *)g2h(ctx, ctx->rsi),
                                     (const char *)g2h(ctx, ctx->rdx), ctx->rcx));
}

void shim_sceHttp2DeleteRequest(GuestContext *ctx) {
    RET(sceHttp2DeleteRequest((int32_t)ctx->rdi));
}

void shim_sceHttp2AbortRequest(GuestContext *ctx) {
    RET(sceHttp2AbortRequest((int32_t)ctx->rdi));
}

void shim_sceHttp2AddRequestHeader(GuestContext *ctx) {
    RET(sceHttp2AddRequestHeader((int32_t)ctx->rdi, (const char *)g2h(ctx, ctx->rsi), (const char *)g2h(ctx, ctx->rdx),
                                 (uint32_t)ctx->rcx));
}

void shim_sceHttp2GetAllResponseHeaders(GuestContext *ctx) {
    RET(sceHttp2GetAllResponseHeaders(ctx, (int32_t)ctx->rdi, (uint64_t *)g2h(ctx, ctx->rsi),
                                      (uint64_t *)g2h(ctx, ctx->rdx)));
}

void shim_sceHttp2GetResponseContentLength(GuestContext *ctx) {
    RET(sceHttp2GetResponseContentLength((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi),
                                         (uint64_t *)g2h(ctx, ctx->rdx)));
}

void shim_sceHttp2GetStatusCode(GuestContext *ctx) {
    RET(sceHttp2GetStatusCode((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttp2ReadData(GuestContext *ctx) {
    RET(sceHttp2ReadData((int32_t)ctx->rdi, g2h(ctx, ctx->rsi), ctx->rdx));
}

void shim_sceHttp2SendRequest(GuestContext *ctx) {
    RET(sceHttp2SendRequest((int32_t)ctx->rdi, g2h(ctx, ctx->rsi), ctx->rdx));
}

void shim_sceHttp2SetRequestContentLength(GuestContext *ctx) {
    RET(sceHttp2SetRequestContentLength((int32_t)ctx->rdi, ctx->rsi));
}

// Stubs in shadPS4 as well: cookies and the cookie box, authentication, redirect, timeout,
// SSL and callback settings, the asynchronous calls and the statistics.
PS4_HLE_STUB(sceHttp2AddCookie)
PS4_HLE_STUB(sceHttp2AuthCacheFlush)
PS4_HLE_STUB(sceHttp2CookieExport)
PS4_HLE_STUB(sceHttp2CookieFlush)
PS4_HLE_STUB(sceHttp2CookieImport)
PS4_HLE_STUB(sceHttp2CreateCookieBox)
PS4_HLE_STUB(sceHttp2DeleteCookieBox)
PS4_HLE_STUB(sceHttp2GetAuthEnabled)
PS4_HLE_STUB(sceHttp2GetAutoRedirect)
PS4_HLE_STUB(sceHttp2GetCookie)
PS4_HLE_STUB(sceHttp2GetCookieBox)
PS4_HLE_STUB(sceHttp2GetCookieStats)
PS4_HLE_STUB(sceHttp2GetMemoryPoolStats)
PS4_HLE_STUB(sceHttp2ReadDataAsync)
PS4_HLE_STUB(sceHttp2RedirectCacheFlush)
PS4_HLE_STUB(sceHttp2RemoveRequestHeader)
PS4_HLE_STUB(sceHttp2SendRequestAsync)
PS4_HLE_STUB(sceHttp2SetAuthEnabled)
PS4_HLE_STUB(sceHttp2SetAuthInfoCallback)
PS4_HLE_STUB(sceHttp2SetAutoRedirect)
PS4_HLE_STUB(sceHttp2SetConnectionWaitTimeOut)
PS4_HLE_STUB(sceHttp2SetConnectTimeOut)
PS4_HLE_STUB(sceHttp2SetCookieBox)
PS4_HLE_STUB(sceHttp2SetCookieMaxNum)
PS4_HLE_STUB(sceHttp2SetCookieMaxNumPerDomain)
PS4_HLE_STUB(sceHttp2SetCookieMaxSize)
PS4_HLE_STUB(sceHttp2SetCookieRecvCallback)
PS4_HLE_STUB(sceHttp2SetCookieSendCallback)
PS4_HLE_STUB(sceHttp2SetInflateGZIPEnabled)
PS4_HLE_STUB(sceHttp2SetMinSslVersion)
PS4_HLE_STUB(sceHttp2SetPreSendCallback)
PS4_HLE_STUB(sceHttp2SetRecvTimeOut)
PS4_HLE_STUB(sceHttp2SetRedirectCallback)
PS4_HLE_STUB(sceHttp2SetResolveRetry)
PS4_HLE_STUB(sceHttp2SetResolveTimeOut)
PS4_HLE_STUB(sceHttp2SetSendTimeOut)
PS4_HLE_STUB(sceHttp2SetSslCallback)
PS4_HLE_STUB(sceHttp2SetTimeOut)
PS4_HLE_STUB(sceHttp2SslDisableOption)
PS4_HLE_STUB(sceHttp2SslEnableOption)
PS4_HLE_STUB(sceHttp2WaitAsync)
