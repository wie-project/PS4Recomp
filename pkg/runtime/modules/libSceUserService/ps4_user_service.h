// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_USER_SERVICE_H
#define PS4_USER_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include "recomp_runtime.h"

#define ORBIS_USER_SERVICE_MAX_LOGIN_USERS 4
#define ORBIS_USER_SERVICE_MAX_REGISTER_USERS 16
#define ORBIS_USER_SERVICE_MAX_USER_NAME_LENGTH 16

#define ORBIS_USER_SERVICE_USER_ID_SYSTEM 0xFF
#define ORBIS_USER_SERVICE_USER_ID_INVALID -1
// The user this host logs in at boot, on player index 1 (shadPS4's default user).
#define ORBIS_USER_SERVICE_INITIAL_USER_ID 0x10000000

// Error codes (shadPS4 userservice_error.h).
#define ORBIS_USER_SERVICE_ERROR_INTERNAL ((int32_t)0x80960001)
#define ORBIS_USER_SERVICE_ERROR_NOT_INITIALIZED ((int32_t)0x80960002)
#define ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED ((int32_t)0x80960003)
#define ORBIS_USER_SERVICE_ERROR_NO_MEMORY ((int32_t)0x80960004)
#define ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT ((int32_t)0x80960005)
#define ORBIS_USER_SERVICE_ERROR_OPERATION_NOT_SUPPORTED ((int32_t)0x80960006)
#define ORBIS_USER_SERVICE_ERROR_NO_EVENT ((int32_t)0x80960007)
#define ORBIS_USER_SERVICE_ERROR_NOT_LOGGED_IN ((int32_t)0x80960009)
#define ORBIS_USER_SERVICE_ERROR_BUFFER_TOO_SHORT ((int32_t)0x8096000A)

typedef int32_t OrbisUserServiceUserId;

typedef struct OrbisUserServiceInitializeParams {
    int32_t priority;
} OrbisUserServiceInitializeParams;

typedef struct OrbisUserServiceLoginUserIdList {
    OrbisUserServiceUserId userId[ORBIS_USER_SERVICE_MAX_LOGIN_USERS];
} OrbisUserServiceLoginUserIdList;

typedef struct OrbisUserServiceRegisteredUserIdList {
    OrbisUserServiceUserId userId[ORBIS_USER_SERVICE_MAX_REGISTER_USERS];
} OrbisUserServiceRegisteredUserIdList;

// OrbisUserServiceUserColor
#define ORBIS_USER_SERVICE_USER_COLOR_BLUE 0
#define ORBIS_USER_SERVICE_USER_COLOR_RED 1
#define ORBIS_USER_SERVICE_USER_COLOR_GREEN 2
#define ORBIS_USER_SERVICE_USER_COLOR_PINK 3

// OrbisUserServiceEventType
#define ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN 0
#define ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT 1

typedef struct OrbisUserServiceEvent {
    int32_t eventType;
    OrbisUserServiceUserId userId;
} OrbisUserServiceEvent;

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceUserServiceInitialize(const OrbisUserServiceInitializeParams *params);
int32_t sceUserServiceGetInitialUser(int32_t *userId);
int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList *list);
int32_t sceUserServiceGetUserName(int32_t userId, char *userName, size_t size);
int32_t sceUserServiceGetEvent(OrbisUserServiceEvent *event);
int32_t sceUserServiceGetUserColor(int32_t userId, int32_t *color);
// callback is the guest address of a void (*)(const OrbisUserServiceEvent *, void *userdata).
int32_t sceUserServiceRegisterEventCallback(uint64_t callback, uint64_t userdata);
int32_t sceUserServiceUnregisterEventCallback(uint64_t callback, uint64_t userdata);

// Queues a login or logout event and calls the registered event callbacks on ctx's thread
// (shadPS4 AddUserServiceEvent). The runtime's only event source is the boot login of the
// initial user, queued before the guest can register a callback.
void ps4_user_service_add_event(GuestContext *ctx, int32_t eventType, OrbisUserServiceUserId userId);

// Guest ABI shims
void shim_sceUserServiceInitialize(GuestContext *ctx);
void shim_sceUserServiceGetInitialUser(GuestContext *ctx);
void shim_sceUserServiceGetLoginUserIdList(GuestContext *ctx);
void shim_sceUserServiceGetUserName(GuestContext *ctx);
void shim_sceUserServiceGetEvent(GuestContext *ctx);
void shim_sceUserServiceRegisterEventCallback(GuestContext *ctx);
void shim_sceUserServiceUnregisterEventCallback(GuestContext *ctx);
void shim_sceUserServiceGetUserColor(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_USER_SERVICE_H
