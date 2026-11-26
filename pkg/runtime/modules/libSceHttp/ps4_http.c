// SPDX-License-Identifier: GPL-2.0-or-later

// libSceHttp after shadPS4 network/http.cpp.
//
// Library contexts (sceHttpInit) own templates; a template owns connections, a connection
// owns requests. Each level carries its settings (timeouts, redirects, gzip, HTTPS flags, ...),
// its epoll binding and its extra request headers; a new connection or request starts from the
// settings and epoll binding of its parent, and a request sends the headers of its template,
// connection and itself, in that order.
//
// sceHttpSendRequest hands the request to a worker thread and returns. With the host
// disconnected (the default; PS4_NETWORK=1 connects it, see ps4_netctl.c) the worker answers as
// a failed name resolution, ORBIS_HTTP_ERROR_RESOLVER_ENODNS, like shadPS4 offline. Connected,
// it performs the request on the network (ps4_http_transport.c). The getters (status code,
// content length, headers, data) wait for the worker, or answer EAGAIN on a non-blocking
// request. A request bound to an epoll queues an event there when it completes, which
// sceHttpWaitRequest reports.
//
// PS4_HTTPS_DISABLED=1 is shadPS4's "forced HTTPS disabled" setting: sceHttpsEnableOption
// clears the flags instead of setting them and server certificates are not verified.
// PS4_HTTP_LOG=1 logs the library's calls to stderr.
//
// The functions shadPS4 leaves as stubs (cookies, authentication, callbacks, debug and
// statistics queries) are stubs here too (end of file).
#include "ps4_http.h"
#include "ps4_hle_stub.h"
#include "ps4_kernel_sys.h"
#include "ps4_netctl.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#define HTTP_FW_170 0x1700000u // Common::ElfInfo::FW_170

typedef enum HttpRequestState {
    HTTP_REQ_CREATED,
    HTTP_REQ_SENDING,
    HTTP_REQ_SENT,
    HTTP_REQ_ABORTED,
} HttpRequestState;

typedef struct HttpContext {
    int32_t id;
    bool loaded_certs; // sceHttpsLoadCert was called: HTTPS skips server verification
} HttpContext;

typedef struct HttpTemplate {
    struct HttpTemplate *next;
    int32_t id;
    int32_t ctx_id;
    char *user_agent;
    int32_t http_version;
    int32_t auto_proxy_conf;
    HttpSettings settings;
    int32_t epoll_id;
    uint64_t epoll_user_arg;
    HttpHeaderList headers;
} HttpTemplate;

typedef struct HttpConnection {
    struct HttpConnection *next;
    int32_t id;
    int32_t tmpl_id;
    char *url;
    char scheme[8];
    char *hostname;
    uint16_t port;
    bool keep_alive;
    bool is_secure;
    HttpSettings settings;
    int32_t epoll_id;
    uint64_t epoll_user_arg;
    HttpHeaderList headers;
} HttpConnection;

typedef struct HttpRequest {
    struct HttpRequest *next;
    int32_t id;
    int32_t refs; // the request list, the worker and blocked getters each hold one
    int32_t conn_id;
    int32_t method;
    char *method_str;
    char *url;
    uint64_t content_length;
    HttpRequestState state;
    bool deleted;
    int32_t last_errno;
    HttpSettings settings;
    HttpResponse res;
    pthread_cond_t cv; // signalled when the state leaves SENDING
    int32_t epoll_id;
    uint64_t epoll_user_arg;
    HttpHeaderList headers;
    // Guest copy of res.all_headers handed out by sceHttpGetAllResponseHeaders.
    GuestContext *guest_proc;
    uint64_t guest_headers;
    size_t guest_headers_size;
} HttpRequest;

typedef struct HttpEpoll {
    struct HttpEpoll *next;
    int32_t id;
    int32_t ctx_id;
    int32_t refs; // the epoll list and blocked waiters each hold one
    OrbisHttpNBEvent *events; // FIFO ring
    size_t head;
    size_t count;
    size_t cap;
    pthread_cond_t cv;
    bool destroyed;
    bool abort_requested;
} HttpEpoll;

typedef struct HttpRedirect {
    struct HttpRedirect *next;
    int32_t ctx_id;
    char *key; // the requested "scheme://host:port/path"
    char scheme[8];
    char *host;
    uint16_t port;
    char *path;
} HttpRedirect;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static bool g_inited;
static bool g_shutting_down;
static int32_t g_next_ctx_id;
static int32_t g_next_obj_id;
static bool g_default_accept_encoding_gzip;
static HttpContext *g_contexts;
static size_t g_context_count;
static size_t g_context_cap;
static HttpTemplate *g_templates;
static HttpConnection *g_connections;
static HttpRequest *g_requests;
static HttpEpoll *g_epolls;
static HttpRedirect *g_redirects;

static void http_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void http_log(const char *fmt, ...) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("PS4_HTTP_LOG");
        enabled = e && strcmp(e, "1") == 0;
    }
    if (!enabled) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("[libSceHttp] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static bool https_forced_disabled(void) {
    const char *e = getenv("PS4_HTTPS_DISABLED");
    return e && strcmp(e, "1") == 0;
}

// ---------------------------------------------------------------------------------------------
// Shared helpers

bool ps4_http_header_name_matches(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

static bool is_credential_header(const char *name) {
    return ps4_http_header_name_matches(name, "Authorization") ||
           ps4_http_header_name_matches(name, "Proxy-Authorization") || ps4_http_header_name_matches(name, "Cookie");
}

bool ps4_http_headers_add(HttpHeaderList *list, const char *name, const char *value) {
    if (list->count == list->cap) {
        const size_t cap = list->cap ? list->cap * 2 : 8;
        HttpHeader *items = realloc(list->items, cap * sizeof(*items));
        if (!items) return false;
        list->items = items;
        list->cap = cap;
    }
    char *n = strdup(name);
    char *v = strdup(value);
    if (!n || !v) {
        free(n);
        free(v);
        return false;
    }
    list->items[list->count].name = n;
    list->items[list->count].value = v;
    list->count++;
    return true;
}

void ps4_http_headers_free(HttpHeaderList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].value);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static bool headers_append_all(HttpHeaderList *dst, const HttpHeaderList *src) {
    for (size_t i = 0; i < src->count; i++) {
        if (!ps4_http_headers_add(dst, src->items[i].name, src->items[i].value)) return false;
    }
    return true;
}

// Removes every header named name; returns how many were removed.
static size_t headers_remove(HttpHeaderList *list, const char *name) {
    size_t kept = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (ps4_http_header_name_matches(list->items[i].name, name)) {
            free(list->items[i].name);
            free(list->items[i].value);
        } else {
            list->items[kept++] = list->items[i];
        }
    }
    const size_t removed = list->count - kept;
    list->count = kept;
    return removed;
}

void ps4_http_response_clear(HttpResponse *res) {
    free(res->body);
    free(res->all_headers);
    memset(res, 0, sizeof(*res));
    res->content_length_result = ORBIS_HTTP_ERROR_NO_CONTENT_LENGTH;
}

const char *ps4_http_status_reason(int code) {
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return NULL;
    }
}

// "libhttp/<major>.<minor>" of the system software version, as libhttp reports itself.
char *ps4_http_build_user_agent(const char *app_user_agent) {
    const uint32_t sw = PS4_SYSTEM_SW_VERSION;
    const uint32_t major = ((sw >> 0x18) & 0xf) + ((sw >> 0x1c) * 10);
    const uint32_t minor = ((sw >> 0x10) & 0xf) + (((sw >> 0x14) & 0xf) * 10);
    char *out = NULL;
    int n;
    if (!app_user_agent || !app_user_agent[0]) {
        n = asprintf(&out, "libhttp/%u.%02u (PlayStation 4)", major, minor);
    } else {
        n = asprintf(&out, "%s libhttp/%u.%02u (PlayStation 4)", app_user_agent, major, minor);
    }
    return n < 0 ? NULL : out;
}

static void settings_default(HttpSettings *s) {
    memset(s, 0, sizeof(*s));
    s->auto_redirect = true;
    s->inflate_gzip = true;
    s->cache_redirected_connection = true;
    s->ssl_flags = ORBIS_HTTPS_FLAG_SDK_DEFAULT;
}

// Returns ORBIS_OK or INVALID_VALUE / UNKNOWN_SCHEME; "http" and "https" in any case.
static int32_t check_scheme(const char *scheme, bool *is_secure) {
    if (!scheme) return ORBIS_HTTP_ERROR_INVALID_VALUE;
    if (strcasecmp(scheme, "HTTP") == 0) {
        *is_secure = false;
        return 0;
    }
    if (strcasecmp(scheme, "HTTPS") == 0) {
        *is_secure = true;
        return 0;
    }
    return ORBIS_HTTP_ERROR_UNKNOWN_SCHEME;
}

static bool contains_crlf(const char *s) {
    for (; s && *s; ++s) {
        if (*s == '\r' || *s == '\n') return true;
    }
    return false;
}

// The method code of a method name; CUSTOM for names outside the standard table.
static int32_t method_from_name(const char *m) {
    static const struct {
        const char *name;
        int32_t code;
    } table[] = {
        {"GET", ORBIS_HTTP_METHOD_GET},       {"POST", ORBIS_HTTP_METHOD_POST},
        {"HEAD", ORBIS_HTTP_METHOD_HEAD},     {"PUT", ORBIS_HTTP_METHOD_PUT},
        {"DELETE", ORBIS_HTTP_METHOD_DELETE}, {"TRACE", ORBIS_HTTP_METHOD_TRACE},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(m, table[i].name) == 0) return table[i].code;
    }
    return ORBIS_HTTP_METHOD_CUSTOM;
}

// "scheme://host:port" followed by path (a '/' is inserted when path lacks one).
static char *connection_url_with_path(const HttpConnection *conn, const char *path) {
    char *url = NULL;
    const char *sep = (path[0] != '\0' && path[0] != '/') ? "/" : "";
    if (asprintf(&url, "%s://%s:%u%s%s", conn->scheme, conn->hostname, (unsigned)conn->port, sep, path) < 0) return NULL;
    return url;
}

// ---------------------------------------------------------------------------------------------
// Object lookup (g_mu held)

static HttpContext *find_context(int32_t id) {
    for (size_t i = 0; i < g_context_count; i++) {
        if (g_contexts[i].id == id) return &g_contexts[i];
    }
    return NULL;
}

static HttpTemplate *find_template(int32_t id) {
    for (HttpTemplate *t = g_templates; t; t = t->next) {
        if (t->id == id) return t;
    }
    return NULL;
}

static HttpConnection *find_connection(int32_t id) {
    for (HttpConnection *c = g_connections; c; c = c->next) {
        if (c->id == id) return c;
    }
    return NULL;
}

static HttpRequest *find_request(int32_t id) {
    for (HttpRequest *r = g_requests; r; r = r->next) {
        if (r->id == id) return r;
    }
    return NULL;
}

static HttpEpoll *find_epoll(int32_t id) {
    for (HttpEpoll *e = g_epolls; e; e = e->next) {
        if (e->id == id) return e;
    }
    return NULL;
}

static HttpSettings *resolve_settings(int32_t id) {
    HttpTemplate *t = find_template(id);
    if (t) return &t->settings;
    HttpConnection *c = find_connection(id);
    if (c) return &c->settings;
    HttpRequest *r = find_request(id);
    if (r) return &r->settings;
    return NULL;
}

static HttpHeaderList *resolve_headers(int32_t id) {
    HttpTemplate *t = find_template(id);
    if (t) return &t->headers;
    HttpConnection *c = find_connection(id);
    if (c) return &c->headers;
    HttpRequest *r = find_request(id);
    if (r) return &r->headers;
    return NULL;
}

static bool resolve_epoll_binding(int32_t id, int32_t **epoll_id, uint64_t **user_arg) {
    HttpTemplate *t = find_template(id);
    if (t) {
        *epoll_id = &t->epoll_id;
        *user_arg = &t->epoll_user_arg;
        return true;
    }
    HttpConnection *c = find_connection(id);
    if (c) {
        *epoll_id = &c->epoll_id;
        *user_arg = &c->epoll_user_arg;
        return true;
    }
    HttpRequest *r = find_request(id);
    if (r) {
        *epoll_id = &r->epoll_id;
        *user_arg = &r->epoll_user_arg;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Object lifetime (g_mu held)

static void template_free(HttpTemplate *t) {
    free(t->user_agent);
    ps4_http_headers_free(&t->headers);
    free(t);
}

static void connection_free(HttpConnection *c) {
    free(c->url);
    free(c->hostname);
    ps4_http_headers_free(&c->headers);
    free(c);
}

static void request_release_guest_headers(HttpRequest *r) {
    if (r->guest_headers && r->guest_proc) {
        recomp_vm_free(r->guest_proc, r->guest_headers, r->guest_headers_size);
    }
    r->guest_headers = 0;
    r->guest_headers_size = 0;
    r->guest_proc = NULL;
}

static void request_unref(HttpRequest *r) {
    if (--r->refs > 0) return;
    free(r->method_str);
    free(r->url);
    ps4_http_response_clear(&r->res);
    ps4_http_headers_free(&r->headers);
    pthread_cond_destroy(&r->cv);
    free(r);
}

static void epoll_unref(HttpEpoll *e) {
    if (--e->refs > 0) return;
    free(e->events);
    pthread_cond_destroy(&e->cv);
    free(e);
}

static bool epoll_push(HttpEpoll *e, const OrbisHttpNBEvent *ev) {
    if (e->count == e->cap) {
        const size_t cap = e->cap ? e->cap * 2 : 8;
        OrbisHttpNBEvent *events = malloc(cap * sizeof(*events));
        if (!events) return false;
        for (size_t i = 0; i < e->count; i++) events[i] = e->events[(e->head + i) % e->cap];
        free(e->events);
        e->events = events;
        e->head = 0;
        e->cap = cap;
    }
    e->events[(e->head + e->count) % e->cap] = *ev;
    e->count++;
    return true;
}

static OrbisHttpNBEvent epoll_pop(HttpEpoll *e) {
    const OrbisHttpNBEvent ev = e->events[e->head];
    e->head = (e->head + 1) % e->cap;
    e->count--;
    return ev;
}

static void redirect_free(HttpRedirect *r) {
    free(r->key);
    free(r->host);
    free(r->path);
    free(r);
}

static size_t redirect_flush_ctx(int32_t ctx_id) {
    size_t n = 0;
    HttpRedirect **pp = &g_redirects;
    while (*pp) {
        HttpRedirect *r = *pp;
        if (r->ctx_id == ctx_id) {
            *pp = r->next;
            redirect_free(r);
            n++;
        } else {
            pp = &r->next;
        }
    }
    return n;
}

bool ps4_http_redirect_cache_get(int32_t ctx_id, const char *key, char *scheme, size_t scheme_size, char **host,
                                 uint16_t *port, char **path) {
    bool found = false;
    pthread_mutex_lock(&g_mu);
    for (HttpRedirect *r = g_redirects; r; r = r->next) {
        if (r->ctx_id == ctx_id && strcmp(r->key, key) == 0) {
            char *h = strdup(r->host);
            char *p = strdup(r->path);
            if (h && p) {
                snprintf(scheme, scheme_size, "%s", r->scheme);
                free(*host);
                free(*path);
                *host = h;
                *path = p;
                *port = r->port;
                found = true;
            } else {
                free(h);
                free(p);
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return found;
}

void ps4_http_redirect_cache_put(int32_t ctx_id, const char *key, const char *scheme, const char *host, uint16_t port,
                                 const char *path) {
    pthread_mutex_lock(&g_mu);
    HttpRedirect *r = NULL;
    for (HttpRedirect *it = g_redirects; it; it = it->next) {
        if (it->ctx_id == ctx_id && strcmp(it->key, key) == 0) {
            r = it;
            break;
        }
    }
    char *h = strdup(host);
    char *p = strdup(path);
    if (!h || !p) {
        free(h);
        free(p);
        pthread_mutex_unlock(&g_mu);
        return;
    }
    if (!r) {
        r = calloc(1, sizeof(*r));
        char *k = strdup(key);
        if (!r || !k) {
            free(r);
            free(k);
            free(h);
            free(p);
            pthread_mutex_unlock(&g_mu);
            return;
        }
        r->ctx_id = ctx_id;
        r->key = k;
        r->next = g_redirects;
        g_redirects = r;
    } else {
        free(r->host);
        free(r->path);
    }
    snprintf(r->scheme, sizeof(r->scheme), "%s", scheme);
    r->host = h;
    r->path = p;
    r->port = port;
    pthread_mutex_unlock(&g_mu);
}

// ---------------------------------------------------------------------------------------------
// Init / Term

int32_t sceHttpInit(int32_t libnetMemId, int32_t libsslCtxId, uint64_t poolSize) {
    http_log("sceHttpInit libnetMemId=%d libsslCtxId=%d poolSize=%llu", libnetMemId, libsslCtxId,
             (unsigned long long)poolSize);
    pthread_mutex_lock(&g_mu);
    if (poolSize == 0) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_KERNEL_ERROR_EINVAL_HTTP;
    }
    if (g_context_count == g_context_cap) {
        const size_t cap = g_context_cap ? g_context_cap * 2 : 4;
        HttpContext *c = realloc(g_contexts, cap * sizeof(*c));
        if (!c) {
            pthread_mutex_unlock(&g_mu);
            return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        }
        g_contexts = c;
        g_context_cap = cap;
    }
    const int32_t id = ++g_next_ctx_id;
    g_contexts[g_context_count++] = (HttpContext){.id = id, .loaded_certs = false};
    g_inited = true; // while at least one context is alive
    g_shutting_down = false;
    pthread_mutex_unlock(&g_mu);
    return id;
}

int32_t sceHttpTerm(int32_t libhttpCtxId) {
    http_log("sceHttpTerm ctx=%d", libhttpCtxId);
    pthread_mutex_lock(&g_mu);
    if (!g_inited) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_BEFORE_INIT;
    }
    HttpContext *c = find_context(libhttpCtxId);
    if (!c) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_INVALID_ID;
    }
    *c = g_contexts[--g_context_count];
    redirect_flush_ctx(libhttpCtxId);
    if (g_context_count == 0) {
        // The last context: every object of the library goes, in-flight results are dropped.
        g_shutting_down = true;
        while (g_requests) {
            HttpRequest *r = g_requests;
            g_requests = r->next;
            r->deleted = true;
            r->state = HTTP_REQ_ABORTED;
            request_release_guest_headers(r);
            pthread_cond_broadcast(&r->cv);
            request_unref(r);
        }
        while (g_epolls) {
            HttpEpoll *e = g_epolls;
            g_epolls = e->next;
            e->destroyed = true;
            pthread_cond_broadcast(&e->cv);
            epoll_unref(e);
        }
        while (g_connections) {
            HttpConnection *conn = g_connections;
            g_connections = conn->next;
            connection_free(conn);
        }
        while (g_templates) {
            HttpTemplate *t = g_templates;
            g_templates = t->next;
            template_free(t);
        }
        while (g_redirects) {
            HttpRedirect *r = g_redirects;
            g_redirects = r->next;
            redirect_free(r);
        }
        g_inited = false;
    }
    pthread_mutex_unlock(&g_mu);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Templates and connections

int32_t sceHttpCreateTemplate(int32_t libhttpCtxId, const char *userAgent, int32_t httpVer, int32_t isAutoProxyConf) {
    http_log("sceHttpCreateTemplate ctx=%d ua=%s httpVer=%d", libhttpCtxId, userAgent ? userAgent : "(null)", httpVer);
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!find_context(libhttpCtxId)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        HttpTemplate *t = calloc(1, sizeof(*t));
        char *ua = ps4_http_build_user_agent(userAgent ? userAgent : "");
        if (!t || !ua) {
            free(t);
            free(ua);
            rc = ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        } else {
            t->id = ++g_next_obj_id;
            t->ctx_id = libhttpCtxId;
            t->user_agent = ua;
            t->http_version = httpVer;
            t->auto_proxy_conf = isAutoProxyConf;
            settings_default(&t->settings);
            t->settings.accept_encoding_gzip = g_default_accept_encoding_gzip;
            t->next = g_templates;
            g_templates = t;
            rc = t->id;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpDeleteTemplate(int32_t tmplId) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = ORBIS_HTTP_ERROR_INVALID_ID;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else {
        for (HttpTemplate **pp = &g_templates; *pp; pp = &(*pp)->next) {
            if ((*pp)->id == tmplId) {
                HttpTemplate *t = *pp;
                *pp = t->next;
                template_free(t);
                rc = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// Adds a connection of tmplId (g_mu held, the template checked). Takes ownership of url and
// hostname.
static int32_t add_connection(int32_t tmplId, char *url, const char *scheme, char *hostname, uint16_t port,
                              bool keep_alive, bool is_secure) {
    HttpConnection *conn = calloc(1, sizeof(*conn));
    if (!conn || !url || !hostname) {
        free(conn);
        free(url);
        free(hostname);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }
    conn->id = ++g_next_obj_id;
    conn->tmpl_id = tmplId;
    conn->url = url;
    snprintf(conn->scheme, sizeof(conn->scheme), "%s", scheme);
    conn->hostname = hostname;
    conn->port = port;
    conn->keep_alive = keep_alive;
    conn->is_secure = is_secure;
    const HttpTemplate *t = find_template(tmplId);
    conn->settings = t->settings;
    conn->epoll_id = t->epoll_id;
    conn->epoll_user_arg = t->epoll_user_arg;
    conn->next = g_connections;
    g_connections = conn;
    http_log("created connection %d %s://%s:%u", conn->id, conn->scheme, conn->hostname, (unsigned)conn->port);
    return conn->id;
}

int32_t sceHttpCreateConnection(int32_t tmplId, const char *serverName, const char *scheme, uint16_t port,
                                int32_t isEnableKeepalive) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    bool is_secure = false;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!find_template(tmplId)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (!serverName) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if ((rc = check_scheme(scheme, &is_secure)) == 0) {
        char scheme_str[8];
        snprintf(scheme_str, sizeof(scheme_str), "%s", is_secure ? "https" : "http");
        char *host = strdup(serverName);
        uint16_t effective_port = port;
        if (host) ps4_http_apply_host_override(scheme_str, sizeof(scheme_str), &host, &effective_port, &is_secure);
        char *url = NULL;
        if (host && asprintf(&url, "%s://%s:%u", scheme_str, host, (unsigned)effective_port) < 0) url = NULL;
        rc = add_connection(tmplId, url, scheme_str, host, effective_port, isEnableKeepalive != 0, is_secure);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpCreateConnectionWithURL(int32_t tmplId, const char *url, bool enableKeepalive) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!find_template(tmplId)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_unlock(&g_mu);
    if (rc < 0) return rc;
    if (!url) return ORBIS_HTTP_ERROR_INVALID_URL;

    uint64_t required = 0;
    rc = sceHttpUriParse(NULL, url, NULL, &required, 0);
    if (rc < 0) return rc;
    char *pool = malloc(required);
    if (!pool) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    OrbisHttpUriElement parsed;
    rc = sceHttpUriParse(&parsed, url, pool, &required, required);
    bool is_secure = false;
    if (rc == 0) rc = check_scheme(parsed.scheme, &is_secure);
    if (rc == 0 && (!parsed.hostname || !parsed.hostname[0])) rc = ORBIS_HTTP_ERROR_INVALID_URL;
    if (rc < 0) {
        free(pool);
        return rc;
    }
    char scheme_str[8];
    snprintf(scheme_str, sizeof(scheme_str), "%s", is_secure ? "https" : "http");
    uint16_t port = parsed.port;
    char *host = strdup(parsed.hostname);
    free(pool);
    if (host) ps4_http_apply_host_override(scheme_str, sizeof(scheme_str), &host, &port, &is_secure);

    pthread_mutex_lock(&g_mu);
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT; // raced with sceHttpTerm
        free(host);
    } else if (!find_template(tmplId)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID; // raced with sceHttpDeleteTemplate
        free(host);
    } else {
        rc = add_connection(tmplId, strdup(url), scheme_str, host, port, enableKeepalive, is_secure);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpDeleteConnection(int32_t connId) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = ORBIS_HTTP_ERROR_INVALID_ID;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else {
        for (HttpConnection **pp = &g_connections; *pp; pp = &(*pp)->next) {
            if ((*pp)->id == connId) {
                HttpConnection *c = *pp;
                *pp = c->next;
                connection_free(c);
                rc = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// ---------------------------------------------------------------------------------------------
// Requests

int32_t sceHttpCreateRequestWithURL(int32_t connId, int32_t method, const char *url, uint64_t contentLength) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    HttpConnection *conn = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!(conn = find_connection(connId))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (!url) {
        rc = ORBIS_HTTP_ERROR_INVALID_URL;
    } else if (method > 8 || ((0x177u >> ((uint32_t)method & 0x1f)) & 1u) == 0) {
        // GET, POST, HEAD, PUT, DELETE, TRACE and CUSTOM; OPTIONS and CONNECT are refused.
        rc = ORBIS_HTTP_ERROR_UNKNOWN_METHOD;
    } else {
        HttpRequest *r = calloc(1, sizeof(*r));
        char *u = strdup(url);
        if (!r || !u) {
            free(r);
            free(u);
            rc = ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
        } else {
            r->id = ++g_next_obj_id;
            r->refs = 1;
            r->conn_id = connId;
            r->method = method;
            r->url = u;
            r->content_length = contentLength;
            r->state = HTTP_REQ_CREATED;
            r->settings = conn->settings;
            r->epoll_id = conn->epoll_id;
            r->epoll_user_arg = conn->epoll_user_arg;
            r->res.content_length_result = ORBIS_HTTP_ERROR_NO_CONTENT_LENGTH;
            pthread_cond_init(&r->cv, NULL);
            r->next = g_requests;
            g_requests = r;
            rc = r->id;
            http_log("created request %d method=%d url=%s", r->id, method, url);
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// Sets the method name of a request just created.
static void set_method_str(int32_t reqId, const char *method) {
    pthread_mutex_lock(&g_mu);
    HttpRequest *r = find_request(reqId);
    if (r) {
        free(r->method_str);
        r->method_str = strdup(method);
    }
    pthread_mutex_unlock(&g_mu);
}

// The URL of a request created on connId with path (shared by sceHttpCreateRequest[2]).
static int32_t request_url_for_path(int32_t connId, const char *path, char **url) {
    if (!g_inited) return ORBIS_HTTP_ERROR_BEFORE_INIT;
    HttpConnection *conn = find_connection(connId);
    if (!conn) return ORBIS_HTTP_ERROR_INVALID_ID;
    if (!path) return ORBIS_HTTP_ERROR_INVALID_VALUE;
    if (contains_crlf(path)) return ORBIS_HTTP_ERROR_INVALID_VALUE; // CRLF injection
    *url = connection_url_with_path(conn, path);
    return *url ? 0 : ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
}

int32_t sceHttpCreateRequest(int32_t connId, int32_t method, const char *path, uint64_t contentLength) {
    char *url = NULL;
    pthread_mutex_lock(&g_mu);
    const int32_t rc = request_url_for_path(connId, path, &url);
    pthread_mutex_unlock(&g_mu);
    if (rc < 0) return rc;
    const int32_t id = sceHttpCreateRequestWithURL(connId, method, url, contentLength);
    free(url);
    return id;
}

int32_t sceHttpCreateRequest2(int32_t connId, const char *method, const char *path, uint64_t contentLength) {
    char *url = NULL;
    pthread_mutex_lock(&g_mu);
    int32_t rc = request_url_for_path(connId, path, &url);
    if (rc == 0 && !method) {
        free(url);
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    pthread_mutex_unlock(&g_mu);
    if (rc < 0) return rc;
    const int32_t id = sceHttpCreateRequestWithURL(connId, method_from_name(method), url, contentLength);
    free(url);
    if (id > 0) set_method_str(id, method);
    return id;
}

int32_t sceHttpCreateRequestWithURL2(int32_t connId, const char *method, const char *url, uint64_t contentLength) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!find_connection(connId)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (!method) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    pthread_mutex_unlock(&g_mu);
    if (rc < 0) return rc;
    const int32_t id = sceHttpCreateRequestWithURL(connId, method_from_name(method), url, contentLength);
    if (id > 0) set_method_str(id, method);
    return id;
}

int32_t sceHttpDeleteRequest(int32_t reqId) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = ORBIS_HTTP_ERROR_INVALID_ID;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else {
        for (HttpRequest **pp = &g_requests; *pp; pp = &(*pp)->next) {
            if ((*pp)->id == reqId) {
                HttpRequest *r = *pp;
                *pp = r->next;
                r->deleted = true;
                r->state = HTTP_REQ_ABORTED;
                request_release_guest_headers(r);
                pthread_cond_broadcast(&r->cv);
                request_unref(r);
                rc = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

typedef struct HttpWorker {
    HttpRequest *req;
    int32_t req_id;
    HttpSendPlan plan;
    bool online;
} HttpWorker;

static void plan_free(HttpSendPlan *p) {
    free(p->host);
    free(p->path);
    free(p->method_str);
    free(p->content_type);
    free(p->body);
    ps4_http_headers_free(&p->headers);
}

static void *http_worker_main(void *arg) {
    HttpWorker *w = arg;
    HttpResponse res;
    memset(&res, 0, sizeof(res));
    res.content_length_result = ORBIS_HTTP_ERROR_NO_CONTENT_LENGTH;
    uint32_t success_bits = 0;
    int32_t err;
    if (!w->online) {
        err = ORBIS_HTTP_ERROR_RESOLVER_ENODNS;
    } else {
        err = ps4_http_run_request(&w->plan, &res, &success_bits);
    }

    pthread_mutex_lock(&g_mu);
    HttpRequest *r = w->req;
    if (g_shutting_down || r->deleted || r->state == HTTP_REQ_ABORTED) {
        http_log("request %d finished after it was %s; result dropped", w->req_id,
                 g_shutting_down ? "terminated" : (r->deleted ? "deleted" : "aborted"));
        ps4_http_response_clear(&res);
    } else {
        ps4_http_response_clear(&r->res);
        r->res = res;
        r->state = HTTP_REQ_SENT;
        r->last_errno = err;
        http_log("request %d done: errno=%#x status=%d body=%zu", w->req_id, (unsigned)err, res.status_code,
                 res.body_size);
        if (r->epoll_id != 0) {
            HttpEpoll *e = find_epoll(r->epoll_id);
            if (e && !e->destroyed) {
                const uint32_t bits =
                    err == 0 ? success_bits : (ORBIS_HTTP_NB_EVENT_RESOLVER_ERR | ORBIS_HTTP_NB_EVENT_HUP);
                const OrbisHttpNBEvent ev = {
                    .events = bits, .eventDetail = bits, .id = w->req_id, .userArg = r->epoll_user_arg};
                if (epoll_push(e, &ev)) pthread_cond_broadcast(&e->cv);
            }
        }
    }
    pthread_cond_broadcast(&r->cv);
    request_unref(r);
    pthread_mutex_unlock(&g_mu);

    plan_free(&w->plan);
    free(w);
    return NULL;
}

// Builds the transport plan of a request (g_mu held).
static bool build_plan(const HttpRequest *r, const void *postData, uint64_t size, HttpSendPlan *plan) {
    memset(plan, 0, sizeof(*plan));
    plan->method = r->method;
    // A CUSTOM request without a name (np_web_api uses that for PATCH) is sent as PATCH.
    const char *method_str = r->method_str;
    if (plan->method == ORBIS_HTTP_METHOD_CUSTOM && (!method_str || !method_str[0])) method_str = "PATCH";
    if (method_str && !(plan->method_str = strdup(method_str))) return false;

    const char *scheme_end = strstr(r->url, "://");
    const char *path = scheme_end ? strchr(scheme_end + 3, '/') : NULL;
    if (!(plan->path = strdup(path ? path : "/"))) return false;
    plan->settings = r->settings;

    const char *tmpl_user_agent = NULL;
    const HttpConnection *conn = find_connection(r->conn_id);
    if (conn) {
        snprintf(plan->scheme, sizeof(plan->scheme), "%s", conn->scheme);
        if (!(plan->host = strdup(conn->hostname))) return false;
        plan->port = conn->port;
        const HttpTemplate *t = find_template(conn->tmpl_id);
        if (t) {
            if (!headers_append_all(&plan->headers, &t->headers)) return false;
            tmpl_user_agent = t->user_agent;
            const HttpContext *c = find_context(t->ctx_id);
            plan->ctx_has_loaded_certs = c && c->loaded_certs;
            plan->ctx_id = t->ctx_id;
        }
        if (!headers_append_all(&plan->headers, &conn->headers)) return false;
    } else if (!(plan->host = strdup(""))) {
        return false;
    }
    if (!headers_append_all(&plan->headers, &r->headers)) return false;

    // The template's User-Agent unless a header sets one.
    bool has_ua = false;
    for (size_t i = 0; i < plan->headers.count; i++) {
        if (ps4_http_header_name_matches(plan->headers.items[i].name, "User-Agent")) has_ua = true;
    }
    if (!has_ua) {
        char *ua = (tmpl_user_agent && tmpl_user_agent[0]) ? strdup(tmpl_user_agent) : ps4_http_build_user_agent("");
        const bool ok = ua && ps4_http_headers_add(&plan->headers, "User-Agent", ua);
        free(ua);
        if (!ok) return false;
    }
    for (size_t i = 0; i < plan->headers.count; i++) {
        if (ps4_http_header_name_matches(plan->headers.items[i].name, "Content-Type")) {
            if (!(plan->content_type = strdup(plan->headers.items[i].value))) return false;
            break;
        }
    }
    if (postData && size > 0) {
        if (!(plan->body = malloc(size))) return false;
        memcpy(plan->body, postData, size);
        plan->body_size = size;
    }
    return true;
}

int32_t sceHttpSendRequest(int32_t reqId, const void *postData, uint64_t size) {
    pthread_mutex_lock(&g_mu);
    if (!g_inited) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_BEFORE_INIT;
    }
    HttpRequest *r = find_request(reqId);
    if (!r) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_INVALID_ID;
    }
    if (r->state == HTTP_REQ_SENDING || r->state == HTTP_REQ_SENT) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_AFTER_SEND;
    }
    if (r->state == HTTP_REQ_ABORTED) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_ABORTED;
    }
    HttpWorker *w = calloc(1, sizeof(*w));
    if (!w || !build_plan(r, postData, size, &w->plan)) {
        if (w) plan_free(&w->plan);
        free(w);
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }
    w->req = r;
    w->req_id = reqId;
    w->online = ps4_netctl_connected();
    r->state = HTTP_REQ_SENDING;
    r->refs++;
    http_log("request %d dispatched [%s] %s://%s:%u%s", reqId, w->online ? "online" : "offline", w->plan.scheme,
             w->plan.host, (unsigned)w->plan.port, w->plan.path);

    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    const int err = pthread_create(&th, &attr, http_worker_main, w);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        r->state = HTTP_REQ_CREATED;
        r->refs--;
        plan_free(&w->plan);
        free(w);
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceHttpAbortRequest(int32_t reqId) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!(r = find_request(reqId))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (r->state == HTTP_REQ_CREATED || r->state == HTTP_REQ_SENDING) {
        r->state = HTTP_REQ_ABORTED;
        pthread_cond_broadcast(&r->cv);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// Waits until the response of r is in (g_mu held; may release it while waiting). r must be
// referenced by the caller.
static int32_t wait_response_ready(HttpRequest *r) {
    if (r->state == HTTP_REQ_ABORTED) return ORBIS_HTTP_ERROR_ABORTED;
    if (r->state == HTTP_REQ_CREATED) return ORBIS_HTTP_ERROR_BEFORE_SEND;
    if (r->state == HTTP_REQ_SENT) return 0;
    if (r->settings.nonblock) return ORBIS_HTTP_ERROR_EAGAIN;
    while (r->state == HTTP_REQ_SENDING && !g_shutting_down) pthread_cond_wait(&r->cv, &g_mu);
    if (g_shutting_down || r->state == HTTP_REQ_ABORTED) return ORBIS_HTTP_ERROR_ABORTED;
    return 0;
}

// Finds reqId and waits for its response (g_mu held). On success *out is the request with a
// reference the caller drops with request_unref.
static int32_t acquire_ready_request(int32_t reqId, HttpRequest **out) {
    HttpRequest *r = find_request(reqId);
    if (!r) return ORBIS_HTTP_ERROR_INVALID_ID;
    r->refs++;
    const int32_t rc = wait_response_ready(r);
    if (rc != 0) {
        request_unref(r);
        return rc;
    }
    *out = r;
    return 0;
}

int32_t sceHttpReadData(int32_t reqId, void *data, uint64_t size) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!data) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if ((rc = acquire_ready_request(reqId, &r)) == 0) {
        const uint64_t remaining = r->res.body_size - r->res.read_cursor;
        const uint64_t to_copy = size < remaining ? size : remaining;
        if (to_copy > 0) {
            memcpy(data, r->res.body + r->res.read_cursor, to_copy);
            r->res.read_cursor += to_copy;
        }
        rc = (int32_t)to_copy;
        request_unref(r);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpGetStatusCode(int32_t reqId, int32_t *statusCode) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!statusCode) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if ((rc = acquire_ready_request(reqId, &r)) == 0) {
        if (r->res.status_code == 0 && r->last_errno != 0) {
            rc = ORBIS_HTTP_ERROR_BEFORE_SEND; // no status line was received
        } else {
            *statusCode = r->res.status_code;
        }
        request_unref(r);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpGetResponseContentLength(int32_t reqId, int32_t *result, uint64_t *contentLength) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!result || !contentLength) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if ((rc = acquire_ready_request(reqId, &r)) == 0) {
        *result = r->res.content_length_result;
        *contentLength = r->res.content_length;
        request_unref(r);
    } else if (rc == ORBIS_HTTP_ERROR_EAGAIN) {
        rc = ORBIS_HTTP_ERROR_BEFORE_SEND;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpGetAllResponseHeaders(GuestContext *ctx, int32_t reqId, uint64_t *header, uint64_t *headerSize) {
    pthread_mutex_lock(&g_mu);
    int32_t rc;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!header || !headerSize) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if ((rc = acquire_ready_request(reqId, &r)) == 0) {
        if (!r->res.all_headers || r->res.all_headers_size == 0) {
            *header = 0;
            *headerSize = 0;
        } else {
            if (!r->guest_headers && !r->deleted) {
                GuestContext *proc = ctx && ctx->process_ctx ? ctx->process_ctx : ctx;
                const size_t size = r->res.all_headers_size + 1;
                const uint64_t addr = proc ? recomp_vm_alloc(proc, size) : (uint64_t)-1;
                if (addr != (uint64_t)-1 && addr != 0) {
                    memcpy(proc->mem_base + addr, r->res.all_headers, size);
                    r->guest_proc = proc;
                    r->guest_headers = addr;
                    r->guest_headers_size = size;
                }
            }
            if (r->guest_headers) {
                *header = r->guest_headers;
                *headerSize = r->res.all_headers_size;
            } else {
                rc = ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
            }
        }
        request_unref(r);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpGetLastErrno(int32_t reqId, int32_t *errNum) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!errNum) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(r = find_request(reqId))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        *errNum = r->last_errno;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// ---------------------------------------------------------------------------------------------
// Request headers

int32_t sceHttpAddRequestHeader(int32_t id, const char *name, const char *value, int32_t mode) {
    if (name && !is_credential_header(name)) {
        http_log("sceHttpAddRequestHeader id=%d %s: %s mode=%d", id, name, value ? value : "(null)", mode);
    }
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpHeaderList *headers = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (mode != ORBIS_HTTP_HEADER_OVERWRITE && mode != ORBIS_HTTP_HEADER_ADD) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!name || !value) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(headers = resolve_headers(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        if (mode == ORBIS_HTTP_HEADER_OVERWRITE) headers_remove(headers, name);
        if (!ps4_http_headers_add(headers, name, value)) rc = ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpRemoveRequestHeader(int32_t id, const char *name) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpHeaderList *headers = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!name) {
        rc = ORBIS_HTTP_ERROR_NOT_FOUND;
    } else if (!(headers = resolve_headers(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (headers_remove(headers, name) == 0) {
        rc = ORBIS_HTTP_ERROR_NOT_FOUND;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpSetRequestContentLength(int32_t id, uint64_t contentLength) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!(r = find_request(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (r->state != HTTP_REQ_CREATED) {
        rc = ORBIS_HTTP_ERROR_BUSY;
    } else {
        r->content_length = contentLength;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// ---------------------------------------------------------------------------------------------
// Settings of a template, connection or request

// Locks the library and finds the settings of id: 0 with *s set (g_mu held), or the error
// (g_mu released).
static int32_t lock_settings(int32_t id, HttpSettings **s) {
    pthread_mutex_lock(&g_mu);
    if (!g_inited) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_BEFORE_INIT;
    }
    *s = resolve_settings(id);
    if (!*s) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_INVALID_ID;
    }
    return 0;
}

#define HTTP_SETTER(fn, type, field, expr)                     \
    int32_t fn(int32_t id, type value) {                       \
        HttpSettings *s;                                       \
        const int32_t rc = lock_settings(id, &s);              \
        if (rc != 0) return rc;                                \
        s->field = (expr);                                     \
        pthread_mutex_unlock(&g_mu);                           \
        return 0;                                              \
    }

HTTP_SETTER(sceHttpSetConnectTimeOut, uint32_t, connect_timeout_us, value)
HTTP_SETTER(sceHttpSetSendTimeOut, uint32_t, send_timeout_us, value)
HTTP_SETTER(sceHttpSetRecvTimeOut, uint32_t, recv_timeout_us, value)
HTTP_SETTER(sceHttpSetAutoRedirect, int32_t, auto_redirect, value != 0)
HTTP_SETTER(sceHttpSetNonblock, int32_t, nonblock, value != 0)
HTTP_SETTER(sceHttpSetAcceptEncodingGZIPEnabled, int32_t, accept_encoding_gzip, value != 0)
HTTP_SETTER(sceHttpCacheRedirectedConnectionEnabled, int32_t, cache_redirected_connection, value != 0)
HTTP_SETTER(sceHttpSetRecvBlockSize, uint32_t, recv_block_size, value)
HTTP_SETTER(sceHttpSetResponseHeaderMaxSize, uint64_t, response_header_max, value)

#define HTTP_GETTER(fn, field)                                 \
    int32_t fn(int32_t id, int32_t *isEnable) {                \
        pthread_mutex_lock(&g_mu);                             \
        int32_t rc = 0;                                        \
        HttpSettings *s = NULL;                                \
        if (!g_inited) {                                       \
            rc = ORBIS_HTTP_ERROR_BEFORE_INIT;                 \
        } else if (!isEnable) {                                \
            rc = ORBIS_HTTP_ERROR_INVALID_VALUE;               \
        } else if (!(s = resolve_settings(id))) {              \
            rc = ORBIS_HTTP_ERROR_INVALID_ID;                  \
        } else {                                               \
            *isEnable = s->field ? 1 : 0;                      \
        }                                                      \
        pthread_mutex_unlock(&g_mu);                           \
        return rc;                                             \
    }

HTTP_GETTER(sceHttpGetAutoRedirect, auto_redirect)
HTTP_GETTER(sceHttpGetNonblock, nonblock)
HTTP_GETTER(sceHttpGetAcceptEncodingGZIPEnabled, accept_encoding_gzip)

int32_t sceHttpSetResolveTimeOut(int32_t id, uint32_t usec, uint32_t sdkVersion) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpSettings *s = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (sdkVersion >= HTTP_FW_170 && usec <= 999999u) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE; // at least one second from SDK 1.70 on
    } else if (!(s = resolve_settings(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        s->resolve_timeout_us = usec;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpSetResolveRetry(int32_t id, int32_t retry) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpSettings *s = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (retry < 0) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(s = resolve_settings(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        s->resolve_retry = retry;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpSetInflateGZIPEnabled(int32_t id, int32_t isEnable) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpSettings *s = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if ((uint32_t)isEnable >= 2) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(s = resolve_settings(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        s->inflate_gzip = isEnable != 0;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpSetProxy(int32_t id, int32_t httpProxyConf, int32_t wlanProxyConf, const char *host, uint16_t port) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpSettings *s = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!host) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(s = resolve_settings(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        s->proxy_http_conf = httpProxyConf;
        s->proxy_wlan_conf = wlanProxyConf;
        snprintf(s->proxy_host, sizeof(s->proxy_host), "%s", host);
        s->proxy_port = port;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpSetDefaultAcceptEncodingGZIPEnabled(int32_t libhttpCtxId, int32_t isEnable) {
    (void)libhttpCtxId; // a library-wide default, whatever the context
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else {
        g_default_accept_encoding_gzip = isEnable != 0;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpRedirectCacheFlush(int32_t libhttpCtxId) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!find_context(libhttpCtxId)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        redirect_flush_ctx(libhttpCtxId);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// Sets (enable) or clears the HTTPS flags of id, which must lie in valid_mask.
static int32_t https_option(int32_t id, uint32_t sslFlags, uint32_t valid_mask, bool enable) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpSettings *s = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if ((sslFlags & ~valid_mask) != 0) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(s = resolve_settings(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (enable) {
        s->ssl_flags |= sslFlags;
    } else {
        s->ssl_flags &= ~sslFlags;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpsEnableOption(int32_t id, uint32_t sslFlags) {
    return https_option(id, sslFlags, ORBIS_HTTPS_FLAG_PUBLIC_VALID, !https_forced_disabled());
}

int32_t sceHttpsDisableOption(int32_t id, uint32_t sslFlags) {
    return https_option(id, sslFlags, ORBIS_HTTPS_FLAG_PUBLIC_VALID, false);
}

int32_t sceHttpsEnableOptionPrivate(int32_t id, uint32_t sslFlags) {
    return https_option(id, sslFlags, ORBIS_HTTPS_FLAG_PRIVATE_VALID, !https_forced_disabled());
}

int32_t sceHttpsDisableOptionPrivate(int32_t id, uint32_t sslFlags) {
    return https_option(id, sslFlags, ORBIS_HTTPS_FLAG_PRIVATE_VALID, false);
}

// Locks the library and finds context id: the context (g_mu held) or NULL with *rc set (g_mu
// released).
static HttpContext *lock_context(int32_t id, int32_t *rc) {
    pthread_mutex_lock(&g_mu);
    if (!g_inited) {
        *rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else {
        HttpContext *c = find_context(id);
        if (c) return c;
        *rc = ORBIS_HTTP_ERROR_INVALID_ID;
    }
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

// The certificates are libSceSsl data this runtime does not parse: the context is marked, and
// its HTTPS requests skip server verification so the title's private-CA servers are reachable
// (as in shadPS4).
int32_t sceHttpsLoadCert(int32_t libhttpCtxId, int32_t caCertNum, uint64_t caList, uint64_t cert, uint64_t privKey) {
    (void)caCertNum;
    (void)caList;
    (void)cert;
    (void)privKey;
    int32_t rc = 0;
    HttpContext *c = lock_context(libhttpCtxId, &rc);
    if (!c) return rc;
    c->loaded_certs = true;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceHttpsUnloadCert(int32_t libhttpCtxId) {
    int32_t rc = 0;
    HttpContext *c = lock_context(libhttpCtxId, &rc);
    if (!c) return rc;
    c->loaded_certs = false;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

// The CA list is empty: there is no libSceSsl certificate store (shadPS4 likewise).
int32_t sceHttpsGetCaList(int32_t libhttpCtxId, OrbisHttpsCaList *list) {
    int32_t rc = 0;
    if (!lock_context(libhttpCtxId, &rc)) return rc;
    if (!list) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else {
        list->certsNum = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpsFreeCaList(int32_t libhttpCtxId, OrbisHttpsCaList *list) {
    return sceHttpsGetCaList(libhttpCtxId, list);
}

// ---------------------------------------------------------------------------------------------
// Non-blocking processing

int32_t sceHttpCreateEpoll(int32_t libhttpCtxId, uint64_t *eh) {
    int32_t rc = 0;
    if (!lock_context(libhttpCtxId, &rc)) return rc;
    HttpEpoll *e = NULL;
    if (!eh) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(e = calloc(1, sizeof(*e)))) {
        rc = ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    } else {
        e->id = ++g_next_obj_id;
        e->ctx_id = libhttpCtxId;
        e->refs = 1;
        pthread_cond_init(&e->cv, NULL);
        e->next = g_epolls;
        g_epolls = e;
        *eh = (uint64_t)(int64_t)e->id;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpDestroyEpoll(int32_t libhttpCtxId, uint64_t eh) {
    int32_t rc = 0;
    if (!lock_context(libhttpCtxId, &rc)) return rc;
    if (!eh) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
        for (HttpEpoll **pp = &g_epolls; *pp; pp = &(*pp)->next) {
            if ((*pp)->id == (int32_t)eh) {
                HttpEpoll *e = *pp;
                *pp = e->next;
                e->destroyed = true;
                pthread_cond_broadcast(&e->cv);
                epoll_unref(e);
                rc = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpSetEpoll(int32_t id, uint64_t eh, uint64_t userArg) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    int32_t *epoll_id = NULL;
    uint64_t *user_arg = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!find_epoll((int32_t)eh)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else if (!resolve_epoll_binding(id, &epoll_id, &user_arg)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        *epoll_id = (int32_t)eh;
        *user_arg = userArg;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpGetEpoll(int32_t id, uint64_t *eh, uint64_t *userArg) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    int32_t *epoll_id = NULL;
    uint64_t *user_arg = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!eh) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!resolve_epoll_binding(id, &epoll_id, &user_arg)) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        *eh = (uint64_t)(int64_t)*epoll_id;
        if (userArg) *userArg = *user_arg;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// Only a request's binding can be cleared.
int32_t sceHttpUnsetEpoll(int32_t id) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpRequest *r = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!(r = find_request(id))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        r->epoll_id = 0;
        r->epoll_user_arg = 0;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceHttpAbortWaitRequest(uint64_t eh) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    HttpEpoll *e = NULL;
    if (!g_inited) {
        rc = ORBIS_HTTP_ERROR_BEFORE_INIT;
    } else if (!eh) {
        rc = ORBIS_HTTP_ERROR_INVALID_VALUE;
    } else if (!(e = find_epoll((int32_t)eh))) {
        rc = ORBIS_HTTP_ERROR_INVALID_ID;
    } else {
        e->abort_requested = true;
        pthread_cond_broadcast(&e->cv);
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

static int32_t epoll_drain(HttpEpoll *e, OrbisHttpNBEvent *nbev, int32_t maxevents) {
    int32_t n = 0;
    while (n < maxevents && e->count > 0) nbev[n++] = epoll_pop(e);
    return n;
}

int32_t sceHttpWaitRequest(uint64_t eh, OrbisHttpNBEvent *nbev, int32_t maxevents, int32_t timeout) {
    pthread_mutex_lock(&g_mu);
    if (!g_inited) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_BEFORE_INIT;
    }
    if (maxevents <= 0 || !eh || !nbev) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_INVALID_VALUE;
    }
    HttpEpoll *e = find_epoll((int32_t)eh);
    if (!e) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_INVALID_ID;
    }
    if (e->abort_requested) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_HTTP_ERROR_ABORTED;
    }
    int32_t n = epoll_drain(e, nbev, maxevents);
    if (n > 0 || timeout == 0 || e->destroyed || g_shutting_down) {
        pthread_mutex_unlock(&g_mu);
        return n;
    }

    e->refs++;
    if (timeout < 0) {
        while (e->count == 0 && !e->destroyed && !e->abort_requested && !g_shutting_down) {
            pthread_cond_wait(&e->cv, &g_mu);
        }
    } else {
        struct timeval now;
        gettimeofday(&now, NULL);
        const uint64_t deadline_us = (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_usec + (uint64_t)timeout;
        const struct timespec deadline = {.tv_sec = (time_t)(deadline_us / 1000000u),
                                          .tv_nsec = (long)(deadline_us % 1000000u) * 1000};
        while (e->count == 0 && !e->destroyed && !e->abort_requested && !g_shutting_down) {
            if (pthread_cond_timedwait(&e->cv, &g_mu, &deadline) != 0) break;
        }
    }
    int32_t rc;
    if (e->abort_requested) {
        rc = ORBIS_HTTP_ERROR_ABORTED;
    } else {
        rc = epoll_drain(e, nbev, maxevents);
    }
    epoll_unref(e);
    pthread_mutex_unlock(&g_mu);
    return rc;
}

// ---------------------------------------------------------------------------------------------
// Shims

static inline void *g2h(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

static inline const char *g2s(const GuestContext *ctx, uint64_t addr) {
    return (const char *)g2h(ctx, addr);
}

// The guest's OrbisHttpUriElement: guest addresses in the pointer fields.
typedef struct OrbisHttpUriElementGuest {
    uint8_t opaque;
    uint8_t pad[7];
    uint64_t scheme;
    uint64_t username;
    uint64_t password;
    uint64_t hostname;
    uint64_t path;
    uint64_t query;
    uint64_t fragment;
    uint16_t port;
    uint8_t reserved[10];
} OrbisHttpUriElementGuest;

_Static_assert(sizeof(OrbisHttpUriElementGuest) == 80, "OrbisHttpUriElement is 80 bytes");
_Static_assert(sizeof(OrbisHttpNBEvent) == 24, "OrbisHttpNBEvent is 24 bytes");
_Static_assert(sizeof(OrbisHttpsCaList) == 16, "OrbisHttpsCaList is 16 bytes");

static inline uint64_t h2g(const GuestContext *ctx, const void *p) {
    return p ? (uint64_t)((const uint8_t *)p - ctx->mem_base) : 0;
}

static void uri_element_to_host(const GuestContext *ctx, const OrbisHttpUriElementGuest *g, OrbisHttpUriElement *h) {
    memset(h, 0, sizeof(*h));
    h->opaque = g->opaque != 0;
    h->scheme = (char *)g2h(ctx, g->scheme);
    h->username = (char *)g2h(ctx, g->username);
    h->password = (char *)g2h(ctx, g->password);
    h->hostname = (char *)g2h(ctx, g->hostname);
    h->path = (char *)g2h(ctx, g->path);
    h->query = (char *)g2h(ctx, g->query);
    h->fragment = (char *)g2h(ctx, g->fragment);
    h->port = g->port;
    memcpy(h->reserved, g->reserved, sizeof(h->reserved));
}

static void uri_element_to_guest(const GuestContext *ctx, const OrbisHttpUriElement *h, OrbisHttpUriElementGuest *g) {
    memset(g, 0, sizeof(*g));
    g->opaque = h->opaque ? 1 : 0;
    g->scheme = h2g(ctx, h->scheme);
    g->username = h2g(ctx, h->username);
    g->password = h2g(ctx, h->password);
    g->hostname = h2g(ctx, h->hostname);
    g->path = h2g(ctx, h->path);
    g->query = h2g(ctx, h->query);
    g->fragment = h2g(ctx, h->fragment);
    g->port = h->port;
    memcpy(g->reserved, h->reserved, sizeof(g->reserved));
}

#define RET(expr)                                  \
    do {                                           \
        ctx->rax = (uint64_t)(int64_t)(expr);      \
        SHIM_RETURN();                             \
    } while (0)

void shim_sceHttpInit(GuestContext *ctx) {
    RET(sceHttpInit((int32_t)ctx->rdi, (int32_t)ctx->rsi, ctx->rdx));
}

void shim_sceHttpTerm(GuestContext *ctx) {
    RET(sceHttpTerm((int32_t)ctx->rdi));
}

void shim_sceHttpCreateTemplate(GuestContext *ctx) {
    RET(sceHttpCreateTemplate((int32_t)ctx->rdi, g2s(ctx, ctx->rsi), (int32_t)ctx->rdx, (int32_t)ctx->rcx));
}

void shim_sceHttpDeleteTemplate(GuestContext *ctx) {
    RET(sceHttpDeleteTemplate((int32_t)ctx->rdi));
}

void shim_sceHttpCreateConnection(GuestContext *ctx) {
    RET(sceHttpCreateConnection((int32_t)ctx->rdi, g2s(ctx, ctx->rsi), g2s(ctx, ctx->rdx), (uint16_t)ctx->rcx,
                                (int32_t)ctx->r8));
}

void shim_sceHttpCreateConnectionWithURL(GuestContext *ctx) {
    RET(sceHttpCreateConnectionWithURL((int32_t)ctx->rdi, g2s(ctx, ctx->rsi), (uint8_t)ctx->rdx != 0));
}

void shim_sceHttpDeleteConnection(GuestContext *ctx) {
    RET(sceHttpDeleteConnection((int32_t)ctx->rdi));
}

void shim_sceHttpCreateRequest(GuestContext *ctx) {
    RET(sceHttpCreateRequest((int32_t)ctx->rdi, (int32_t)ctx->rsi, g2s(ctx, ctx->rdx), ctx->rcx));
}

void shim_sceHttpCreateRequest2(GuestContext *ctx) {
    RET(sceHttpCreateRequest2((int32_t)ctx->rdi, g2s(ctx, ctx->rsi), g2s(ctx, ctx->rdx), ctx->rcx));
}

void shim_sceHttpCreateRequestWithURL(GuestContext *ctx) {
    RET(sceHttpCreateRequestWithURL((int32_t)ctx->rdi, (int32_t)ctx->rsi, g2s(ctx, ctx->rdx), ctx->rcx));
}

void shim_sceHttpCreateRequestWithURL2(GuestContext *ctx) {
    RET(sceHttpCreateRequestWithURL2((int32_t)ctx->rdi, g2s(ctx, ctx->rsi), g2s(ctx, ctx->rdx), ctx->rcx));
}

void shim_sceHttpDeleteRequest(GuestContext *ctx) {
    RET(sceHttpDeleteRequest((int32_t)ctx->rdi));
}

void shim_sceHttpSendRequest(GuestContext *ctx) {
    RET(sceHttpSendRequest((int32_t)ctx->rdi, g2h(ctx, ctx->rsi), ctx->rdx));
}

void shim_sceHttpAbortRequest(GuestContext *ctx) {
    RET(sceHttpAbortRequest((int32_t)ctx->rdi));
}

void shim_sceHttpAbortRequestForce(GuestContext *ctx) {
    RET(sceHttpAbortRequest((int32_t)ctx->rdi));
}

void shim_sceHttpReadData(GuestContext *ctx) {
    RET(sceHttpReadData((int32_t)ctx->rdi, g2h(ctx, ctx->rsi), ctx->rdx));
}

void shim_sceHttpGetStatusCode(GuestContext *ctx) {
    RET(sceHttpGetStatusCode((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpGetResponseContentLength(GuestContext *ctx) {
    RET(sceHttpGetResponseContentLength((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi),
                                        (uint64_t *)g2h(ctx, ctx->rdx)));
}

void shim_sceHttpGetAllResponseHeaders(GuestContext *ctx) {
    RET(sceHttpGetAllResponseHeaders(ctx, (int32_t)ctx->rdi, (uint64_t *)g2h(ctx, ctx->rsi),
                                     (uint64_t *)g2h(ctx, ctx->rdx)));
}

void shim_sceHttpGetLastErrno(GuestContext *ctx) {
    RET(sceHttpGetLastErrno((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpAddRequestHeader(GuestContext *ctx) {
    RET(sceHttpAddRequestHeader((int32_t)ctx->rdi, g2s(ctx, ctx->rsi), g2s(ctx, ctx->rdx), (int32_t)ctx->rcx));
}

void shim_sceHttpRemoveRequestHeader(GuestContext *ctx) {
    RET(sceHttpRemoveRequestHeader((int32_t)ctx->rdi, g2s(ctx, ctx->rsi)));
}

void shim_sceHttpSetRequestContentLength(GuestContext *ctx) {
    RET(sceHttpSetRequestContentLength((int32_t)ctx->rdi, ctx->rsi));
}

void shim_sceHttpSetConnectTimeOut(GuestContext *ctx) {
    RET(sceHttpSetConnectTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpSetSendTimeOut(GuestContext *ctx) {
    RET(sceHttpSetSendTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpSetRecvTimeOut(GuestContext *ctx) {
    RET(sceHttpSetRecvTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpSetResolveTimeOut(GuestContext *ctx) {
    RET(sceHttpSetResolveTimeOut((int32_t)ctx->rdi, (uint32_t)ctx->rsi, ps4_compiled_sdk_version(ctx)));
}

void shim_sceHttpSetResolveRetry(GuestContext *ctx) {
    RET(sceHttpSetResolveRetry((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpSetAutoRedirect(GuestContext *ctx) {
    RET(sceHttpSetAutoRedirect((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpGetAutoRedirect(GuestContext *ctx) {
    RET(sceHttpGetAutoRedirect((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpSetNonblock(GuestContext *ctx) {
    RET(sceHttpSetNonblock((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpGetNonblock(GuestContext *ctx) {
    RET(sceHttpGetNonblock((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpTrySetNonblock(GuestContext *ctx) {
    RET(sceHttpSetNonblock((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpTryGetNonblock(GuestContext *ctx) {
    RET(sceHttpGetNonblock((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpSetInflateGZIPEnabled(GuestContext *ctx) {
    RET(sceHttpSetInflateGZIPEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpSetAcceptEncodingGZIPEnabled(GuestContext *ctx) {
    RET(sceHttpSetAcceptEncodingGZIPEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpGetAcceptEncodingGZIPEnabled(GuestContext *ctx) {
    RET(sceHttpGetAcceptEncodingGZIPEnabled((int32_t)ctx->rdi, (int32_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpSetDefaultAcceptEncodingGZIPEnabled(GuestContext *ctx) {
    RET(sceHttpSetDefaultAcceptEncodingGZIPEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpCacheRedirectedConnectionEnabled(GuestContext *ctx) {
    RET(sceHttpCacheRedirectedConnectionEnabled((int32_t)ctx->rdi, (int32_t)ctx->rsi));
}

void shim_sceHttpRedirectCacheFlush(GuestContext *ctx) {
    RET(sceHttpRedirectCacheFlush((int32_t)ctx->rdi));
}

void shim_sceHttpSetRecvBlockSize(GuestContext *ctx) {
    RET(sceHttpSetRecvBlockSize((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpSetResponseHeaderMaxSize(GuestContext *ctx) {
    RET(sceHttpSetResponseHeaderMaxSize((int32_t)ctx->rdi, ctx->rsi));
}

void shim_sceHttpSetProxy(GuestContext *ctx) {
    RET(sceHttpSetProxy((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, g2s(ctx, ctx->rcx),
                        (uint16_t)ctx->r8));
}

void shim_sceHttpsEnableOption(GuestContext *ctx) {
    RET(sceHttpsEnableOption((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpsDisableOption(GuestContext *ctx) {
    RET(sceHttpsDisableOption((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpsEnableOptionPrivate(GuestContext *ctx) {
    RET(sceHttpsEnableOptionPrivate((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpsDisableOptionPrivate(GuestContext *ctx) {
    RET(sceHttpsDisableOptionPrivate((int32_t)ctx->rdi, (uint32_t)ctx->rsi));
}

void shim_sceHttpsLoadCert(GuestContext *ctx) {
    RET(sceHttpsLoadCert((int32_t)ctx->rdi, (int32_t)ctx->rsi, ctx->rdx, ctx->rcx, ctx->r8));
}

void shim_sceHttpsUnloadCert(GuestContext *ctx) {
    RET(sceHttpsUnloadCert((int32_t)ctx->rdi));
}

void shim_sceHttpsGetCaList(GuestContext *ctx) {
    RET(sceHttpsGetCaList((int32_t)ctx->rdi, (OrbisHttpsCaList *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpsFreeCaList(GuestContext *ctx) {
    RET(sceHttpsFreeCaList((int32_t)ctx->rdi, (OrbisHttpsCaList *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpCreateEpoll(GuestContext *ctx) {
    RET(sceHttpCreateEpoll((int32_t)ctx->rdi, (uint64_t *)g2h(ctx, ctx->rsi)));
}

void shim_sceHttpDestroyEpoll(GuestContext *ctx) {
    RET(sceHttpDestroyEpoll((int32_t)ctx->rdi, ctx->rsi));
}

void shim_sceHttpSetEpoll(GuestContext *ctx) {
    RET(sceHttpSetEpoll((int32_t)ctx->rdi, ctx->rsi, ctx->rdx));
}

void shim_sceHttpGetEpoll(GuestContext *ctx) {
    RET(sceHttpGetEpoll((int32_t)ctx->rdi, (uint64_t *)g2h(ctx, ctx->rsi), (uint64_t *)g2h(ctx, ctx->rdx)));
}

void shim_sceHttpUnsetEpoll(GuestContext *ctx) {
    RET(sceHttpUnsetEpoll((int32_t)ctx->rdi));
}

void shim_sceHttpAbortWaitRequest(GuestContext *ctx) {
    RET(sceHttpAbortWaitRequest(ctx->rdi));
}

void shim_sceHttpWaitRequest(GuestContext *ctx) {
    RET(sceHttpWaitRequest(ctx->rdi, (OrbisHttpNBEvent *)g2h(ctx, ctx->rsi), (int32_t)ctx->rdx, (int32_t)ctx->rcx));
}

// int sceHttpParseStatusLine(const char *statusLine, size_t lineLen, int *httpMajorVer,
//                            int *httpMinorVer, int *responseCode, const char **reasonPhrase,
//                            size_t *phraseLen): phraseLen is the first stack argument.
void shim_sceHttpParseStatusLine(GuestContext *ctx) {
    uint64_t phrase_len_g = 0;
    if (ctx->rsp + 16 <= ctx->mem_size) phrase_len_g = *(const uint64_t *)(ctx->mem_base + ctx->rsp + 8);
    uint64_t *reason_out = (uint64_t *)g2h(ctx, ctx->r9);
    const char *reason = NULL;
    const int32_t rc = sceHttpParseStatusLine(g2s(ctx, ctx->rdi), ctx->rsi, (int32_t *)g2h(ctx, ctx->rdx),
                                              (int32_t *)g2h(ctx, ctx->rcx), (int32_t *)g2h(ctx, ctx->r8),
                                              reason_out ? &reason : NULL, (uint64_t *)g2h(ctx, phrase_len_g));
    if (rc >= 0 && reason_out) *reason_out = h2g(ctx, reason);
    RET(rc);
}

// int sceHttpParseResponseHeader(const char *header, size_t headerLen, const char *fieldStr,
//                                const char **fieldValue, size_t *valueLen)
void shim_sceHttpParseResponseHeader(GuestContext *ctx) {
    uint64_t *value_out = (uint64_t *)g2h(ctx, ctx->rcx);
    const char *value = NULL;
    const int32_t rc = sceHttpParseResponseHeader(g2s(ctx, ctx->rdi), ctx->rsi, g2s(ctx, ctx->rdx),
                                                  value_out ? &value : NULL, (uint64_t *)g2h(ctx, ctx->r8));
    if (rc >= 0 && value_out) *value_out = h2g(ctx, value);
    RET(rc);
}

void shim_sceHttpUriParse(GuestContext *ctx) {
    OrbisHttpUriElementGuest *out_g = (OrbisHttpUriElementGuest *)g2h(ctx, ctx->rdi);
    OrbisHttpUriElement out;
    const int32_t rc = sceHttpUriParse(out_g ? &out : NULL, g2s(ctx, ctx->rsi), g2h(ctx, ctx->rdx),
                                       (uint64_t *)g2h(ctx, ctx->rcx), ctx->r8);
    if (rc == 0 && out_g && ctx->rdx) uri_element_to_guest(ctx, &out, out_g);
    RET(rc);
}

void shim_sceHttpUriBuild(GuestContext *ctx) {
    const OrbisHttpUriElementGuest *src_g = (const OrbisHttpUriElementGuest *)g2h(ctx, ctx->rcx);
    OrbisHttpUriElement src;
    if (src_g) uri_element_to_host(ctx, src_g, &src);
    RET(sceHttpUriBuild((char *)g2h(ctx, ctx->rdi), (uint64_t *)g2h(ctx, ctx->rsi), ctx->rdx, src_g ? &src : NULL,
                        (uint32_t)ctx->r8));
}

void shim_sceHttpUriEscape(GuestContext *ctx) {
    RET(sceHttpUriEscape((char *)g2h(ctx, ctx->rdi), (uint64_t *)g2h(ctx, ctx->rsi), ctx->rdx, g2s(ctx, ctx->rcx)));
}

void shim_sceHttpUriUnescape(GuestContext *ctx) {
    RET(sceHttpUriUnescape((char *)g2h(ctx, ctx->rdi), (uint64_t *)g2h(ctx, ctx->rsi), ctx->rdx, g2s(ctx, ctx->rcx)));
}

void shim_sceHttpUriMerge(GuestContext *ctx) {
    RET(sceHttpUriMerge((char *)g2h(ctx, ctx->rdi), g2s(ctx, ctx->rsi), g2s(ctx, ctx->rdx),
                        (uint64_t *)g2h(ctx, ctx->rcx), ctx->r8, (uint32_t)ctx->r9));
}

void shim_sceHttpUriSweepPath(GuestContext *ctx) {
    RET(sceHttpUriSweepPath((char *)g2h(ctx, ctx->rdi), g2s(ctx, ctx->rsi), ctx->rdx));
}

// Stubs in shadPS4 as well: cookies, authentication, the request callbacks, debug output and
// statistics, SSL version and error queries.
PS4_HLE_STUB(sceHttpAddCookie)
PS4_HLE_STUB(sceHttpAddQuery)
PS4_HLE_STUB(sceHttpAddRequestHeaderRaw)
PS4_HLE_STUB(sceHttpAuthCacheExport)
PS4_HLE_STUB(sceHttpAuthCacheFlush)
PS4_HLE_STUB(sceHttpAuthCacheImport)
PS4_HLE_STUB(sceHttpCookieExport)
PS4_HLE_STUB(sceHttpCookieFlush)
PS4_HLE_STUB(sceHttpCookieImport)
PS4_HLE_STUB(sceHttpDbgEnableProfile)
PS4_HLE_STUB(sceHttpDbgGetConnectionStat)
PS4_HLE_STUB(sceHttpDbgGetRequestStat)
PS4_HLE_STUB(sceHttpDbgSetPrintf)
PS4_HLE_STUB(sceHttpDbgShowConnectionStat)
PS4_HLE_STUB(sceHttpDbgShowMemoryPoolStat)
PS4_HLE_STUB(sceHttpDbgShowRequestStat)
PS4_HLE_STUB(sceHttpDbgShowStat)
PS4_HLE_STUB(sceHttpGetAuthEnabled)
PS4_HLE_STUB(sceHttpGetConnectionStat)
PS4_HLE_STUB(sceHttpGetCookie)
PS4_HLE_STUB(sceHttpGetCookieEnabled)
PS4_HLE_STUB(sceHttpGetCookieStats)
PS4_HLE_STUB(sceHttpGetEpollId)
PS4_HLE_STUB(sceHttpGetMemoryPoolStats)
PS4_HLE_STUB(sceHttpGetRegisteredCtxIds)
PS4_HLE_STUB(sceHttpRequestGetAllHeaders)
PS4_HLE_STUB(sceHttpSetAuthEnabled)
PS4_HLE_STUB(sceHttpSetAuthInfoCallback)
PS4_HLE_STUB(sceHttpSetChunkedTransferEnabled)
PS4_HLE_STUB(sceHttpSetCookieEnabled)
PS4_HLE_STUB(sceHttpSetCookieMaxNum)
PS4_HLE_STUB(sceHttpSetCookieMaxNumPerDomain)
PS4_HLE_STUB(sceHttpSetCookieMaxSize)
PS4_HLE_STUB(sceHttpSetCookieRecvCallback)
PS4_HLE_STUB(sceHttpSetCookieSendCallback)
PS4_HLE_STUB(sceHttpSetCookieTotalMaxSize)
PS4_HLE_STUB(sceHttpSetDelayBuildRequestEnabled)
PS4_HLE_STUB(sceHttpSetEpollId)
PS4_HLE_STUB(sceHttpSetHttp09Enabled)
PS4_HLE_STUB(sceHttpSetPolicyOption)
PS4_HLE_STUB(sceHttpSetPriorityOption)
PS4_HLE_STUB(sceHttpSetRedirectCallback)
PS4_HLE_STUB(sceHttpSetRequestStatusCallback)
PS4_HLE_STUB(sceHttpSetSocketCreationCallback)
PS4_HLE_STUB(sceHttpsGetSslError)
PS4_HLE_STUB(sceHttpsSetMinSslVersion)
PS4_HLE_STUB(sceHttpsSetSslCallback)
PS4_HLE_STUB(sceHttpsSetSslVersion)
PS4_HLE_STUB(sceHttpUriCopy)
