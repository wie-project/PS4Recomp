// libSceNpParty. The host is always offline and signed out, so the console is never in a
// party: the registration and housekeeping calls keep the library's state and succeed, and
// every query that needs a party fails with NOT_IN_PARTY. No party event can ever happen,
// so a registered handler is stored and never invoked.
//
// Error codes (np_error.h in shadPS4, same values on the console): 0x805525xx.
//
// Uncharted 4 (eboot) calls Initialize at 0x99ef32 and RegisterHandler at 0x99ef8b with
// stack blocks that start with the SDK version (0x4008061), RegisterPrivateHandler at 0x99ff00
// and GetVoiceChatPriority at 0x99ff16 with a pointer to a global, polls CheckCallback at
// 0x99da5d and calls Terminate at 0x9a776e. The blocks are opaque to the host: only the
// address of a registered block is recorded.
#include "ps4_np.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#define NP_PARTY_ERROR_ALREADY_INITIALIZED ((int32_t)0x80552502)
#define NP_PARTY_ERROR_NOT_INITIALIZED     ((int32_t)0x80552503)
#define NP_PARTY_ERROR_INVALID_ARGUMENT    ((int32_t)0x80552504)
#define NP_PARTY_ERROR_NOT_IN_PARTY        ((int32_t)0x80552506)

static pthread_mutex_t g_party_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_party_initialized;
static uint64_t g_party_handler;         // guest address of the registered handler block
static uint64_t g_party_private_handler; // guest address of the registered private-handler block

int32_t sceNpPartyInitialize(void) {
    int32_t rc = 0;
    pthread_mutex_lock(&g_party_mutex);
    if (g_party_initialized) {
        rc = NP_PARTY_ERROR_ALREADY_INITIALIZED;
    } else {
        g_party_initialized = true;
        g_party_handler = 0;
        g_party_private_handler = 0;
    }
    pthread_mutex_unlock(&g_party_mutex);
    return rc;
}

int32_t sceNpPartyTerminate(void) {
    int32_t rc = 0;
    pthread_mutex_lock(&g_party_mutex);
    if (!g_party_initialized) {
        rc = NP_PARTY_ERROR_NOT_INITIALIZED;
    } else {
        g_party_initialized = false;
        g_party_handler = 0;
        g_party_private_handler = 0;
    }
    pthread_mutex_unlock(&g_party_mutex);
    return rc;
}

// Stores `param_gaddr` in *slot. NULL is an invalid argument.
static int32_t party_register(uint64_t *slot, uint64_t param_gaddr) {
    int32_t rc = 0;
    pthread_mutex_lock(&g_party_mutex);
    if (!g_party_initialized) {
        rc = NP_PARTY_ERROR_NOT_INITIALIZED;
    } else if (!param_gaddr) {
        rc = NP_PARTY_ERROR_INVALID_ARGUMENT;
    } else {
        *slot = param_gaddr;
    }
    pthread_mutex_unlock(&g_party_mutex);
    return rc;
}

int32_t sceNpPartyRegisterHandler(uint64_t param_gaddr) {
    return party_register(&g_party_handler, param_gaddr);
}

int32_t sceNpPartyRegisterPrivateHandler(uint64_t param_gaddr) {
    return party_register(&g_party_private_handler, param_gaddr);
}

int32_t sceNpPartyUnregisterPrivateHandler(void) {
    int32_t rc = 0;
    pthread_mutex_lock(&g_party_mutex);
    if (!g_party_initialized) {
        rc = NP_PARTY_ERROR_NOT_INITIALIZED;
    } else {
        g_party_private_handler = 0;
    }
    pthread_mutex_unlock(&g_party_mutex);
    return rc;
}

// Poll-style dispatch of pending party events: there are never any.
int32_t sceNpPartyCheckCallback(void) {
    pthread_mutex_lock(&g_party_mutex);
    int32_t rc = g_party_initialized ? 0 : NP_PARTY_ERROR_NOT_INITIALIZED;
    pthread_mutex_unlock(&g_party_mutex);
    return rc;
}

// sceNpPartyGetVoiceChatPriority / SetVoiceChatPriority: the voice chat priority belongs to
// the local member of a party. Outside a party they fail with NOT_IN_PARTY; shadPS4 returns
// SCE_OK from a stub here, which would leave the caller's output unwritten but "valid".
static int32_t party_voice_priority(uint64_t arg_gaddr) {
    int32_t rc = NP_PARTY_ERROR_NOT_IN_PARTY;
    pthread_mutex_lock(&g_party_mutex);
    if (!g_party_initialized) {
        rc = NP_PARTY_ERROR_NOT_INITIALIZED;
    } else if (!arg_gaddr) {
        rc = NP_PARTY_ERROR_INVALID_ARGUMENT;
    }
    pthread_mutex_unlock(&g_party_mutex);
    return rc;
}

int32_t sceNpPartyGetVoiceChatPriority(uint64_t out_gaddr) {
    return party_voice_priority(out_gaddr);
}

int32_t sceNpPartySetVoiceChatPriority(uint64_t in_gaddr) {
    return party_voice_priority(in_gaddr);
}

// Test hooks: the recorded handler blocks (0 = none).
uint64_t ps4_np_party_handler(bool private_handler) {
    pthread_mutex_lock(&g_party_mutex);
    uint64_t v = private_handler ? g_party_private_handler : g_party_handler;
    pthread_mutex_unlock(&g_party_mutex);
    return v;
}

#define PARTY_SHIM_RET(expr)                                                   \
    do {                                                                       \
        ctx->rax = (uint64_t)(int64_t)(expr);                                  \
        SHIM_RETURN();                                                         \
    } while (0)

void shim_sceNpPartyInitialize(GuestContext *ctx) { PARTY_SHIM_RET(sceNpPartyInitialize()); }
void shim_sceNpPartyTerminate(GuestContext *ctx) { PARTY_SHIM_RET(sceNpPartyTerminate()); }
void shim_sceNpPartyRegisterHandler(GuestContext *ctx) { PARTY_SHIM_RET(sceNpPartyRegisterHandler(ctx->rdi)); }
void shim_sceNpPartyRegisterPrivateHandler(GuestContext *ctx) {
    PARTY_SHIM_RET(sceNpPartyRegisterPrivateHandler(ctx->rdi));
}
void shim_sceNpPartyUnregisterPrivateHandler(GuestContext *ctx) {
    PARTY_SHIM_RET(sceNpPartyUnregisterPrivateHandler());
}
void shim_sceNpPartyCheckCallback(GuestContext *ctx) { PARTY_SHIM_RET(sceNpPartyCheckCallback()); }
void shim_sceNpPartyGetVoiceChatPriority(GuestContext *ctx) {
    PARTY_SHIM_RET(sceNpPartyGetVoiceChatPriority(ctx->rdi));
}
void shim_sceNpPartySetVoiceChatPriority(GuestContext *ctx) {
    PARTY_SHIM_RET(sceNpPartySetVoiceChatPriority(ctx->rdi));
}
