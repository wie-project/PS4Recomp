// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_AUTH_H
#define PS4_NP_AUTH_H

#include <stdint.h>
#include "ps4_np.h"

#ifdef __cplusplus
extern "C" {
#endif

// shadPS4 np/np_error.h
#define ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT  ((int32_t)0x80550301)
#define ORBIS_NP_AUTH_ERROR_INVALID_SIZE      ((int32_t)0x80550302)
#define ORBIS_NP_AUTH_ERROR_OUT_OF_MEMORY     ((int32_t)0x80550303)
#define ORBIS_NP_AUTH_ERROR_ABORTED           ((int32_t)0x80550304)
#define ORBIS_NP_AUTH_ERROR_REQUEST_MAX       ((int32_t)0x80550305)
#define ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND ((int32_t)0x80550306)
#define ORBIS_NP_AUTH_ERROR_INVALID_ID        ((int32_t)0x80550307)

// shadPS4 np/np_auth/np_auth.h
#define ORBIS_NP_AUTH_REQUEST_LIMIT 0x10
#define ORBIS_NP_AUTH_REQUEST_ID_OFFSET 0x10000000
#define ORBIS_NP_AUTH_POLL_ASYNC_RET_FINISHED 0
#define ORBIS_NP_AUTH_POLL_ASYNC_RET_RUNNING 1
#define ORBIS_NP_AUTH_TIMEOUT_NO_EFFECT 0

typedef struct OrbisNpClientId {
    char id[129];
    uint8_t padding[7];
} OrbisNpClientId;

typedef struct OrbisNpClientSecret {
    char secret[257];
    uint8_t padding[7];
} OrbisNpClientSecret;

typedef struct OrbisNpAuthorizationCode {
    char code[129];
    uint8_t padding[7];
} OrbisNpAuthorizationCode;

typedef struct OrbisNpIdToken {
    char token[4097];
    uint8_t padding[7];
} OrbisNpIdToken;

// The parameter structures as the guest lays them out: pointers are 64-bit guest addresses.
typedef struct OrbisNpAuthCreateAsyncRequestParameter {
    uint64_t size;
    uint64_t cpu_affinity_mask;
    int32_t thread_priority;
    uint8_t padding[4];
} OrbisNpAuthCreateAsyncRequestParameter;

typedef struct OrbisNpAuthGetAuthorizationCodeParameter {
    uint64_t size;
    uint64_t online_id; // const OrbisNpOnlineId *
    uint64_t client_id; // const OrbisNpClientId *
    uint64_t scope;     // const char *
} OrbisNpAuthGetAuthorizationCodeParameter;

typedef struct OrbisNpAuthGetAuthorizationCodeParameterA {
    uint64_t size;
    int32_t user_id;
    uint8_t padding[4];
    uint64_t client_id; // const OrbisNpClientId *
    uint64_t scope;     // const char *
} OrbisNpAuthGetAuthorizationCodeParameterA;

typedef struct OrbisNpAuthGetIdTokenParameter {
    uint64_t size;
    uint64_t online_id;     // const OrbisNpOnlineId *
    uint64_t client_id;     // const OrbisNpClientId *
    uint64_t client_secret; // const OrbisNpClientSecret *
    uint64_t scope;         // const char *
} OrbisNpAuthGetIdTokenParameter;

typedef struct OrbisNpAuthGetIdTokenParameterA {
    uint64_t size;
    int32_t user_id;
    uint8_t padding[4];
    uint64_t client_id;     // const OrbisNpClientId *
    uint64_t client_secret; // const OrbisNpClientSecret *
    uint64_t scope;         // const char *
} OrbisNpAuthGetIdTokenParameterA;

int32_t sceNpAuthCreateRequest(void);
int32_t sceNpAuthCreateAsyncRequest(const OrbisNpAuthCreateAsyncRequestParameter *param);
int32_t sceNpAuthGetAuthorizationCode(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameter *param,
                                      OrbisNpAuthorizationCode *auth_code, int32_t *issuer_id);
int32_t sceNpAuthGetAuthorizationCodeA(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameterA *param,
                                       OrbisNpAuthorizationCode *auth_code, int32_t *issuer_id);
int32_t sceNpAuthGetIdToken(int32_t req_id, const OrbisNpAuthGetIdTokenParameter *param, OrbisNpIdToken *token);
int32_t sceNpAuthGetIdTokenA(int32_t req_id, const OrbisNpAuthGetIdTokenParameterA *param, OrbisNpIdToken *token);
int32_t sceNpAuthSetTimeout(int32_t req_id, int32_t resolve_retry, uint32_t resolve_timeout, uint32_t conn_timeout,
                            uint32_t send_timeout, uint32_t recv_timeout);
int32_t sceNpAuthAbortRequest(int32_t req_id);
int32_t sceNpAuthWaitAsync(int32_t req_id, int32_t *result);
int32_t sceNpAuthPollAsync(int32_t req_id, int32_t *result);
int32_t sceNpAuthDeleteRequest(int32_t req_id);

void shim_sceNpAuthCreateRequest(GuestContext *ctx);
void shim_sceNpAuthCreateAsyncRequest(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCode(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCodeA(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCodeV3(GuestContext *ctx);
void shim_sceNpAuthGetIdToken(GuestContext *ctx);
void shim_sceNpAuthGetIdTokenA(GuestContext *ctx);
void shim_sceNpAuthGetIdTokenV3(GuestContext *ctx);
void shim_sceNpAuthSetTimeout(GuestContext *ctx);
void shim_sceNpAuthAbortRequest(GuestContext *ctx);
void shim_sceNpAuthWaitAsync(GuestContext *ctx);
void shim_sceNpAuthPollAsync(GuestContext *ctx);
void shim_sceNpAuthDeleteRequest(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_AUTH_H
