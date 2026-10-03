#include "ps4_json2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static inline void *guest_to_host(GuestContext *ctx, uint64_t addr) {
    if (!addr || !ctx || !ctx->mem_base) return NULL;
    if (addr >= (uintptr_t)ctx->mem_base && addr < (uintptr_t)ctx->mem_base + ctx->mem_size) {
        return (void *)addr;
    }
    if (addr < ctx->mem_size) {
        return (void *)(ctx->mem_base + addr);
    }
    return (void *)addr;
}

static uint64_t alloc_guest_string(GuestContext *ctx, const char *str, size_t *out_len) {
    if (!str) {
        if (out_len) *out_len = 0;
        return 0;
    }
    size_t len = strlen(str);
    if (out_len) *out_len = len;
    size_t alloc_sz = (len + 16) & ~15ULL;
    uint64_t g_addr = recomp_vm_alloc(ctx, alloc_sz);
    if (g_addr) {
        char *h_ptr = (char *)(ctx->mem_base + g_addr);
        memcpy(h_ptr, str, len + 1);
    }
    return g_addr;
}

static SceJsonValue g_json_null_value = { .type = SCE_JSON_TYPE_NULL };

static void free_json_value(GuestContext *ctx, SceJsonValue *v) {
    (void)ctx;
    if (!v) return;
    if (v->type == SCE_JSON_TYPE_OBJECT) {
        SceJsonObjectEntry *cur = v->as.obj.head;
        while (cur) {
            SceJsonObjectEntry *next = cur->next;
            if (cur->key) free(cur->key);
            if (cur->val) {
                free_json_value(ctx, cur->val);
                free(cur->val);
            }
            free(cur);
            cur = next;
        }
        v->as.obj.head = NULL;
        v->as.obj.count = 0;
    } else if (v->type == SCE_JSON_TYPE_ARRAY) {
        if (v->as.arr.items) {
            for (size_t i = 0; i < v->as.arr.count; i++) {
                if (v->as.arr.items[i]) {
                    free_json_value(ctx, v->as.arr.items[i]);
                    free(v->as.arr.items[i]);
                }
            }
            free(v->as.arr.items);
            v->as.arr.items = NULL;
        }
        v->as.arr.count = 0;
        v->as.arr.cap = 0;
    }
    v->type = SCE_JSON_TYPE_NULL;
}

static void deep_copy_value(GuestContext *ctx, SceJsonValue *dst, const SceJsonValue *src) {
    if (!dst || !src) return;
    free_json_value(ctx, dst);
    dst->type = src->type;
    switch (src->type) {
        case SCE_JSON_TYPE_BOOL:
            dst->as.b = src->as.b;
            break;
        case SCE_JSON_TYPE_INT:
            dst->as.i = src->as.i;
            break;
        case SCE_JSON_TYPE_REAL:
            dst->as.r = src->as.r;
            break;
        case SCE_JSON_TYPE_STRING:
            dst->as.s = src->as.s;
            break;
        case SCE_JSON_TYPE_OBJECT: {
            dst->as.obj.head = NULL;
            dst->as.obj.count = 0;
            SceJsonObjectEntry *cur = src->as.obj.head;
            while (cur) {
                SceJsonObjectEntry *e = (SceJsonObjectEntry *)malloc(sizeof(SceJsonObjectEntry));
                e->key = cur->key ? strdup(cur->key) : NULL;
                e->val = (SceJsonValue *)calloc(1, sizeof(SceJsonValue));
                if (cur->val) {
                    deep_copy_value(ctx, e->val, cur->val);
                }
                e->next = dst->as.obj.head;
                dst->as.obj.head = e;
                dst->as.obj.count++;
                cur = cur->next;
            }
            break;
        }
        case SCE_JSON_TYPE_ARRAY: {
            dst->as.arr.count = src->as.arr.count;
            dst->as.arr.cap = src->as.arr.count;
            if (src->as.arr.count > 0) {
                dst->as.arr.items = (SceJsonValue **)malloc(src->as.arr.count * sizeof(SceJsonValue *));
                for (size_t i = 0; i < src->as.arr.count; i++) {
                    dst->as.arr.items[i] = (SceJsonValue *)calloc(1, sizeof(SceJsonValue));
                    if (src->as.arr.items[i]) {
                        deep_copy_value(ctx, dst->as.arr.items[i], src->as.arr.items[i]);
                    }
                }
            } else {
                dst->as.arr.items = NULL;
            }
            break;
        }
        default:
            break;
    }
}

// Serialization
typedef struct DynamicBuffer {
    char *buf;
    size_t len;
    size_t cap;
} DynamicBuffer;

static void buf_append_str(DynamicBuffer *db, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    if (db->len + slen + 1 > db->cap) {
        size_t new_cap = (db->cap ? db->cap * 2 : 128) + slen;
        char *nb = (char *)realloc(db->buf, new_cap);
        if (!nb) return;
        db->buf = nb;
        db->cap = new_cap;
    }
    memcpy(db->buf + db->len, s, slen);
    db->len += slen;
    db->buf[db->len] = '\0';
}

static void buf_append_escaped_str(DynamicBuffer *db, const char *s) {
    buf_append_str(db, "\"");
    if (s) {
        for (size_t i = 0; s[i]; i++) {
            char c = s[i];
            if (c == '"') buf_append_str(db, "\\\"");
            else if (c == '\\') buf_append_str(db, "\\\\");
            else if (c == '\n') buf_append_str(db, "\\n");
            else if (c == '\r') buf_append_str(db, "\\r");
            else if (c == '\t') buf_append_str(db, "\\t");
            else {
                char ch[2] = {c, '\0'};
                buf_append_str(db, ch);
            }
        }
    }
    buf_append_str(db, "\"");
}

static void serialize_json_rec(GuestContext *ctx, const SceJsonValue *v, DynamicBuffer *db) {
    if (!v || v->type == SCE_JSON_TYPE_NULL) {
        buf_append_str(db, "null");
        return;
    }
    switch (v->type) {
        case SCE_JSON_TYPE_BOOL:
            buf_append_str(db, v->as.b ? "true" : "false");
            break;
        case SCE_JSON_TYPE_INT: {
            char num[32];
            snprintf(num, sizeof(num), "%" PRId64, v->as.i);
            buf_append_str(db, num);
            break;
        }
        case SCE_JSON_TYPE_REAL: {
            char num[64];
            snprintf(num, sizeof(num), "%g", v->as.r);
            buf_append_str(db, num);
            break;
        }
        case SCE_JSON_TYPE_STRING: {
            const char *cstr = "";
            if (v->as.s.data) {
                const char *hc = (const char *)guest_to_host(ctx, (uint64_t)v->as.s.data);
                if (hc) cstr = hc;
            }
            buf_append_escaped_str(db, cstr);
            break;
        }
        case SCE_JSON_TYPE_ARRAY: {
            buf_append_str(db, "[");
            for (size_t i = 0; i < v->as.arr.count; i++) {
                if (i > 0) buf_append_str(db, ",");
                serialize_json_rec(ctx, v->as.arr.items[i], db);
            }
            buf_append_str(db, "]");
            break;
        }
        case SCE_JSON_TYPE_OBJECT: {
            buf_append_str(db, "{");
            size_t idx = 0;
            SceJsonObjectEntry *cur = v->as.obj.head;
            while (cur) {
                if (idx > 0) buf_append_str(db, ",");
                buf_append_escaped_str(db, cur->key ? cur->key : "");
                buf_append_str(db, ":");
                serialize_json_rec(ctx, cur->val, db);
                idx++;
                cur = cur->next;
            }
            buf_append_str(db, "}");
            break;
        }
        default:
            buf_append_str(db, "null");
            break;
    }
}

// Initializer & Allocator shims
void shim__ZN3sce4Json12MemAllocatorC2Ev(GuestContext *ctx) {
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json12MemAllocatorD2Ev(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json14InitParameter2C1Ev(GuestContext *ctx) {
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json14InitParameter212setAllocatorEPNS0_12MemAllocatorEPv(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json14InitParameter217setFileBufferSizeEm(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json11InitializerC1Ev(GuestContext *ctx) {
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json11InitializerD1Ev(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json11Initializer9terminateEv(GuestContext *ctx) {
    ctx->rax = 0;
    SHIM_RETURN();
}

// String shims
void shim__ZN3sce4Json6StringC1Ev(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)guest_to_host(ctx, ctx->rdi);
    if (s) {
        s->data = NULL;
        s->len = 0;
        s->cap = 0;
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6StringC1EPKc(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)guest_to_host(ctx, ctx->rdi);
    const char *cstr = (const char *)guest_to_host(ctx, ctx->rsi);
    if (s) {
        if (cstr) {
            size_t len = 0;
            uint64_t g_str = alloc_guest_string(ctx, cstr, &len);
            s->data = (char *)g_str;
            s->len = len;
            s->cap = len;
        } else {
            s->data = NULL;
            s->len = 0;
            s->cap = 0;
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6StringD1Ev(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)guest_to_host(ctx, ctx->rdi);
    if (s) {
        s->data = NULL;
        s->len = 0;
        s->cap = 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZNK3sce4Json6String5c_strEv(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)guest_to_host(ctx, ctx->rdi);
    if (s && s->data) {
        ctx->rax = (uint64_t)s->data;
    } else {
        static uint64_t g_empty_str = 0;
        if (!g_empty_str) {
            g_empty_str = alloc_guest_string(ctx, "", NULL);
        }
        ctx->rax = g_empty_str;
    }
    SHIM_RETURN();
}

void shim__ZNK3sce4Json6String6lengthEv(GuestContext *ctx) {
    const SceJsonString *s = (const SceJsonString *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = s ? (uint64_t)s->len : 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6StringaSERKS1_(GuestContext *ctx) {
    SceJsonString *dst = (SceJsonString *)guest_to_host(ctx, ctx->rdi);
    const SceJsonString *src = (const SceJsonString *)guest_to_host(ctx, ctx->rsi);
    if (dst && src && dst != src) {
        dst->data = src->data;
        dst->len = src->len;
        dst->cap = src->cap;
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

// Value Constructors & Lifecycle
void shim__ZN3sce4Json5ValueC1Ev(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_NULL;
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueC1El(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_INT;
        v->as.i = (int64_t)ctx->rsi;
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueC1Ed(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_REAL;
        v->as.r = ctx->xmm[0].f64[0];
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueC1Eb(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_BOOL;
        v->as.b = (bool)(ctx->rsi != 0);
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueC1EPKc(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    const char *cstr = (const char *)guest_to_host(ctx, ctx->rsi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_STRING;
        if (cstr) {
            size_t len = 0;
            uint64_t g_str = alloc_guest_string(ctx, cstr, &len);
            v->as.s.data = (char *)g_str;
            v->as.s.len = len;
            v->as.s.cap = len;
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueC1ERKNS0_6StringE(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    const SceJsonString *s = (const SceJsonString *)guest_to_host(ctx, ctx->rsi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_STRING;
        if (s) {
            v->as.s = *s;
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueC1ERKNS0_6ObjectE(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    const SceJsonObject *obj = (const SceJsonObject *)guest_to_host(ctx, ctx->rsi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_OBJECT;
        if (obj) {
            v->as.obj = *obj;
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueD1Ev(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    free_json_value(ctx, v);
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5ValueaSERKS1_(GuestContext *ctx) {
    SceJsonValue *dst = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    const SceJsonValue *src = (const SceJsonValue *)guest_to_host(ctx, ctx->rsi);
    if (dst && src && dst != src) {
        deep_copy_value(ctx, dst, src);
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

// Value Getters & Setters
void shim__ZNK3sce4Json5Value7getTypeEv(GuestContext *ctx) {
    const SceJsonValue *v = (const SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = v ? (uint64_t)v->type : 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json5Value3setENS0_9ValueTypeE(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v) {
        free_json_value(ctx, v);
        v->type = (SceJsonType)ctx->rsi;
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Value10getIntegerEv(GuestContext *ctx) {
    const SceJsonValue *v = (const SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v && v->type == SCE_JSON_TYPE_INT) {
        ctx->rax = (uint64_t)v->as.i;
    } else {
        ctx->rax = 0;
    }
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Value7getRealEv(GuestContext *ctx) {
    const SceJsonValue *v = (const SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v && v->type == SCE_JSON_TYPE_REAL) {
        ctx->xmm[0].f64[0] = v->as.r;
    } else if (v && v->type == SCE_JSON_TYPE_INT) {
        ctx->xmm[0].f64[0] = (double)v->as.i;
    } else {
        ctx->xmm[0].f64[0] = 0.0;
    }
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Value10getBooleanEv(GuestContext *ctx) {
    const SceJsonValue *v = (const SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = (v && v->type == SCE_JSON_TYPE_BOOL) ? (uint64_t)v->as.b : 0;
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Value9getStringEv(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v && v->type == SCE_JSON_TYPE_STRING) {
        ctx->rax = ctx->rdi + offsetof(SceJsonValue, as.s);
    } else {
        static uint64_t g_empty_str_obj = 0;
        if (!g_empty_str_obj) {
            g_empty_str_obj = recomp_vm_alloc(ctx, sizeof(SceJsonString));
            SceJsonString *es = (SceJsonString *)guest_to_host(ctx, g_empty_str_obj);
            if (es) {
                es->data = (char *)alloc_guest_string(ctx, "", NULL);
                es->len = 0;
                es->cap = 0;
            }
        }
        ctx->rax = g_empty_str_obj;
    }
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Value9getObjectEv(GuestContext *ctx) {
    ctx->rax = ctx->rdi + offsetof(SceJsonValue, as.obj);
    SHIM_RETURN();
}

void shim__ZN3sce4Json5Value11referObjectEv(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v && v->type != SCE_JSON_TYPE_OBJECT) {
        free_json_value(ctx, v);
        memset(&v->as.obj, 0, sizeof(SceJsonObject));
        v->type = SCE_JSON_TYPE_OBJECT;
    }
    ctx->rax = ctx->rdi + offsetof(SceJsonValue, as.obj);
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Value8getArrayEv(GuestContext *ctx) {
    ctx->rax = ctx->rdi + offsetof(SceJsonValue, as.arr);
    SHIM_RETURN();
}

void shim__ZN3sce4Json5Value10referArrayEv(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v && v->type != SCE_JSON_TYPE_ARRAY) {
        free_json_value(ctx, v);
        memset(&v->as.arr, 0, sizeof(SceJsonArray));
        v->type = SCE_JSON_TYPE_ARRAY;
    }
    ctx->rax = ctx->rdi + offsetof(SceJsonValue, as.arr);
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5ValueixEPKc(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    const char *key = (const char *)guest_to_host(ctx, ctx->rsi);
    if (v && v->type == SCE_JSON_TYPE_OBJECT && key) {
        SceJsonObjectEntry *cur = v->as.obj.head;
        while (cur) {
            if (cur->key && strcmp(cur->key, key) == 0) {
                ctx->rax = (uint64_t)cur->val;
                SHIM_RETURN();
            }
            cur = cur->next;
        }
    }
    ctx->rax = (uint64_t)&g_json_null_value;
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5ValueixEm(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    size_t idx = (size_t)ctx->rsi;
    if (v && v->type == SCE_JSON_TYPE_ARRAY && idx < v->as.arr.count) {
        ctx->rax = (uint64_t)v->as.arr.items[idx];
    } else {
        ctx->rax = (uint64_t)&g_json_null_value;
    }
    SHIM_RETURN();
}

void shim__ZN3sce4Json5Value9serializeERNS0_6StringE(GuestContext *ctx) {
    const SceJsonValue *v = (const SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    SceJsonString *out = (SceJsonString *)guest_to_host(ctx, ctx->rsi);
    if (out) {
        DynamicBuffer db = {0};
        serialize_json_rec(ctx, v, &db);
        if (db.buf) {
            size_t len = db.len;
            uint64_t g_str = alloc_guest_string(ctx, db.buf, &len);
            free(db.buf);
            out->data = (char *)g_str;
            out->len = len;
            out->cap = len;
        } else {
            out->data = NULL;
            out->len = 0;
            out->cap = 0;
        }
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

// Array methods
void shim__ZNK3sce4Json5Array4sizeEv(GuestContext *ctx) {
    const SceJsonArray *arr = (const SceJsonArray *)guest_to_host(ctx, ctx->rdi);
    ctx->rax = arr ? (uint64_t)arr->count : 0;
    SHIM_RETURN();
}

void shim__ZNK3sce4Json5Array4backEv(GuestContext *ctx) {
    const SceJsonArray *arr = (const SceJsonArray *)guest_to_host(ctx, ctx->rdi);
    if (arr && arr->count > 0 && arr->items) {
        ctx->rax = (uint64_t)arr->items[arr->count - 1];
    } else {
        ctx->rax = (uint64_t)&g_json_null_value;
    }
    SHIM_RETURN();
}

void shim__ZN3sce4Json5Array9push_backERKNS0_5ValueE(GuestContext *ctx) {
    SceJsonArray *arr = (SceJsonArray *)guest_to_host(ctx, ctx->rdi);
    const SceJsonValue *val = (const SceJsonValue *)guest_to_host(ctx, ctx->rsi);
    if (arr && val) {
        if (arr->count + 1 > arr->cap) {
            size_t new_cap = arr->cap ? arr->cap * 2 : 8;
            SceJsonValue **new_items = (SceJsonValue **)realloc(arr->items, new_cap * sizeof(SceJsonValue *));
            if (new_items) {
                arr->items = new_items;
                arr->cap = new_cap;
            }
        }
        if (arr->count < arr->cap) {
            SceJsonValue *nv = (SceJsonValue *)calloc(1, sizeof(SceJsonValue));
            deep_copy_value(ctx, nv, val);
            arr->items[arr->count++] = nv;
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

// Object methods
void shim__ZN3sce4Json6ObjectC1Ev(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)guest_to_host(ctx, ctx->rdi);
    if (obj) {
        memset(obj, 0, sizeof(SceJsonObject));
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6ObjectC1ERKS1_(GuestContext *ctx) {
    SceJsonObject *dst = (SceJsonObject *)guest_to_host(ctx, ctx->rdi);
    const SceJsonObject *src = (const SceJsonObject *)guest_to_host(ctx, ctx->rsi);
    if (dst) {
        memset(dst, 0, sizeof(SceJsonObject));
        if (src) {
            SceJsonObjectEntry *cur = src->head;
            while (cur) {
                SceJsonObjectEntry *e = (SceJsonObjectEntry *)malloc(sizeof(SceJsonObjectEntry));
                e->key = cur->key ? strdup(cur->key) : NULL;
                e->val = (SceJsonValue *)calloc(1, sizeof(SceJsonValue));
                if (cur->val) {
                    deep_copy_value(ctx, e->val, cur->val);
                }
                e->next = dst->head;
                dst->head = e;
                dst->count++;
                cur = cur->next;
            }
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6ObjectD1Ev(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)guest_to_host(ctx, ctx->rdi);
    if (obj) {
        SceJsonObjectEntry *cur = obj->head;
        while (cur) {
            SceJsonObjectEntry *next = cur->next;
            if (cur->key) free(cur->key);
            if (cur->val) {
                free_json_value(ctx, cur->val);
                free(cur->val);
            }
            free(cur);
            cur = next;
        }
        obj->head = NULL;
        obj->count = 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6Object5clearEv(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)guest_to_host(ctx, ctx->rdi);
    if (obj) {
        SceJsonObjectEntry *cur = obj->head;
        while (cur) {
            SceJsonObjectEntry *next = cur->next;
            if (cur->key) free(cur->key);
            if (cur->val) {
                free_json_value(ctx, cur->val);
                free(cur->val);
            }
            free(cur);
            cur = next;
        }
        obj->head = NULL;
        obj->count = 0;
    }
    ctx->rax = 0;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6ObjectaSERKS1_(GuestContext *ctx) {
    SceJsonObject *dst = (SceJsonObject *)guest_to_host(ctx, ctx->rdi);
    const SceJsonObject *src = (const SceJsonObject *)guest_to_host(ctx, ctx->rsi);
    if (dst && src && dst != src) {
        // Clear dst
        SceJsonObjectEntry *cur = dst->head;
        while (cur) {
            SceJsonObjectEntry *next = cur->next;
            if (cur->key) free(cur->key);
            if (cur->val) {
                free_json_value(ctx, cur->val);
                free(cur->val);
            }
            free(cur);
            cur = next;
        }
        dst->head = NULL;
        dst->count = 0;

        // Copy from src
        cur = src->head;
        while (cur) {
            SceJsonObjectEntry *e = (SceJsonObjectEntry *)malloc(sizeof(SceJsonObjectEntry));
            e->key = cur->key ? strdup(cur->key) : NULL;
            e->val = (SceJsonValue *)calloc(1, sizeof(SceJsonValue));
            if (cur->val) {
                deep_copy_value(ctx, e->val, cur->val);
            }
            e->next = dst->head;
            dst->head = e;
            dst->count++;
            cur = cur->next;
        }
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6ObjectixERKNS0_6StringE(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)guest_to_host(ctx, ctx->rdi);
    const SceJsonString *s = (const SceJsonString *)guest_to_host(ctx, ctx->rsi);
    const char *key = "";
    if (s && s->data) {
        const char *hk = (const char *)guest_to_host(ctx, (uint64_t)s->data);
        if (hk) key = hk;
    }
    if (obj) {
        SceJsonObjectEntry *cur = obj->head;
        while (cur) {
            if (cur->key && strcmp(cur->key, key) == 0) {
                ctx->rax = (uint64_t)cur->val;
                SHIM_RETURN();
            }
            cur = cur->next;
        }
        // Insert new entry
        SceJsonObjectEntry *entry = (SceJsonObjectEntry *)malloc(sizeof(SceJsonObjectEntry));
        entry->key = strdup(key);
        entry->val = (SceJsonValue *)calloc(1, sizeof(SceJsonValue));
        entry->val->type = SCE_JSON_TYPE_NULL;
        entry->next = obj->head;
        obj->head = entry;
        obj->count++;
        ctx->rax = (uint64_t)entry->val;
        SHIM_RETURN();
    }
    ctx->rax = (uint64_t)&g_json_null_value;
    SHIM_RETURN();
}

void shim__ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rsi);
    if (v) {
        free_json_value(ctx, v);
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_OBJECT;
    }
    ctx->rax = 0; // SCE_OK
    SHIM_RETURN();
}
