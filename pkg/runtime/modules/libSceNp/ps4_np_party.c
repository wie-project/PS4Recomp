// SPDX-License-Identifier: GPL-2.0-or-later

// libSceNpParty after shadPS4 (src/core/libraries/np/np_party.cpp). The host is offline, so the
// console is never in a party: sceNpPartyGetState reports NotInParty, the member queries fail
// with NOT_IN_PARTY, and every other entry point is a stub in shadPS4 as well (ORBIS_OK, no
// effect). libSceNpPartyCompat exports the same NIDs and resolves to the same shims.
//
// Uncharted 4 (eboot) calls Initialize at 0x99ef32, RegisterHandler at 0x99ef8b,
// RegisterPrivateHandler at 0x99ff00 and GetVoiceChatPriority at 0x99ff16, polls CheckCallback
// at 0x99da5d and calls Terminate at 0x9a776e; all of them succeed, as in shadPS4.

#include "ps4_np.h"
#include "ps4_hle_stub.h"

#include <stdio.h>

int32_t sceNpPartyGetState(uint16_t *state) {
    // shadPS4 writes through the pointer unchecked; a NULL pointer is rejected here instead of
    // writing to guest address 0.
    if (!state) return ORBIS_NP_PARTY_ERROR_INVALID_ARGUMENT;
    *state = ORBIS_NP_PARTY_STATE_NOT_IN_PARTY;
    return 0;
}

// sceNpPartyGetMemberInfo(A) / sceNpPartyGetMembers(A): there is no party to read.
int32_t sceNpPartyGetMembers(void) {
    return ORBIS_NP_PARTY_ERROR_NOT_IN_PARTY;
}

void shim_sceNpPartyGetState(GuestContext *ctx) {
    uint16_t *state = ctx->rdi ? (uint16_t *)(ctx->mem_base + ctx->rdi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceNpPartyGetState(state);
    SHIM_RETURN();
}

#define NP_PARTY_NOT_IN_PARTY(name)                                                                \
    void shim_##name(GuestContext *ctx) {                                                          \
        static int logged_;                                                                        \
        if (!logged_) {                                                                            \
            logged_ = 1;                                                                           \
            fprintf(stderr, "[libSceNpParty] %s -> NOT_IN_PARTY (offline)\n", #name);              \
        }                                                                                          \
        ctx->rax = (uint64_t)(int64_t)sceNpPartyGetMembers();                                      \
        SHIM_RETURN();                                                                             \
    }

NP_PARTY_NOT_IN_PARTY(sceNpPartyGetMemberInfo)
NP_PARTY_NOT_IN_PARTY(sceNpPartyGetMemberInfoA)
NP_PARTY_NOT_IN_PARTY(sceNpPartyGetMembers)
NP_PARTY_NOT_IN_PARTY(sceNpPartyGetMembersA)

PS4_HLE_STUB(sceNpPartyCheckCallback)
PS4_HLE_STUB(sceNpPartyCreate)
PS4_HLE_STUB(sceNpPartyCreateA)
PS4_HLE_STUB(sceNpPartyGetId)
PS4_HLE_STUB(sceNpPartyGetMemberSessionInfo)
PS4_HLE_STUB(sceNpPartyGetMemberVoiceInfo)
PS4_HLE_STUB(sceNpPartyGetStateAsUser)
PS4_HLE_STUB(sceNpPartyGetStateAsUserA)
PS4_HLE_STUB(sceNpPartyGetVoiceChatPriority)
PS4_HLE_STUB(sceNpPartyInitialize)
PS4_HLE_STUB(sceNpPartyJoin)
PS4_HLE_STUB(sceNpPartyLeave)
PS4_HLE_STUB(sceNpPartyRegisterHandler)
PS4_HLE_STUB(sceNpPartyRegisterHandlerA)
PS4_HLE_STUB(sceNpPartyRegisterPrivateHandler)
PS4_HLE_STUB(sceNpPartySendBinaryMessage)
PS4_HLE_STUB(sceNpPartySetVoiceChatPriority)
PS4_HLE_STUB(sceNpPartyShowInvitationList)
PS4_HLE_STUB(sceNpPartyShowInvitationListA)
PS4_HLE_STUB(sceNpPartyTerminate)
PS4_HLE_STUB(sceNpPartyUnregisterPrivateHandler)
