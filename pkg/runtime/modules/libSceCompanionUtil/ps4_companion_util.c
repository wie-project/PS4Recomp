// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCompanionUtil, after shadPS4 (src/core/libraries/companion/companion_util.cpp): the
// PS4 Second Screen app never connects to the host, so no event ever arrives.

#include "ps4_companion_util.h"
#include "ps4_hle_stub.h"

int32_t sceCompanionUtilGetEvent(void *event) {
    return ORBIS_COMPANION_UTIL_NO_EVENT;
}

void shim_sceCompanionUtilGetEvent(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceCompanionUtilGetEvent((void *)(uintptr_t)ctx->rdi);
    SHIM_RETURN();
}

PS4_HLE_STUB(sceCompanionUtilGetRemoteOskEvent)
PS4_HLE_STUB(sceCompanionUtilInitialize)
PS4_HLE_STUB(sceCompanionUtilOptParamInitialize)
PS4_HLE_STUB(sceCompanionUtilTerminate)
