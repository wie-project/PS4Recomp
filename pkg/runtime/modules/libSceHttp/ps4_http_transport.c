// SPDX-License-Identifier: GPL-2.0-or-later

// The network side of libSceHttp (shadPS4 network/http.cpp RunRealHttpRequest), over the
// system libcurl in place of shadPS4's cpp-httplib.
//
// Redirects are followed here, not by libcurl, with the console's rules: 300-303 and 307, at
// most 5 times, never for a POST unless the answer is 303; a 303 turns the method into GET
// (unless it was HEAD) and drops the body with its Content-Type and Content-Length headers; a
// cross-host redirect rewrites a Host header the title set. A 301 is remembered per library
// context and replayed without asking the server again (sceHttpCacheRedirectedConnectionEnabled,
// sceHttpRedirectCacheFlush). A gzip-encoded body is inflated when the request's inflate setting
// is on (the default), and stays as received when it does not inflate.
//
// Timeouts: the connect timeout bounds the connection (30 s when unset); the receive and send
// timeouts (120 s when unset) bound how long the transfer may stall, which is what cpp-httplib's
// read and write timeouts do. Server certificates are verified unless the request cleared
// ORBIS_HTTPS_FLAG_SERVER_VERIFY, its context loaded its own certificates (sceHttpsLoadCert,
// which this runtime cannot hand to the TLS layer) or PS4_HTTPS_DISABLED=1.
#include "ps4_http.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#define HTTP_MAX_REDIRECTS 5

static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;

static void curl_init_once(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

typedef struct ByteBuf {
    uint8_t *data;
    size_t size;
    size_t cap;
    bool failed;
} ByteBuf;

static void buf_append(ByteBuf *b, const void *p, size_t n) {
    if (b->failed || n == 0) return;
    if (b->size + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->size + n + 1) cap *= 2;
        uint8_t *d = realloc(b->data, cap);
        if (!d) {
            b->failed = true;
            return;
        }
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->size, p, n);
    b->size += n;
    b->data[b->size] = 0;
}

static size_t on_body(char *ptr, size_t size, size_t nmemb, void *user) {
    ByteBuf *b = user;
    buf_append(b, ptr, size * nmemb);
    return b->failed ? 0 : size * nmemb;
}

// Response header lines of the last response ("HTTP/..." starts a new one: interim 1xx answers
// are dropped).
static size_t on_header(char *ptr, size_t size, size_t nmemb, void *user) {
    HttpHeaderList *list = user;
    const size_t n = size * nmemb;
    if (n >= 5 && strncmp(ptr, "HTTP/", 5) == 0) {
        ps4_http_headers_free(list);
        return n;
    }
    const char *colon = memchr(ptr, ':', n);
    if (!colon || colon == ptr) return n;
    const char *v = colon + 1;
    const char *end = ptr + n;
    while (v < end && (*v == ' ' || *v == '\t')) v++;
    while (end > v && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) end--;
    char *name = strndup(ptr, (size_t)(colon - ptr));
    char *value = strndup(v, (size_t)(end - v));
    const bool ok = name && value && ps4_http_headers_add(list, name, value);
    free(name);
    free(value);
    return ok ? n : 0;
}

static const char *find_header(const HttpHeaderList *list, const char *name) {
    for (size_t i = 0; i < list->count; i++) {
        if (ps4_http_header_name_matches(list->items[i].name, name)) return list->items[i].value;
    }
    return NULL;
}

static int32_t translate_curl_error(CURLcode rc) {
    switch (rc) {
    case CURLE_OK:
        return 0;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
        return ORBIS_HTTP_ERROR_RESOLVER_ENOHOST; // no connection to the host
    case CURLE_OPERATION_TIMEDOUT:
        return ORBIS_HTTP_ERROR_TIMEOUT;
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
        return ORBIS_HTTP_ERROR_BROKEN; // the stream broke or the peer closed early
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_PEER_FAILED_VERIFICATION:
        return ORBIS_HTTP_ERROR_SSL;
    case CURLE_COULDNT_RESOLVE_PROXY:
        return ORBIS_HTTP_ERROR_PROXY;
    case CURLE_ABORTED_BY_CALLBACK:
        return ORBIS_HTTP_ERROR_ABORTED;
    case CURLE_TOO_MANY_REDIRECTS:
        return ORBIS_HTTP_ERROR_NETWORK;
    default:
        return ORBIS_HTTP_ERROR_UNKNOWN;
    }
}

static long timeout_seconds(uint32_t us, long default_s) {
    if (us == 0) return default_s;
    const uint64_t secs = ((uint64_t)us + 999999u) / 1000000u;
    return secs == 0 ? 1 : (long)secs;
}

// Inflates a gzip body (header, deflate stream, trailer). Returns false when it is not one.
static bool inflate_gzip(const uint8_t *in, size_t in_size, uint8_t **out, size_t *out_size) {
    *out = NULL;
    *out_size = 0;
    if (in_size == 0) return true;
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, 15 + 16) != Z_OK) return false;
    strm.next_in = (Bytef *)(uintptr_t)in;
    strm.avail_in = (uInt)in_size;
    size_t cap = in_size * 3 > 4096 ? in_size * 3 : 4096;
    uint8_t *buf = malloc(cap);
    size_t produced = 0;
    bool ok = buf != NULL;
    while (ok) {
        if (produced == cap) {
            uint8_t *b = realloc(buf, cap * 2);
            if (!b) {
                ok = false;
                break;
            }
            buf = b;
            cap *= 2;
        }
        strm.next_out = buf + produced;
        strm.avail_out = (uInt)(cap - produced);
        const uInt before = strm.avail_out;
        const int ret = inflate(&strm, Z_NO_FLUSH);
        produced += before - strm.avail_out;
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK || (strm.avail_in == 0 && strm.avail_out > 0)) ok = false;
    }
    inflateEnd(&strm);
    if (!ok) {
        free(buf);
        return false;
    }
    *out = buf;
    *out_size = produced;
    return true;
}

static bool is_followable_redirect(int status, int32_t method) {
    if (!((status >= 300 && status <= 303) || status == 307)) return false;
    return !(method == ORBIS_HTTP_METHOD_POST && status != 303);
}

// A Location resolved against the current request: an absolute http(s) URL or an absolute path.
typedef struct ResolvedRedirect {
    char scheme[8];
    char *host;
    uint16_t port;
    char *path;
} ResolvedRedirect;

static bool resolve_location(const HttpSendPlan *plan, const char *location, ResolvedRedirect *out) {
    memset(out, 0, sizeof(*out));
    if (!location || !location[0]) return false;
    const char *scheme_end = strstr(location, "://");
    if (scheme_end) {
        const size_t scheme_len = (size_t)(scheme_end - location);
        if (scheme_len == 4 && strncasecmp(location, "http", 4) == 0) {
            snprintf(out->scheme, sizeof(out->scheme), "http");
        } else if (scheme_len == 5 && strncasecmp(location, "https", 5) == 0) {
            snprintf(out->scheme, sizeof(out->scheme), "https");
        } else {
            return false;
        }
        const char *authority = scheme_end + 3;
        if (!*authority) return false;
        const char *path = strchr(authority, '/');
        const size_t auth_len = path ? (size_t)(path - authority) : strlen(authority);
        if (auth_len == 0) return false;
        const char *colon = memchr(authority, ':', auth_len);
        if (colon) {
            char digits[16];
            const size_t dlen = auth_len - (size_t)(colon + 1 - authority);
            if (dlen == 0 || dlen >= sizeof(digits)) return false;
            memcpy(digits, colon + 1, dlen);
            digits[dlen] = 0;
            char *endp = NULL;
            const unsigned long p = strtoul(digits, &endp, 10);
            if (endp == digits || p == 0 || p > 65535) return false;
            out->port = (uint16_t)p;
            out->host = strndup(authority, (size_t)(colon - authority));
        } else {
            out->port = strcmp(out->scheme, "https") == 0 ? 443 : 80;
            out->host = strndup(authority, auth_len);
        }
        out->path = strdup(path ? path : "/");
    } else if (location[0] == '/') {
        snprintf(out->scheme, sizeof(out->scheme), "%s", plan->scheme);
        out->host = strdup(plan->host);
        out->port = plan->port;
        out->path = strdup(location);
    } else {
        return false;
    }
    if (!out->host || !out->path) {
        free(out->host);
        free(out->path);
        return false;
    }
    return true;
}

static char *cache_key(const HttpSendPlan *plan) {
    char *key = NULL;
    if (asprintf(&key, "%s://%s:%u%s", plan->scheme, plan->host, (unsigned)plan->port, plan->path) < 0) return NULL;
    return key;
}

static bool is_default_port(const char *scheme, uint16_t port) {
    return (strcmp(scheme, "https") == 0 && port == 443) || (strcmp(scheme, "http") == 0 && port == 80);
}

// Points a Host header the title set at the plan's current authority.
static void rewrite_host_header(HttpSendPlan *plan) {
    char *value = NULL;
    int n;
    if (is_default_port(plan->scheme, plan->port)) {
        n = asprintf(&value, "%s", plan->host);
    } else {
        n = asprintf(&value, "%s:%u", plan->host, (unsigned)plan->port);
    }
    if (n < 0) return;
    for (size_t i = 0; i < plan->headers.count; i++) {
        if (ps4_http_header_name_matches(plan->headers.items[i].name, "Host")) {
            char *v = strdup(value);
            if (v) {
                free(plan->headers.items[i].value);
                plan->headers.items[i].value = v;
            }
        }
    }
    free(value);
}

static void drop_headers(HttpHeaderList *list, const char *a, const char *b) {
    size_t kept = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (ps4_http_header_name_matches(list->items[i].name, a) || ps4_http_header_name_matches(list->items[i].name, b)) {
            free(list->items[i].name);
            free(list->items[i].value);
        } else {
            list->items[kept++] = list->items[i];
        }
    }
    list->count = kept;
}

static struct curl_slist *slist_add(struct curl_slist *list, const char *line, bool *ok) {
    struct curl_slist *l = curl_slist_append(list, line);
    if (!l) *ok = false;
    return l ? l : list;
}

// The request headers as libcurl lines. A value is sent without surrounding blanks; an empty
// one as "Name;" (libcurl's form for an empty header). libcurl's own Expect and default
// Content-Type are suppressed so the request carries only what the title asked for.
static struct curl_slist *build_header_lines(const HttpSendPlan *plan, bool *ok) {
    struct curl_slist *list = NULL;
    bool has_accept_encoding = false;
    for (size_t i = 0; i < plan->headers.count && *ok; i++) {
        const char *name = plan->headers.items[i].name;
        const char *v = plan->headers.items[i].value;
        if (ps4_http_header_name_matches(name, "Accept-Encoding")) has_accept_encoding = true;
        while (*v == ' ' || *v == '\t') v++;
        size_t vlen = strlen(v);
        while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) vlen--;
        char *line = NULL;
        const int n = vlen ? asprintf(&line, "%s: %.*s", name, (int)vlen, v) : asprintf(&line, "%s;", name);
        if (n < 0) {
            *ok = false;
            break;
        }
        list = slist_add(list, line, ok);
        free(line);
    }
    if (*ok && plan->settings.accept_encoding_gzip && !has_accept_encoding) {
        list = slist_add(list, "Accept-Encoding: gzip", ok);
    }
    if (*ok) list = slist_add(list, "Expect:", ok);
    if (*ok && !plan->content_type) list = slist_add(list, "Content-Type:", ok);
    return list;
}

static void set_method(CURL *c, const HttpSendPlan *plan) {
    const char *body = plan->body ? (const char *)plan->body : "";
    switch (plan->method) {
    case ORBIS_HTTP_METHOD_GET:
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
        break;
    case ORBIS_HTTP_METHOD_POST:
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)plan->body_size);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        break;
    case ORBIS_HTTP_METHOD_HEAD:
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        break;
    case ORBIS_HTTP_METHOD_OPTIONS:
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "OPTIONS");
        break;
    case ORBIS_HTTP_METHOD_PUT:
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)plan->body_size);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
        break;
    case ORBIS_HTTP_METHOD_DELETE:
    case ORBIS_HTTP_METHOD_CUSTOM:
        if (plan->body_size > 0) {
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)plan->body_size);
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        }
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST,
                         plan->method == ORBIS_HTTP_METHOD_DELETE
                             ? "DELETE"
                             : (plan->method_str && plan->method_str[0] ? plan->method_str : "GET"));
        break;
    default:
        // TRACE: not sent as such (as in shadPS4), a GET goes out instead.
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
        break;
    }
}

static bool https_forced_disabled(void) {
    const char *e = getenv("PS4_HTTPS_DISABLED");
    return e && strcmp(e, "1") == 0;
}

// Builds the "HTTP/1.1 <code> <reason>\r\n" + "Name: Value\r\n"... + "\r\n" blob.
static bool build_headers_blob(int status, const HttpHeaderList *headers, HttpResponse *res) {
    ByteBuf b = {0};
    char line[64];
    const char *reason = ps4_http_status_reason(status);
    int n = reason ? snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", status, reason)
                   : snprintf(line, sizeof(line), "HTTP/1.1 %d %d\r\n", status, status);
    buf_append(&b, line, (size_t)n);
    for (size_t i = 0; i < headers->count; i++) {
        buf_append(&b, headers->items[i].name, strlen(headers->items[i].name));
        buf_append(&b, ": ", 2);
        buf_append(&b, headers->items[i].value, strlen(headers->items[i].value));
        buf_append(&b, "\r\n", 2);
    }
    buf_append(&b, "\r\n", 2);
    if (b.failed) {
        free(b.data);
        return false;
    }
    res->all_headers = (char *)b.data;
    res->all_headers_size = b.size;
    return true;
}

// One exchange with the server. Fills res (status, body, headers) and *resp_headers.
static int32_t perform_once(HttpSendPlan *plan, HttpResponse *res, HttpHeaderList *resp_headers) {
    CURL *c = curl_easy_init();
    if (!c) return ORBIS_HTTP_ERROR_UNKNOWN;
    char *url = NULL;
    const bool ipv6 = strchr(plan->host, ':') != NULL;
    const char *lb = ipv6 ? "[" : "", *rb = ipv6 ? "]" : "";
    const int n = is_default_port(plan->scheme, plan->port)
                      ? asprintf(&url, "%s://%s%s%s%s", plan->scheme, lb, plan->host, rb, plan->path)
                      : asprintf(&url, "%s://%s%s%s:%u%s", plan->scheme, lb, plan->host, rb, (unsigned)plan->port,
                                 plan->path);
    bool ok = n >= 0;
    struct curl_slist *lines = ok ? build_header_lines(plan, &ok) : NULL;
    if (!ok) {
        free(url);
        curl_slist_free_all(lines);
        curl_easy_cleanup(c);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }

    ByteBuf body = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_PROXY, ""); // no proxy from the host environment
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, lines);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, resp_headers);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, timeout_seconds(plan->settings.connect_timeout_us, 30));
    const long recv_s = timeout_seconds(plan->settings.recv_timeout_us, 120);
    const long send_s = timeout_seconds(plan->settings.send_timeout_us, 120);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, recv_s > send_s ? recv_s : send_s);
    if (strcmp(plan->scheme, "https") == 0) {
        const bool verify = !https_forced_disabled() && (plan->settings.ssl_flags & ORBIS_HTTPS_FLAG_SERVER_VERIFY) &&
                            !plan->ctx_has_loaded_certs;
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
    }
    set_method(c, plan);

    const CURLcode cr = curl_easy_perform(c);
    long status = 0;
    if (cr == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    curl_slist_free_all(lines);
    free(url);
    if (cr != CURLE_OK) {
        free(body.data);
        return translate_curl_error(cr);
    }
    if (body.failed) {
        free(body.data);
        return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    }

    ps4_http_response_clear(res);
    res->status_code = (int32_t)status;
    res->content_length_result = 0;
    const char *encoding = find_header(resp_headers, "Content-Encoding");
    uint8_t *inflated = NULL;
    size_t inflated_size = 0;
    if (plan->settings.inflate_gzip && encoding && strcasecmp(encoding, "gzip") == 0 &&
        inflate_gzip(body.data, body.size, &inflated, &inflated_size)) {
        free(body.data);
        res->body = inflated;
        res->body_size = inflated_size;
    } else {
        res->body = body.data;
        res->body_size = body.size;
    }
    res->content_length = res->body_size;
    if (!build_headers_blob(res->status_code, resp_headers, res)) return ORBIS_HTTP_ERROR_OUT_OF_MEMORY;
    return 0;
}

int32_t ps4_http_run_request(HttpSendPlan *plan, HttpResponse *res, uint32_t *event_bits) {
    pthread_once(&g_curl_once, curl_init_once);
    *event_bits = 0;
    for (int depth = 0; depth <= HTTP_MAX_REDIRECTS; ++depth) {
        // Replay a cached 301.
        if (depth < HTTP_MAX_REDIRECTS && plan->settings.auto_redirect && plan->settings.cache_redirected_connection) {
            char *key = cache_key(plan);
            char scheme[8];
            char *host = NULL, *path = NULL;
            uint16_t port = 0;
            const bool hit = key && ps4_http_redirect_cache_get(plan->ctx_id, key, scheme, sizeof(scheme), &host,
                                                                &port, &path);
            free(key);
            if (hit) {
                const bool host_changed = strcmp(host, plan->host) != 0 || port != plan->port;
                snprintf(plan->scheme, sizeof(plan->scheme), "%s", scheme);
                free(plan->host);
                free(plan->path);
                plan->host = host;
                plan->path = path;
                plan->port = port;
                if (host_changed) rewrite_host_header(plan);
                continue;
            }
        }

        HttpHeaderList resp_headers = {0};
        const int32_t err = perform_once(plan, res, &resp_headers);
        if (err != 0) {
            ps4_http_headers_free(&resp_headers);
            ps4_http_response_clear(res);
            return err;
        }

        // The console's redirect decision.
        const char *location = find_header(&resp_headers, "Location");
        ResolvedRedirect next;
        if (!plan->settings.auto_redirect || depth >= HTTP_MAX_REDIRECTS ||
            !is_followable_redirect(res->status_code, plan->method) || !location ||
            !resolve_location(plan, location, &next)) {
            ps4_http_headers_free(&resp_headers);
            break;
        }
        ps4_http_headers_free(&resp_headers);

        const int prev_status = res->status_code;
        const int32_t prev_method = plan->method;
        const bool host_changed = strcmp(next.host, plan->host) != 0 || next.port != plan->port;
        if (prev_status == 301 && plan->settings.cache_redirected_connection) {
            char *key = cache_key(plan);
            if (key) ps4_http_redirect_cache_put(plan->ctx_id, key, next.scheme, next.host, next.port, next.path);
            free(key);
        }
        snprintf(plan->scheme, sizeof(plan->scheme), "%s", next.scheme);
        free(plan->host);
        free(plan->path);
        plan->host = next.host;
        plan->path = next.path;
        plan->port = next.port;
        if (prev_status == 303 && prev_method != ORBIS_HTTP_METHOD_HEAD) {
            plan->method = ORBIS_HTTP_METHOD_GET;
            free(plan->body);
            plan->body = NULL;
            plan->body_size = 0;
            free(plan->content_type);
            plan->content_type = NULL;
            drop_headers(&plan->headers, "Content-Type", "Content-Length");
        }
        if (host_changed) rewrite_host_header(plan);
    }
    *event_bits = ORBIS_HTTP_NB_EVENT_IN | ORBIS_HTTP_NB_EVENT_OUT | ORBIS_HTTP_NB_EVENT_RESOLVED;
    return 0;
}
