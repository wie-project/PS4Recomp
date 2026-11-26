// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_HTTP_H
#define PS4_HTTP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// libSceHttp error codes (shadPS4 network/http_error.h).
#define ORBIS_HTTP_ERROR_BEFORE_INIT ((int32_t)0x80431001)
#define ORBIS_HTTP_ERROR_ALREADY_INITED ((int32_t)0x80431020)
#define ORBIS_HTTP_ERROR_BUSY ((int32_t)0x80431021)
#define ORBIS_HTTP_ERROR_OUT_OF_MEMORY ((int32_t)0x80431022)
#define ORBIS_HTTP_ERROR_NOT_FOUND ((int32_t)0x80431025)
#define ORBIS_HTTP_ERROR_INVALID_ID ((int32_t)0x80431100)
#define ORBIS_HTTP_ERROR_INVALID_VALUE ((int32_t)0x804311fe)
#define ORBIS_HTTP_ERROR_INVALID_URL ((int32_t)0x80433060)
#define ORBIS_HTTP_ERROR_UNKNOWN_SCHEME ((int32_t)0x80431061)
#define ORBIS_HTTP_ERROR_NETWORK ((int32_t)0x80431063)
#define ORBIS_HTTP_ERROR_BEFORE_SEND ((int32_t)0x80431065)
#define ORBIS_HTTP_ERROR_AFTER_SEND ((int32_t)0x80431066)
#define ORBIS_HTTP_ERROR_TIMEOUT ((int32_t)0x80431068)
#define ORBIS_HTTP_ERROR_UNKNOWN_METHOD ((int32_t)0x8043106b)
#define ORBIS_HTTP_ERROR_NO_CONTENT_LENGTH ((int32_t)0x80431071)
#define ORBIS_HTTP_ERROR_SSL ((int32_t)0x80431075)
#define ORBIS_HTTP_ERROR_ABORTED ((int32_t)0x80431080)
#define ORBIS_HTTP_ERROR_UNKNOWN ((int32_t)0x80431081)
#define ORBIS_HTTP_ERROR_EAGAIN ((int32_t)0x80431082)
#define ORBIS_HTTP_ERROR_PROXY ((int32_t)0x80431084)
#define ORBIS_HTTP_ERROR_BROKEN ((int32_t)0x80431085)
#define ORBIS_HTTP_ERROR_PARSE_HTTP_NOT_FOUND ((int32_t)0x80432025)
#define ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE ((int32_t)0x80432060)
#define ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE ((int32_t)0x804321fe)
#define ORBIS_HTTP_ERROR_RESOLVER_ENODNS ((int32_t)0x80436002)
#define ORBIS_HTTP_ERROR_RESOLVER_ENOHOST ((int32_t)0x80436007)
#define ORBIS_KERNEL_ERROR_EINVAL_HTTP ((int32_t)0x80020016) // ORBIS_KERNEL_ERROR_EINVAL

// Non-blocking event bits (OrbisHttpNBEvents).
#define ORBIS_HTTP_NB_EVENT_IN 0x00000001u
#define ORBIS_HTTP_NB_EVENT_OUT 0x00000002u
#define ORBIS_HTTP_NB_EVENT_SOCK_ERR 0x00000008u
#define ORBIS_HTTP_NB_EVENT_HUP 0x00000010u
#define ORBIS_HTTP_NB_EVENT_RESOLVED 0x00010000u
#define ORBIS_HTTP_NB_EVENT_RESOLVER_ERR 0x00020000u

// HTTPS flags (OrbisHttpsFlags) and the masks sceHttpsEnable/DisableOption[Private] accept.
#define ORBIS_HTTPS_FLAG_SERVER_VERIFY 0x01u
#define ORBIS_HTTPS_FLAG_CN_CHECK 0x04u
#define ORBIS_HTTPS_FLAG_KNOWN_CA_CHECK 0x20u
#define ORBIS_HTTPS_FLAG_SNI 0x80u
#define ORBIS_HTTPS_FLAG_SDK_DEFAULT \
    (ORBIS_HTTPS_FLAG_SERVER_VERIFY | ORBIS_HTTPS_FLAG_CN_CHECK | ORBIS_HTTPS_FLAG_KNOWN_CA_CHECK | ORBIS_HTTPS_FLAG_SNI)
#define ORBIS_HTTPS_FLAG_PUBLIC_VALID 0x000020ffu
#define ORBIS_HTTPS_FLAG_PRIVATE_VALID 0x00002dffu

// OrbisHttpMethod.
#define ORBIS_HTTP_METHOD_GET 0
#define ORBIS_HTTP_METHOD_POST 1
#define ORBIS_HTTP_METHOD_HEAD 2
#define ORBIS_HTTP_METHOD_OPTIONS 3
#define ORBIS_HTTP_METHOD_PUT 4
#define ORBIS_HTTP_METHOD_DELETE 5
#define ORBIS_HTTP_METHOD_TRACE 6
#define ORBIS_HTTP_METHOD_CONNECT 7
#define ORBIS_HTTP_METHOD_CUSTOM 8

// sceHttpAddRequestHeader modes.
#define ORBIS_HTTP_HEADER_OVERWRITE 0
#define ORBIS_HTTP_HEADER_ADD 1

// sceHttpUriBuild options.
#define ORBIS_HTTP_URI_BUILD_WITH_SCHEME 0x01u
#define ORBIS_HTTP_URI_BUILD_WITH_HOSTNAME 0x02u
#define ORBIS_HTTP_URI_BUILD_WITH_PORT 0x04u
#define ORBIS_HTTP_URI_BUILD_WITH_PATH 0x08u
#define ORBIS_HTTP_URI_BUILD_WITH_USERNAME 0x10u
#define ORBIS_HTTP_URI_BUILD_WITH_PASSWORD 0x20u
#define ORBIS_HTTP_URI_BUILD_WITH_QUERY 0x40u
#define ORBIS_HTTP_URI_BUILD_WITH_FRAGMENT 0x80u

// OrbisHttpsCaList (shadPS4: Ssl::OrbisSslCaList).
typedef struct OrbisHttpsCaList {
    uint64_t certs; // guest address
    int32_t certsNum;
} OrbisHttpsCaList;

// One event of sceHttpWaitRequest. userArg is the guest value bound by sceHttpSetEpoll.
typedef struct OrbisHttpNBEvent {
    uint32_t events;
    uint32_t eventDetail;
    int32_t id;
    uint64_t userArg;
} OrbisHttpNBEvent;

// A parsed URI with host pointers into the parse pool (the guest layout, with guest addresses in
// the pointer fields, is OrbisHttpUriElementGuest in ps4_http.c).
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

// ---------------------------------------------------------------------------------------------
// libSceHttp after shadPS4 network/http.cpp. Templates, connections, requests and epolls share
// one id counter; library contexts (sceHttpInit) have their own.

int32_t sceHttpInit(int32_t libnetMemId, int32_t libsslCtxId, uint64_t poolSize);
int32_t sceHttpTerm(int32_t libhttpCtxId);
int32_t sceHttpCreateTemplate(int32_t libhttpCtxId, const char *userAgent, int32_t httpVer, int32_t isAutoProxyConf);
int32_t sceHttpDeleteTemplate(int32_t tmplId);
int32_t sceHttpCreateConnection(int32_t tmplId, const char *serverName, const char *scheme, uint16_t port,
                                int32_t isEnableKeepalive);
int32_t sceHttpCreateConnectionWithURL(int32_t tmplId, const char *url, bool enableKeepalive);
int32_t sceHttpDeleteConnection(int32_t connId);
int32_t sceHttpCreateRequest(int32_t connId, int32_t method, const char *path, uint64_t contentLength);
int32_t sceHttpCreateRequest2(int32_t connId, const char *method, const char *path, uint64_t contentLength);
int32_t sceHttpCreateRequestWithURL(int32_t connId, int32_t method, const char *url, uint64_t contentLength);
int32_t sceHttpCreateRequestWithURL2(int32_t connId, const char *method, const char *url, uint64_t contentLength);
int32_t sceHttpDeleteRequest(int32_t reqId);
int32_t sceHttpSendRequest(int32_t reqId, const void *postData, uint64_t size);
int32_t sceHttpAbortRequest(int32_t reqId);
int32_t sceHttpReadData(int32_t reqId, void *data, uint64_t size);
int32_t sceHttpGetStatusCode(int32_t reqId, int32_t *statusCode);
int32_t sceHttpGetResponseContentLength(int32_t reqId, int32_t *result, uint64_t *contentLength);
// The headers blob lives in guest memory (allocated through ctx on the first call for a
// request, released with the request): *header receives its guest address.
int32_t sceHttpGetAllResponseHeaders(GuestContext *ctx, int32_t reqId, uint64_t *header, uint64_t *headerSize);
int32_t sceHttpGetLastErrno(int32_t reqId, int32_t *errNum);
int32_t sceHttpAddRequestHeader(int32_t id, const char *name, const char *value, int32_t mode);
int32_t sceHttpRemoveRequestHeader(int32_t id, const char *name);
int32_t sceHttpSetRequestContentLength(int32_t id, uint64_t contentLength);

// Settings of a template, connection or request (inherited down the chain when created).
int32_t sceHttpSetConnectTimeOut(int32_t id, uint32_t usec);
int32_t sceHttpSetSendTimeOut(int32_t id, uint32_t usec);
int32_t sceHttpSetRecvTimeOut(int32_t id, uint32_t usec);
// sdkVersion: the title's compiled SDK version (sceKernelGetCompiledSdkVersion).
int32_t sceHttpSetResolveTimeOut(int32_t id, uint32_t usec, uint32_t sdkVersion);
int32_t sceHttpSetResolveRetry(int32_t id, int32_t retry);
int32_t sceHttpSetAutoRedirect(int32_t id, int32_t isEnable);
int32_t sceHttpGetAutoRedirect(int32_t id, int32_t *isEnable);
int32_t sceHttpSetNonblock(int32_t id, int32_t isEnable);
int32_t sceHttpGetNonblock(int32_t id, int32_t *isEnable);
int32_t sceHttpSetInflateGZIPEnabled(int32_t id, int32_t isEnable);
int32_t sceHttpSetAcceptEncodingGZIPEnabled(int32_t id, int32_t isEnable);
int32_t sceHttpGetAcceptEncodingGZIPEnabled(int32_t id, int32_t *isEnable);
int32_t sceHttpSetDefaultAcceptEncodingGZIPEnabled(int32_t libhttpCtxId, int32_t isEnable);
int32_t sceHttpCacheRedirectedConnectionEnabled(int32_t id, int32_t isEnable);
int32_t sceHttpRedirectCacheFlush(int32_t libhttpCtxId);
int32_t sceHttpSetRecvBlockSize(int32_t id, uint32_t blockSize);
int32_t sceHttpSetResponseHeaderMaxSize(int32_t id, uint64_t headerSize);
int32_t sceHttpSetProxy(int32_t id, int32_t httpProxyConf, int32_t wlanProxyConf, const char *host, uint16_t port);
int32_t sceHttpsEnableOption(int32_t id, uint32_t sslFlags);
int32_t sceHttpsDisableOption(int32_t id, uint32_t sslFlags);
int32_t sceHttpsEnableOptionPrivate(int32_t id, uint32_t sslFlags);
int32_t sceHttpsDisableOptionPrivate(int32_t id, uint32_t sslFlags);
int32_t sceHttpsLoadCert(int32_t libhttpCtxId, int32_t caCertNum, uint64_t caList, uint64_t cert, uint64_t privKey);
int32_t sceHttpsUnloadCert(int32_t libhttpCtxId);
int32_t sceHttpsGetCaList(int32_t libhttpCtxId, OrbisHttpsCaList *list);
int32_t sceHttpsFreeCaList(int32_t libhttpCtxId, OrbisHttpsCaList *list);

// Non-blocking processing. An epoll handle is the epoll's id.
int32_t sceHttpCreateEpoll(int32_t libhttpCtxId, uint64_t *eh);
int32_t sceHttpDestroyEpoll(int32_t libhttpCtxId, uint64_t eh);
int32_t sceHttpSetEpoll(int32_t id, uint64_t eh, uint64_t userArg);
int32_t sceHttpGetEpoll(int32_t id, uint64_t *eh, uint64_t *userArg);
int32_t sceHttpUnsetEpoll(int32_t id);
int32_t sceHttpAbortWaitRequest(uint64_t eh);
// timeout in microseconds; 0 polls, a negative value waits without limit.
int32_t sceHttpWaitRequest(uint64_t eh, OrbisHttpNBEvent *nbev, int32_t maxevents, int32_t timeout);

// Pure parsers and URI helpers (ps4_http_parse.c).
int32_t sceHttpParseStatusLine(const char *statusLine, uint64_t lineLen, int32_t *httpMajorVer, int32_t *httpMinorVer,
                               int32_t *responseCode, const char **reasonPhrase, uint64_t *phraseLen);
int32_t sceHttpParseResponseHeader(const char *header, uint64_t headerLen, const char *fieldStr, const char **fieldValue,
                                   uint64_t *valueLen);
int32_t sceHttpUriParse(OrbisHttpUriElement *out, const char *srcUri, void *pool, uint64_t *require, uint64_t prepare);
int32_t sceHttpUriBuild(char *out, uint64_t *require, uint64_t prepare, const OrbisHttpUriElement *srcElement,
                        uint32_t option);
int32_t sceHttpUriEscape(char *out, uint64_t *require, uint64_t prepare, const char *in);
int32_t sceHttpUriUnescape(char *out, uint64_t *require, uint64_t prepare, const char *in);
int32_t sceHttpUriMerge(char *mergedUrl, const char *url, const char *relativeUri, uint64_t *require, uint64_t prepare,
                        uint32_t option);
int32_t sceHttpUriSweepPath(char *dst, const char *src, uint64_t srcSize);

// ---------------------------------------------------------------------------------------------
// Internals shared by the libSceHttp sources.

// One request as the transport sees it (built under the library lock by sceHttpSendRequest).
typedef struct HttpHeader {
    char *name;
    char *value;
} HttpHeader;

typedef struct HttpHeaderList {
    HttpHeader *items;
    size_t count;
    size_t cap;
} HttpHeaderList;

typedef struct HttpSettings {
    uint32_t connect_timeout_us;
    uint32_t send_timeout_us;
    uint32_t recv_timeout_us;
    uint32_t resolve_timeout_us;
    int32_t resolve_retry;
    uint64_t recv_block_size;
    uint64_t response_header_max;
    bool auto_redirect;
    bool inflate_gzip;
    bool accept_encoding_gzip;
    bool cache_redirected_connection;
    uint32_t ssl_flags;
    bool nonblock;
    char proxy_host[256];
    uint16_t proxy_port;
    int32_t proxy_http_conf;
    int32_t proxy_wlan_conf;
} HttpSettings;

typedef struct HttpSendPlan {
    char scheme[8]; // "http" or "https"
    char *host;
    uint16_t port;
    char *path; // path and query
    int32_t method;
    char *method_str; // the name for ORBIS_HTTP_METHOD_CUSTOM
    char *content_type;
    uint8_t *body;
    size_t body_size;
    HttpHeaderList headers; // template, connection, request headers in that order
    HttpSettings settings;
    bool ctx_has_loaded_certs;
    int32_t ctx_id;
} HttpSendPlan;

typedef struct HttpResponse {
    int32_t status_code;
    uint64_t content_length;
    int32_t content_length_result;
    uint8_t *body;
    size_t body_size;
    uint64_t read_cursor;
    char *all_headers; // "HTTP/1.1 <code> <reason>\r\nName: Value\r\n...\r\n", NULL when empty
    size_t all_headers_size;
} HttpResponse;

bool ps4_http_headers_add(HttpHeaderList *list, const char *name, const char *value);
void ps4_http_headers_free(HttpHeaderList *list);
bool ps4_http_header_name_matches(const char *a, const char *b);
void ps4_http_response_clear(HttpResponse *res);
// The status line's reason for a code ("OK" for 200), or NULL for a code without one.
const char *ps4_http_status_reason(int code);

// Performs the request on the network (ps4_http_transport.c), following redirects as the
// console does. Fills res and *event_bits on success and returns 0, or returns the transport
// error with res cleared.
int32_t ps4_http_run_request(HttpSendPlan *plan, HttpResponse *res, uint32_t *event_bits);

// The 301 redirect cache of a library context (ps4_http.c, under the library lock).
bool ps4_http_redirect_cache_get(int32_t ctx_id, const char *key, char *scheme, size_t scheme_size, char **host,
                                 uint16_t *port, char **path);
void ps4_http_redirect_cache_put(int32_t ctx_id, const char *key, const char *scheme, const char *host, uint16_t port,
                                 const char *path);

// Host overrides (ps4_http_overrides.c): rewrites scheme, host and port when the override file
// names the endpoint. *host may be replaced by a new heap string (the old one is freed).
bool ps4_http_apply_host_override(char *scheme, size_t scheme_size, char **host, uint16_t *port, bool *is_secure);
// The running title's id (param.sfo TITLE_ID), "" when unknown (ps4_http_title.c).
void ps4_http_title_id(char *out, size_t out_size);

// The libhttp User-Agent for an application string (may be empty): a heap string.
char *ps4_http_build_user_agent(const char *app_user_agent);

// ---------------------------------------------------------------------------------------------
// libSceHttp2 after shadPS4 network/http2.cpp (ps4_http2.c).
int32_t sceHttp2Init(int32_t netId, int32_t sslId, uint64_t poolSize, int32_t maxRequests);
int32_t sceHttp2Term(int32_t ctxId);
int32_t sceHttp2CreateTemplate(int32_t ctxId, const char *userAgent, int32_t httpVer, int32_t autoProxyConf);
int32_t sceHttp2DeleteTemplate(int32_t tmplId);
int32_t sceHttp2CreateRequestWithURL(int32_t tmplId, const char *method, const char *url, uint64_t contentLength);
int32_t sceHttp2DeleteRequest(int32_t reqId);
int32_t sceHttp2AbortRequest(int32_t reqId);
int32_t sceHttp2AddRequestHeader(int32_t id, const char *name, const char *value, uint32_t mode);
int32_t sceHttp2GetAllResponseHeaders(GuestContext *ctx, int32_t reqId, uint64_t *header, uint64_t *headerSize);
int32_t sceHttp2GetResponseContentLength(int32_t reqId, int32_t *result, uint64_t *contentLength);
int32_t sceHttp2GetStatusCode(int32_t reqId, int32_t *statusCode);
int32_t sceHttp2ReadData(int32_t reqId, void *data, uint64_t size);
int32_t sceHttp2SendRequest(int32_t reqId, const void *data, uint64_t size);
int32_t sceHttp2SetRequestContentLength(int32_t reqId, uint64_t contentLength);
// The connection sceHttp2CreateRequestWithURL created for a request, 0 when none is tracked.
int32_t ps4_http2_connection_of(int32_t reqId);

#ifdef __cplusplus
}
#endif

#endif // PS4_HTTP_H
