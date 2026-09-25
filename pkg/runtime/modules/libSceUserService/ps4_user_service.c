#include "ps4_user_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

static int g_user_service_initialized = 0;
static pthread_mutex_t g_user_service_mutex = PTHREAD_MUTEX_INITIALIZER;

int32_t sceUserServiceInitialize(const OrbisUserServiceInitializeParams *params) {
    (void)params;
    pthread_mutex_lock(&g_user_service_mutex);
    g_user_service_initialized = 1;
    pthread_mutex_unlock(&g_user_service_mutex);
    return 0;
}

int32_t sceUserServiceGetInitialUser(int32_t *userId) {
    if (!userId) {
        return -EINVAL;
    }
    *userId = ORBIS_USER_SERVICE_INITIAL_USER_ID;
    return 0;
}

int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList *list) {
    if (!list) {
        return -EINVAL;
    }
    list->userId[0] = ORBIS_USER_SERVICE_INITIAL_USER_ID;
    for (int i = 1; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++) {
        list->userId[i] = ORBIS_USER_SERVICE_USER_ID_INVALID;
    }
    return 0;
}

int32_t sceUserServiceGetUserName(int32_t userId, char *userName, const size_t size) {
    if (!userName || size == 0) {
        return -EINVAL;
    }
    (void)userId;
    const char *name = getenv("USER");
    if (!name || strlen(name) == 0) {
        name = "PS4User";
    }
    strncpy(userName, name, size - 1);
    userName[size - 1] = '\0';
    return 0;
}

static int g_user_event_delivered = 0;

int32_t sceUserServiceGetEvent(OrbisUserServiceEvent *event) {
    if (!event) {
        return -EINVAL;
    }
    pthread_mutex_lock(&g_user_service_mutex);
    if (!g_user_event_delivered) {
        event->eventType = ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN;
        event->userId = ORBIS_USER_SERVICE_INITIAL_USER_ID;
        g_user_event_delivered = 1;
        pthread_mutex_unlock(&g_user_service_mutex);
        return 0;
    }
    pthread_mutex_unlock(&g_user_service_mutex);
    return (int32_t)ORBIS_USER_SERVICE_ERROR_NO_EVENT;
}

int32_t sceUserServiceTerminate(void) {
    pthread_mutex_lock(&g_user_service_mutex);
    g_user_service_initialized = 0;
    g_user_event_delivered = 0;
    pthread_mutex_unlock(&g_user_service_mutex);
    return 0;
}

// Guest ABI Shims
void shim_sceUserServiceInitialize(GuestContext *ctx) {
    uint64_t paramsGuest = ctx->rdi;
    const OrbisUserServiceInitializeParams *params =
        paramsGuest ? (const OrbisUserServiceInitializeParams *)(ctx->mem_base + paramsGuest) : NULL;

    int32_t ret = sceUserServiceInitialize(params);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceUserServiceGetInitialUser(GuestContext *ctx) {
    uint64_t userIdGuest = ctx->rdi;
    int32_t *userId = userIdGuest ? (int32_t *)(ctx->mem_base + userIdGuest) : NULL;

    int32_t ret = sceUserServiceGetInitialUser(userId);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceUserServiceGetLoginUserIdList(GuestContext *ctx) {
    uint64_t listGuest = ctx->rdi;
    OrbisUserServiceLoginUserIdList *list =
        listGuest ? (OrbisUserServiceLoginUserIdList *)(ctx->mem_base + listGuest) : NULL;

    int32_t ret = sceUserServiceGetLoginUserIdList(list);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceUserServiceGetUserName(GuestContext *ctx) {
    int32_t userId = (int32_t)ctx->rdi;
    uint64_t nameGuest = ctx->rsi;
    size_t size = (size_t)ctx->rdx;
    char *userName = nameGuest ? (char *)(ctx->mem_base + nameGuest) : NULL;

    int32_t ret = sceUserServiceGetUserName(userId, userName, size);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceUserServiceGetEvent(GuestContext *ctx) {
    uint64_t eventGuest = ctx->rdi;
    OrbisUserServiceEvent *event = eventGuest ? (OrbisUserServiceEvent *)(ctx->mem_base + eventGuest) : NULL;
    int32_t ret = sceUserServiceGetEvent(event);
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}

void shim_sceUserServiceTerminate(GuestContext *ctx) {
    int32_t ret = sceUserServiceTerminate();
    ctx->rax = (uint64_t)(int64_t)ret;
    SHIM_RETURN();
}
