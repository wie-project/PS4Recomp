// SPDX-License-Identifier: GPL-2.0-or-later

// libSceInvitationDialog and libSceNpProfileDialog on host memory (guest base 0, so guest
// addresses are host pointers). The windows are replaced by the hooks below, which record what
// they would show; the cases press their buttons through the finish entry points.

#include "ps4_common_dialog.h"
#include "ps4_invitation_dialog.h"
#include "ps4_np_profile_dialog.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#define A(p) ((uint64_t)(uintptr_t)(p))

// Calls a shim with rdi = arg on a zeroed context and returns its rax.
static int64_t call(void (*shim)(GuestContext *), uint64_t arg) {
    GuestContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.rdi = arg;
    ctx.rax = 99;
    shim(&ctx);
    return (int64_t)ctx.rax;
}

// ---------------------------------------------------------------------------------------------
// Hooks: the windows.

static bool g_ui_available = true;
static int g_inv_shown, g_inv_dismissed, g_prof_shown, g_prof_dismissed;
static InvitationDialogView g_inv_seen;
static NpProfileDialogView g_prof_seen;

bool ps4_invitation_dialog_ui_show(const InvitationDialogView *view) {
    if (!g_ui_available) return false;
    g_inv_shown++;
    g_inv_seen = *view;
    return true;
}

void ps4_invitation_dialog_ui_dismiss(void) {
    g_inv_dismissed++;
}

bool ps4_np_profile_dialog_ui_show(const NpProfileDialogView *view) {
    if (!g_ui_available) return false;
    g_prof_shown++;
    g_prof_seen = *view;
    return true;
}

void ps4_np_profile_dialog_ui_dismiss(void) {
    g_prof_dismissed++;
}

// ---------------------------------------------------------------------------------------------
// InvitationDialog

static InvitationDialogParam g_ip;
static InvitationDialogSendInfo g_send;
static char g_session[48];
static uint64_t g_account_ids[20];

static void make_invitation(int32_t mode, int32_t address_type, uint32_t ids_count) {
    memset(&g_ip, 0, sizeof(g_ip));
    memset(&g_send, 0, sizeof(g_send));
    memset(g_session, 0, sizeof(g_session));
    snprintf(g_session, sizeof(g_session), "%s", "session-0001");
    g_ip.baseSize = sizeof(g_ip);
    g_ip.size = sizeof(g_ip);
    g_ip.mode = mode;
    g_ip.userId = 0x10000000;
    g_ip.callbackArg = 0xCA11BAC4;
    g_ip.dataParam = A(&g_send);
    g_send.userMessage = A("Join my game");
    g_send.sessionId = A(g_session);
    g_send.addressType = address_type;
    g_send.ids = A(g_account_ids);
    g_send.idsCount = ids_count;
}

static int test_invitation_lifecycle(void) {
    // Initialize needs the common dialog system, and takes it.
    CHECK(sceInvitationDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(ps4_common_dialog_acquire());
    CHECK(sceInvitationDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_BUSY);
    ps4_common_dialog_release();
    CHECK(call(shim_sceInvitationDialogGetStatus, 0) == INVITATION_DIALOG_STATUS_NONE);

    InvitationDialogResult r;
    memset(&r, 0, sizeof(r));
    CHECK(sceInvitationDialogGetResultA(NULL, &r) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceInvitationDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceInvitationDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    // Open checks the state first, OpenA the parameter and the mode first.
    make_invitation(INVITATION_DIALOG_MODE_SEND, INVITATION_DIALOG_ADDRESS_TYPE_USERENABLE, 0);
    CHECK(call(shim_sceInvitationDialogOpen, 0) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(call(shim_sceInvitationDialogOpenA, 0) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    g_ip.mode = 7;
    CHECK(call(shim_sceInvitationDialogOpenA, A(&g_ip)) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    g_ip.mode = INVITATION_DIALOG_MODE_SEND;
    CHECK(call(shim_sceInvitationDialogOpenA, A(&g_ip)) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);

    CHECK(call(shim_sceInvitationDialogInitialize, 0) == 0);
    CHECK(sceCommonDialogIsUsed());
    CHECK(sceInvitationDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceInvitationDialogGetResult(NULL, &r) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(call(shim_sceInvitationDialogOpen, 0) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    g_ip.mode = INVITATION_DIALOG_MODE_INVALID;
    CHECK(call(shim_sceInvitationDialogOpen, A(&g_ip)) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    g_ip.mode = INVITATION_DIALOG_MODE_SEND;
    g_ip.dataParam = 0; // SEND needs its data
    CHECK(call(shim_sceInvitationDialogOpen, A(&g_ip)) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(sceInvitationDialogGetStatus() == INVITATION_DIALOG_STATUS_INITIALIZED);
    CHECK(g_inv_shown == 0);

    // A running dialog: the game closes it, which finishes it with OK.
    make_invitation(INVITATION_DIALOG_MODE_RECV, 0, 0);
    g_ip.dataParam = 0; // RECV does not read it
    CHECK(call(shim_sceInvitationDialogOpenA, A(&g_ip)) == 0);
    CHECK(g_inv_shown == 1 && g_inv_seen.mode == INVITATION_DIALOG_MODE_RECV);
    CHECK(call(shim_sceInvitationDialogUpdateStatus, 0) == INVITATION_DIALOG_STATUS_RUNNING);
    CHECK(call(shim_sceInvitationDialogOpenA, A(&g_ip)) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceInvitationDialogGetResultA(NULL, &r) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(call(shim_sceInvitationDialogClose, 0) == 0);
    CHECK(g_inv_dismissed == 1);
    CHECK(sceInvitationDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceInvitationDialogGetStatus() == INVITATION_DIALOG_STATUS_FINISHED);
    CHECK(sceInvitationDialogGetResultA(NULL, NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(call(shim_sceInvitationDialogGetResultA, A(&r)) == INVITATION_DIALOG_RESULT_OK);
    CHECK(r.callbackArg == 0xCA11BAC4 && r.errorCode == 0 && r.result == INVITATION_DIALOG_RESULT_OK);

    // The window's Cancel: GetResult returns USER_CANCELED itself; a stale window is ignored.
    CHECK(sceInvitationDialogOpen(NULL, &g_ip) == 0);
    const uint32_t first = g_inv_seen.serial;
    ps4_invitation_dialog_finish(first - 1, INVITATION_DIALOG_RESULT_OK);
    CHECK(sceInvitationDialogGetStatus() == INVITATION_DIALOG_STATUS_RUNNING);
    ps4_invitation_dialog_finish(first, INVITATION_DIALOG_RESULT_USER_CANCELED);
    CHECK(sceInvitationDialogGetStatus() == INVITATION_DIALOG_STATUS_FINISHED);
    CHECK(call(shim_sceInvitationDialogGetResult, A(&r)) == INVITATION_DIALOG_RESULT_USER_CANCELED);
    CHECK(r.result == INVITATION_DIALOG_RESULT_USER_CANCELED);

    // Terminate closes a running dialog and gives the common dialog system back.
    CHECK(sceInvitationDialogOpen(NULL, &g_ip) == 0);
    CHECK(call(shim_sceInvitationDialogTerminate, 0) == 0);
    CHECK(g_inv_dismissed == 2);
    CHECK(sceInvitationDialogGetStatus() == INVITATION_DIALOG_STATUS_NONE);
    CHECK(!sceCommonDialogIsUsed());
    CHECK(sceInvitationDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);

    // Without a window the dialog finishes at once, as if Cancel had been pressed.
    CHECK(sceInvitationDialogInitialize() == 0);
    g_ui_available = false;
    CHECK(sceInvitationDialogOpen(NULL, &g_ip) == 0);
    g_ui_available = true;
    CHECK(sceInvitationDialogUpdateStatus() == INVITATION_DIALOG_STATUS_FINISHED);
    CHECK(sceInvitationDialogGetResult(NULL, &r) == INVITATION_DIALOG_RESULT_USER_CANCELED);
    CHECK(sceInvitationDialogTerminate() == 0);
    return 0;
}

static int test_invitation_send(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceInvitationDialogInitialize() == 0);

    // USERDISABLE: the app's fixed recipients; the window shows session and message.
    make_invitation(INVITATION_DIALOG_MODE_SEND, INVITATION_DIALOG_ADDRESS_TYPE_USERDISABLE, 20);
    CHECK(sceInvitationDialogOpenA(NULL, &g_ip) == 0);
    CHECK(g_inv_seen.mode == INVITATION_DIALOG_MODE_SEND && !g_inv_seen.userEditable);
    CHECK(strcmp(g_inv_seen.sessionId, "session-0001") == 0);
    CHECK(strcmp(g_inv_seen.message, "Join my game") == 0);
    // Send: not delivered without an NP sign-in, the dialog finishes with OK and no recipients.
    ps4_invitation_dialog_send(g_inv_seen.serial);
    CHECK(sceInvitationDialogGetStatus() == INVITATION_DIALOG_STATUS_FINISHED);
    struct {
        uint32_t count;
        uint8_t users[16 * 32];
    } sent;
    memset(&sent, 0xAB, sizeof(sent));
    InvitationDialogResult r;
    memset(&r, 0, sizeof(r));
    r.sentUsers = A(&sent);
    CHECK(sceInvitationDialogGetResultA(NULL, &r) == INVITATION_DIALOG_RESULT_OK);
    CHECK(sent.count == 0 && sent.users[0] == 0xAB);
    // A second Send from the same window does nothing more.
    ps4_invitation_dialog_send(g_inv_seen.serial);
    CHECK(sceInvitationDialogGetResultA(NULL, &r) == INVITATION_DIALOG_RESULT_OK);

    // USERENABLE: the player picks the recipients; the session id stops at its terminator and
    // the message at 512 bytes.
    make_invitation(INVITATION_DIALOG_MODE_SEND, INVITATION_DIALOG_ADDRESS_TYPE_USERENABLE, 4);
    memset(g_session, 'S', sizeof(g_session));
    g_session[45] = '\0';
    static char long_msg[700];
    memset(long_msg, 'm', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    g_send.userMessage = A(long_msg);
    CHECK(sceInvitationDialogOpen(NULL, &g_ip) == 0);
    CHECK(g_inv_seen.userEditable);
    CHECK(strlen(g_inv_seen.sessionId) == 45);
    CHECK(strlen(g_inv_seen.message) == INVITATION_DIALOG_MAX_USER_MSG_SIZE);
    ps4_invitation_dialog_finish(g_inv_seen.serial, INVITATION_DIALOG_RESULT_USER_CANCELED);
    memset(&sent, 0xAB, sizeof(sent));
    CHECK(sceInvitationDialogGetResult(NULL, &r) == INVITATION_DIALOG_RESULT_USER_CANCELED);
    CHECK(sent.count == 0);

    // No session id and no message: the window shows neither.
    make_invitation(INVITATION_DIALOG_MODE_SEND, INVITATION_DIALOG_ADDRESS_TYPE_INVALID, 0);
    g_send.sessionId = 0;
    g_send.userMessage = 0;
    CHECK(sceInvitationDialogOpen(NULL, &g_ip) == 0);
    CHECK(g_inv_seen.sessionId[0] == '\0' && g_inv_seen.message[0] == '\0' && !g_inv_seen.userEditable);
    CHECK(sceInvitationDialogTerminate() == 0);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// NpProfileDialog

static NpProfileDialogParam g_pp;
static NpProfileDialogParamA g_pa;

static void make_profile(uint32_t mode) {
    memset(&g_pp, 0, sizeof(g_pp));
    g_pp.baseSize = sizeof(g_pp);
    g_pp.size = sizeof(g_pp);
    g_pp.mode = mode;
    g_pp.userId = 0x10000000;
    snprintf(g_pp.targetOnlineId, sizeof(g_pp.targetOnlineId), "%s", "OtherPlayer");
    g_pp.userData = 0xDA7A;
    memset(&g_pa, 0, sizeof(g_pa));
    g_pa.baseSize = sizeof(g_pa);
    g_pa.size = sizeof(g_pa);
    g_pa.mode = mode;
    g_pa.userId = 0x10000000;
    g_pa.targetAccountId = 1234567890123ULL;
    g_pa.userData = 0xDA7B;
    g_pa.griefReportItem = 0xF;
}

static int test_profile_lifecycle(void) {
    CHECK(sceNpProfileDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0);
    make_profile(NP_PROFILE_DIALOG_MODE_NORMAL);
    NpProfileDialogResult r;
    memset(&r, 0, sizeof(r));
    CHECK(call(shim_sceNpProfileDialogOpen, A(&g_pp)) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(call(shim_sceNpProfileDialogOpenA, 0) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceNpProfileDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceNpProfileDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceNpProfileDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);

    CHECK(call(shim_sceNpProfileDialogInitialize, 0) == 0);
    CHECK(sceCommonDialogIsUsed());
    CHECK(sceNpProfileDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceNpProfileDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceNpProfileDialogGetResult(NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL); // before the state
    CHECK(sceNpProfileDialogGetResult(&r) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(call(shim_sceNpProfileDialogOpen, 0) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    g_pp.mode = NP_PROFILE_DIALOG_MODE_INVALID;
    CHECK(sceNpProfileDialogOpen(&g_pp) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    g_pa.mode = 5;
    CHECK(sceNpProfileDialogOpenA(&g_pa) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(g_prof_shown == 0);

    // Open: the window shows the online ID; the game closes it, which finishes it with OK.
    make_profile(NP_PROFILE_DIALOG_MODE_FRIEND_REQUEST);
    CHECK(call(shim_sceNpProfileDialogOpen, A(&g_pp)) == 0);
    CHECK(g_prof_shown == 1 && g_prof_seen.mode == NP_PROFILE_DIALOG_MODE_FRIEND_REQUEST);
    CHECK(strcmp(g_prof_seen.target, "OtherPlayer") == 0);
    CHECK(call(shim_sceNpProfileDialogUpdateStatus, 0) == NP_PROFILE_DIALOG_STATUS_RUNNING);
    CHECK(sceNpProfileDialogOpen(&g_pp) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(call(shim_sceNpProfileDialogClose, 0) == 0);
    CHECK(g_prof_dismissed == 1);
    CHECK(sceNpProfileDialogClose() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_CLOSE);
    memset(&r, 0xEE, sizeof(r));
    CHECK(call(shim_sceNpProfileDialogGetResult, A(&r)) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    memset(&r, 0, sizeof(r));
    r.result = 77;
    CHECK(call(shim_sceNpProfileDialogGetResult, A(&r)) == 0);
    CHECK(r.result == 0 && r.userAction == NP_PROFILE_DIALOG_RESULT_OK && r.userData == 0xDA7A);

    // OpenA: the window shows the account ID; the player's Close is USER_CANCELED.
    make_profile(NP_PROFILE_DIALOG_MODE_ADD_TO_BLOCK_LIST);
    CHECK(call(shim_sceNpProfileDialogOpenA, A(&g_pa)) == 0);
    CHECK(strcmp(g_prof_seen.target, "1234567890123") == 0);
    ps4_np_profile_dialog_finish(g_prof_seen.serial - 1, NP_PROFILE_DIALOG_RESULT_OK); // stale window
    CHECK(sceNpProfileDialogGetStatus() == NP_PROFILE_DIALOG_STATUS_RUNNING);
    ps4_np_profile_dialog_finish(g_prof_seen.serial, NP_PROFILE_DIALOG_RESULT_USER_CANCELED);
    memset(&r, 0, sizeof(r));
    CHECK(sceNpProfileDialogGetResult(&r) == 0);
    CHECK(r.result == 0 && r.userAction == NP_PROFILE_DIALOG_RESULT_USER_CANCELED && r.userData == 0xDA7B);

    // Terminate closes a running dialog and gives the common dialog system back.
    CHECK(sceNpProfileDialogOpenA(&g_pa) == 0);
    CHECK(call(shim_sceNpProfileDialogTerminate, 0) == 0);
    CHECK(g_prof_dismissed == 2);
    CHECK(sceNpProfileDialogGetStatus() == NP_PROFILE_DIALOG_STATUS_NONE);
    CHECK(!sceCommonDialogIsUsed());

    // Without a window the dialog finishes at once, as if Close had been pressed.
    CHECK(sceNpProfileDialogInitialize() == 0);
    g_ui_available = false;
    CHECK(sceNpProfileDialogOpen(&g_pp) == 0);
    g_ui_available = true;
    CHECK(sceNpProfileDialogGetStatus() == NP_PROFILE_DIALOG_STATUS_FINISHED);
    memset(&r, 0, sizeof(r));
    CHECK(sceNpProfileDialogGetResult(&r) == 0 && r.userAction == NP_PROFILE_DIALOG_RESULT_USER_CANCELED);
    CHECK(sceNpProfileDialogTerminate() == 0);
    return 0;
}

static int test_profile_grief_report(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceNpProfileDialogInitialize() == 0);
    // The operating user has no online ID and account ID 0 without an NP sign-in: a report
    // naming them targets the operating user.
    make_profile(NP_PROFILE_DIALOG_MODE_GRIEF_REPORT);
    memset(g_pp.targetOnlineId, 0, sizeof(g_pp.targetOnlineId));
    CHECK(sceNpProfileDialogOpen(&g_pp) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    g_pa.targetAccountId = 0;
    CHECK(sceNpProfileDialogOpenA(&g_pa) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(sceNpProfileDialogGetStatus() == NP_PROFILE_DIALOG_STATUS_INITIALIZED);
    // Other players can be reported; an online ID fills all 16 bytes up to the term byte.
    make_profile(NP_PROFILE_DIALOG_MODE_GRIEF_REPORT);
    memset(g_pp.targetOnlineId, 'X', sizeof(g_pp.targetOnlineId));
    CHECK(sceNpProfileDialogOpen(&g_pp) == 0);
    CHECK(strcmp(g_prof_seen.target, "XXXXXXXXXXXXXXXX") == 0);
    CHECK(g_prof_seen.mode == NP_PROFILE_DIALOG_MODE_GRIEF_REPORT);
    CHECK(sceNpProfileDialogClose() == 0);
    CHECK(sceNpProfileDialogOpenA(&g_pa) == 0);
    CHECK(sceNpProfileDialogTerminate() == 0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int rc = 2;
    if (strcmp(argv[1], "invitation-lifecycle") == 0) rc = test_invitation_lifecycle();
    if (strcmp(argv[1], "invitation-send") == 0) rc = test_invitation_send();
    if (strcmp(argv[1], "profile-lifecycle") == 0) rc = test_profile_lifecycle();
    if (strcmp(argv[1], "profile-grief-report") == 0) rc = test_profile_grief_report();
    if (rc == 0) printf("PASS %s\n", argv[1]);
    return rc;
}
