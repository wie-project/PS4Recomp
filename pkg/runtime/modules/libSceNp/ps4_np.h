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
#define ORBIS_NP_ERROR_CALLBACK_ALREADY_REGISTERED 0x80550008
#define ORBIS_NP_ERROR_REQUEST_MAX            0x80550013
#define ORBIS_NP_ERROR_REQUEST_NOT_FOUND      0x80550014
#define ORBIS_NP_ERROR_INVALID_SIZE           0x80550011
#define ORBIS_NP_ERROR_ABORTED                0x80550012
#define ORBIS_NP_ERROR_INVALID_ID             0x80550015
#define ORBIS_NP_UTIL_ERROR_NOT_MATCH         0x80550609

// NP Auth error codes

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

// C APIs
int32_t sceNpCmpOnlineId(const OrbisNpOnlineId* id1, const OrbisNpOnlineId* id2);
int32_t sceNpCmpNpId(const OrbisNpId* id1, const OrbisNpId* id2);


int32_t sceNpMatching2Initialize(void* param);
int32_t sceNpMatching2Terminate(void);
int32_t sceNpMatching2CreateContext(const void* param, uint16_t* ctxId);
int32_t sceNpMatching2CreateContextA(const void* param, uint16_t* ctxId);

int32_t sceNpSignalingInitialize(int64_t memorySize, int32_t threadPriority, int32_t cpuAffinityMask, int64_t threadStackSize);
int32_t sceNpSignalingTerminate(void);
int32_t sceNpSignalingCreateContext(const void* npId, void* callback, void* callbackArg, uint32_t* outContextId);
int32_t sceNpSignalingCreateContextA(int32_t userId, void* callback, void* callbackArg, uint32_t* outContextId);
int32_t sceNpSignalingSetContextOption(uint32_t ctxId, int32_t optionId, int32_t optionValue);
void ps4_np_signaling_ctx_forget(uint32_t ctxId);
int64_t ps4_np_signaling_ctx_flags(uint32_t ctxId);

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
int32_t sceNpTssCreateNpTitleCtxA(OrbisNpServiceLabel serviceLabel, int32_t userId);
int32_t sceNpTusSetThreadParam(void *param);
int32_t sceNpTusDeleteNpTitleCtx(int32_t ctxId);
int32_t sceNpTusCreateRequest(int32_t libCtxId);
int32_t sceNpTusDeleteRequest(int32_t requestId);
int32_t sceNpTusPollAsync(int32_t reqId, int32_t* result);
int32_t sceNpTusWaitAsync(int32_t reqId, int32_t* result);


int32_t sceNpGameIntentInitialize(void);
int32_t sceNpGameIntentTerminate(void);

int32_t sceNpTssGetData(void *ctx, void *data);
int32_t sceNpTssGetSmallStorage(int32_t reqId, void *data, uint64_t maxSize, uint64_t *contentLength, void *option);
int32_t sceNpTusAddAndGetVariableVUserAsync(int32_t reqId, uint64_t targetVirtualUserId, int32_t slotId, int64_t inVariable, void *outVariable, void *option);
int32_t sceNpTusGetMultiSlotVariableVUserAsync(int32_t reqId, uint64_t targetVirtualUserId, const int32_t *slotIdArray, uint32_t slotIdNum, void *outVariableArray, void *option);
int32_t sceNpTssCreateNpTitleCtx(OrbisNpServiceLabel serviceLabel, OrbisNpId* npId);


int32_t sceNpWebApiDeleteHandle(int32_t handle);

// GuestContext shims
void shim_sceNpCmpOnlineId(GuestContext *ctx);
void shim_sceNpCmpNpId(GuestContext *ctx);


void shim_sceNpMatching2Initialize(GuestContext *ctx);
void shim_sceNpMatching2Terminate(GuestContext *ctx);
void shim_sceNpMatching2CreateContext(GuestContext *ctx);
void shim_sceNpMatching2CreateContextA(GuestContext *ctx);
void shim_sceNpMatching2SignalingGetConnectionInfo(GuestContext *ctx);

void shim_sceNpSignalingInitialize(GuestContext *ctx);
void shim_sceNpSignalingTerminate(GuestContext *ctx);
void shim_sceNpSignalingCreateContext(GuestContext *ctx);
void shim_sceNpSignalingCreateContextA(GuestContext *ctx);
void shim_sceNpSignalingSetContextOption(GuestContext *ctx);

void shim_sceNpWebApiInitialize(GuestContext *ctx);
void shim_sceNpWebApiTerminate(GuestContext *ctx);
void shim_sceNpWebApiCreateContext(GuestContext *ctx);
void shim_sceNpWebApiCreateContextA(GuestContext *ctx);
void shim_sceNpWebApiDeleteContext(GuestContext *ctx);
void shim_sceNpWebApiCreateRequest(GuestContext *ctx);
void shim_sceNpWebApiSendRequest(GuestContext *ctx);
void shim_sceNpWebApiSendRequest2(GuestContext *ctx);
void shim_sceNpWebApiDeleteRequest(GuestContext *ctx);
void shim_sceNpWebApiGetErrorCode(GuestContext *ctx);

void shim_sceNpScoreCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpScoreDeleteNpTitleCtx(GuestContext *ctx);
void shim_sceNpScoreCreateRequest(GuestContext *ctx);
void shim_sceNpScoreDeleteRequest(GuestContext *ctx);
void shim_sceNpScorePollAsync(GuestContext *ctx);
void shim_sceNpScoreWaitAsync(GuestContext *ctx);
void shim_sceNpScoreCensorCommentAsync(GuestContext *ctx);
void shim_sceNpScoreGetFriendsRanking(GuestContext *ctx);
void shim_sceNpScoreGetRankingByNpId(GuestContext *ctx);
void shim_sceNpScoreRecordGameDataAsync(GuestContext *ctx);
void shim_sceNpScoreRecordScoreAsync(GuestContext *ctx);
void shim_sceNpScoreGetRankingByAccountId(GuestContext *ctx);
void shim_sceNpScoreGetGameDataByAccountId(GuestContext *ctx);
void shim_sceNpScoreGetRankingByRangeA(GuestContext *ctx);
void shim_sceNpScoreGetGameDataByAccountIdAsync(GuestContext *ctx);
void shim_sceNpScoreGetRankingByAccountIdAsync(GuestContext *ctx);
void shim_sceNpScoreGetFriendsRankingA(GuestContext *ctx);
void shim_sceNpScoreGetRankingByRangeAAsync(GuestContext *ctx);

void shim_sceNpMatching2GetRoomDataExternalList(GuestContext *ctx);
void shim_sceNpMatching2GetRoomMemberDataInternal(GuestContext *ctx);
void shim_sceNpMatching2RegisterLobbyMessageCallback(GuestContext *ctx);
void shim_sceNpMatching2GetUserInfoListA(GuestContext *ctx);
void shim_sceNpMatching2SendRoomMessage(GuestContext *ctx);
void shim_sceNpMatching2CreateJoinRoomA(GuestContext *ctx);
void shim_sceNpMatching2JoinRoomA(GuestContext *ctx);
void shim_sceNpMatching2SignalingGetConnectionInfoA(GuestContext *ctx);
void shim_sceNpMatching2AbortContextStart(GuestContext *ctx);
void shim_sceNpMatching2RegisterRoomMessageCallback(GuestContext *ctx);

void shim_sceNpTusCreateNpTitleCtx(GuestContext *ctx);
void shim_sceNpTusCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpTssCreateNpTitleCtxA(GuestContext *ctx);
void shim_sceNpTusSetThreadParam(GuestContext *ctx);
void shim_sceNpTusDeleteNpTitleCtx(GuestContext *ctx);
void shim_sceNpTusCreateRequest(GuestContext *ctx);
void shim_sceNpTusDeleteRequest(GuestContext *ctx);
void shim_sceNpTusPollAsync(GuestContext *ctx);
void shim_sceNpTusWaitAsync(GuestContext *ctx);
void shim_sceNpTusAbortRequest(GuestContext *ctx);
void shim_sceNpTusSetMultiSlotVariableAsync(GuestContext *ctx);
void shim_sceNpTusSetDataAsync(GuestContext *ctx);
void shim_sceNpTusSetDataAAsync(GuestContext *ctx);
void shim_sceNpTusAddAndGetVariableAsync(GuestContext *ctx);
void shim_sceNpTusGetMultiSlotVariableAsync(GuestContext *ctx);
void shim_sceNpTusGetDataAsync(GuestContext *ctx);
void shim_sceNpTusGetData(GuestContext *ctx);
void shim_sceNpTusGetDataVUser(GuestContext *ctx);
void shim_sceNpTusSetData(GuestContext *ctx);
void shim_sceNpTusSetDataVUser(GuestContext *ctx);
void shim_sceNpTusGetMultiSlotVariable(GuestContext *ctx);
void shim_sceNpTusGetMultiSlotVariableVUser(GuestContext *ctx);
void shim_sceNpTusSetMultiSlotVariable(GuestContext *ctx);
void shim_sceNpTusSetMultiSlotVariableVUser(GuestContext *ctx);
void shim_sceNpTusDeleteMultiSlotData(GuestContext *ctx);
void shim_sceNpTusDeleteMultiSlotDataVUser(GuestContext *ctx);
void shim_sceNpTusAddAndGetVariable(GuestContext *ctx);
void shim_sceNpTusAddAndGetVariableVUser(GuestContext *ctx);
void shim_sceNpScoreGetGameDataAsync(GuestContext *ctx);
void shim_sceNpScoreGetRankingByNpIdAsync(GuestContext *ctx);
void shim_sceNpScoreGetRankingByRangeAsync(GuestContext *ctx);
void shim_sceNpScoreGetFriendsRankingAsync(GuestContext *ctx);
void shim_sceNpTusGetDataAAsync(GuestContext *ctx);


void shim_sceNpGameIntentInitialize(GuestContext *ctx);
void shim_sceNpGameIntentTerminate(GuestContext *ctx);

void shim_sceNpTssGetData(GuestContext *ctx);
void shim_sceNpTssGetSmallStorage(GuestContext *ctx);
void shim_sceNpTusAddAndGetVariableVUserAsync(GuestContext *ctx);
void shim_sceNpTusGetMultiSlotVariableVUserAsync(GuestContext *ctx);
void shim_sceNpTssCreateNpTitleCtx(GuestContext *ctx);

void shim_sceNpWebApiDeleteHandle(GuestContext *ctx);

// libSceNpParty (ps4_np_party.c). Errors from shadPS4 np_error.h.
#define ORBIS_NP_PARTY_ERROR_INVALID_ARGUMENT ((int32_t)0x80552504)
#define ORBIS_NP_PARTY_ERROR_NOT_IN_PARTY ((int32_t)0x80552506)
// OrbisNpPartyState (u16).
#define ORBIS_NP_PARTY_STATE_NOT_IN_PARTY 2
#define ORBIS_NP_PARTY_STATE_IN_PRIVATE_PARTY 3
int32_t sceNpPartyGetState(uint16_t *state);
int32_t sceNpPartyGetMembers(void);
void shim_sceNpPartyCheckCallback(GuestContext *ctx);
void shim_sceNpPartyCreate(GuestContext *ctx);
void shim_sceNpPartyCreateA(GuestContext *ctx);
void shim_sceNpPartyGetId(GuestContext *ctx);
void shim_sceNpPartyGetMemberInfo(GuestContext *ctx);
void shim_sceNpPartyGetMemberInfoA(GuestContext *ctx);
void shim_sceNpPartyGetMembers(GuestContext *ctx);
void shim_sceNpPartyGetMembersA(GuestContext *ctx);
void shim_sceNpPartyGetMemberSessionInfo(GuestContext *ctx);
void shim_sceNpPartyGetMemberVoiceInfo(GuestContext *ctx);
void shim_sceNpPartyGetState(GuestContext *ctx);
void shim_sceNpPartyGetStateAsUser(GuestContext *ctx);
void shim_sceNpPartyGetStateAsUserA(GuestContext *ctx);
void shim_sceNpPartyGetVoiceChatPriority(GuestContext *ctx);
void shim_sceNpPartyInitialize(GuestContext *ctx);
void shim_sceNpPartyJoin(GuestContext *ctx);
void shim_sceNpPartyLeave(GuestContext *ctx);
void shim_sceNpPartyRegisterHandler(GuestContext *ctx);
void shim_sceNpPartyRegisterHandlerA(GuestContext *ctx);
void shim_sceNpPartyRegisterPrivateHandler(GuestContext *ctx);
void shim_sceNpPartySendBinaryMessage(GuestContext *ctx);
void shim_sceNpPartySetVoiceChatPriority(GuestContext *ctx);
void shim_sceNpPartyShowInvitationList(GuestContext *ctx);
void shim_sceNpPartyShowInvitationListA(GuestContext *ctx);
void shim_sceNpPartyTerminate(GuestContext *ctx);
void shim_sceNpPartyUnregisterPrivateHandler(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_H
