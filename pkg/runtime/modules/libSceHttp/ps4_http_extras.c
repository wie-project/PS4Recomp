// Additional libSceHttp entry points (offline host).
//  - sceHttpParseResponseHeader: real header parser (pure string function).
//  - sceHttpRemoveRequestHeader: accepted, no-op (no requests are ever sent).
#include "recomp_runtime.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define ORBIS_HTTP_ERROR_INVALID_VALUE         ((int32_t)0x804311FE)
#define ORBIS_HTTP_ERROR_PARSE_HTTP_NOT_FOUND  ((int32_t)0x80432025)

// int sceHttpParseResponseHeader(const char *header, size_t headerLen,
//                                const char *fieldStr, const char **fieldValue,
//                                size_t *valueLen)
// fieldValue receives a guest pointer into the caller's header buffer.
void shim_sceHttpParseResponseHeader(GuestContext *ctx) {
    uint64_t header_g = ctx->rdi;
    uint64_t header_len = ctx->rsi;
    uint64_t field_g = ctx->rdx;
    uint64_t value_out_g = ctx->rcx;
    uint64_t len_out_g = ctx->r8;

    if (!header_g || !field_g || !value_out_g || !len_out_g || !ctx->mem_base) {
        ctx->rax = (uint64_t)(int64_t)ORBIS_HTTP_ERROR_INVALID_VALUE;
        SHIM_RETURN();
    }
    const char *header = (const char *)(ctx->mem_base + header_g);
    const char *field = (const char *)(ctx->mem_base + field_g);
    size_t field_len = strlen(field);

    size_t pos = 0;
    // Skip the status line if present ("HTTP/1.1 200 OK\r\n").
    if (header_len >= 5 && strncasecmp(header, "HTTP/", 5) == 0) {
        while (pos < header_len && header[pos] != '\n') pos++;
        if (pos < header_len) pos++;
    }
    while (pos < header_len) {
        size_t line_start = pos;
        size_t line_end = pos;
        while (line_end < header_len && header[line_end] != '\n') line_end++;
        size_t next = line_end < header_len ? line_end + 1 : line_end;
        size_t content_end = line_end;
        if (content_end > line_start && header[content_end - 1] == '\r') content_end--;

        size_t colon = line_start;
        while (colon < content_end && header[colon] != ':') colon++;
        if (colon < content_end && colon - line_start == field_len &&
            strncasecmp(header + line_start, field, field_len) == 0) {
            size_t v = colon + 1;
            while (v < content_end && (header[v] == ' ' || header[v] == '\t')) v++;
            size_t vend = content_end;
            while (vend > v && (header[vend - 1] == ' ' || header[vend - 1] == '\t')) vend--;
            *(uint64_t *)(ctx->mem_base + value_out_g) = header_g + v;
            *(uint64_t *)(ctx->mem_base + len_out_g) = (uint64_t)(vend - v);
            ctx->rax = 0;
            SHIM_RETURN();
        }
        if (next == pos) break;
        pos = next;
    }
    ctx->rax = (uint64_t)(int64_t)ORBIS_HTTP_ERROR_PARSE_HTTP_NOT_FOUND;
    SHIM_RETURN();
}

// int sceHttpRemoveRequestHeader(int id, const char *name)
void shim_sceHttpRemoveRequestHeader(GuestContext *ctx) {
    static int warned;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "[libSceHttp] sceHttpRemoveRequestHeader -> 0 (offline no-op)\n");
    }
    ctx->rax = 0;
    SHIM_RETURN();
}
