// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_PARTNER_H
#define PS4_NP_PARTNER_H

#include <stdbool.h>
#include <stdint.h>
#include "recomp_runtime.h"

// shadPS4 np/np_partner_error.h
#define ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED ((int32_t)0x819D0001)
#define ORBIS_NP_PARTNER_ERROR_INVALID_ARGUMENT ((int32_t)0x819D0002)

int32_t sceNpEAAccessInitialize(void);
int32_t sceNpEAAccessTerminate(void);
int32_t sceNpHasEAAccessSubscription(int32_t user_id, bool *result);
int32_t sceNpHasEAAccessSubscriptionAbortRequest(void);

void shim_sceNpEAAccessInitialize(GuestContext *ctx);
void shim_sceNpEAAccessTerminate(GuestContext *ctx);
void shim_sceNpHasEAAccessSubscription(GuestContext *ctx);
void shim_sceNpHasEAAccessSubscriptionAbortRequest(GuestContext *ctx);

#endif // PS4_NP_PARTNER_H
