// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCommonDialog, libSceNpParty and libSceNpCommerce on host memory (guest base 0, so guest
// addresses are host pointers). The NpCommerce windows are replaced by the hooks below, which
// record what they would show.

#include "ps4_common_dialog.h"
#include "ps4_np.h"
#include "ps4_np_commerce.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#include "../host_guest_mem.h"

static int64_t rc_of(const GuestContext *ctx) {
    return (int64_t)ctx->rax;
}

// Calls a shim with rdi = arg on a zeroed context and returns its rax.
static int64_t call(void (*shim)(GuestContext *), uint64_t arg) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mem_base = GUEST_MEM;
    ctx.rdi = arg;
    ctx.rax = 99;
    shim(&ctx);
    return rc_of(&ctx);
}

// ---------------------------------------------------------------------------------------------
// Hooks: the user name and the NpCommerce windows.

int32_t sceUserServiceGetUserName(int32_t userId, char *userName, const size_t size) {
    if (userId != 0x10000000) return -1;
    snprintf(userName, size, "%s", "Player1");
    return 0;
}

static bool g_ui_available = true;
static int g_shown, g_dismissed, g_icon_calls;
static NpCommerceView g_seen;
static bool g_icon_shown;
static int32_t g_icon_pos, g_icon_layout;

bool ps4_np_commerce_ui_show(const NpCommerceView *view) {
    if (!g_ui_available) return false;
    g_shown++;
    g_seen = *view;
    return true;
}

void ps4_np_commerce_ui_dismiss(void) {
    g_dismissed++;
}

void ps4_np_commerce_ui_icon(bool shown, int32_t pos, int32_t layout) {
    g_icon_calls++;
    g_icon_shown = shown;
    g_icon_pos = pos;
    g_icon_layout = layout;
}

// ---------------------------------------------------------------------------------------------
// CommonDialog

static int test_common_dialog(void) {
    CHECK(!sceCommonDialogIsUsed());
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceCommonDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_SYSTEM_INITIALIZED);
    CHECK(call(shim_sceCommonDialogIsUsed, 0) == 0);
    CHECK(ps4_common_dialog_acquire());
    CHECK(call(shim_sceCommonDialogIsUsed, 0) == 1);
    ps4_common_dialog_release();
    // Stubs in shadPS4 as well.
    CHECK(call(shim__ZN3sce16CommonDialogUtil12getSelfAppIdEv, 0) == 0);
    CHECK(call(shim__ZNK3sce16CommonDialogUtil6Client9getResultEv, 0) == 0);
    CHECK(call(shim_Func_F2AEE270605622B0, 0) == 0);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// NpParty

static int test_np_party(void) {
    uint16_t st[2] = {0xFFFF, 0xFFFF};
    CHECK(call(shim_sceNpPartyGetState, A(&st[0])) == 0);
    CHECK(st[0] == ORBIS_NP_PARTY_STATE_NOT_IN_PARTY);
    CHECK(st[1] == 0xFFFF); // a u16: the neighbour is untouched
    CHECK(call(shim_sceNpPartyGetState, 0) == ORBIS_NP_PARTY_ERROR_INVALID_ARGUMENT);

    CHECK(call(shim_sceNpPartyGetMemberInfo, 0) == ORBIS_NP_PARTY_ERROR_NOT_IN_PARTY);
    CHECK(call(shim_sceNpPartyGetMemberInfoA, 0) == ORBIS_NP_PARTY_ERROR_NOT_IN_PARTY);
    CHECK(call(shim_sceNpPartyGetMembers, 0) == ORBIS_NP_PARTY_ERROR_NOT_IN_PARTY);
    CHECK(call(shim_sceNpPartyGetMembersA, 0) == ORBIS_NP_PARTY_ERROR_NOT_IN_PARTY);

    // Everything else is a stub in shadPS4: no initialization rules, always ORBIS_OK.
    CHECK(call(shim_sceNpPartyCheckCallback, 0) == 0);
    CHECK(call(shim_sceNpPartyInitialize, 0) == 0);
    CHECK(call(shim_sceNpPartyInitialize, 0) == 0);
    CHECK(call(shim_sceNpPartyRegisterHandler, 0) == 0);
    CHECK(call(shim_sceNpPartyGetVoiceChatPriority, 0) == 0);
    CHECK(call(shim_sceNpPartyShowInvitationListA, 0) == 0);
    CHECK(call(shim_sceNpPartyTerminate, 0) == 0);
    CHECK(call(shim_sceNpPartyTerminate, 0) == 0);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// NpCommerce

static NpCommerceDialogParam g_param;
static uint64_t g_targets[12];

static void make_param(int32_t mode, uint32_t num_targets) {
    memset(&g_param, 0, sizeof(g_param));
    g_param.baseSize = sizeof(g_param);
    g_param.size = sizeof(g_param);
    g_param.userId = 0x10000000;
    g_param.mode = mode;
    g_param.serviceLabel = 7;
    g_param.targets = A(g_targets);
    g_param.numTargets = num_targets;
    g_param.features = 1;
    g_param.userData = 0xC0FFEE;
}

static int test_commerce_lifecycle(void) {
    CHECK(sceNpCommerceDialogGetStatus() == NP_COMMERCE_STATUS_NONE);
    make_param(NP_COMMERCE_MODE_PRODUCT, 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED); // state first
    CHECK(sceNpCommerceDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceNpCommerceDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    NpCommerceDialogResult res;
    CHECK(sceNpCommerceDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(sceNpCommerceDialogGetResult(&res) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);

    CHECK(call(shim_sceNpCommerceDialogInitializeInternal, 0) == 0);
    CHECK(sceNpCommerceDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceNpCommerceDialogUpdateStatus() == NP_COMMERCE_STATUS_INITIALIZED);
    CHECK(sceNpCommerceDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(!sceCommonDialogIsUsed()); // the library does not take the common dialog system

    // The player presses Close.
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(g_shown == 1);
    CHECK(sceNpCommerceDialogUpdateStatus() == NP_COMMERCE_STATUS_RUNNING);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceNpCommerceDialogGetResult(&res) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    ps4_np_commerce_finish(g_seen.serial + 1, NP_COMMERCE_RESULT_PURCHASED); // another dialog's window
    CHECK(sceNpCommerceDialogGetStatus() == NP_COMMERCE_STATUS_RUNNING);
    ps4_np_commerce_finish(g_seen.serial, NP_COMMERCE_RESULT_USER_CANCELED);
    CHECK(sceNpCommerceDialogGetStatus() == NP_COMMERCE_STATUS_FINISHED);
    memset(&res, 0xAA, sizeof(res));
    CHECK(sceNpCommerceDialogGetResult(&res) == NP_COMMERCE_RESULT_USER_CANCELED); // the code itself
    CHECK(res.result == NP_COMMERCE_RESULT_USER_CANCELED && res.authorized == 0 && res.userData == 0xC0FFEE);
    CHECK(sceNpCommerceDialogClose() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_CLOSE);
    CHECK(g_dismissed == 0);

    // PLUS: the action button buys and authorizes.
    make_param(NP_COMMERCE_MODE_PLUS, 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(g_seen.mode == NP_COMMERCE_MODE_PLUS && g_seen.features == 1);
    ps4_np_commerce_finish(g_seen.serial, NP_COMMERCE_RESULT_PURCHASED);
    CHECK(sceNpCommerceDialogGetResult(&res) == NP_COMMERCE_RESULT_PURCHASED);
    CHECK(res.authorized == 1);

    // Outside PLUS a purchase is not "authorized" and features are dropped.
    make_param(NP_COMMERCE_MODE_CHECKOUT, 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(g_seen.features == 0);
    ps4_np_commerce_finish(g_seen.serial, NP_COMMERCE_RESULT_PURCHASED);
    CHECK(sceNpCommerceDialogGetResult(&res) == NP_COMMERCE_RESULT_PURCHASED);
    CHECK(res.authorized == 0);

    // The game closes it: OK.
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(call(shim_sceNpCommerceDialogClose, 0) == 0);
    CHECK(g_dismissed == 1);
    CHECK(sceNpCommerceDialogGetResult(&res) == NP_COMMERCE_RESULT_OK);

    // An invalid mode finishes the dialog with the error as its result.
    make_param(6, 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(sceNpCommerceDialogGetStatus() == NP_COMMERCE_STATUS_FINISHED);
    CHECK(sceNpCommerceDialogGetResult(&res) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(res.result == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    make_param(-1, 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);

    // Terminate ends a dialog still up.
    make_param(NP_COMMERCE_MODE_DOWNLOADLIST, 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(call(shim_sceNpCommerceDialogTerminate, 0) == 0);
    CHECK(g_dismissed == 2);
    CHECK(call(shim_sceNpCommerceDialogGetStatus, 0) == NP_COMMERCE_STATUS_NONE);
    CHECK(sceNpCommerceDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);

    // No window (headless run): the dialog finishes at once, cancelled.
    g_ui_available = false;
    CHECK(sceNpCommerceDialogInitialize() == 0);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(sceNpCommerceDialogUpdateStatus() == NP_COMMERCE_STATUS_FINISHED);
    CHECK(sceNpCommerceDialogGetResult(&res) == NP_COMMERCE_RESULT_USER_CANCELED);
    CHECK(sceNpCommerceDialogTerminate() == 0);
    return 0;
}

// The request the window shows: user name, targets (NULL labels skipped, at most 10, at most 128
// bytes each).
static int test_commerce_view(void) {
    static char long_label[200];
    memset(long_label, 'x', sizeof(long_label) - 1);
    const char *labels[12] = {"EP0001-CUSA00001_00-ITEM", NULL, long_label, "c", "d", "e",
                              "f", "g", "h", "i", "j", "k"};
    for (int i = 0; i < 12; i++) g_targets[i] = A(labels[i]);

    CHECK(sceNpCommerceDialogInitialize() == 0);
    make_param(NP_COMMERCE_MODE_CATEGORY, 12);
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(strcmp(g_seen.userName, "Player1") == 0);
    CHECK(g_seen.userId == 0x10000000 && g_seen.serviceLabel == 7);
    CHECK(g_seen.targetCount == 9); // 10 read, the NULL one skipped
    CHECK(strcmp(g_seen.targets[0], "EP0001-CUSA00001_00-ITEM") == 0);
    CHECK(strlen(g_seen.targets[1]) == NP_COMMERCE_MAX_LABEL_LEN);
    CHECK(strcmp(g_seen.targets[8], "i") == 0); // labels[9]: "j" and "k" are past the limit
    CHECK(sceNpCommerceDialogClose() == 0);

    // Unknown user: no name.
    make_param(NP_COMMERCE_MODE_PRODUCT_CODE, 0);
    g_param.userId = 5;
    CHECK(sceNpCommerceDialogOpen(GUEST_MEM, &g_param) == 0);
    CHECK(g_seen.userName[0] == '\0' && g_seen.targetCount == 0);
    CHECK(sceNpCommerceDialogTerminate() == 0);
    return 0;
}

static int test_commerce_icon(void) {
    CHECK(call(shim_sceNpCommerceHidePsStoreIcon, 0) == 0);
    CHECK(g_icon_calls == 0); // not shown: nothing to hide
    CHECK(call(shim_sceNpCommerceShowPsStoreIcon, NP_COMMERCE_ICON_POS_RIGHT) == 0);
    CHECK(g_icon_shown && g_icon_pos == NP_COMMERCE_ICON_POS_RIGHT && g_icon_layout == 0);
    CHECK(call(shim_sceNpCommerceShowPsStoreIcon, 7) == 0); // unknown position: center
    CHECK(g_icon_pos == NP_COMMERCE_ICON_POS_CENTER);
    CHECK(call(shim_sceNpCommerceSetPsStoreIconLayout, NP_COMMERCE_ICON_LAYOUT_FIXED_90) == 0);
    CHECK(g_icon_shown && g_icon_layout == NP_COMMERCE_ICON_LAYOUT_FIXED_90);
    const int calls = g_icon_calls;
    CHECK(call(shim_sceNpCommerceSetPsStoreIconLayout, 3) == 0); // unknown layout: ignored, OK
    CHECK(g_icon_calls == calls && g_icon_layout == NP_COMMERCE_ICON_LAYOUT_FIXED_90);
    CHECK(call(shim_sceNpCommerceHidePsStoreIcon, 0) == 0);
    CHECK(!g_icon_shown);
    // A layout set while hidden applies to the next Show.
    CHECK(sceNpCommerceSetPsStoreIconLayout(NP_COMMERCE_ICON_LAYOUT_FOLLOW_SAFE_AREA) == 0);
    CHECK(g_icon_calls == calls + 1);
    CHECK(sceNpCommerceShowPsStoreIcon(NP_COMMERCE_ICON_POS_LEFT) == 0);
    CHECK(g_icon_layout == NP_COMMERCE_ICON_LAYOUT_FOLLOW_SAFE_AREA && g_icon_pos == NP_COMMERCE_ICON_POS_LEFT);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int rc = 2;
    if (strcmp(argv[1], "common-dialog") == 0) rc = test_common_dialog();
    if (strcmp(argv[1], "np-party") == 0) rc = test_np_party();
    if (strcmp(argv[1], "commerce-lifecycle") == 0) rc = test_commerce_lifecycle();
    if (strcmp(argv[1], "commerce-view") == 0) rc = test_commerce_view();
    if (strcmp(argv[1], "commerce-icon") == 0) rc = test_commerce_icon();
    if (rc == 0) printf("PASS %s\n", argv[1]);
    return rc;
}
