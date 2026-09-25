#ifndef PS4_NP_H
#define PS4_NP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// Base NP Error codes (0x80550001 - 0x8055001F)
#define ORBIS_NP_ERROR_ALREADY_INITIALIZED    0x80550001
#define ORBIS_NP_ERROR_NOT_INITIALIZED        0x80550002
#define ORBIS_NP_ERROR_INVALID_ARGUMENT       0x80550003
#define ORBIS_NP_ERROR_OUT_OF_MEMORY          0x80550005
#define ORBIS_NP_ERROR_SIGNED_OUT             0x80550006
#define ORBIS_NP_ERROR_USER_NOT_FOUND         0x80550007
#define ORBIS_NP_ERROR_CALLBACK_MAX           0x8055001D
#define ORBIS_NP_ERROR_CALLBACK_NOT_REGISTERED 0x80550009
#define ORBIS_NP_ERROR_REQUEST_NOT_FOUND      0x80550014
#define ORBIS_NP_ERROR_INVALID_SIZE           0x80550011
#define ORBIS_NP_ERROR_ABORTED                0x80550012
#define ORBIS_NP_ERROR_INVALID_ID             0x80550015

// NP Auth error codes
#define ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT  0x80550301
#define ORBIS_NP_AUTH_ERROR_INVALID_SIZE      0x80550302
#define ORBIS_NP_AUTH_ERROR_NO_TOKEN_RECEIVED 0x80550308
#define ORBIS_NP_AUTH_ERROR_REQUEST_NOT_FOUND 0x80550306

// NP Matching2 error codes
#define ORBIS_NP_MATCHING2_ERROR_NOT_INITIALIZED 0x80550C03
#define ORBIS_NP_MATCHING2_ERROR_INVALID_ARGUMENT 0x80550C0A
#define ORBIS_NP_MATCHING2_ERROR_NP_SIGNED_OUT    0x80550C26
#define ORBIS_NP_MATCHING2_ERROR_NOT_NP_SIGN_IN   0x80550C2F

// NP Signaling error codes
#define ORBIS_NP_SIGNALING_ERROR_NOT_INITIALIZED 0x80552701
#define ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT 0x80552715

// NP Community / Score error codes
#define ORBIS_NP_COMMUNITY_ERROR_NOT_INITIALIZED 0x80550702
#define ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT 0x80550704
#define ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN        0x80550705
#define ORBIS_NP_COMMUNITY_ERROR_INVALID_ID      0x8055070E

// NP WebApi error codes
#define ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN      0x80552907
#define ORBIS_NP_WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND 0x80552904
#define ORBIS_NP_WEBAPI_ERROR_USER_CONTEXT_NOT_FOUND 0x80552905

#define ORBIS_NP_ONLINEID_MAX_LENGTH 16

typedef uint64_t OrbisNpAccountId;
typedef uint32_t OrbisNpServiceLabel;

typedef struct OrbisNpOnlineId {
    char data[ORBIS_NP_ONLINEID_MAX_LENGTH];
    int8_t term;
    int8_t dummy[3];
} OrbisNpOnlineId;

typedef struct OrbisNpId {
    OrbisNpOnlineId handle;
    uint8_t opt[8];
    uint8_t reserved[8];
} OrbisNpId;

typedef struct OrbisNpCountryCode {
    char country_code[2];
    char end;
    char pad;
} OrbisNpCountryCode;

typedef struct OrbisNpDate {
    uint16_t year;
    uint8_t month;
    uint8_t day;
} OrbisNpDate;

typedef struct OrbisNpLanguageCode {
    char code[6];
    char padding[10];
} OrbisNpLanguageCode;

typedef struct OrbisNpCheckPlusParameter {
    uint64_t size;
    int32_t user_id;
    uint8_t padding[4];
    uint64_t features;
    uint8_t reserved[32];
} OrbisNpCheckPlusParameter;

typedef struct OrbisNpCheckPlusResult {
    bool authorized;
    uint8_t reserved[32];
} OrbisNpCheckPlusResult;

typedef struct OrbisNpCreateAsyncRequestParameter {
    uint64_t size;
    uint64_t cpu_affinity_mask;
    int32_t thread_priority;
    uint8_t padding[4];
} OrbisNpCreateAsyncRequestParameter;

typedef struct OrbisNpAuthCreateAsyncRequestParameter {
    uint64_t size;
    uint64_t cpu_affinity_mask;
    int32_t thread_priority;
    uint8_t padding[4];
} OrbisNpAuthCreateAsyncRequestParameter;

typedef struct OrbisNpAuthGetAuthorizationCodeParameterA {
    uint64_t size;
    int32_t user_id;
    uint8_t padding[4];
    const char* client_id;
    const char* scope;
} OrbisNpAuthGetAuthorizationCodeParameterA;

typedef struct OrbisNpAuthorizationCode {
    char code[129];
    uint8_t padding[7];
} OrbisNpAuthorizationCode;

typedef struct OrbisNpScoreBoardInfo {
    uint32_t rankLimit;
    uint32_t updateMode;
    uint32_t sortMode;
    uint32_t uploadNumLimit;
    uint64_t uploadSizeLimit;
} OrbisNpScoreBoardInfo;

typedef struct OrbisNpScoreComment {
    char utf8Comment[64];
} OrbisNpScoreComment;

typedef struct OrbisNpScoreGameInfo {
    uint64_t infoSize;
    uint8_t data[189];
    uint8_t pad2[3];
} OrbisNpScoreGameInfo;

typedef struct OrbisNpCommerceDialogResult {
    int32_t result;
    bool authorized;
    char dummy8;
    int16_t dummy16;
    void* userData;
    uint8_t reserved[32];
} OrbisNpCommerceDialogResult;

// C APIs
int32_t sceNpCheckNpAvailability(int32_t req_id, OrbisNpOnlineId* online_id);
int32_t sceNpCheckNpAvailabilityA(int32_t req_id, int32_t user_id);
int32_t sceNpCheckPlus(int32_t req_id, const OrbisNpCheckPlusParameter* param, OrbisNpCheckPlusResult* result);
int32_t sceNpCreateAsyncRequest(const OrbisNpCreateAsyncRequestParameter* param);
int32_t sceNpCreateRequest(void);
int32_t sceNpDeleteRequest(int32_t req_id);
int32_t sceNpGetAccountCountry(OrbisNpOnlineId* online_id, OrbisNpCountryCode* country_code);
int32_t sceNpGetAccountCountryA(int32_t user_id, OrbisNpCountryCode* country_code);
int32_t sceNpGetAccountDateOfBirth(OrbisNpOnlineId* online_id, OrbisNpDate* date_of_birth);
int32_t sceNpGetAccountDateOfBirthA(int32_t user_id, OrbisNpDate* date_of_birth);
int32_t sceNpGetAccountId(OrbisNpOnlineId* online_id, uint64_t* account_id);
int32_t sceNpGetAccountIdA(int32_t user_id, uint64_t* account_id);
int32_t sceNpGetAccountLanguage(OrbisNpOnlineId* online_id, OrbisNpLanguageCode* language);
int32_t sceNpGetAccountLanguageA(int32_t user_id, OrbisNpLanguageCode* language);
int32_t sceNpGetGamePresenceStatus(OrbisNpOnlineId* online_id, int32_t* game_status);
int32_t sceNpGetGamePresenceStatusA(int32_t user_id, int32_t* game_status);
int32_t sceNpGetNpId(int32_t user_id, OrbisNpId* np_id);
int32_t sceNpGetNpReachabilityState(int32_t user_id, int32_t* state);
int32_t sceNpGetOnlineId(int32_t user_id, OrbisNpOnlineId* online_id);
int32_t sceNpGetState(int32_t user_id, int32_t* state);
int32_t sceNpHasSignedUp(int32_t user_id, bool* has_signed_up);
int32_t sceNpIsPlusMember(int32_t user_id, bool* result);
int32_t sceNpPollAsync(int32_t req_id, int32_t* result);
int32_t sceNpWaitAsync(int32_t req_id, int32_t* result);
int32_t sceNpRegisterStateCallback(void* callback, void* userdata);
int32_t sceNpRegisterStateCallbackA(void* callback, void* userdata);
int32_t sceNpUnregisterStateCallback(int32_t callback_id);
int32_t sceNpUnregisterStateCallbackA(int32_t callback_id);

int32_t sceNpAuthCreateRequest(void);
int32_t sceNpAuthCreateAsyncRequest(const OrbisNpAuthCreateAsyncRequestParameter* param);
int32_t sceNpAuthDeleteRequest(int32_t req_id);
int32_t sceNpAuthGetAuthorizationCode(int32_t req_id, const void* param, OrbisNpAuthorizationCode* auth_code, int32_t* issuer_id);
int32_t sceNpAuthGetAuthorizationCodeA(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameterA* param, OrbisNpAuthorizationCode* auth_code, int32_t* issuer_id);
int32_t sceNpAuthPollAsync(int32_t req_id, int32_t* result);
int32_t sceNpAuthWaitAsync(int32_t req_id, int32_t* result);

int32_t sceNpMatching2Initialize(void* param);
int32_t sceNpMatching2Terminate(void);
int32_t sceNpMatching2CreateContext(const void* param, uint16_t* ctxId);
int32_t sceNpMatching2CreateContextA(const void* param, uint16_t* ctxId);

int32_t sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t cpuAffinityMask, int64_t threadStackSize);
int32_t sceNpSignalingTerminate(void);
int32_t sceNpSignalingCreateContext(const void* npId, void* callback, void* callbackArg, uint32_t* outContextId);
int32_t sceNpSignalingCreateContextA(int32_t userId, void* callback, void* callbackArg, uint32_t* outContextId);

int32_t sceNpWebApiInitialize(int32_t libHttpCtxId, uint64_t poolSize);
int32_t sceNpWebApiTerminate(int32_t libCtxId);
int32_t sceNpWebApiCreateContext(int32_t libCtxId, OrbisNpOnlineId* onlineId);
int32_t sceNpWebApiCreateContextA(int32_t libCtxId, int32_t userId);
int32_t sceNpWebApiDeleteContext(int32_t titleUserCtxId);
int32_t sceNpWebApiCreateRequest(int32_t titleUserCtxId, const char* pApiGroup, const char* pPath, int32_t method, const void* pContentParameter, int64_t* pRequestId);
int32_t sceNpWebApiSendRequest(int64_t requestId, const void* pData, uint64_t dataSize);
int32_t sceNpWebApiSendRequest2(int64_t requestId, const void* pData, uint64_t dataSize, void* pRespInfoOption);
int32_t sceNpWebApiDeleteRequest(int64_t requestId);

int32_t sceNpScoreCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, const OrbisNpId* selfNpId);
int32_t sceNpScoreCreateNpTitleCtxA(OrbisNpServiceLabel npServiceLabel, int32_t selfId);
int32_t sceNpScoreDeleteNpTitleCtx(int32_t titleCtxId);
int32_t sceNpScoreCreateRequest(int32_t titleCtxId);
int32_t sceNpScoreDeleteRequest(int32_t reqId);
int32_t sceNpScorePollAsync(int32_t reqId, int32_t* result);
int32_t sceNpScoreWaitAsync(int32_t reqId, int32_t* result);

int32_t sceNpTusCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, OrbisNpId* npId);
int32_t sceNpTusCreateNpTitleCtxA(OrbisNpServiceLabel serviceLabel, int32_t userId);
int32_t sceNpTusDeleteNpTitleCtx(int32_t ctxId);
int32_t sceNpTusCreateRequest(int32_t libCtxId);
int32_t sceNpTusDeleteRequest(int32_t requestId);
int32_t sceNpTusPollAsync(int32_t reqId, int32_t* result);
int32_t sceNpTusWaitAsync(int32_t reqId, int32_t* result);

int32_t sceNpCommerceDialogInitialize(void);
int32_t sceNpCommerceDialogTerminate(void);
int32_t sceNpCommerceDialogClose(void);
int8_t sceNpCommerceDialogGetStatus(void);
int32_t sceNpCommerceDialogGetResult(OrbisNpCommerceDialogResult* result);
int32_t sceNpCommerceDialogOpen(const void* param);

int32_t sceNpUtilityInit(void);
int32_t sceNpUtilityTerm(void);
int32_t sceNpGameIntentInitialize(void);
int32_t sceNpGameIntentTerminate(void);

// GuestContext shims
void shim_sceNpCheckNpAvailability(GuestContext *ctx);
void shim_sceNpCheckNpAvailabilityA(GuestContext *ctx);
void shim_sceNpCheckPlus(GuestContext *ctx);
void shim_sceNpCreateAsyncRequest(GuestContext *ctx);
void shim_sceNpCreateRequest(GuestContext *ctx);
void shim_sceNpDeleteRequest(GuestContext *ctx);
void shim_sceNpGetAccountCountry(GuestContext *ctx);
void shim_sceNpGetAccountCountryA(GuestContext *ctx);
void shim_sceNpGetAccountDateOfBirth(GuestContext *ctx);
void shim_sceNpGetAccountDateOfBirthA(GuestContext *ctx);
void shim_sceNpGetAccountId(GuestContext *ctx);
void shim_sceNpGetAccountIdA(GuestContext *ctx);
void shim_sceNpGetAccountLanguage(GuestContext *ctx);
void shim_sceNpGetAccountLanguageA(GuestContext *ctx);
void shim_sceNpGetGamePresenceStatus(GuestContext *ctx);
void shim_sceNpGetGamePresenceStatusA(GuestContext *ctx);
void shim_sceNpGetNpId(GuestContext *ctx);
void shim_sceNpGetNpReachabilityState(GuestContext *ctx);
void shim_sceNpGetOnlineId(GuestContext *ctx);
void shim_sceNpGetState(GuestContext *ctx);
void shim_sceNpHasSignedUp(GuestContext *ctx);
void shim_sceNpIsPlusMember(GuestContext *ctx);
void shim_sceNpPollAsync(GuestContext *ctx);
void shim_sceNpWaitAsync(GuestContext *ctx);
void shim_sceNpRegisterStateCallback(GuestContext *ctx);
void shim_sceNpRegisterStateCallbackA(GuestContext *ctx);
void shim_sceNpUnregisterStateCallback(GuestContext *ctx);
void shim_sceNpUnregisterStateCallbackA(GuestContext *ctx);

void shim_sceNpAuthCreateRequest(GuestContext *ctx);
void shim_sceNpAuthCreateAsyncRequest(GuestContext *ctx);
void shim_sceNpAuthDeleteRequest(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCode(GuestContext *ctx);
void shim_sceNpAuthGetAuthorizationCodeA(GuestContext *ctx);
void shim_sceNpAuthPollAsync(GuestContext *ctx);
void shim_sceNpAuthWaitAsync(GuestContext *ctx);

void shim_sceNpMatching2Initialize(GuestContext *ctx);
void shim_sceNpMatching2Terminate(GuestContext *ctx);
void shim_sceNpMatching2CreateContext(GuestContext *ctx);
void shim_sceNpMatching2CreateContextA(GuestContext *ctx);

void shim_sceNpSignalingInitialize(GuestContext *ctx);
void shim_sceNpSignalingTerminate(GuestContext *ctx);
void shim_sceNpSignalingCreateContext(GuestContext *ctx);
void shim_sceNpSignalingCreateContextA(GuestContext *ctx);

void shim_sceNpWebApiInitialize(GuestContext *ctx);
void shim_sceNpWebApiTerminate(GuestContext *ctx);
void shim_sceNpWebApiCreateContext(GuestContext *ctx);
void shim_sceNpWebApiCreateContextA(GuestContext *ctx);
void shim_sceNpWebApiDeleteContext(GuestContext *ctx);
void shim_sceNpWebApiCreateRequest(GuestContext *ctx);
void shim_sceNpWebApiSendRequest(GuestContext *ctx);
void shim_sceNpWebApiSendRequest2(GuestContext *ctx);
void shim_sceNpWebApiDeleteRequest(GuestContext *ctx);

void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpScoreDeleteNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateRequest(GuestContext *ctx);
void shim_sceNpScoreDeleteRequest(GuestContext *ctx);
void shim_sceNpScorePollAsync(GuestContext *ctx);
void shim_sceNpScoreWaitAsync(GuestContext *ctx);

void shim_sceNpTusCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpTusCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpTusDeleteNpTitleCtx(GuestContext *ctx);
void shim_sceNpTusCreateRequest(GuestContext *ctx);
void shim_sceNpTusDeleteRequest(GuestContext *ctx);
void shim_sceNpTusPollAsync(GuestContext *ctx);
void shim_sceNpTusWaitAsync(GuestContext *ctx);

void shim_sceNpCommerceDialogInitialize(GuestContext *ctx);
void shim_sceNpCommerceDialogTerminate(GuestContext *ctx);
void shim_sceNpCommerceDialogClose(GuestContext *ctx);
void shim_sceNpCommerceDialogGetStatus(GuestContext *ctx);
void shim_sceNpCommerceDialogGetResult(GuestContext *ctx);
void shim_sceNpCommerceDialogOpen(GuestContext *ctx);

void shim_sceNpUtilityInit(GuestContext *ctx);
void shim_sceNpUtilityTerm(GuestContext *ctx);
void shim_sceNpGameIntentInitialize(GuestContext *ctx);
void shim_sceNpGameIntentTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_H
