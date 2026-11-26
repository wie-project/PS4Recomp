// SPDX-License-Identifier: GPL-2.0-or-later

// libSceUserService against shadPS4's userservice.cpp on host memory (guest base 0, so guest
// addresses are host pointers): the boot login event and the event queue, the argument checks
// and their error codes, the user name buffer check, the user color, and event callbacks, which
// are C functions in the dispatch table.

#include "ps4_user_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime symbols ps4_user_service.c links against.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];
static GuestContext g_thread_ctx;
GuestContext *recomp_get_current_ctx(void) { return &g_thread_ctx; }
uint32_t ps4_compiled_sdk_version(const GuestContext *ctx) {
    (void)ctx;
    return 0;
}

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define A(p) ((uint64_t)(uintptr_t)(p))
#define USER ORBIS_USER_SERVICE_INITIAL_USER_ID

static uint8_t g_stack[0x4000] __attribute__((aligned(16)));

static void reset_ctx(GuestContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->mem_size = ~0ull;
    ctx->rsp = A(g_stack + sizeof(g_stack) - 8); // as on entry to a called function
}

// Calls a shim with the given arguments and returns its rax.
static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1, uint64_t a2) {
    GuestContext ctx;
    reset_ctx(&ctx);
    ctx.rdi = a0;
    ctx.rsi = a1;
    ctx.rdx = a2;
    ctx.rax = 99;
    shim(&ctx);
    return (int32_t)ctx.rax;
}

static void register_entry(uint64_t addr, recomp_fn_t fn) {
    const uint64_t l1 = (addr >> DISPATCH_L1_SHIFT) & DISPATCH_LEVEL_MASK;
    const uint64_t l2 = (addr >> DISPATCH_L2_SHIFT) & DISPATCH_LEVEL_MASK;
    if (!g_dispatch_l1[l1]) g_dispatch_l1[l1] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t *));
    if (!g_dispatch_l1[l1][l2]) g_dispatch_l1[l1][l2] = calloc(DISPATCH_LEVEL_SIZE, sizeof(recomp_fn_t));
    g_dispatch_l1[l1][l2][addr & DISPATCH_LEVEL_MASK] = fn;
}

// What the guest callbacks saw.
static struct {
    int calls_a, calls_b;
    OrbisUserServiceEvent last;
    uint64_t last_userdata;
    int misaligned;
} seen;

static void callback_common(GuestContext *ctx) {
    if ((ctx->rsp + 8) % 16 != 0) seen.misaligned++;
    memcpy(&seen.last, (const void *)(uintptr_t)ctx->rdi, sizeof(seen.last));
    seen.last_userdata = ctx->rsi;
}

static void callback_a(GuestContext *ctx) {
    seen.calls_a++;
    callback_common(ctx);
    SHIM_RETURN();
}

static void callback_b(GuestContext *ctx) {
    seen.calls_b++;
    callback_common(ctx);
    SHIM_RETURN();
}

#define CB_A 0x400000ull
#define CB_B 0x400100ull

static int case_events(void) {
    OrbisUserServiceEvent ev;
    CHECK(call(shim_sceUserServiceGetEvent, 0, 0, 0) == ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceUserServiceInitialize, 0, 0, 0) == 0);
    CHECK(call(shim_sceUserServiceInitialize, 0, 0, 0) == 0); // shadPS4: no ALREADY_INITIALIZED
    // The boot login of the initial user, once.
    memset(&ev, 0xff, sizeof(ev));
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == 0);
    CHECK(ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN && ev.userId == USER);
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == ORBIS_USER_SERVICE_ERROR_NO_EVENT);
    // Later events come out in order.
    ps4_user_service_add_event(NULL, ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT, USER);
    ps4_user_service_add_event(NULL, ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN, USER + 1);
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == 0);
    CHECK(ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT && ev.userId == USER);
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == 0);
    CHECK(ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN && ev.userId == USER + 1);
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == ORBIS_USER_SERVICE_ERROR_NO_EVENT);
    // More events than the initial queue capacity.
    for (int i = 0; i < 10; i++) ps4_user_service_add_event(NULL, i & 1, USER + i);
    for (int i = 0; i < 10; i++) {
        CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == 0);
        CHECK(ev.eventType == (i & 1) && ev.userId == USER + i);
    }
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == ORBIS_USER_SERVICE_ERROR_NO_EVENT);
    printf("PASS events\n");
    return 0;
}

static int case_users(void) {
    int32_t id = 0, color = -1;
    OrbisUserServiceLoginUserIdList list;
    char name[64], tiny[1];

    CHECK(call(shim_sceUserServiceGetInitialUser, 0, 0, 0) == ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceUserServiceGetInitialUser, A(&id), 0, 0) == 0 && id == USER);

    CHECK(call(shim_sceUserServiceGetLoginUserIdList, 0, 0, 0) == ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);
    memset(&list, 0, sizeof(list));
    CHECK(call(shim_sceUserServiceGetLoginUserIdList, A(&list), 0, 0) == 0);
    CHECK(list.userId[0] == USER);
    for (int i = 1; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++) {
        CHECK(list.userId[i] == ORBIS_USER_SERVICE_USER_ID_INVALID);
    }

    // The name check order: the user id, then the buffer, then its size.
    CHECK(call(shim_sceUserServiceGetUserName, (uint64_t)(int64_t)-1, A(name), sizeof(name)) ==
          ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceUserServiceGetUserName, USER, 0, sizeof(name)) == ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceUserServiceGetUserName, USER, A(tiny), sizeof(tiny)) ==
          ORBIS_USER_SERVICE_ERROR_BUFFER_TOO_SHORT);
    setenv("USER", "player-one", 1);
    memset(name, 'x', sizeof(name));
    CHECK(call(shim_sceUserServiceGetUserName, USER, A(name), sizeof(name)) == 0);
    CHECK(strcmp(name, "player-one") == 0);
    CHECK(call(shim_sceUserServiceGetUserName, USER, A(name), strlen("player-one")) ==
          ORBIS_USER_SERVICE_ERROR_BUFFER_TOO_SHORT);
    CHECK(call(shim_sceUserServiceGetUserName, USER, A(name), strlen("player-one") + 1) == 0);

    CHECK(call(shim_sceUserServiceGetUserColor, USER, 0, 0) == ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);
    CHECK(call(shim_sceUserServiceGetUserColor, USER + 1, A(&color), 0) == ORBIS_USER_SERVICE_ERROR_NOT_LOGGED_IN);
    CHECK(call(shim_sceUserServiceGetUserColor, USER, A(&color), 0) == 0);
    CHECK(color == ORBIS_USER_SERVICE_USER_COLOR_BLUE);
    printf("PASS users\n");
    return 0;
}

static int case_callbacks(void) {
    OrbisUserServiceEvent ev;
    register_entry(CB_A, callback_a);
    register_entry(CB_B, callback_b);
    reset_ctx(&g_thread_ctx);

    // A null callback is accepted on registration and refused on removal.
    CHECK(call(shim_sceUserServiceRegisterEventCallback, 0, 7, 0) == 0);
    CHECK(call(shim_sceUserServiceUnregisterEventCallback, 0, 7, 0) == ORBIS_USER_SERVICE_ERROR_INVALID_ARGUMENT);

    // Registering drains nothing: the boot login is queued before, without a callback.
    CHECK(call(shim_sceUserServiceRegisterEventCallback, CB_A, 11, 0) == 0);
    CHECK(call(shim_sceUserServiceRegisterEventCallback, CB_A, 11, 0) == 0); // duplicate ignored
    CHECK(call(shim_sceUserServiceRegisterEventCallback, CB_A, 12, 0) == 0);
    CHECK(call(shim_sceUserServiceRegisterEventCallback, CB_B, 13, 0) == 0);
    CHECK(seen.calls_a == 0 && seen.calls_b == 0);
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == 0);
    CHECK(ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN && ev.userId == USER);

    uint64_t rsp_before = g_thread_ctx.rsp;
    ps4_user_service_add_event(NULL, ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT, USER);
    CHECK(seen.calls_a == 2 && seen.calls_b == 1 && seen.misaligned == 0);
    CHECK(seen.last.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT && seen.last.userId == USER);
    CHECK(seen.last_userdata == 13);
    CHECK(g_thread_ctx.rsp == rsp_before); // the callbacks ran on a copy of the context
    // The event is queued as well.
    CHECK(call(shim_sceUserServiceGetEvent, A(&ev), 0, 0) == 0);
    CHECK(ev.eventType == ORBIS_USER_SERVICE_EVENT_TYPE_LOGOUT);

    // Removal by callback and userdata, then by callback alone (userdata 0 matches all).
    CHECK(call(shim_sceUserServiceUnregisterEventCallback, CB_A, 11, 0) == 0);
    ps4_user_service_add_event(&g_thread_ctx, ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN, USER);
    CHECK(seen.calls_a == 3 && seen.calls_b == 2);
    CHECK(call(shim_sceUserServiceUnregisterEventCallback, CB_B, 0, 0) == 0);
    ps4_user_service_add_event(&g_thread_ctx, ORBIS_USER_SERVICE_EVENT_TYPE_LOGIN, USER);
    CHECK(seen.calls_a == 4 && seen.calls_b == 2 && seen.last_userdata == 12);
    printf("PASS callbacks\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "events") == 0) return case_events();
    if (strcmp(argv[1], "users") == 0) return case_users();
    if (strcmp(argv[1], "callbacks") == 0) return case_callbacks();
    return 2;
}
