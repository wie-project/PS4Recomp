#ifndef PS4_JSON2_H
#define PS4_JSON2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceJsonString {
    char *data;
    size_t len;
    size_t cap;
} SceJsonString;

typedef struct SceJsonObjectEntry {
    char *key;
    struct SceJsonValue *val;
    struct SceJsonObjectEntry *next;
} SceJsonObjectEntry;

typedef struct SceJsonObject {
    SceJsonObjectEntry *head;
    size_t count;
} SceJsonObject;

typedef enum SceJsonType {
    SCE_JSON_TYPE_NULL   = 0,
    SCE_JSON_TYPE_BOOL   = 1,
    SCE_JSON_TYPE_INT    = 2,
    SCE_JSON_TYPE_REAL   = 3,
    SCE_JSON_TYPE_STRING = 4,
    SCE_JSON_TYPE_ARRAY  = 5,
    SCE_JSON_TYPE_OBJECT = 6
} SceJsonType;

typedef struct SceJsonValue {
    SceJsonType type;
    union {
        bool b;
        int64_t i;
        double r;
        SceJsonString s;
        SceJsonObject obj;
    } as;
} SceJsonValue;

// Shims for all 23 C++ mangled symbols
void shim__ZN3sce4Json12MemAllocatorC2Ev(GuestContext *ctx);
void shim__ZN3sce4Json12MemAllocatorD2Ev(GuestContext *ctx);
void shim__ZN3sce4Json14InitParameter2C1Ev(GuestContext *ctx);
void shim__ZN3sce4Json14InitParameter212setAllocatorEPNS0_12MemAllocatorEPv(GuestContext *ctx);
void shim__ZN3sce4Json14InitParameter217setFileBufferSizeEm(GuestContext *ctx);
void shim__ZN3sce4Json11InitializerC1Ev(GuestContext *ctx);
void shim__ZN3sce4Json11InitializerD1Ev(GuestContext *ctx);
void shim__ZN3sce4Json11Initializer10initializeEPKNS0_14InitParameter2E(GuestContext *ctx);
void shim__ZN3sce4Json11Initializer9terminateEv(GuestContext *ctx);
void shim__ZN3sce4Json6StringC1EPKc(GuestContext *ctx);
void shim__ZN3sce4Json6StringD1Ev(GuestContext *ctx);
void shim__ZNK3sce4Json6String5c_strEv(GuestContext *ctx);
void shim__ZN3sce4Json5ValueC1Ev(GuestContext *ctx);
void shim__ZN3sce4Json5ValueD1Ev(GuestContext *ctx);
void shim__ZN3sce4Json5ValueaSERKS1_(GuestContext *ctx);
void shim__ZN3sce4Json5ValueC1ERKNS0_6StringE(GuestContext *ctx);
void shim__ZNK3sce4Json5Value9getStringEv(GuestContext *ctx);
void shim__ZNK3sce4Json5ValueixEPKc(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectC1Ev(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectC1ERKS1_(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectD1Ev(GuestContext *ctx);
void shim__ZN3sce4Json6ObjectixERKNS0_6StringE(GuestContext *ctx);
void shim__ZN3sce4Json6Parser5parseERNS0_5ValueEPKcm(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_JSON2_H
