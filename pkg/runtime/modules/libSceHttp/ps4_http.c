#include "ps4_http.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_http_mutex = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_http_next_id = 1;

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
}

void shim_sceHttpTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpTerm((int32_t)ctx->rdi);
}

void shim_sceHttpCreateTemplate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpCreateTemplate(
        (int32_t)ctx->rdi, (const char *)ctx->rsi, (int32_t)ctx->rdx, (int32_t)ctx->rcx);
}

void shim_sceHttpDeleteTemplate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDeleteTemplate((int32_t)ctx->rdi);
}

void shim_sceHttpCreateConnectionWithURL(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpCreateConnectionWithURL((int32_t)ctx->rdi, (const char *)ctx->rsi, (bool)ctx->rdx);
}

void shim_sceHttpDeleteConnection(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDeleteConnection((int32_t)ctx->rdi);
}

void shim_sceHttpCreateRequestWithURL(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpCreateRequestWithURL(
        (int32_t)ctx->rdi, (int32_t)ctx->rsi, (const char *)ctx->rdx, (uint64_t)ctx->rcx);
}

void shim_sceHttpCreateRequestWithURL2(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpCreateRequestWithURL2(
        (int32_t)ctx->rdi, (const char *)ctx->rsi, (const char *)ctx->rdx, (uint64_t)ctx->rcx);
}

void shim_sceHttpDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDeleteRequest((int32_t)ctx->rdi);
}

void shim_sceHttpSendRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSendRequest((int32_t)ctx->rdi, (const void *)ctx->rsi, (uint64_t)ctx->rdx);
}

void shim_sceHttpAbortRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpAbortRequest((int32_t)ctx->rdi);
}

void shim_sceHttpWaitRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpWaitRequest(
        (OrbisHttpEpollHandle)ctx->rdi, (OrbisHttpNBEvent *)ctx->rsi, (int32_t)ctx->rdx, (int32_t)ctx->rcx);
}

void shim_sceHttpReadData(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpReadData((int32_t)ctx->rdi, (void *)ctx->rsi, (uint64_t)ctx->rdx);
}

void shim_sceHttpGetStatusCode(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpGetStatusCode((int32_t)ctx->rdi, (int32_t *)ctx->rsi);
}

void shim_sceHttpGetResponseContentLength(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpGetResponseContentLength(
        (int32_t)ctx->rdi, (int32_t *)ctx->rsi, (uint64_t *)ctx->rdx);
}

void shim_sceHttpGetAllResponseHeaders(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpGetAllResponseHeaders(
        (int32_t)ctx->rdi, (char **)ctx->rsi, (uint64_t *)ctx->rdx);
}

void shim_sceHttpAddRequestHeader(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpAddRequestHeader(
        (int32_t)ctx->rdi, (const char *)ctx->rsi, (const char *)ctx->rdx, (int32_t)ctx->rcx);
}

void shim_sceHttpSetNonblock(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetNonblock((int32_t)ctx->rdi, (int32_t)ctx->rsi);
}

void shim_sceHttpGetLastErrno(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpGetLastErrno((int32_t)ctx->rdi, (int32_t *)ctx->rsi);
}

void shim_sceHttpUriParse(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpUriParse(
        (OrbisHttpUriElement *)ctx->rdi, (const char *)ctx->rsi, (void *)ctx->rdx, (uint64_t *)ctx->rcx, (uint64_t)ctx->r8);
}

void shim_sceHttpUriBuild(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpUriBuild(
        (char *)ctx->rdi, (uint64_t *)ctx->rsi, (uint64_t)ctx->rdx, (const OrbisHttpUriElement *)ctx->rcx, (uint32_t)ctx->r8);
}

void shim_sceHttpCreateEpoll(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpCreateEpoll((int32_t)ctx->rdi, (OrbisHttpEpollHandle *)ctx->rsi);
}

void shim_sceHttpSetEpoll(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpSetEpoll((int32_t)ctx->rdi, (OrbisHttpEpollHandle)ctx->rsi, (void *)ctx->rdx);
}

void shim_sceHttpDestroyEpoll(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttpDestroyEpoll((int32_t)ctx->rdi, (OrbisHttpEpollHandle)ctx->rsi);
}

void shim_sceHttp2Init(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttp2Init((int32_t)ctx->rdi, (int32_t)ctx->rsi, (uint64_t)ctx->rdx, (int32_t)ctx->rcx);
}

void shim_sceHttp2Term(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceHttp2Term((int32_t)ctx->rdi);
}
