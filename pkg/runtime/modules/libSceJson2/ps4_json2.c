#include "ps4_json2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SceJsonValue g_json_null_value = { .type = SCE_JSON_TYPE_NULL };

void shim__ZN3sce4Json12MemAllocatorC2Ev(GuestContext *ctx) {
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json12MemAllocatorD2Ev(GuestContext *ctx) {
    ctx->rax = 0;
}

void shim__ZN3sce4Json14InitParameter2C1Ev(GuestContext *ctx) {
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json14InitParameter212setAllocatorEPNS0_12MemAllocatorEPv(GuestContext *ctx) {
    ctx->rax = 0;
}

void shim__ZN3sce4Json14InitParameter217setFileBufferSizeEm(GuestContext *ctx) {
    ctx->rax = 0;
}

void shim__ZN3sce4Json11InitializerC1Ev(GuestContext *ctx) {
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json11InitializerD1Ev(GuestContext *ctx) {
    ctx->rax = 0;
}

void shim__ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(GuestContext *ctx) {
    ctx->rax = 0;
}

void shim__ZN3sce4Json11Initializer9terminateEv(GuestContext *ctx) {
    ctx->rax = 0;
}

void shim__ZN3sce4Json6StringC1EPKc(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)ctx->rdi;
    const char *cstr = (const char *)ctx->rsi;
    if (s) {
        if (cstr) {
            s->data = strdup(cstr);
            s->len = strlen(cstr);
            s->cap = s->len;
        } else {
            s->data = NULL;
            s->len = 0;
            s->cap = 0;
        }
    }
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json6StringD1Ev(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)ctx->rdi;
    if (s && s->data) {
        free(s->data);
        s->data = NULL;
        s->len = 0;
        s->cap = 0;
    }
    ctx->rax = 0;
}

void shim__ZNK3sce4Json6String5c_strEv(GuestContext *ctx) {
    SceJsonString *s = (SceJsonString *)ctx->rdi;
    if (s && s->data) {
        ctx->rax = (uint64_t)s->data;
    } else {
        ctx->rax = (uint64_t)"";
    }
}

void shim__ZN3sce4Json5ValueC1Ev(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)ctx->rdi;
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_NULL;
    }
    ctx->rax = ctx->rdi;
}

static void free_json_value(SceJsonValue *v) {
    if (!v) return;
    if (v->type == SCE_JSON_TYPE_STRING && v->as.s.data) {
        free(v->as.s.data);
        v->as.s.data = NULL;
    } else if (v->type == SCE_JSON_TYPE_OBJECT) {
        SceJsonObjectEntry *cur = v->as.obj.head;
        while (cur) {
            SceJsonObjectEntry *next = cur->next;
            if (cur->key) free(cur->key);
            if (cur->val) {
                free_json_value(cur->val);
                free(cur->val);
            }
            free(cur);
            cur = next;
        }
        v->as.obj.head = NULL;
    }
}

void shim__ZN3sce4Json5ValueD1Ev(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)ctx->rdi;
    free_json_value(v);
    ctx->rax = 0;
}

void shim__ZN3sce4Json5ValueaSERKS1_(GuestContext *ctx) {
    SceJsonValue *dst = (SceJsonValue *)ctx->rdi;
    const SceJsonValue *src = (const SceJsonValue *)ctx->rsi;
    if (dst && src && dst != src) {
        free_json_value(dst);
        memcpy(dst, src, sizeof(SceJsonValue));
        if (src->type == SCE_JSON_TYPE_STRING && src->as.s.data) {
            dst->as.s.data = strdup(src->as.s.data);
        }
    }
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json5ValueC1ERKNS0_6StringE(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)ctx->rdi;
    const SceJsonString *s = (const SceJsonString *)ctx->rsi;
    if (v) {
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_STRING;
        if (s && s->data) {
            v->as.s.data = strdup(s->data);
            v->as.s.len = s->len;
            v->as.s.cap = s->cap;
        }
    }
    ctx->rax = ctx->rdi;
}

void shim__ZNK3sce4Json5Value9getStringEv(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)ctx->rdi;
    if (v && v->type == SCE_JSON_TYPE_STRING) {
        ctx->rax = (uint64_t)&v->as.s;
    } else {
        static SceJsonString empty_str = { .data = "", .len = 0, .cap = 0 };
        ctx->rax = (uint64_t)&empty_str;
    }
}

void shim__ZNK3sce4Json5ValueixEPKc(GuestContext *ctx) {
    SceJsonValue *v = (SceJsonValue *)ctx->rdi;
    const char *key = (const char *)ctx->rsi;
    if (v && v->type == SCE_JSON_TYPE_OBJECT && key) {
        SceJsonObjectEntry *cur = v->as.obj.head;
        while (cur) {
            if (cur->key && strcmp(cur->key, key) == 0) {
                ctx->rax = (uint64_t)cur->val;
                return;
            }
            cur = cur->next;
        }
    }
    ctx->rax = (uint64_t)&g_json_null_value;
}

void shim__ZN3sce4Json6ObjectC1Ev(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)ctx->rdi;
    if (obj) {
        memset(obj, 0, sizeof(SceJsonObject));
    }
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json6ObjectC1ERKS1_(GuestContext *ctx) {
    SceJsonObject *dst = (SceJsonObject *)ctx->rdi;
    if (dst) {
        memset(dst, 0, sizeof(SceJsonObject));
    }
    ctx->rax = ctx->rdi;
}

void shim__ZN3sce4Json6ObjectD1Ev(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)ctx->rdi;
    if (obj) {
        SceJsonObjectEntry *cur = obj->head;
        while (cur) {
            SceJsonObjectEntry *next = cur->next;
            if (cur->key) free(cur->key);
            if (cur->val) {
                free_json_value(cur->val);
                free(cur->val);
            }
            free(cur);
            cur = next;
        }
        obj->head = NULL;
    }
    ctx->rax = 0;
}

void shim__ZN3sce4Json6ObjectixERKNS0_6StringE(GuestContext *ctx) {
    SceJsonObject *obj = (SceJsonObject *)ctx->rdi;
    const SceJsonString *s = (const SceJsonString *)ctx->rsi;
    const char *key = (s && s->data) ? s->data : "";
    if (obj) {
        SceJsonObjectEntry *cur = obj->head;
        while (cur) {
            if (cur->key && strcmp(cur->key, key) == 0) {
                ctx->rax = (uint64_t)cur->val;
                return;
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
        return;
    }
    ctx->rax = (uint64_t)&g_json_null_value;
}

void shim__ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(GuestContext *ctx) {
    (void)ctx;
    SceJsonValue *v = (SceJsonValue *)ctx->rsi;
    if (v) {
        free_json_value(v);
        memset(v, 0, sizeof(SceJsonValue));
        v->type = SCE_JSON_TYPE_OBJECT;
    }
    ctx->rax = 0; // SCE_OK
}
