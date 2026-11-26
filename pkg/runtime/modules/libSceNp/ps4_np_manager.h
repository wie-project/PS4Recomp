// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_MANAGER_H
#define PS4_NP_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "ps4_np.h"

#ifdef __cplusplus
extern "C" {
#endif

// shadPS4 np/np_manager.h
#define ORBIS_NP_MANAGER_REQUEST_LIMIT 0x20
#define ORBIS_NP_MANAGER_REQUEST_ID_OFFSET 0x20000000
#define ORBIS_NP_STATE_CALLBACK_MAX 8

// OrbisNpState
#define ORBIS_NP_STATE_NOT_SIGNED_UP 0
#define ORBIS_NP_STATE_SIGNED_OUT 1
#define ORBIS_NP_STATE_SIGNED_IN 2

// OrbisNpReachabilityState
#define ORBIS_NP_REACHABILITY_STATE_UNAVAILABLE 0
#define ORBIS_NP_REACHABILITY_STATE_AVAILABLE 1
#define ORBIS_NP_REACHABILITY_STATE_REACHABLE 2

// OrbisNpGamePresenseStatus
#define ORBIS_NP_GAME_PRESENCE_STATUS_OFFLINE 0
#define ORBIS_NP_GAME_PRESENCE_STATUS_ONLINE 1

// The firmware versions from which an invalid user id is an invalid argument (shadPS4 FW_400,
// FW_900); compared with the game's compiled SDK version.
#define ORBIS_NP_SDK_FW_400 0x4000000u
#define ORBIS_NP_SDK_FW_900 0x9000000u

typedef struct OrbisNpParentalControlInfo {
    bool content_restriction;
    bool chat_restriction;
    bool user_generated_content_restriction;
} OrbisNpParentalControlInfo;

typedef struct OrbisNpContentRestriction {
    uint64_t size;
    int8_t default_age_restriction;
    uint8_t padding[3];
    int32_t age_restriction_count;
    uint64_t age_restriction; // const OrbisNpAgeRestriction * (guest address)
} OrbisNpContentRestriction;

// Called by libSceUserService when the game takes a login or logout event
// (shadPS4 NotifyNpStateFromUserServiceEvent): without a sign-in, the user's NP state is
// SignedOut and is reported to the state callbacks at the next sceNpCheckCallback.
void ps4_np_manager_notify_user_event(int32_t eventType, int32_t userId);

void shim_sceNpCreateRequest(GuestContext *ctx);
void shim_sceNpCreateAsyncRequest(GuestContext *ctx);
void shim_sceNpCheckNpAvailability(GuestContext *ctx);
void shim_sceNpCheckNpAvailabilityA(GuestContext *ctx);
void shim_sceNpCheckNpReachability(GuestContext *ctx);
void shim_sceNpCheckPlus(GuestContext *ctx);
void shim_sceNpGetAccountLanguage(GuestContext *ctx);
void shim_sceNpGetAccountLanguageA(GuestContext *ctx);
void shim_sceNpGetParentalControlInfo(GuestContext *ctx);
void shim_sceNpGetParentalControlInfoA(GuestContext *ctx);
void shim_sceNpAbortRequest(GuestContext *ctx);
void shim_sceNpSetTimeout(GuestContext *ctx);
void shim_sceNpWaitAsync(GuestContext *ctx);
void shim_sceNpPollAsync(GuestContext *ctx);
void shim_sceNpDeleteRequest(GuestContext *ctx);
void shim_sceNpGetAccountCountry(GuestContext *ctx);
void shim_sceNpGetAccountCountryA(GuestContext *ctx);
void shim_sceNpGetAccountDateOfBirth(GuestContext *ctx);
void shim_sceNpGetAccountDateOfBirthA(GuestContext *ctx);
void shim_sceNpGetGamePresenceStatus(GuestContext *ctx);
void shim_sceNpGetGamePresenceStatusA(GuestContext *ctx);
void shim_sceNpGetAccountId(GuestContext *ctx);
void shim_sceNpGetAccountIdA(GuestContext *ctx);
void shim_sceNpGetNpId(GuestContext *ctx);
void shim_sceNpGetOnlineId(GuestContext *ctx);
void shim_sceNpGetNpReachabilityState(GuestContext *ctx);
void shim_sceNpGetState(GuestContext *ctx);
void shim_sceNpGetUserIdByAccountId(GuestContext *ctx);
void shim_sceNpGetUserIdByOnlineId(GuestContext *ctx);
void shim_sceNpHasSignedUp(GuestContext *ctx);
void shim_sceNpSetContentRestriction(GuestContext *ctx);
void shim_sceNpSetNpTitleId(GuestContext *ctx);
void shim_sceNpCheckCallback(GuestContext *ctx);
void shim_sceNpCheckCallbackForLib(GuestContext *ctx);
void shim_sceNpRegisterStateCallback(GuestContext *ctx);
void shim_sceNpUnregisterStateCallback(GuestContext *ctx);
void shim_sceNpRegisterStateCallbackA(GuestContext *ctx);
void shim_sceNpUnregisterStateCallbackA(GuestContext *ctx);
void shim_sceNpRegisterNpReachabilityStateCallback(GuestContext *ctx);
void shim_sceNpUnregisterNpReachabilityStateCallback(GuestContext *ctx);
void shim_sceNpRegisterStateCallbackForToolkit(GuestContext *ctx);
void shim_sceNpUnregisterStateCallbackForToolkit(GuestContext *ctx);
void shim_sceNpSetGamePresenceOnline(GuestContext *ctx);
void shim_sceNpSetGamePresenceOnlineA(GuestContext *ctx);
void shim_sceNpRegisterGamePresenceCallback(GuestContext *ctx);
void shim_sceNpRegisterGamePresenceCallbackA(GuestContext *ctx);
void shim_sceNpUnregisterGamePresenceCallbackA(GuestContext *ctx);
void shim_sceNpIsPlusMember(GuestContext *ctx);
void shim_sceNpNotifyPlusFeature(GuestContext *ctx);
void shim_sceNpRegisterPlusEventCallback(GuestContext *ctx);
void shim_sceNpUnregisterPlusEventCallback(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_MANAGER_H
