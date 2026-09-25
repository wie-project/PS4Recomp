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
#define ORBIS_USER_SERVICE_INITIAL_USER_ID 0x10000000

typedef int32_t OrbisUserServiceUserId;

typedef struct OrbisUserServiceInitializeParams {
    uint32_t priority;
} OrbisUserServiceInitializeParams;

typedef struct OrbisUserServiceLoginUserIdList {
    OrbisUserServiceUserId userId[ORBIS_USER_SERVICE_MAX_LOGIN_USERS];
} OrbisUserServiceLoginUserIdList;

typedef struct OrbisUserServiceRegisteredUserIdList {
    OrbisUserServiceUserId userId[ORBIS_USER_SERVICE_MAX_REGISTER_USERS];
} OrbisUserServiceRegisteredUserIdList;

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceUserServiceInitialize(const OrbisUserServiceInitializeParams *params);
int32_t sceUserServiceGetInitialUser(int32_t *userId);
int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList *list);
int32_t sceUserServiceGetUserName(int32_t userId, char *userName, const size_t size);
int32_t sceUserServiceTerminate(void);

// Guest ABI shims
void shim_sceUserServiceInitialize(GuestContext *ctx);
void shim_sceUserServiceGetInitialUser(GuestContext *ctx);
void shim_sceUserServiceGetLoginUserIdList(GuestContext *ctx);
void shim_sceUserServiceGetUserName(GuestContext *ctx);
void shim_sceUserServiceTerminate(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_USER_SERVICE_H
