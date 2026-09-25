#ifndef PS4_HTTP_H
#define PS4_HTTP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
#define ORBIS_HTTP_ERROR_NETWORK   0x80431063
#define ORBIS_HTTP_ERROR_TIMEOUT   0x80431068
#define ORBIS_HTTP_ERROR_INVALID_ID 0x80431100

typedef void* OrbisHttpEpollHandle;

typedef struct OrbisHttpNBEvent {
    uint32_t events;
    uint32_t eventDetail;
    int32_t id;
    void *userArg;
} OrbisHttpNBEvent;

typedef struct OrbisHttpUriElement {
    bool opaque;
    char *scheme;
    char *username;
    char *password;
    char *hostname;
    char *path;
    char *query;
    char *fragment;
    uint16_t port;
    uint8_t reserved[10];
} OrbisHttpUriElement;

// libSceHttp C API
int32_t sceHttpInit(int32_t libnetMemId, int32_t libsslCtxId, uint64_t poolSize);
int32_t sceHttpTerm(int32_t libhttpCtxId);
int32_t sceHttpCreateTemplate(int32_t libhttpCtxId, const char *userAgent, int32_t httpVer, int32_t isAutoProxyConf);
int32_t sceHttpDeleteTemplate(int32_t tmplId);
int32_t sceHttpCreateConnectionWithURL(int32_t tmplId, const char *url, bool enableKeepalive);
int32_t sceHttpDeleteConnection(int32_t connId);
int32_t sceHttpCreateRequestWithURL(int32_t connId, int32_t method, const char *url, uint64_t contentLength);
int32_t sceHttpCreateRequestWithURL2(int32_t connId, const char *method, const char *url, uint64_t contentLength);
int32_t sceHttpDeleteRequest(int32_t reqId);
int32_t sceHttpSendRequest(int32_t reqId, const void *postData, uint64_t size);
int32_t sceHttpAbortRequest(int32_t reqId);
int32_t sceHttpWaitRequest(OrbisHttpEpollHandle eh, OrbisHttpNBEvent *nbev, int32_t maxevents, int32_t timeout);
int32_t sceHttpReadData(int32_t reqId, void *data, uint64_t size);
int32_t sceHttpGetStatusCode(int32_t reqId, int32_t *statusCode);
int32_t sceHttpGetResponseContentLength(int32_t reqId, int32_t *result, uint64_t *contentLength);
int32_t sceHttpGetAllResponseHeaders(int32_t reqId, char **header, uint64_t *headerSize);
int32_t sceHttpAddRequestHeader(int32_t id, const char *name, const char *value, int32_t mode);
int32_t sceHttpSetNonblock(int32_t id, int32_t isEnable);
int32_t sceHttpGetLastErrno(int32_t reqId, int32_t *errNum);
int32_t sceHttpUriParse(OrbisHttpUriElement *out, const char *srcUri, void *pool, uint64_t *require, uint64_t prepare);
int32_t sceHttpUriBuild(char *out, uint64_t *require, uint64_t prepare, const OrbisHttpUriElement *srcElement, uint32_t option);
int32_t sceHttpCreateEpoll(int32_t libhttpCtxId, OrbisHttpEpollHandle *eh);
int32_t sceHttpSetEpoll(int32_t id, OrbisHttpEpollHandle eh, void *userArg);
int32_t sceHttpDestroyEpoll(int32_t libhttpCtxId, OrbisHttpEpollHandle eh);

// libSceHttp2 C API
int32_t sceHttp2Init(int32_t net_id, int32_t ssl_id, uint64_t pool_size, int32_t max_requests);
int32_t sceHttp2Term(int32_t ctx_id);

// Shims
void shim_sceHttpInit(GuestContext *ctx);
void shim_sceHttpTerm(GuestContext *ctx);
void shim_sceHttpCreateTemplate(GuestContext *ctx);
void shim_sceHttpDeleteTemplate(GuestContext *ctx);
void shim_sceHttpCreateConnectionWithURL(GuestContext *ctx);
void shim_sceHttpDeleteConnection(GuestContext *ctx);
void shim_sceHttpCreateRequestWithURL(GuestContext *ctx);
void shim_sceHttpCreateRequestWithURL2(GuestContext *ctx);
void shim_sceHttpDeleteRequest(GuestContext *ctx);
void shim_sceHttpSendRequest(GuestContext *ctx);
void shim_sceHttpAbortRequest(GuestContext *ctx);
void shim_sceHttpWaitRequest(GuestContext *ctx);
void shim_sceHttpReadData(GuestContext *ctx);
void shim_sceHttpGetStatusCode(GuestContext *ctx);
void shim_sceHttpGetResponseContentLength(GuestContext *ctx);
void shim_sceHttpGetAllResponseHeaders(GuestContext *ctx);
void shim_sceHttpAddRequestHeader(GuestContext *ctx);
void shim_sceHttpSetNonblock(GuestContext *ctx);
void shim_sceHttpGetLastErrno(GuestContext *ctx);
void shim_sceHttpUriParse(GuestContext *ctx);
void shim_sceHttpUriBuild(GuestContext *ctx);
void shim_sceHttpCreateEpoll(GuestContext *ctx);
void shim_sceHttpSetEpoll(GuestContext *ctx);
void shim_sceHttpDestroyEpoll(GuestContext *ctx);
void shim_sceHttp2Init(GuestContext *ctx);
void shim_sceHttp2Term(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_HTTP_H
