#include "ps4_json2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void *guest_to_host(GuestContext *ctx, uint64_t addr) {
    if (!addr || !ctx || !ctx->mem_base) return NULL;
    return (void *)(ctx->mem_base + addr);
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

void shim__ZN3sce4Json5ValueC1Ev(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_NULL;
    }
    ctx->rax = ctx->rdi;
    SHIM_RETURN();
}

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
    }
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
        free_json_value(ctx, dst);
        memcpy(dst, src, sizeof(SceJsonValue));
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

void shim__ZNK3sce4Json5Value9getStringEv(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)guest_to_host(ctx, ctx->rdi);
    if (v && v->type == SCE_JSON_TYPE_STRING) {
        // Return address of SceJsonString within SceJsonValue in guest memory
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

static SceJsonValue g_json_null_value = { .type = SCE_JSON_TYPE_NULL };

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
    if (dst) {
        memset(dst, 0, sizeof(SceJsonObject));
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
    }
    ctx->rax = 0;
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
