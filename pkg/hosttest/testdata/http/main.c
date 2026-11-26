// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHttp and libSceHttp2 against shadPS4's network/http.cpp and http2.cpp,
// on host memory.
//
//  objects    ids, errors before init and for unknown ids, setting inheritance
//  from template to
//             connection to request, request headers, HTTPS option masks,
//             sceHttpTerm.
//  offline    the host disconnected (no PS4_NETWORK): a sent request fails as a
//  name resolution
//             failure, reported to its epoll; AbortWaitRequest, UnsetEpoll.
//  online     PS4_NETWORK=1 against a local HTTP server: status, length,
//  headers (in guest
//             memory, released with the request), data, the libhttp User-Agent
//             and the header order, gzip inflation, redirects (301 cache, 303
//             to GET, no POST on 302).
//  nonblock   a non-blocking request answers EAGAIN while in flight, its epoll
//  reports
//             completion; an aborted request.
//  http2      libSceHttp2 over libSceHttp: the connection
//  sceHttp2CreateRequestWithURL opens is
//             tracked per request and deleted with it, also on a failed request
//             creation.
//  parse      URI parse/build/escape/unescape/merge/sweep, status line and
//  header parsers,
//             through the shims with guest addresses.
//  overrides  a host override file sends a connection to the local server.

#include "ps4_http.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#include "../host_guest_mem.h"

// Runtime symbols the sources link against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
static GuestContext g_thread_ctx;
GuestContext *recomp_get_current_ctx(void) { return &g_thread_ctx; }

static int g_allocs, g_frees;

uint64_t recomp_vm_alloc(GuestContext *ctx, size_t size) {
  (void)ctx;
  void *p = calloc(1, size);
  if (!p)
    return (uint64_t)-1;
  g_allocs++;
  return A(p);
}

int recomp_vm_free(GuestContext *ctx, uint64_t addr, size_t size) {
  (void)ctx;
  (void)size;
  g_frees++;
  free(host_ptr(addr));
  return 0;
}

uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
  (void)ctx;
  return 0x4500000; // SDK 4.50
}

void ps4_http_title_id(char *out, size_t out_size) {
  snprintf(out, out_size, "%s", ""); // no param.sfo here
}

void shim_sceHttpUriParse(GuestContext *ctx);
void shim_sceHttpUriBuild(GuestContext *ctx);
void shim_sceHttpParseStatusLine(GuestContext *ctx);
void shim_sceHttpParseResponseHeader(GuestContext *ctx);
void shim_sceHttpGetAllResponseHeaders(GuestContext *ctx);
void shim_sceHttpCookieFlush(GuestContext *ctx);
void shim_sceHttp2SetTimeOut(GuestContext *ctx);

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c);    \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static uint8_t g_stack[0x1000] __attribute__((aligned(16)));

static GuestContext guest_ctx(void) {
  GuestContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.mem_base = GUEST_MEM;
  ctx.mem_size = ~0ull;
  ctx.rsp = A(g_stack + sizeof(g_stack) / 2);
  return ctx;
}

// ---------------------------------------------------------------------------------------------
// Local HTTP server: one connection at a time, "Connection: close".

static int g_listen_fd = -1;
static uint16_t g_port;
static int g_old_hits;
static uint8_t g_gz[256];
static size_t g_gz_size;
static const char g_gz_plain[] = "compressed payload, inflated by libSceHttp";

static void send_response(int fd, const char *status, const char *extra,
                          const void *body, size_t len) {
  char head[1024];
  const int n = snprintf(
      head, sizeof(head),
      "HTTP/1.1 %s\r\nContent-Length: %zu\r\nConnection: close\r\n%s\r\n",
      status, len, extra ? extra : "");
  (void)!write(fd, head, (size_t)n);
  if (len)
    (void)!write(fd, body, len);
}

static void handle(int fd) {
  char req[16384];
  size_t got = 0;
  char *end = NULL;
  while (got < sizeof(req) - 1) {
    const ssize_t r = read(fd, req + got, sizeof(req) - 1 - got);
    if (r <= 0)
      return;
    got += (size_t)r;
    req[got] = 0;
    if ((end = strstr(req, "\r\n\r\n")))
      break;
  }
  if (!end)
    return;
  const size_t head_len = (size_t)(end - req);
  size_t content_length = 0;
  const char *cl = strcasestr(req, "\r\nContent-Length:");
  if (cl && cl < end)
    content_length = strtoul(cl + 17, NULL, 10);
  size_t body_have = got - head_len - 4;
  while (body_have < content_length && got < sizeof(req) - 1) {
    const ssize_t r = read(fd, req + got, sizeof(req) - 1 - got);
    if (r <= 0)
      break;
    got += (size_t)r;
    body_have += (size_t)r;
  }
  char method[16] = {0}, path[256] = {0};
  sscanf(req, "%15s %255s", method, path);

  if (!strcmp(path, "/hello")) {
    send_response(fd, "200 OK", "X-Test: 1\r\n", "hello world", 11);
  } else if (!strcmp(path, "/gz")) {
    send_response(fd, "200 OK", "Content-Encoding: gzip\r\n", g_gz, g_gz_size);
  } else if (!strcmp(path, "/old")) {
    g_old_hits++;
    send_response(fd, "301 Moved Permanently", "Location: /hello\r\n", NULL, 0);
  } else if (!strcmp(path, "/post303")) {
    send_response(fd, "303 See Other", "Location: /echo\r\n", NULL, 0);
  } else if (!strcmp(path, "/post302")) {
    send_response(fd, "302 Found", "Location: /hello\r\n", NULL, 0);
  } else if (!strcmp(path, "/echo")) {
    char body[64];
    const int n =
        snprintf(body, sizeof(body), "%s %zu", method, content_length);
    send_response(fd, "200 OK", NULL, body, (size_t)n);
  } else if (!strcmp(path, "/echo-headers")) {
    send_response(fd, "200 OK", NULL, req, head_len);
  } else if (!strcmp(path, "/slow")) {
    usleep(300000);
    send_response(fd, "200 OK", NULL, "slow", 4);
  } else {
    send_response(fd, "404 Not Found", NULL, NULL, 0);
  }
}

static void *server_main(void *arg) {
  (void)arg;
  for (;;) {
    const int fd = accept(g_listen_fd, NULL, NULL);
    if (fd < 0)
      return NULL;
    handle(fd);
    close(fd);
  }
}

static int start_server(void) {
  z_stream z;
  memset(&z, 0, sizeof(z));
  if (deflateInit2(&z, 9, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    return 1;
  z.next_in = (Bytef *)(uintptr_t)g_gz_plain;
  z.avail_in = (uInt)strlen(g_gz_plain);
  z.next_out = g_gz;
  z.avail_out = sizeof(g_gz);
  if (deflate(&z, Z_FINISH) != Z_STREAM_END)
    return 1;
  g_gz_size = sizeof(g_gz) - z.avail_out;
  deflateEnd(&z);

  g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t len = sizeof(sa);
  if (g_listen_fd < 0 ||
      bind(g_listen_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
      listen(g_listen_fd, 8) != 0 ||
      getsockname(g_listen_fd, (struct sockaddr *)&sa, &len) != 0)
    return 1;
  g_port = ntohs(sa.sin_port);
  pthread_t th;
  return pthread_create(&th, NULL, server_main, NULL) != 0;
}

// ---------------------------------------------------------------------------------------------

static int32_t g_ctx, g_tmpl, g_conn;

static int setup_online(void) {
  setenv("PS4_NETWORK", "1", 1);
  if (start_server() != 0)
    return 1;
  g_ctx = sceHttpInit(1, 1, 64 * 1024);
  g_tmpl = sceHttpCreateTemplate(g_ctx, "Game/1.0", 2, 0);
  g_conn = sceHttpCreateConnection(g_tmpl, "127.0.0.1", "http", g_port, 1);
  return g_ctx > 0 && g_tmpl > 0 && g_conn > 0 ? 0 : 1;
}

// Reads the whole body of a sent request in small chunks.
static int32_t read_all(int32_t req, char *buf, size_t cap) {
  size_t n = 0;
  for (;;) {
    const int32_t r =
        sceHttpReadData(req, buf + n, cap - 1 - n < 5 ? cap - 1 - n : 5);
    if (r < 0)
      return r;
    if (r == 0)
      break;
    n += (size_t)r;
  }
  buf[n] = 0;
  return (int32_t)n;
}

static int test_objects(void) {
  CHECK(sceHttpCreateTemplate(1, "x", 2, 0) == ORBIS_HTTP_ERROR_BEFORE_INIT);
  CHECK(sceHttpTerm(1) == ORBIS_HTTP_ERROR_BEFORE_INIT);
  CHECK(sceHttpSetConnectTimeOut(1, 1) == ORBIS_HTTP_ERROR_BEFORE_INIT);
  CHECK(sceHttpInit(1, 1, 0) == ORBIS_KERNEL_ERROR_EINVAL_HTTP);
  const int32_t ctx = sceHttpInit(1, 1, 64 * 1024);
  const int32_t ctx2 = sceHttpInit(1, 1, 64 * 1024);
  CHECK(ctx > 0 && ctx2 == ctx + 1);

  CHECK(sceHttpCreateTemplate(ctx + 100, "x", 2, 0) ==
        ORBIS_HTTP_ERROR_INVALID_ID);
  const int32_t tmpl = sceHttpCreateTemplate(ctx, "Game/1.0", 2, 0);
  CHECK(tmpl > 0);
  CHECK(sceHttpCreateConnection(tmpl, NULL, "http", 80, 1) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpCreateConnection(tmpl, "example.com", "ftp", 21, 1) ==
        ORBIS_HTTP_ERROR_UNKNOWN_SCHEME);
  CHECK(sceHttpCreateConnection(tmpl + 100, "example.com", "http", 80, 1) ==
        ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpCreateConnectionWithURL(tmpl, "ftp://example.com/", true) ==
        ORBIS_HTTP_ERROR_UNKNOWN_SCHEME);
  CHECK(sceHttpCreateConnectionWithURL(tmpl, "http:///path", true) ==
        ORBIS_HTTP_ERROR_INVALID_URL);
  CHECK(sceHttpCreateConnectionWithURL(tmpl, NULL, true) ==
        ORBIS_HTTP_ERROR_INVALID_URL);

  // Settings set on the template before the connection exists are inherited by
  // it and by its requests; settings changed on the connection later do not
  // reach the template.
  CHECK(sceHttpSetNonblock(tmpl, 1) == 0);
  CHECK(sceHttpSetAutoRedirect(tmpl, 0) == 0);
  const int32_t conn =
      sceHttpCreateConnectionWithURL(tmpl, "HTTPS://example.com:8443/a", true);
  CHECK(conn > tmpl);
  CHECK(sceHttpSetInflateGZIPEnabled(conn, 2) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpSetInflateGZIPEnabled(conn, 0) == 0);
  CHECK(sceHttpCreateRequestWithURL(conn, ORBIS_HTTP_METHOD_OPTIONS,
                                    "https://example.com/",
                                    0) == ORBIS_HTTP_ERROR_UNKNOWN_METHOD);
  CHECK(sceHttpCreateRequestWithURL(conn, ORBIS_HTTP_METHOD_CONNECT,
                                    "https://example.com/",
                                    0) == ORBIS_HTTP_ERROR_UNKNOWN_METHOD);
  CHECK(sceHttpCreateRequest(conn, ORBIS_HTTP_METHOD_GET, "a\r\nb", 0) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpCreateRequest2(conn, NULL, "/x", 0) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  const int32_t req = sceHttpCreateRequest2(conn, "PATCH", "x", 0);
  CHECK(req > conn);
  int32_t v = -1;
  CHECK(sceHttpGetNonblock(req, &v) == 0 && v == 1);
  CHECK(sceHttpGetAutoRedirect(req, &v) == 0 && v == 0);
  CHECK(sceHttpGetAutoRedirect(req, NULL) == ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpSetAutoRedirect(conn, 1) == 0);
  CHECK(sceHttpGetAutoRedirect(tmpl, &v) == 0 && v == 0);
  CHECK(sceHttpGetAcceptEncodingGZIPEnabled(req, &v) == 0 && v == 0);
  CHECK(sceHttpSetDefaultAcceptEncodingGZIPEnabled(ctx, 1) == 0);
  const int32_t tmpl_gz = sceHttpCreateTemplate(ctx, NULL, 2, 0);
  CHECK(sceHttpGetAcceptEncodingGZIPEnabled(tmpl_gz, &v) == 0 && v == 1);

  // Settings belong to templates, connections and requests.
  CHECK(sceHttpSetConnectTimeOut(9999, 1) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpSetRecvTimeOut(req, 5000000) == 0);
  CHECK(sceHttpSetResolveRetry(req, -1) == ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpSetResolveRetry(req, 2) == 0);
  CHECK(sceHttpSetResolveTimeOut(req, 500000, 0x1700000) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpSetResolveTimeOut(req, 500000, 0x1500000) == 0);
  CHECK(sceHttpSetResolveTimeOut(req, 1000000, 0x1700000) == 0);
  CHECK(sceHttpSetProxy(req, 1, 0, NULL, 8080) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpSetProxy(req, 1, 0, "proxy", 8080) == 0);
  CHECK(sceHttpsEnableOption(req, 0x100) == ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpsEnableOptionPrivate(req, 0x100) == 0);
  CHECK(sceHttpsDisableOption(req, ORBIS_HTTPS_FLAG_SERVER_VERIFY) == 0);
  CHECK(sceHttpCacheRedirectedConnectionEnabled(req, 0) == 0);
  CHECK(sceHttpRedirectCacheFlush(req) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpRedirectCacheFlush(ctx) == 0);

  // Headers: overwrite replaces every header of the name, add appends, remove
  // drops them all.
  CHECK(sceHttpAddRequestHeader(req, "X-A", "1", 2) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpAddRequestHeader(req, NULL, "1", 0) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpAddRequestHeader(9999, "X-A", "1", 0) ==
        ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpAddRequestHeader(req, "X-A", "1", ORBIS_HTTP_HEADER_ADD) == 0);
  CHECK(sceHttpAddRequestHeader(req, "x-a", "2", ORBIS_HTTP_HEADER_ADD) == 0);
  CHECK(sceHttpRemoveRequestHeader(req, "X-B") == ORBIS_HTTP_ERROR_NOT_FOUND);
  CHECK(sceHttpRemoveRequestHeader(req, NULL) == ORBIS_HTTP_ERROR_NOT_FOUND);
  CHECK(sceHttpRemoveRequestHeader(req, "X-A") == 0);
  CHECK(sceHttpRemoveRequestHeader(req, "X-A") == ORBIS_HTTP_ERROR_NOT_FOUND);

  // A request that was never sent: getters answer BEFORE_SEND.
  int32_t status = 0;
  CHECK(sceHttpGetStatusCode(req, &status) == ORBIS_HTTP_ERROR_BEFORE_SEND);
  CHECK(sceHttpSetRequestContentLength(req, 10) == 0);
  CHECK(sceHttpSetRequestContentLength(conn, 10) ==
        ORBIS_HTTP_ERROR_INVALID_ID);

  // Certificates and the CA list belong to the context.
  OrbisHttpsCaList list = {.certs = 0, .certsNum = 7};
  CHECK(sceHttpsLoadCert(9999, 0, 0, 0, 0) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpsLoadCert(ctx, 0, 0, 0, 0) == 0);
  CHECK(sceHttpsUnloadCert(ctx) == 0);
  CHECK(sceHttpsGetCaList(ctx, NULL) == ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpsGetCaList(ctx, &list) == 0 && list.certsNum == 0);

  CHECK(sceHttpDeleteRequest(req) == 0);
  CHECK(sceHttpDeleteRequest(req) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpDeleteConnection(conn) == 0);
  CHECK(sceHttpDeleteConnection(conn) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpDeleteTemplate(tmpl) == 0);
  CHECK(sceHttpDeleteTemplate(tmpl) == ORBIS_HTTP_ERROR_INVALID_ID);

  // The library stays up until its last context goes, then every object goes
  // with it.
  CHECK(sceHttpTerm(ctx + 100) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpTerm(ctx) == 0);
  CHECK(sceHttpTerm(ctx) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpSetConnectTimeOut(tmpl_gz, 1) == 0);
  CHECK(sceHttpTerm(ctx2) == 0);
  CHECK(sceHttpSetConnectTimeOut(tmpl_gz, 1) == ORBIS_HTTP_ERROR_BEFORE_INIT);

  // Stubs, as in shadPS4.
  GuestContext g = guest_ctx();
  g.rax = 77;
  shim_sceHttpCookieFlush(&g);
  CHECK(g.rax == 0);
  printf("PASS objects\n");
  return 0;
}

static int test_offline(void) {
  unsetenv("PS4_NETWORK");
  const int32_t ctx = sceHttpInit(1, 1, 64 * 1024);
  const int32_t tmpl = sceHttpCreateTemplate(ctx, "Game/1.0", 2, 0);
  const int32_t conn =
      sceHttpCreateConnectionWithURL(tmpl, "http://example.com/", true);
  CHECK(conn > 0);
  uint64_t eh = 0;
  CHECK(sceHttpCreateEpoll(ctx, NULL) == ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpCreateEpoll(ctx, &eh) == 0 && eh != 0);
  // Bound on the connection: inherited by its requests.
  CHECK(sceHttpSetEpoll(conn, eh + 100, 0x1234) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpSetEpoll(conn, eh, 0x1234) == 0);
  const int32_t req = sceHttpCreateRequestWithURL(conn, ORBIS_HTTP_METHOD_GET,
                                                  "http://example.com/x", 0);
  uint64_t got_eh = 0, got_arg = 0;
  CHECK(sceHttpGetEpoll(req, &got_eh, &got_arg) == 0 && got_eh == eh &&
        got_arg == 0x1234);

  CHECK(sceHttpSendRequest(req, NULL, 0) == 0);
  OrbisHttpNBEvent ev[4];
  memset(ev, 0, sizeof(ev));
  CHECK(sceHttpWaitRequest(eh, ev, 0, -1) == ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpWaitRequest(eh, ev, 4, -1) == 1);
  CHECK(ev[0].id == req && ev[0].userArg == 0x1234);
  CHECK(ev[0].events ==
        (ORBIS_HTTP_NB_EVENT_RESOLVER_ERR | ORBIS_HTTP_NB_EVENT_HUP));
  CHECK(sceHttpWaitRequest(eh, ev, 4, 0) == 0);    // polling, nothing queued
  CHECK(sceHttpWaitRequest(eh, ev, 4, 2000) == 0); // times out

  int32_t status = 0, err = 0, result = 0;
  uint64_t len = 7, header = 7, header_size = 7;
  CHECK(sceHttpGetStatusCode(req, &status) == ORBIS_HTTP_ERROR_BEFORE_SEND);
  CHECK(sceHttpGetLastErrno(req, &err) == 0 &&
        err == ORBIS_HTTP_ERROR_RESOLVER_ENODNS);
  CHECK(sceHttpGetResponseContentLength(req, &result, &len) == 0);
  CHECK(result == ORBIS_HTTP_ERROR_NO_CONTENT_LENGTH && len == 0);
  GuestContext g = guest_ctx();
  CHECK(sceHttpGetAllResponseHeaders(&g, req, &header, &header_size) == 0 &&
        header == 0 && header_size == 0);
  char buf[8];
  CHECK(sceHttpReadData(req, buf, sizeof(buf)) == 0);
  CHECK(sceHttpReadData(req, NULL, sizeof(buf)) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpSendRequest(req, NULL, 0) == ORBIS_HTTP_ERROR_AFTER_SEND);
  CHECK(sceHttpSetRequestContentLength(req, 1) == ORBIS_HTTP_ERROR_BUSY);

  // Unbound, the next request queues nothing.
  const int32_t req2 = sceHttpCreateRequestWithURL(conn, ORBIS_HTTP_METHOD_GET,
                                                   "http://example.com/y", 0);
  CHECK(sceHttpUnsetEpoll(conn) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpUnsetEpoll(req2) == 0);
  CHECK(sceHttpSendRequest(req2, NULL, 0) == 0);
  CHECK(sceHttpGetStatusCode(req2, &status) == ORBIS_HTTP_ERROR_BEFORE_SEND);
  CHECK(sceHttpWaitRequest(eh, ev, 4, 0) == 0);

  // An aborted request can no longer be sent; AbortWaitRequest stops every
  // later wait.
  const int32_t req3 = sceHttpCreateRequestWithURL(conn, ORBIS_HTTP_METHOD_GET,
                                                   "http://example.com/z", 0);
  CHECK(sceHttpAbortRequest(req3) == 0);
  CHECK(sceHttpSendRequest(req3, NULL, 0) == ORBIS_HTTP_ERROR_ABORTED);
  CHECK(sceHttpGetStatusCode(req3, &status) == ORBIS_HTTP_ERROR_ABORTED);
  CHECK(sceHttpAbortWaitRequest(eh) == 0);
  CHECK(sceHttpWaitRequest(eh, ev, 4, -1) == ORBIS_HTTP_ERROR_ABORTED);
  CHECK(sceHttpDestroyEpoll(ctx, eh) == 0);
  CHECK(sceHttpDestroyEpoll(ctx, eh) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpWaitRequest(eh, ev, 4, -1) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpTerm(ctx) == 0);
  printf("PASS offline\n");
  return 0;
}

static int32_t send_get(int32_t conn, const char *path) {
  const int32_t req =
      sceHttpCreateRequest(conn, ORBIS_HTTP_METHOD_GET, path, 0);
  if (req <= 0 || sceHttpSendRequest(req, NULL, 0) != 0)
    return -1;
  return req;
}

static int test_online(void) {
  CHECK(setup_online() == 0);
  CHECK(sceHttpAddRequestHeader(g_tmpl, "X-Tmpl", "t", 0) == 0);
  CHECK(sceHttpAddRequestHeader(g_conn, "X-Conn", "c", 0) == 0);

  // Status, length, data, headers.
  int32_t req = send_get(g_conn, "/hello");
  CHECK(req > 0);
  int32_t status = 0, result = -1;
  uint64_t len = 0;
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 200);
  CHECK(sceHttpGetResponseContentLength(req, &result, &len) == 0 &&
        result == 0 && len == 11);
  char buf[4096];
  CHECK(read_all(req, buf, sizeof(buf)) == 11 && !strcmp(buf, "hello world"));
  int32_t err = -1;
  CHECK(sceHttpGetLastErrno(req, &err) == 0 && err == 0);
  GuestContext g = guest_ctx();
  uint64_t header = 0, header_size = 0;
  CHECK(sceHttpGetAllResponseHeaders(&g, req, &header, &header_size) == 0 &&
        header != 0);
  const char *blob = host_ptr(header);
  CHECK(strlen(blob) == header_size);
  CHECK(!strncmp(blob, "HTTP/1.1 200 OK\r\n", 17));
  CHECK(strstr(blob, "\r\nX-Test: 1\r\n") != NULL);
  CHECK(!strcmp(blob + header_size - 4, "\r\n\r\n"));
  uint64_t header2 = 0;
  CHECK(sceHttpGetAllResponseHeaders(&g, req, &header2, &header_size) == 0 &&
        header2 == header);
  const int frees = g_frees;
  CHECK(g_allocs == 1);
  CHECK(sceHttpDeleteRequest(req) == 0);
  CHECK(g_frees ==
        frees + 1); // the guest copy of the headers goes with the request

  // The headers sent: template, connection, request, then the libhttp
  // User-Agent.
  req = sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_GET, "/echo-headers", 0);
  CHECK(sceHttpAddRequestHeader(req, "X-Req", "r", 0) == 0);
  CHECK(sceHttpSendRequest(req, NULL, 0) == 0);
  CHECK(read_all(req, buf, sizeof(buf)) > 0);
  const char *t = strstr(buf, "X-Tmpl: t"), *c = strstr(buf, "X-Conn: c"),
             *r = strstr(buf, "X-Req: r");
  CHECK(t && c && r && t < c && c < r);
  const char *ua = strstr(buf, "\r\nUser-Agent: ");
  CHECK(ua && r < ua &&
        !strcmp(ua, "\r\nUser-Agent: Game/1.0 libhttp/9.00 (PlayStation 4)"));
  CHECK(strstr(buf, "Expect:") == NULL);
  CHECK(sceHttpDeleteRequest(req) == 0);

  // gzip: inflated by default, raw when inflation is off.
  req = send_get(g_conn, "/gz");
  CHECK(sceHttpGetResponseContentLength(req, &result, &len) == 0 &&
        len == strlen(g_gz_plain));
  CHECK(read_all(req, buf, sizeof(buf)) == (int32_t)strlen(g_gz_plain) &&
        !strcmp(buf, g_gz_plain));
  CHECK(sceHttpDeleteRequest(req) == 0);
  req = sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_GET, "/gz", 0);
  CHECK(sceHttpSetInflateGZIPEnabled(req, 0) == 0);
  CHECK(sceHttpSendRequest(req, NULL, 0) == 0);
  CHECK(sceHttpGetResponseContentLength(req, &result, &len) == 0 &&
        len == g_gz_size);
  CHECK(sceHttpDeleteRequest(req) == 0);

  // 301 is followed and cached: the second request does not reach /old.
  for (int i = 0; i < 2; i++) {
    req = send_get(g_conn, "/old");
    CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 200);
    CHECK(read_all(req, buf, sizeof(buf)) == 11);
    CHECK(sceHttpDeleteRequest(req) == 0);
  }
  CHECK(g_old_hits == 1);
  CHECK(sceHttpRedirectCacheFlush(g_ctx) == 0);
  req = send_get(g_conn, "/old");
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 200 &&
        g_old_hits == 2);
  CHECK(sceHttpDeleteRequest(req) == 0);
  // Without auto redirect the 301 itself is the answer.
  req = sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_GET, "/old", 0);
  CHECK(sceHttpSetAutoRedirect(req, 0) == 0);
  CHECK(sceHttpSendRequest(req, NULL, 0) == 0);
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 301);
  CHECK(sceHttpDeleteRequest(req) == 0);

  // POST: 303 becomes a GET without the body, 302 is not followed.
  req = sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_POST, "/post303", 5);
  CHECK(sceHttpAddRequestHeader(req, "Content-Type", "text/plain", 0) == 0);
  CHECK(sceHttpSendRequest(req, "abcde", 5) == 0);
  CHECK(read_all(req, buf, sizeof(buf)) > 0 && !strcmp(buf, "GET 0"));
  CHECK(sceHttpDeleteRequest(req) == 0);
  req = sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_POST, "/post302", 5);
  CHECK(sceHttpSendRequest(req, "abcde", 5) == 0);
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 302);
  CHECK(sceHttpDeleteRequest(req) == 0);
  req = sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_POST, "/echo", 3);
  CHECK(sceHttpSendRequest(req, "xyz", 3) == 0);
  CHECK(read_all(req, buf, sizeof(buf)) > 0 && !strcmp(buf, "POST 3"));
  CHECK(sceHttpDeleteRequest(req) == 0);
  // A custom method name is sent as given.
  req =
      sceHttpCreateRequestWithURL2(g_conn, "PATCH", "http://127.0.0.1/echo", 0);
  CHECK(sceHttpSendRequest(req, "1", 1) == 0);
  CHECK(read_all(req, buf, sizeof(buf)) > 0 && !strcmp(buf, "PATCH 1"));
  CHECK(sceHttpDeleteRequest(req) == 0);

  // Not found is a status, not an error.
  req = send_get(g_conn, "/missing");
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 404);
  CHECK(sceHttpDeleteRequest(req) == 0);

  // A refused connection is a transport error.
  const int32_t dead =
      sceHttpCreateConnection(g_tmpl, "127.0.0.1", "http", 1, 1);
  req = send_get(dead, "/");
  CHECK(sceHttpGetStatusCode(req, &status) == ORBIS_HTTP_ERROR_BEFORE_SEND);
  CHECK(sceHttpGetLastErrno(req, &err) == 0 &&
        err == ORBIS_HTTP_ERROR_RESOLVER_ENOHOST);
  CHECK(sceHttpTerm(g_ctx) == 0);
  printf("PASS online\n");
  return 0;
}

static int test_nonblock(void) {
  CHECK(setup_online() == 0);
  uint64_t eh = 0;
  CHECK(sceHttpCreateEpoll(g_ctx, &eh) == 0);
  const int32_t req =
      sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_GET, "/slow", 0);
  CHECK(sceHttpSetNonblock(req, 1) == 0);
  CHECK(sceHttpSetEpoll(req, eh, 0x99) == 0);
  CHECK(sceHttpSendRequest(req, NULL, 0) == 0);
  int32_t status = 0, result = 0;
  uint64_t len = 0;
  char buf[16];
  CHECK(sceHttpGetStatusCode(req, &status) == ORBIS_HTTP_ERROR_EAGAIN);
  CHECK(sceHttpGetResponseContentLength(req, &result, &len) ==
        ORBIS_HTTP_ERROR_BEFORE_SEND);
  CHECK(sceHttpReadData(req, buf, sizeof(buf)) == ORBIS_HTTP_ERROR_EAGAIN);
  OrbisHttpNBEvent ev;
  CHECK(sceHttpWaitRequest(eh, &ev, 1, -1) == 1);
  CHECK(ev.id == req && ev.userArg == 0x99);
  CHECK(ev.events == (ORBIS_HTTP_NB_EVENT_IN | ORBIS_HTTP_NB_EVENT_OUT |
                      ORBIS_HTTP_NB_EVENT_RESOLVED));
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 200);
  CHECK(sceHttpReadData(req, buf, sizeof(buf)) == 4 && !memcmp(buf, "slow", 4));

  // Aborted in flight: the getters answer ABORTED and the late result is
  // dropped (no event).
  const int32_t req2 =
      sceHttpCreateRequest(g_conn, ORBIS_HTTP_METHOD_GET, "/slow", 0);
  CHECK(sceHttpSetEpoll(req2, eh, 0) == 0);
  CHECK(sceHttpSendRequest(req2, NULL, 0) == 0);
  CHECK(sceHttpAbortRequest(req2) == 0);
  CHECK(sceHttpGetStatusCode(req2, &status) == ORBIS_HTTP_ERROR_ABORTED);
  CHECK(sceHttpWaitRequest(eh, &ev, 1, 600000) == 0);
  CHECK(sceHttpTerm(g_ctx) == 0);
  printf("PASS nonblock\n");
  return 0;
}

static int test_http2(void) {
  CHECK(setup_online() == 0);
  const int32_t ctx = sceHttp2Init(1, 1, 64 * 1024, 4);
  CHECK(ctx == g_ctx + 1);
  const int32_t tmpl = sceHttp2CreateTemplate(ctx, "Game2", 2, 0);
  CHECK(tmpl > 0);
  char url[128];
  snprintf(url, sizeof(url), "http://127.0.0.1:%u/hello", (unsigned)g_port);

  const int32_t req = sceHttp2CreateRequestWithURL(tmpl, "GET", url, 0);
  CHECK(req > 0);
  const int32_t conn = ps4_http2_connection_of(req);
  CHECK(conn > 0 && conn == req - 1);
  CHECK(sceHttp2AddRequestHeader(req, "X-H2", "1", 0) == 0);
  CHECK(sceHttp2SetRequestContentLength(req, 0) == 0);
  CHECK(sceHttp2SendRequest(req, NULL, 0) == 0);
  int32_t status = 0, result = -1;
  uint64_t len = 0;
  CHECK(sceHttp2GetStatusCode(req, &status) == 0 && status == 200);
  CHECK(sceHttp2GetResponseContentLength(req, &result, &len) == 0 && len == 11);
  GuestContext g = guest_ctx();
  uint64_t header = 0, header_size = 0;
  CHECK(sceHttp2GetAllResponseHeaders(&g, req, &header, &header_size) == 0 &&
        header_size > 0);
  char buf[32];
  CHECK(sceHttp2ReadData(req, buf, sizeof(buf)) == 11 &&
        !memcmp(buf, "hello world", 11));
  CHECK(sceHttp2AbortRequest(req) == 0);

  // Deleting the request deletes its connection.
  CHECK(sceHttp2DeleteRequest(req) == 0);
  CHECK(ps4_http2_connection_of(req) == 0);
  CHECK(sceHttpDeleteConnection(conn) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttp2DeleteRequest(req) == ORBIS_HTTP_ERROR_INVALID_ID);

  // A failed request creation removes the connection it opened.
  CHECK(sceHttp2CreateRequestWithURL(tmpl, NULL, url, 0) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  const int32_t probe =
      sceHttpCreateConnection(tmpl, "127.0.0.1", "http", g_port, 1);
  CHECK(probe > 0);
  CHECK(sceHttpDeleteConnection(probe - 1) == ORBIS_HTTP_ERROR_INVALID_ID);
  // A bad URL fails before any connection.
  CHECK(sceHttp2CreateRequestWithURL(tmpl, "GET", "ftp://x/", 0) ==
        ORBIS_HTTP_ERROR_UNKNOWN_SCHEME);
  CHECK(sceHttp2CreateRequestWithURL(tmpl + 100, "GET", url, 0) ==
        ORBIS_HTTP_ERROR_INVALID_ID);

  // A request not created through libSceHttp2 keeps its connection.
  const int32_t req1 =
      sceHttpCreateRequestWithURL(probe, ORBIS_HTTP_METHOD_GET, url, 0);
  CHECK(sceHttp2DeleteRequest(req1) == 0);
  CHECK(sceHttpDeleteConnection(probe) == 0);

  // Stubs, as in shadPS4.
  g.rax = 77;
  shim_sceHttp2SetTimeOut(&g);
  CHECK(g.rax == 0);
  CHECK(sceHttp2DeleteTemplate(tmpl) == 0);
  CHECK(sceHttp2Term(ctx) == 0);
  CHECK(sceHttp2Term(ctx) == ORBIS_HTTP_ERROR_INVALID_ID);
  CHECK(sceHttpTerm(g_ctx) == 0);
  CHECK(sceHttp2Term(ctx) == ORBIS_HTTP_ERROR_BEFORE_INIT);
  printf("PASS http2\n");
  return 0;
}

// The guest layout of OrbisHttpUriElement.
typedef struct {
  uint8_t opaque;
  uint8_t pad[7];
  uint64_t scheme, username, password, hostname, path, query, fragment;
  uint16_t port;
  uint8_t reserved[10];
} UriGuest;

static int test_parse(void) {
  // URI parse through the shim: guest addresses into the guest pool.
  static const char uri[] =
      "https://user:pw@Example.com:8443/a/./b/../c?x=1#frag";
  static char pool[256];
  static UriGuest el;
  static uint64_t require;
  GuestContext g = guest_ctx();
  g.rdi = 0;
  g.rsi = A(uri);
  g.rdx = 0;
  g.rcx = A(&require);
  g.r8 = 0;
  shim_sceHttpUriParse(&g);
  CHECK(g.rax == 0 && require > 0 && require <= sizeof(pool));
  g = guest_ctx();
  g.rdi = A(&el);
  g.rsi = A(uri);
  g.rdx = A(pool);
  g.rcx = A(&require);
  g.r8 = sizeof(pool);
  shim_sceHttpUriParse(&g);
  CHECK(g.rax == 0);
  CHECK(!strcmp(host_ptr(el.scheme), "https") &&
        !strcmp(host_ptr(el.username), "user"));
  CHECK(!strcmp(host_ptr(el.password), "pw") &&
        !strcmp(host_ptr(el.hostname), "Example.com"));
  CHECK(!strcmp(host_ptr(el.path), "/a/c") &&
        !strcmp(host_ptr(el.query), "?x=1"));
  CHECK(!strcmp(host_ptr(el.fragment), "#frag") && el.port == 8443 &&
        el.opaque == 0);
  CHECK(el.hostname >= A(pool) && el.hostname < A(pool) + sizeof(pool));

  // Build it back: default ports are left out.
  static char out[256];
  g = guest_ctx();
  g.rdi = A(out);
  g.rsi = A(&require);
  g.rdx = sizeof(out);
  g.rcx = A(&el);
  g.r8 = 0xff;
  shim_sceHttpUriBuild(&g);
  CHECK(g.rax == 0 &&
        !strcmp(out, "https://user:pw@Example.com:8443/a/c?x=1#frag") &&
        require == strlen(out) + 1);
  OrbisHttpUriElement h;
  char hpool[128];
  CHECK(sceHttpUriParse(&h, "http://example.com/p", hpool, &require,
                        sizeof(hpool)) == 0 &&
        h.port == 80);
  CHECK(sceHttpUriBuild(out, &require, sizeof(out), &h, 0xff) == 0 &&
        !strcmp(out, "http://example.com/p"));
  CHECK(sceHttpUriBuild(out, &require, 3, &h, 0xff) ==
        ORBIS_HTTP_ERROR_OUT_OF_MEMORY);
  CHECK(sceHttpUriBuild(NULL, NULL, 0, &h, 0xff) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);
  CHECK(sceHttpUriParse(&h, "http://[::1]:8080/", hpool, &require,
                        sizeof(hpool)) == 0);
  CHECK(!strcmp(h.hostname, "::1") && h.port == 8080);
  CHECK(sceHttpUriParse(&h, "http://host:99999/", hpool, &require,
                        sizeof(hpool)) == ORBIS_HTTP_ERROR_INVALID_URL);
  CHECK(sceHttpUriParse(&h, "mailto:someone", hpool, &require, sizeof(hpool)) ==
            0 &&
        h.opaque);
  CHECK(sceHttpUriParse(&h, "http://example.com/p", hpool, &require, 4) ==
        ORBIS_HTTP_ERROR_OUT_OF_MEMORY);

  // Escape / unescape.
  CHECK(sceHttpUriEscape(NULL, &require, 0, "a b/~") == 0 && require == 10);
  CHECK(sceHttpUriEscape(out, &require, sizeof(out), "a b/~") == 0 &&
        !strcmp(out, "a%20b%2F~"));
  CHECK(sceHttpUriEscape(out, &require, 4, "a b/~") ==
        ORBIS_HTTP_ERROR_OUT_OF_MEMORY);
  CHECK(sceHttpUriUnescape(out, &require, sizeof(out), "a%20b%2f%zz") == 0 &&
        !strcmp(out, "a b/%zz"));
  CHECK(sceHttpUriUnescape(out, &require, sizeof(out), NULL) ==
        ORBIS_HTTP_ERROR_INVALID_VALUE);

  // Sweep and merge.
  CHECK(sceHttpUriSweepPath(out, "/a/./b/../../c/d", 17) == 0 &&
        !strcmp(out, "/c/d"));
  CHECK(sceHttpUriSweepPath(out, "rel/./x", 8) == 0 && !strcmp(out, "rel/./x"));
  char merged[512];
  CHECK(sceHttpUriMerge(NULL, "http://h/a/b", "c", &require, 0, 0) == 0 &&
        require > 0);
  CHECK(sceHttpUriMerge(merged, "http://h/a/b", "c?q", &require, sizeof(merged),
                        0) == 0);
  CHECK(!strcmp(merged, "http://h/a/c?q"));
  CHECK(sceHttpUriMerge(merged, "http://h/a/b", "/root", &require,
                        sizeof(merged), 0) == 0);
  CHECK(!strcmp(merged, "http://h/root"));
  CHECK(sceHttpUriMerge(merged, "http://h/a/b", "//other/x", &require,
                        sizeof(merged), 0) == 0);
  CHECK(!strcmp(merged, "//other/x"));
  CHECK(sceHttpUriMerge(merged, "http://h/", "c", &require, sizeof(merged),
                        1) == ORBIS_HTTP_ERROR_INVALID_VALUE);

  // Status line through the shim: the reason phrase is a guest address;
  // phraseLen is the seventh argument, on the stack.
  static const char line[] = "HTTP/1.1 404 Not Found\r\nX: y\r\n";
  static int32_t major, minor, code;
  static uint64_t reason, phrase_len;
  g = guest_ctx();
  g.rdi = A(line);
  g.rsi = sizeof(line) - 1;
  g.rdx = A(&major);
  g.rcx = A(&minor);
  g.r8 = A(&code);
  g.r9 = A(&reason);
  // The stack slot rsp + 8, written through g_stack itself.
  uint64_t *stack_arg = (uint64_t *)(g_stack + sizeof(g_stack) / 2 + 8);
  CHECK(A(stack_arg) == g.rsp + 8);
  *stack_arg = A(&phrase_len);
  shim_sceHttpParseStatusLine(&g);
  CHECK((int32_t)g.rax == 24);
  CHECK(major == 1 && minor == 1 && code == 404);
  CHECK(reason == A(line + 12) &&
        phrase_len == 10); // " Not Found", from after the code
  const char *phrase = NULL;
  uint64_t plen = 0;
  CHECK(sceHttpParseStatusLine("HTTP/1.1 200", 12, &major, &minor, &code,
                               &phrase, &plen) ==
        ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE);
  CHECK(sceHttpParseStatusLine("HTTP/1.1 200 OK\n", 16, &major, &minor, NULL,
                               &phrase, &plen) ==
        ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE);

  // Response header through the shim: the value is a guest address into the
  // header.
  static const char hdr[] =
      "HTTP/1.1 200 OK\r\nContent-Type:  text/html\r\nX-Fold: a\r\n b\r\n\r\n";
  static const char field[] = "content-type";
  static uint64_t value, value_len;
  g = guest_ctx();
  g.rdi = A(hdr);
  g.rsi = sizeof(hdr) - 1;
  g.rdx = A(field);
  g.rcx = A(&value);
  g.r8 = A(&value_len);
  shim_sceHttpParseResponseHeader(&g);
  CHECK((int32_t)g.rax == 43 && value == A(hdr + 32) && value_len == 9);
  const char *v = NULL;
  uint64_t vlen = 0;
  CHECK(sceHttpParseResponseHeader(hdr, sizeof(hdr) - 1, "X-Fold", &v, &vlen) >
        0);
  CHECK(vlen == 5 &&
        !memcmp(v, "a\r\n b", 5)); // a folded value keeps its line break
  CHECK(
      sceHttpParseResponseHeader(hdr, sizeof(hdr) - 1, "Missing", &v, &vlen) ==
      ORBIS_HTTP_ERROR_PARSE_HTTP_NOT_FOUND);
  CHECK(sceHttpParseResponseHeader(NULL, 0, "X", &v, &vlen) ==
        ORBIS_HTTP_ERROR_PARSE_HTTP_INVALID_RESPONSE);
  printf("PASS parse\n");
  return 0;
}

static int test_overrides(void) {
  setenv("PS4_NETWORK", "1", 1);
  CHECK(start_server() == 0);
  char path[] = "/tmp/ps4_http_overridesXXXXXX";
  const int fd = mkstemp(path);
  CHECK(fd >= 0);
  char json[512];
  const int n = snprintf(
      json, sizeof(json),
      "{\n  \"_comment\": \"test\",\n  \"skip\": {\"nested\": [1, 2, true]},\n"
      "  \"api.example.com\": \"http://127.0.0.1:%u\",\n  \"*\": "
      "\"unused\"\n}\n",
      (unsigned)g_port);
  CHECK(write(fd, json, (size_t)n) == n);
  close(fd);
  setenv("PS4_HTTP_HOST_OVERRIDES_JSON", path, 1);

  const int32_t ctx = sceHttpInit(1, 1, 64 * 1024);
  const int32_t tmpl = sceHttpCreateTemplate(ctx, "Game/1.0", 2, 0);
  const int32_t conn =
      sceHttpCreateConnectionWithURL(tmpl, "https://api.example.com/v1", true);
  CHECK(conn > 0);
  const int32_t req =
      sceHttpCreateRequest(conn, ORBIS_HTTP_METHOD_GET, "/hello", 0);
  CHECK(sceHttpSendRequest(req, NULL, 0) == 0);
  int32_t status = 0;
  CHECK(sceHttpGetStatusCode(req, &status) == 0 && status == 200);
  char buf[32];
  CHECK(read_all(req, buf, sizeof(buf)) == 11);
  CHECK(sceHttpTerm(ctx) == 0);
  unlink(path);
  printf("PASS overrides\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2)
    return 2;
  const char *c = argv[1];
  if (!strcmp(c, "objects"))
    return test_objects();
  if (!strcmp(c, "offline"))
    return test_offline();
  if (!strcmp(c, "online"))
    return test_online();
  if (!strcmp(c, "nonblock"))
    return test_nonblock();
  if (!strcmp(c, "http2"))
    return test_http2();
  if (!strcmp(c, "parse"))
    return test_parse();
  if (!strcmp(c, "overrides"))
    return test_overrides();
  fprintf(stderr, "unknown case %s\n", c);
  return 2;
}
