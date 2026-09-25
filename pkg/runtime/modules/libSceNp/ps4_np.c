#include "ps4_np.h"
#include <stdio.h>
#include <string.h>

#define NP_LOG(fmt, ...) printf("[libSceNp] " fmt "\n", ##__VA_ARGS__)

#define NP_REQUEST_OFFSET 0x20000000
#define NP_AUTH_REQUEST_OFFSET 0x10000000

static int32_t g_np_req_counter = 1;
static int32_t g_np_auth_counter = 1;
static int32_t g_score_req_counter = 1;
static int32_t g_tus_req_counter = 1;
static int32_t g_matching_ctx_counter = 1;
static int32_t g_signaling_ctx_counter = 1;
static int32_t g_webapi_user_ctx_counter = 1;
static int64_t g_webapi_req_counter = 1;

static int8_t g_commerce_status = 0; // 0=None, 1=Initialized, 2=Running, 3=Finished

//=============================================================================
// libSceNpManager
//=============================================================================

int32_t sceNpCheckNpAvailability(int32_t req_id, OrbisNpOnlineId* online_id) {
    (void)online_id;
    NP_LOG("sceNpCheckNpAvailability(req_id=%d) -> SIGNED_OUT", req_id);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpCheckNpAvailabilityA(int32_t req_id, int32_t user_id) {
    NP_LOG("sceNpCheckNpAvailabilityA(req_id=%d, user_id=%d) -> SIGNED_OUT", req_id, user_id);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpCheckPlus(int32_t req_id, const OrbisNpCheckPlusParameter* param, OrbisNpCheckPlusResult* result) {
    NP_LOG("sceNpCheckPlus(req_id=%d) -> SIGNED_OUT", req_id);
    if (!param || !result) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    result->authorized = false;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpCreateAsyncRequest(const OrbisNpCreateAsyncRequestParameter* param) {
    if (!param) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    int32_t id = NP_REQUEST_OFFSET + (g_np_req_counter++);
    NP_LOG("sceNpCreateAsyncRequest -> id=%d", id);
    return id;
}

int32_t sceNpCreateRequest(void) {
    int32_t id = NP_REQUEST_OFFSET + (g_np_req_counter++);
    NP_LOG("sceNpCreateRequest -> id=%d", id);
    return id;
}

int32_t sceNpDeleteRequest(int32_t req_id) {
    NP_LOG("sceNpDeleteRequest(req_id=%d)", req_id);
    return 0;
}

int32_t sceNpGetAccountCountry(OrbisNpOnlineId* online_id, OrbisNpCountryCode* country_code) {
    (void)online_id;
    if (!country_code) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    country_code->country_code[0] = 'u';
    country_code->country_code[1] = 's';
    country_code->end = '\0';
    country_code->pad = 0;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountCountryA(int32_t user_id, OrbisNpCountryCode* country_code) {
    (void)user_id;
    if (!country_code) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    country_code->country_code[0] = 'u';
    country_code->country_code[1] = 's';
    country_code->end = '\0';
    country_code->pad = 0;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountDateOfBirth(OrbisNpOnlineId* online_id, OrbisNpDate* date_of_birth) {
    (void)online_id;
    if (!date_of_birth) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    date_of_birth->year = 2000;
    date_of_birth->month = 1;
    date_of_birth->day = 1;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountDateOfBirthA(int32_t user_id, OrbisNpDate* date_of_birth) {
    (void)user_id;
    if (!date_of_birth) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    date_of_birth->year = 2000;
    date_of_birth->month = 1;
    date_of_birth->day = 1;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountId(OrbisNpOnlineId* online_id, uint64_t* account_id) {
    (void)online_id;
    if (!account_id) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *account_id = 0;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountIdA(int32_t user_id, uint64_t* account_id) {
    (void)user_id;
    if (!account_id) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *account_id = 0;
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountLanguage(OrbisNpOnlineId* online_id, OrbisNpLanguageCode* language) {
    (void)online_id;
    if (!language) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    strncpy(language->code, "en", sizeof(language->code));
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetAccountLanguageA(int32_t user_id, OrbisNpLanguageCode* language) {
    (void)user_id;
    if (!language) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    strncpy(language->code, "en", sizeof(language->code));
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetGamePresenceStatus(OrbisNpOnlineId* online_id, int32_t* game_status) {
    (void)online_id;
    if (!game_status) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *game_status = 0; // Offline
    return 0;
}

int32_t sceNpGetGamePresenceStatusA(int32_t user_id, int32_t* game_status) {
    (void)user_id;
    if (!game_status) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *game_status = 0; // Offline
    return 0;
}

int32_t sceNpGetNpId(int32_t user_id, OrbisNpId* np_id) {
    (void)np_id;
    NP_LOG("sceNpGetNpId(user_id=%d) -> SIGNED_OUT", user_id);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetNpReachabilityState(int32_t user_id, int32_t* state) {
    (void)user_id;
    if (!state) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *state = 0; // Unavailable
    return 0;
}

int32_t sceNpGetOnlineId(int32_t user_id, OrbisNpOnlineId* online_id) {
    (void)online_id;
    NP_LOG("sceNpGetOnlineId(user_id=%d) -> SIGNED_OUT", user_id);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpGetState(int32_t user_id, int32_t* state) {
    if (!state) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *state = 1; // SignedOut (0=NotSignedUp, 1=SignedOut, 2=SignedIn)
    NP_LOG("sceNpGetState(user_id=%d) -> SignedOut (1)", user_id);
    return 0;
}

int32_t sceNpHasSignedUp(int32_t user_id, bool* has_signed_up) {
    (void)user_id;
    if (!has_signed_up) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *has_signed_up = false;
    return 0;
}

int32_t sceNpIsPlusMember(int32_t user_id, bool* result) {
    (void)user_id;
    if (!result) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *result = false;
    return 0;
}

int32_t sceNpPollAsync(int32_t req_id, int32_t* result) {
    (void)req_id;
    if (!result) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_ERROR_SIGNED_OUT;
    return 0; // Returns OK, *result contains operation failure
}

int32_t sceNpWaitAsync(int32_t req_id, int32_t* result) {
    (void)req_id;
    if (!result) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_ERROR_SIGNED_OUT;
    return 0;
}

int32_t sceNpRegisterStateCallback(void* callback, void* userdata) {
    (void)callback;
    (void)userdata;
    NP_LOG("sceNpRegisterStateCallback");
    return 0;
}

int32_t sceNpRegisterStateCallbackA(void* callback, void* userdata) {
    (void)callback;
    (void)userdata;
    NP_LOG("sceNpRegisterStateCallbackA");
    return 1; // Callback handle
}

int32_t sceNpUnregisterStateCallback(int32_t callback_id) {
    (void)callback_id;
    return 0;
}

int32_t sceNpUnregisterStateCallbackA(int32_t callback_id) {
    (void)callback_id;
    return 0;
}

// GuestContext shims for NpManager
void shim_sceNpCheckNpAvailability(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCheckNpAvailability((int32_t)ctx->rdi, (OrbisNpOnlineId*)ctx->rsi);
}

void shim_sceNpCheckNpAvailabilityA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCheckNpAvailabilityA((int32_t)ctx->rdi, (int32_t)ctx->rsi);
}

void shim_sceNpCheckPlus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCheckPlus((int32_t)ctx->rdi, (const OrbisNpCheckPlusParameter*)ctx->rsi, (OrbisNpCheckPlusResult*)ctx->rdx);
}

void shim_sceNpCreateAsyncRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCreateAsyncRequest((const OrbisNpCreateAsyncRequestParameter*)ctx->rdi);
}

void shim_sceNpCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCreateRequest();
}

void shim_sceNpDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpDeleteRequest((int32_t)ctx->rdi);
}

void shim_sceNpGetAccountCountry(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountCountry((OrbisNpOnlineId*)ctx->rdi, (OrbisNpCountryCode*)ctx->rsi);
}

void shim_sceNpGetAccountCountryA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountCountryA((int32_t)ctx->rdi, (OrbisNpCountryCode*)ctx->rsi);
}

void shim_sceNpGetAccountDateOfBirth(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountDateOfBirth((OrbisNpOnlineId*)ctx->rdi, (OrbisNpDate*)ctx->rsi);
}

void shim_sceNpGetAccountDateOfBirthA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountDateOfBirthA((int32_t)ctx->rdi, (OrbisNpDate*)ctx->rsi);
}

void shim_sceNpGetAccountId(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountId((OrbisNpOnlineId*)ctx->rdi, (uint64_t*)ctx->rsi);
}

void shim_sceNpGetAccountIdA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountIdA((int32_t)ctx->rdi, (uint64_t*)ctx->rsi);
}

void shim_sceNpGetAccountLanguage(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountLanguage((OrbisNpOnlineId*)ctx->rdi, (OrbisNpLanguageCode*)ctx->rsi);
}

void shim_sceNpGetAccountLanguageA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetAccountLanguageA((int32_t)ctx->rdi, (OrbisNpLanguageCode*)ctx->rsi);
}

void shim_sceNpGetGamePresenceStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetGamePresenceStatus((OrbisNpOnlineId*)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpGetGamePresenceStatusA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetGamePresenceStatusA((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpGetNpId(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetNpId((int32_t)ctx->rdi, (OrbisNpId*)ctx->rsi);
}

void shim_sceNpGetNpReachabilityState(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetNpReachabilityState((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpGetOnlineId(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetOnlineId((int32_t)ctx->rdi, (OrbisNpOnlineId*)ctx->rsi);
}

void shim_sceNpGetState(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGetState((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpHasSignedUp(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpHasSignedUp((int32_t)ctx->rdi, (bool*)ctx->rsi);
}

void shim_sceNpIsPlusMember(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpIsPlusMember((int32_t)ctx->rdi, (bool*)ctx->rsi);
}

void shim_sceNpPollAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpPollAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpWaitAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWaitAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpRegisterStateCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpRegisterStateCallback((void*)ctx->rdi, (void*)ctx->rsi);
}

void shim_sceNpRegisterStateCallbackA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpRegisterStateCallbackA((void*)ctx->rdi, (void*)ctx->rsi);
}

void shim_sceNpUnregisterStateCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpUnregisterStateCallback((int32_t)ctx->rdi);
}

void shim_sceNpUnregisterStateCallbackA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpUnregisterStateCallbackA((int32_t)ctx->rdi);
}

//=============================================================================
// libSceNpAuth
//=============================================================================

int32_t sceNpAuthCreateRequest(void) {
    int32_t id = NP_AUTH_REQUEST_OFFSET + (g_np_auth_counter++);
    NP_LOG("sceNpAuthCreateRequest -> id=%d", id);
    return id;
}

int32_t sceNpAuthCreateAsyncRequest(const OrbisNpAuthCreateAsyncRequestParameter* param) {
    if (!param) return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    int32_t id = NP_AUTH_REQUEST_OFFSET + (g_np_auth_counter++);
    NP_LOG("sceNpAuthCreateAsyncRequest -> id=%d", id);
    return id;
}

int32_t sceNpAuthDeleteRequest(int32_t req_id) {
    NP_LOG("sceNpAuthDeleteRequest(req_id=%d)", req_id);
    return 0;
}

int32_t sceNpAuthGetAuthorizationCode(int32_t req_id, const void* param, OrbisNpAuthorizationCode* auth_code, int32_t* issuer_id) {
    (void)param;
    (void)auth_code;
    (void)issuer_id;
    NP_LOG("sceNpAuthGetAuthorizationCode(req_id=%d) -> SIGNED_OUT", req_id);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpAuthGetAuthorizationCodeA(int32_t req_id, const OrbisNpAuthGetAuthorizationCodeParameterA* param, OrbisNpAuthorizationCode* auth_code, int32_t* issuer_id) {
    (void)param;
    (void)auth_code;
    (void)issuer_id;
    NP_LOG("sceNpAuthGetAuthorizationCodeA(req_id=%d) -> SIGNED_OUT", req_id);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpAuthPollAsync(int32_t req_id, int32_t* result) {
    (void)req_id;
    if (!result) return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_ERROR_SIGNED_OUT;
    return 0;
}

int32_t sceNpAuthWaitAsync(int32_t req_id, int32_t* result) {
    (void)req_id;
    if (!result) return ORBIS_NP_AUTH_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_ERROR_SIGNED_OUT;
    return 0;
}

void shim_sceNpAuthCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthCreateRequest();
}

void shim_sceNpAuthCreateAsyncRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthCreateAsyncRequest((const OrbisNpAuthCreateAsyncRequestParameter*)ctx->rdi);
}

void shim_sceNpAuthDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthDeleteRequest((int32_t)ctx->rdi);
}

void shim_sceNpAuthGetAuthorizationCode(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthGetAuthorizationCode((int32_t)ctx->rdi, (const void*)ctx->rsi, (OrbisNpAuthorizationCode*)ctx->rdx, (int32_t*)ctx->rcx);
}

void shim_sceNpAuthGetAuthorizationCodeA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthGetAuthorizationCodeA((int32_t)ctx->rdi, (const OrbisNpAuthGetAuthorizationCodeParameterA*)ctx->rsi, (OrbisNpAuthorizationCode*)ctx->rdx, (int32_t*)ctx->rcx);
}

void shim_sceNpAuthPollAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthPollAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpAuthWaitAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpAuthWaitAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

//=============================================================================
// libSceNpMatching2
//=============================================================================

int32_t sceNpMatching2Initialize(void* param) {
    (void)param;
    NP_LOG("sceNpMatching2Initialize");
    return 0;
}

int32_t sceNpMatching2Terminate(void) {
    NP_LOG("sceNpMatching2Terminate");
    return 0;
}

int32_t sceNpMatching2CreateContext(const void* param, uint16_t* ctxId) {
    (void)param;
    if (!ctxId) return ORBIS_NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    *ctxId = (uint16_t)(g_matching_ctx_counter++);
    NP_LOG("sceNpMatching2CreateContext -> ctxId=%u", *ctxId);
    return 0;
}

int32_t sceNpMatching2CreateContextA(const void* param, uint16_t* ctxId) {
    (void)param;
    if (!ctxId) return ORBIS_NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    *ctxId = (uint16_t)(g_matching_ctx_counter++);
    NP_LOG("sceNpMatching2CreateContextA -> ctxId=%u", *ctxId);
    return 0;
}

void shim_sceNpMatching2Initialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2Initialize((void*)ctx->rdi);
}

void shim_sceNpMatching2Terminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2Terminate();
}

void shim_sceNpMatching2CreateContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2CreateContext((const void*)ctx->rdi, (uint16_t*)ctx->rsi);
}

void shim_sceNpMatching2CreateContextA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpMatching2CreateContextA((const void*)ctx->rdi, (uint16_t*)ctx->rsi);
}

//=============================================================================
// libSceNpSignaling
//=============================================================================

int32_t sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t cpuAffinityMask, int64_t threadStackSize) {
    (void)memorySize;
    (void)threadPriority;
    (void)cpuAffinityMask;
    (void)threadStackSize;
    NP_LOG("sceNpSignalingInitialize");
    return 0;
}

int32_t sceNpSignalingTerminate(void) {
    NP_LOG("sceNpSignalingTerminate");
    return 0;
}

int32_t sceNpSignalingCreateContext(const void* npId, void* callback, void* callbackArg, uint32_t* outContextId) {
    (void)npId;
    (void)callback;
    (void)callbackArg;
    if (!outContextId) return ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT;
    *outContextId = (uint32_t)(g_signaling_ctx_counter++);
    NP_LOG("sceNpSignalingCreateContext -> outContextId=%u", *outContextId);
    return 0;
}

int32_t sceNpSignalingCreateContextA(int32_t userId, void* callback, void* callbackArg, uint32_t* outContextId) {
    (void)userId;
    (void)callback;
    (void)callbackArg;
    if (!outContextId) return ORBIS_NP_SIGNALING_ERROR_INVALID_ARGUMENT;
    *outContextId = (uint32_t)(g_signaling_ctx_counter++);
    NP_LOG("sceNpSignalingCreateContextA -> outContextId=%u", *outContextId);
    return 0;
}

void shim_sceNpSignalingInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingInitialize((int64_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, (int64_t)ctx->rcx);
}

void shim_sceNpSignalingTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingTerminate();
}

void shim_sceNpSignalingCreateContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingCreateContext((const void*)ctx->rdi, (void*)ctx->rsi, (void*)ctx->rdx, (uint32_t*)ctx->rcx);
}

void shim_sceNpSignalingCreateContextA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpSignalingCreateContextA((int32_t)ctx->rdi, (void*)ctx->rsi, (void*)ctx->rdx, (uint32_t*)ctx->rcx);
}

//=============================================================================
// libSceNpWebApi & libSceNpWebApi2
//=============================================================================

int32_t sceNpWebApiInitialize(int32_t libHttpCtxId, uint64_t poolSize) {
    NP_LOG("sceNpWebApiInitialize(libHttpCtxId=%d, poolSize=%llu)", libHttpCtxId, (unsigned long long)poolSize);
    return 0;
}

int32_t sceNpWebApiTerminate(int32_t libCtxId) {
    NP_LOG("sceNpWebApiTerminate(libCtxId=%d)", libCtxId);
    return 0;
}

int32_t sceNpWebApiCreateContext(int32_t libCtxId, OrbisNpOnlineId* onlineId) {
    (void)onlineId;
    int32_t id = (libCtxId << 16) | (g_webapi_user_ctx_counter++);
    NP_LOG("sceNpWebApiCreateContext -> userCtxId=%d", id);
    return id;
}

int32_t sceNpWebApiCreateContextA(int32_t libCtxId, int32_t userId) {
    (void)userId;
    int32_t id = (libCtxId << 16) | (g_webapi_user_ctx_counter++);
    NP_LOG("sceNpWebApiCreateContextA -> userCtxId=%d", id);
    return id;
}

int32_t sceNpWebApiDeleteContext(int32_t titleUserCtxId) {
    NP_LOG("sceNpWebApiDeleteContext(titleUserCtxId=%d)", titleUserCtxId);
    return 0;
}

int32_t sceNpWebApiCreateRequest(int32_t titleUserCtxId, const char* pApiGroup, const char* pPath, int32_t method, const void* pContentParameter, int64_t* pRequestId) {
    (void)pApiGroup;
    (void)method;
    (void)pContentParameter;
    if (!pRequestId) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    *pRequestId = ((int64_t)titleUserCtxId << 32) | (g_webapi_req_counter++);
    NP_LOG("sceNpWebApiCreateRequest(path=%s) -> reqId=%lld", pPath ? pPath : "null", (long long)*pRequestId);
    return 0;
}

int32_t sceNpWebApiSendRequest(int64_t requestId, const void* pData, uint64_t dataSize) {
    (void)pData;
    (void)dataSize;
    NP_LOG("sceNpWebApiSendRequest(reqId=%lld) -> NOT_SIGNED_IN", (long long)requestId);
    return ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
}

int32_t sceNpWebApiSendRequest2(int64_t requestId, const void* pData, uint64_t dataSize, void* pRespInfoOption) {
    (void)pData;
    (void)dataSize;
    (void)pRespInfoOption;
    NP_LOG("sceNpWebApiSendRequest2(reqId=%lld) -> NOT_SIGNED_IN", (long long)requestId);
    return ORBIS_NP_WEBAPI_ERROR_NOT_SIGNED_IN;
}

int32_t sceNpWebApiDeleteRequest(int64_t requestId) {
    NP_LOG("sceNpWebApiDeleteRequest(reqId=%lld)", (long long)requestId);
    return 0;
}

void shim_sceNpWebApiInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiInitialize((int32_t)ctx->rdi, (uint64_t)ctx->rsi);
}

void shim_sceNpWebApiTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiTerminate((int32_t)ctx->rdi);
}

void shim_sceNpWebApiCreateContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiCreateContext((int32_t)ctx->rdi, (OrbisNpOnlineId*)ctx->rsi);
}

void shim_sceNpWebApiCreateContextA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiCreateContextA((int32_t)ctx->rdi, (int32_t)ctx->rsi);
}

void shim_sceNpWebApiDeleteContext(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiDeleteContext((int32_t)ctx->rdi);
}

void shim_sceNpWebApiCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiCreateRequest(
        (int32_t)ctx->rdi, (const char*)ctx->rsi, (const char*)ctx->rdx, (int32_t)ctx->rcx, (const void*)ctx->r8, (int64_t*)ctx->r9);
}

void shim_sceNpWebApiSendRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiSendRequest((int64_t)ctx->rdi, (const void*)ctx->rsi, (uint64_t)ctx->rdx);
}

void shim_sceNpWebApiSendRequest2(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiSendRequest2((int64_t)ctx->rdi, (const void*)ctx->rsi, (uint64_t)ctx->rdx, (void*)ctx->rcx);
}

void shim_sceNpWebApiDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpWebApiDeleteRequest((int64_t)ctx->rdi);
}

//=============================================================================
// libSceNpScore
//=============================================================================

int32_t sceNpScoreCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, const OrbisNpId* selfNpId) {
    (void)serviceLabel;
    (void)selfNpId;
    NP_LOG("sceNpScoreCreateNpTitleCtx -> SIGNED_OUT");
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpScoreCreateNpTitleCtxA(OrbisNpServiceLabel npServiceLabel, int32_t selfId) {
    (void)npServiceLabel;
    (void)selfId;
    NP_LOG("sceNpScoreCreateNpTitleCtxA(selfId=%d) -> SIGNED_OUT", selfId);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpScoreDeleteNpTitleCtx(int32_t titleCtxId) {
    NP_LOG("sceNpScoreDeleteNpTitleCtx(titleCtxId=%d)", titleCtxId);
    return 0;
}

int32_t sceNpScoreCreateRequest(int32_t titleCtxId) {
    (void)titleCtxId;
    int32_t id = g_score_req_counter++;
    NP_LOG("sceNpScoreCreateRequest(titleCtxId=%d) -> reqId=%d", titleCtxId, id);
    return id;
}

int32_t sceNpScoreDeleteRequest(int32_t reqId) {
    NP_LOG("sceNpScoreDeleteRequest(reqId=%d)", reqId);
    return 0;
}

int32_t sceNpScorePollAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

int32_t sceNpScoreWaitAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreCreateNpTitleCtx((OrbisNpServiceLabel)ctx->rdi, (const OrbisNpId*)ctx->rsi);
}

void shim_sceNpScoreCreateNpTitleCtxA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreCreateNpTitleCtxA((OrbisNpServiceLabel)ctx->rdi, (int32_t)ctx->rsi);
}

void shim_sceNpScoreDeleteNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreDeleteNpTitleCtx((int32_t)ctx->rdi);
}

void shim_sceNpScoreCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreCreateRequest((int32_t)ctx->rdi);
}

void shim_sceNpScoreDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreDeleteRequest((int32_t)ctx->rdi);
}

void shim_sceNpScorePollAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScorePollAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpScoreWaitAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpScoreWaitAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

//=============================================================================
// libSceNpTus
//=============================================================================

int32_t sceNpTusCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, OrbisNpId* npId) {
    (void)serviceLabel;
    (void)npId;
    NP_LOG("sceNpTusCreateNpTitleCtx -> SIGNED_OUT");
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpTusCreateNpTitleCtxA(OrbisNpServiceLabel serviceLabel, int32_t userId) {
    (void)serviceLabel;
    (void)userId;
    NP_LOG("sceNpTusCreateNpTitleCtxA(userId=%d) -> SIGNED_OUT", userId);
    return ORBIS_NP_ERROR_SIGNED_OUT;
}

int32_t sceNpTusDeleteNpTitleCtx(int32_t ctxId) {
    NP_LOG("sceNpTusDeleteNpTitleCtx(ctxId=%d)", ctxId);
    return 0;
}

int32_t sceNpTusCreateRequest(int32_t libCtxId) {
    (void)libCtxId;
    int32_t id = g_tus_req_counter++;
    NP_LOG("sceNpTusCreateRequest(libCtxId=%d) -> reqId=%d", libCtxId, id);
    return id;
}

int32_t sceNpTusDeleteRequest(int32_t requestId) {
    NP_LOG("sceNpTusDeleteRequest(requestId=%d)", requestId);
    return 0;
}

int32_t sceNpTusPollAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

int32_t sceNpTusWaitAsync(int32_t reqId, int32_t* result) {
    (void)reqId;
    if (!result) return ORBIS_NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    *result = ORBIS_NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

void shim_sceNpTusCreateNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusCreateNpTitleCtx((OrbisNpServiceLabel)ctx->rdi, (OrbisNpId*)ctx->rsi);
}

void shim_sceNpTusCreateNpTitleCtxA(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusCreateNpTitleCtxA((OrbisNpServiceLabel)ctx->rdi, (int32_t)ctx->rsi);
}

void shim_sceNpTusDeleteNpTitleCtx(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusDeleteNpTitleCtx((int32_t)ctx->rdi);
}

void shim_sceNpTusCreateRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusCreateRequest((int32_t)ctx->rdi);
}

void shim_sceNpTusDeleteRequest(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusDeleteRequest((int32_t)ctx->rdi);
}

void shim_sceNpTusPollAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusPollAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

void shim_sceNpTusWaitAsync(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpTusWaitAsync((int32_t)ctx->rdi, (int32_t*)ctx->rsi);
}

//=============================================================================
// libSceNpCommerce
//=============================================================================

int32_t sceNpCommerceDialogInitialize(void) {
    NP_LOG("sceNpCommerceDialogInitialize");
    g_commerce_status = 1; // Initialized
    return 0;
}

int32_t sceNpCommerceDialogTerminate(void) {
    NP_LOG("sceNpCommerceDialogTerminate");
    g_commerce_status = 0; // None
    return 0;
}

int32_t sceNpCommerceDialogClose(void) {
    NP_LOG("sceNpCommerceDialogClose");
    g_commerce_status = 3; // Finished
    return 0;
}

int8_t sceNpCommerceDialogGetStatus(void) {
    return g_commerce_status;
}

int32_t sceNpCommerceDialogGetResult(OrbisNpCommerceDialogResult* result) {
    if (!result) return ORBIS_NP_ERROR_INVALID_ARGUMENT;
    result->result = 1; // User canceled
    result->authorized = false;
    return 0;
}

int32_t sceNpCommerceDialogOpen(const void* param) {
    (void)param;
    NP_LOG("sceNpCommerceDialogOpen -> Finished (canceled)");
    g_commerce_status = 3; // Finished
    return 0;
}

void shim_sceNpCommerceDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCommerceDialogInitialize();
}

void shim_sceNpCommerceDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCommerceDialogTerminate();
}

void shim_sceNpCommerceDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCommerceDialogClose();
}

void shim_sceNpCommerceDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCommerceDialogGetStatus();
}

void shim_sceNpCommerceDialogGetResult(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCommerceDialogGetResult((OrbisNpCommerceDialogResult*)ctx->rdi);
}

void shim_sceNpCommerceDialogOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpCommerceDialogOpen((const void*)ctx->rdi);
}

//=============================================================================
// libSceNpUtility & libSceNpGameIntent
//=============================================================================

int32_t sceNpUtilityInit(void) {
    NP_LOG("sceNpUtilityInit");
    return 0;
}

int32_t sceNpUtilityTerm(void) {
    NP_LOG("sceNpUtilityTerm");
    return 0;
}

int32_t sceNpGameIntentInitialize(void) {
    NP_LOG("sceNpGameIntentInitialize");
    return 0;
}

int32_t sceNpGameIntentTerminate(void) {
    NP_LOG("sceNpGameIntentTerminate");
    return 0;
}

void shim_sceNpUtilityInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpUtilityInit();
}

void shim_sceNpUtilityTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpUtilityTerm();
}

void shim_sceNpGameIntentInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGameIntentInitialize();
}

void shim_sceNpGameIntentTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceNpGameIntentTerminate();
}
