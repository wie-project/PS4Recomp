// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS4_COMPANION_HTTPD_H
#define PS4_COMPANION_HTTPD_H

#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Companion HTTPD event codes
#define ORBIS_COMPANION_HTTPD_EVENT_CONNECT 0x10000001
#define ORBIS_COMPANION_HTTPD_EVENT_DISCONNECT 0x10000002

// Companion HTTPD error codes
#define ORBIS_COMPANION_HTTPD_ERROR_UNKNOWN ((int32_t)0x80E40001)
#define ORBIS_COMPANION_HTTPD_ERROR_FATAL ((int32_t)0x80E40002)
#define ORBIS_COMPANION_HTTPD_ERROR_NOMEM ((int32_t)0x80E40003)
#define ORBIS_COMPANION_HTTPD_ERROR_INVALID_PARAM ((int32_t)0x80E40004)
#define ORBIS_COMPANION_HTTPD_ERROR_INVALID_OPERATION ((int32_t)0x80E40005)
#define ORBIS_COMPANION_HTTPD_ERROR_NOT_INITIALIZED ((int32_t)0x80E40006)
#define ORBIS_COMPANION_HTTPD_ERROR_ALREADY_INITIALIZED ((int32_t)0x80E40007)
#define ORBIS_COMPANION_HTTPD_ERROR_NO_EVENT ((int32_t)0x80E40008)
#define ORBIS_COMPANION_HTTPD_ERROR_NOT_GENERATE_RESPONSE ((int32_t)0x80E40009)
#define ORBIS_COMPANION_HTTPD_ERROR_ALREADY_STARTED ((int32_t)0x80E4000A)
#define ORBIS_COMPANION_HTTPD_ERROR_NOT_STARTED ((int32_t)0x80E4000B)
#define ORBIS_COMPANION_HTTPD_ERROR_ALREADY_REGISTERED ((int32_t)0x80E4000C)
#define ORBIS_COMPANION_HTTPD_ERROR_NOT_CONNECTED ((int32_t)0x80E4000D)
#define ORBIS_COMPANION_HTTPD_ERROR_USER_NOT_FOUND ((int32_t)0x80E4000E)

typedef struct OrbisCompanionHttpdEvent {
    int32_t event;
    union {
        char reserved[256];
    } data;
} OrbisCompanionHttpdEvent;

int32_t sceCompanionHttpdInitialize2(const void *param);
int32_t sceCompanionHttpdTerminate(void);
int32_t sceCompanionHttpdStart(void);
int32_t sceCompanionHttpdStop(void);
int32_t sceCompanionHttpdGetEvent(OrbisCompanionHttpdEvent *event);
int32_t sceCompanionHttpdInitialize(void);
int32_t sceCompanionHttpdRegisterRequestCallback(void *cb, void *userData);
int32_t sceCompanionHttpdRegisterRequestCallback2(void *cb, void *userData);
int32_t sceCompanionHttpdUnregisterRequestCallback(void);
int32_t sceCompanionHttpdRegisterRequestBodyReceptionCallback(uint64_t cb, uint64_t param);
int32_t sceCompanionHttpdUnregisterRequestBodyReceptionCallback(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_COMPANION_HTTPD_H
