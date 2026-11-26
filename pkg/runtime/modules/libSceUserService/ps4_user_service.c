// SPDX-License-Identifier: GPL-2.0-or-later

// libSceUserService, after shadPS4 src/core/libraries/system/userservice.cpp. The functions
// shadPS4 stubs are in ps4_user_service_stubs.c.
//
// shadPS4 keeps up to four configured users and logs one in for each connected controller. The
// runtime has one pad user (libScePad), so one user is logged in: the initial user on player
// index 1, as shadPS4 at boot with one controller or none. Its login event is queued before the
// guest runs, like shadPS4's boot login, and its name is the host account's.

#include "ps4_user_service.h"
#include "ps4_np_manager.h"
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define USER_SERVICE_PLAYER_INDEX 1     // player index of the initial user
#define USER_SERVICE_FALLBACK_NAME "shadPS4"

typedef struct EventCallback {
    uint64_t func;     // guest address
    uint64_t userdata; // guest value passed back
} EventCallback;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_boot_once = PTHREAD_ONCE_INIT;

// Event queue (FIFO).
static OrbisUserServiceEvent *g_events;
static size_t g_events_head, g_events_len, g_events_cap;

static EventCallback *g_callbacks;
static size_t g_callbacks_len, g_callbacks_cap;

static void *guest_ptr(const GuestContext *ctx, uint64_t addr) {
    return addr ? (void *)(ctx->mem_base + addr) : NULL;
}

// Appends e to the queue; g_mutex held.
static void queue_push(OrbisUserServiceEvent e) {
    if (g_events_head + g_events_len == g_events_cap) {
        if (g_events_head > 0) {
            memmove(g_events, g_events + g_events_head, g_events_len * sizeof(*g_events));
            g_events_head = 0;
        } else {
            size_t cap = g_events_cap ? g_events_cap * 2 : 4;
            OrbisUserServiceEvent *grown = realloc(g_events, cap * sizeof(*g_events));
            if (!grown) {
                fprintf(stderr, "[user-service] out of memory, event %d for user %d dropped\n",
                        e.eventType, e.userId);
                return;
            }
            g_events = grown;
            g_events_cap = cap;
        }
    }
    g_events[g_events_head + g_events_len++] = e;
}

// Calls the guest callback with a pointer to a copy of e on the guest stack.
static void invoke_callback(GuestContext *ctx, const EventCallback *cb, OrbisUserServiceEvent e) {
    GuestContext sub = *ctx;
    uint64_t ev = (sub.rsp - 128 - sizeof(e)) & ~(uint64_t)15; // below the red zone
    memcpy(ctx->mem_base + ev, &e, sizeof(e));
    sub.rsp = ev;
    sub.rdi = ev;
    sub.rsi = cb->userdata;
    recomp_call_guest(&sub, cb->func);
}

void ps4_user_service_add_event(GuestContext *ctx, int32_t eventType, OrbisUserServiceUserId userId) {
    OrbisUserServiceEvent e = {.eventType = eventType, .userId = userId};
    EventCallback local[16];
    EventCallback *snapshot = local;
    size_t n;

    pthread_mutex_lock(&g_mutex);
    queue_push(e);
    n = g_callbacks_len;
    if (n > sizeof(local) / sizeof(local[0])) {
        snapshot = malloc(n * sizeof(*snapshot));
        if (!snapshot) {
            n = 0;
        }
    }
    if (n) {
        memcpy(snapshot, g_callbacks, n * sizeof(*snapshot));
    }
    pthread_mutex_unlock(&g_mutex);

    if (n && !ctx) {
        ctx = recomp_get_current_ctx();
    }
    for (size_t i = 0; ctx && i < n; i++) {
        invoke_callback(ctx, &snapshot[i], e);
    }
    if (snapshot != local) {
        free(snapshot);
    }
}

// The boot login of the initial user, queued once before the guest reads or registers anything.
static void boot_login(void) {
    ps4_user_service_add_event(NULL, ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN,
                               ORBIS_USER_SERVICE_INITIAL_USER_ID);
}

static void ensure_booted(void) {
    pthread_once(&g_boot_once, boot_login);
}

// The name of the host account, the counterpart of shadPS4's configured user name.
static const char *host_user_name(void) {
    const char *name = getenv("USER");
    if (name && name[0]) {
        return name;
    }
    const struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name && pw->pw_name[0]) {
        return pw->pw_name;
    }
    return USER_SERVICE_FALLBACK_NAME;
}

int32_t sceUserServiceInitialize(const OrbisUserServiceInitializeParams *params) {
    (void)params;
    ensure_booted();
    return 0;
}

int32_t sceUserServiceGetInitialUser(int32_t *userId) {
    if (!userId) {
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    *userId = ORBIS_USER_SERVICE_INITIAL_USER_ID;
    return 0;
}

int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList *list) {
    if (!list) {
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++) {
        list->userId[i] = ORBIS_USER_SERVICE_USER_ID_INVALID;
    }
    list->userId[USER_SERVICE_PLAYER_INDEX - 1] = ORBIS_USER_SERVICE_INITIAL_USER_ID;
    return 0;
}

int32_t sceUserServiceGetUserName(int32_t userId, char *userName, size_t size) {
    if (userId == ORBIS_USER_SERVICE_USER_ID_INVALID || !userName) {
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    // shadPS4 answers an unknown user with its default name as well.
    const char *name = host_user_name();
    size_t len = strlen(name);
    if (size < len + 1) {
        return ORBIS_USER_SERVICE_ERROR_BUFFER_TOO_SHORT;
    }
    memcpy(userName, name, len + 1);
    return 0;
}

int32_t sceUserServiceGetEvent(OrbisUserServiceEvent *event) {
    if (!event) {
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    ensure_booted();
    pthread_mutex_lock(&g_mutex);
    if (g_events_len == 0) {
        pthread_mutex_unlock(&g_mutex);
        return ORBIS_USER_SERVICE_ERROR_NO_EVENT;
    }
    *event = g_events[g_events_head++];
    if (--g_events_len == 0) {
        g_events_head = 0;
    }
    const OrbisUserServiceEvent taken = *event;
    pthread_mutex_unlock(&g_mutex);
    // shadPS4: the NP state follows the login state once the game has seen the event.
    ps4_np_manager_notify_user_event(taken.eventType, taken.userId);
    return 0;
}

int32_t sceUserServiceGetUserColor(int32_t userId, int32_t *color) {
    if (!color) {
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    if (userId != ORBIS_USER_SERVICE_INITIAL_USER_ID) {
        return ORBIS_USER_SERVICE_ERROR_NOT_LOGGED_IN;
    }
    // shadPS4: user_color - 1 of the configured color, 1 (blue) for the first user.
    *color = ORBIS_USER_SERVICE_USER_COLOR_BLUE;
    return 0;
}

int32_t sceUserServiceRegisterEventCallback(uint64_t callback, uint64_t userdata) {
    if (!callback) {
        return 0; // shadPS4 accepts and ignores a null callback
    }
    ensure_booted();
    pthread_mutex_lock(&g_mutex);
    for (size_t i = 0; i < g_callbacks_len; i++) {
        if (g_callbacks[i].func == callback && g_callbacks[i].userdata == userdata) {
            pthread_mutex_unlock(&g_mutex);
            return 0;
        }
    }
    if (g_callbacks_len == g_callbacks_cap) {
        size_t cap = g_callbacks_cap ? g_callbacks_cap * 2 : 4;
        EventCallback *grown = realloc(g_callbacks, cap * sizeof(*g_callbacks));
        if (!grown) {
            pthread_mutex_unlock(&g_mutex);
            return ORBIS_USER_SERVICE_ERROR_NO_MEMORY;
        }
        g_callbacks = grown;
        g_callbacks_cap = cap;
    }
    g_callbacks[g_callbacks_len++] = (EventCallback){.func = callback, .userdata = userdata};
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

// Removes every registration of callback, or only those with userdata when it is not null.
int32_t sceUserServiceUnregisterEventCallback(uint64_t callback, uint64_t userdata) {
    if (!callback) {
        return ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&g_mutex);
    size_t kept = 0;
    for (size_t i = 0; i < g_callbacks_len; i++) {
        const EventCallback *cb = &g_callbacks[i];
        if (cb->func == callback && (userdata == 0 || cb->userdata == userdata)) {
            continue;
        }
        g_callbacks[kept++] = *cb;
    }
    g_callbacks_len = kept;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

// Guest ABI shims

void shim_sceUserServiceInitialize(GuestContext *ctx) {
    const OrbisUserServiceInitializeParams *params = guest_ptr(ctx, ctx->rdi);
    ctx->rax = (uint64_t)(int64_t)sceUserServiceInitialize(params);
    SHIM_RETURN();
}

void shim_sceUserServiceGetInitialUser(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceGetInitialUser(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceUserServiceGetLoginUserIdList(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceGetLoginUserIdList(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceUserServiceGetUserName(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceGetUserName((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi),
                                                            (size_t)ctx->rdx);
    SHIM_RETURN();
}

void shim_sceUserServiceGetEvent(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceGetEvent(guest_ptr(ctx, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceUserServiceGetUserColor(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceGetUserColor((int32_t)ctx->rdi, guest_ptr(ctx, ctx->rsi));
    SHIM_RETURN();
}

void shim_sceUserServiceRegisterEventCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceRegisterEventCallback(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}

void shim_sceUserServiceUnregisterEventCallback(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceUserServiceUnregisterEventCallback(ctx->rdi, ctx->rsi);
    SHIM_RETURN();
}
