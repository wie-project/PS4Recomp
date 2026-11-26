// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpPartner001 after shadPS4 (np/np_partner.cpp): the EA Access subscription query. The
// console asks the PlayStation Network whether the user subscribes to EA Access; there is no
// network service on the host, so the answer is "no subscription". The library only tracks
// whether it is initialized.

#include "ps4_np_partner.h"
#include <pthread.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static bool g_init;

static bool initialized(void) {
    pthread_mutex_lock(&g_mu);
    const bool init = g_init;
    pthread_mutex_unlock(&g_mu);
    return init;
}

// The console also sends the title's SDK version to the server here.
int32_t sceNpEAAccessInitialize(void) {
    pthread_mutex_lock(&g_mu);
    g_init = true;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int32_t sceNpEAAccessTerminate(void) {
    pthread_mutex_lock(&g_mu);
    const bool init = g_init;
    g_init = false;
    pthread_mutex_unlock(&g_mu);
    return init ? 0 : ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED;
}

int32_t sceNpHasEAAccessSubscription(int32_t user_id, bool *result) {
    (void)user_id;
    if (!initialized()) return ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED;
    if (!result) return ORBIS_NP_PARTNER_ERROR_INVALID_ARGUMENT;
    *result = false;
    return 0;
}

// No request is ever in flight, so there is nothing to abort.
int32_t sceNpHasEAAccessSubscriptionAbortRequest(void) {
    return initialized() ? 0 : ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED;
}

static void np_partner_return(GuestContext *ctx, int32_t rc) {
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceNpEAAccessInitialize(GuestContext *ctx) {
    np_partner_return(ctx, sceNpEAAccessInitialize());
}

void shim_sceNpEAAccessTerminate(GuestContext *ctx) {
    np_partner_return(ctx, sceNpEAAccessTerminate());
}

// s32 sceNpHasEAAccessSubscription(OrbisUserServiceUserId user_id, bool *result)
void shim_sceNpHasEAAccessSubscription(GuestContext *ctx) {
    bool *result = ctx->rsi ? (bool *)(ctx->mem_base + ctx->rsi) : NULL;
    np_partner_return(ctx, sceNpHasEAAccessSubscription((int32_t)ctx->rdi, result));
}

void shim_sceNpHasEAAccessSubscriptionAbortRequest(GuestContext *ctx) {
    np_partner_return(ctx, sceNpHasEAAccessSubscriptionAbortRequest());
}
