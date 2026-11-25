// SPDX-License-Identifier: GPL-2.0-or-later

// libSceGameCustomDataDialog on host memory (guest base 0, so guest addresses are host
// pointers): common dialog system rules, parameter validation, captured request, results.
// The window is replaced by the hooks below, which record what it would show.

#include "ps4_common_dialog.h"
#include "ps4_game_custom_data_dialog.h"
#include "ps4_user_service.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define A(p) ((uint64_t)(uintptr_t)(p))
#define USER ORBIS_USER_SERVICE_INITIAL_USER_ID

static bool g_ui_available = true;
static int g_shown, g_dismissed;
static GcddView g_seen;

bool ps4_gcdd_ui_show(const GcddView *view) {
    if (!g_ui_available) return false;
    g_shown++;
    g_seen = *view;
    return true;
}

void ps4_gcdd_ui_dismiss(void) {
    g_dismissed++;
}

// A valid SEND request with two fixed online-ID recipients.
static uint8_t g_thumb[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 1, 2, 3};
static uint8_t g_blob[64];
static const char *g_platform_names[] = {"PS4", "PS5"};
static uint64_t g_platforms[2];
static GcddNpOnlineId g_online_ids[2];
static uint64_t g_account_ids[2] = {1001, 1002};
static GcddDataParam g_data;
static GcddParam g_param;

static void make_send(int mode, bool account_ids) {
    memset(&g_data, 0, sizeof(g_data));
    memset(&g_param, 0, sizeof(g_param));
    g_platforms[0] = A(g_platform_names[0]);
    g_platforms[1] = A(g_platform_names[1]);
    strcpy(g_online_ids[0].data, "alice");
    strcpy(g_online_ids[1].data, "bob");
    GcddSendInfo *s = &g_data.send;
    s->userMessage = A("Try this ghost!");
    s->dataName = A("Ghost run");
    s->dataDetail = A("Track 3, 1:02.5");
    s->thumbnail = A(g_thumb);
    s->thumbnailSize = sizeof(g_thumb);
    if (mode == GCDD_MODE_SEND_URL) {
        s->data = A("https://example.com/ghost");
        s->dataSize = 1; // withItemId
    } else {
        s->data = A(g_blob);
        s->dataSize = sizeof(g_blob);
    }
    s->expireMinutes = 2 * 24 * 60;
    s->platformCount = 2;
    s->platformName = A(g_platforms);
    s->addressParam.addressType = GCDD_ADDRESS_TYPE_USERDISABLE;
    s->addressParam.info.fixed.ids = account_ids ? A(g_account_ids) : A(g_online_ids);
    s->addressParam.info.fixed.count = 2;
    g_param.size = sizeof(GcddParam);
    g_param.mode = mode;
    g_param.userId = USER;
    g_param.callbackArg = 0xC0FFEE;
    g_param.dataParam = A(&g_data);
    if (mode == GCDD_MODE_RECV) memset(&g_data, 0, sizeof(g_data)); // RecvInfo is reserved bytes
}

static int test_lifecycle(void) {
    GcddResult res;
    memset(&res, 0, sizeof(res));
    CHECK(sceGameCustomDataDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceCommonDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_SYSTEM_INITIALIZED);
    CHECK(ps4_common_dialog_acquire()); // another dialog library holds the system
    CHECK(sceGameCustomDataDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_BUSY);
    ps4_common_dialog_release();

    CHECK(sceGameCustomDataDialogUpdateStatus() == GCDD_STATUS_NONE);
    CHECK(sceGameCustomDataDialogInitialize() == 0);
    CHECK(sceCommonDialogIsUsed());
    CHECK(sceGameCustomDataDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceGameCustomDataDialogGetStatus() == GCDD_STATUS_INITIALIZED);
    CHECK(sceGameCustomDataDialogGetResult(NULL, NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    res.reserved[5] = 1;
    CHECK(sceGameCustomDataDialogGetResult(NULL, &res) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    res.reserved[5] = 0;
    CHECK(sceGameCustomDataDialogGetResult(NULL, &res) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(sceGameCustomDataDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);

    CHECK(sceGameCustomDataDialogTerminate() == 0);
    CHECK(!sceCommonDialogIsUsed());
    CHECK(sceGameCustomDataDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceGameCustomDataDialogGetResult(NULL, &res) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceGameCustomDataDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    printf("PASS lifecycle\n");
    return 0;
}

static int test_validation(void) {
    make_send(GCDD_MODE_SEND, false);
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0 && sceGameCustomDataDialogInitialize() == 0);

#define EXPECT(err, mutate)                                       \
    do {                                                          \
        make_send(GCDD_MODE_SEND, false);                         \
        mutate;                                                   \
        CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == (err)); \
    } while (0)
    GcddSendInfo *s = &g_data.send;
    CHECK(sceGameCustomDataDialogOpen(NULL, NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, g_param.size = 0x88);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, g_param.reserved[63] = 1);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, g_param.dataParam = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, g_param.userId = USER + 1);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, g_param.mode = 4);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, s->userMessage = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->dataName = A(""));
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, s->dataDetail = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, s->thumbnail = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->thumbnailSize = GCDD_MAX_THUMBNAIL_SIZE + 1);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, s->data = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->dataSize = GCDD_MAX_DATA_SIZE + 1);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->addressParam.addressType = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->platformCount = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, g_platforms[1] = A(""));
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->addressParam.info.fixed.count = 17);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, s->addressParam.info.fixed.ids = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, g_online_ids[1].data[0] = 0);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->dataNameMultiLangNum = 31);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID,
           (s->addressParam.addressType = GCDD_ADDRESS_TYPE_USERENABLE, s->addressParam.info.editable.userMaxCount = 0));

    // 513 two-byte characters fit in 2048 bytes but exceed 512 characters.
    static char msg[1100];
    for (int i = 0; i < 513; i++) memcpy(msg + i * 2, "\xc3\xa9", 2);
    EXPECT(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, s->userMessage = A(msg));
    msg[1024] = 0; // 512 characters
    make_send(GCDD_MODE_SEND, false);
    s->userMessage = A(msg);
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == 0);
    CHECK(sceGameCustomDataDialogClose() == 0);

    // RECV needs zeroed reserved bytes; SEND_URL needs a URL.
    make_send(GCDD_MODE_RECV, false);
    g_data.recvReserved[100] = 1;
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    make_send(GCDD_MODE_SEND_URL, false);
    s->data = A("");
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    // OpenA reads account IDs: a zero ID is empty.
    make_send(GCDD_MODE_SEND, true);
    g_account_ids[0] = 0;
    CHECK(sceGameCustomDataDialogOpenA(NULL, &g_param) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    g_account_ids[0] = 1001;
    CHECK(sceGameCustomDataDialogTerminate() == 0);
    printf("PASS validation\n");
    return 0;
}

static int test_send_cancel(void) {
    CHECK(sceCommonDialogInitialize() == 0 && sceGameCustomDataDialogInitialize() == 0);
    make_send(GCDD_MODE_SEND, false);
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == 0);
    CHECK(sceGameCustomDataDialogUpdateStatus() == GCDD_STATUS_RUNNING && g_shown == 1);
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);

    // The window got a copy of the request.
    CHECK(g_seen.mode == GCDD_MODE_SEND && strcmp(g_seen.data_name, "Ghost run") == 0);
    CHECK(strcmp(g_seen.message, "Try this ghost!") == 0 && g_seen.data_size == sizeof(g_blob));
    CHECK(g_seen.platform_count == 2 && strcmp(g_seen.platforms[1], "PS5") == 0);
    CHECK(!g_seen.user_editable && g_seen.recipient_count == 2 && strcmp(g_seen.recipients[1], "bob") == 0);
    CHECK(g_seen.expire_minutes == 2880);
    CHECK(g_seen.thumbnail_size == sizeof(g_thumb) && g_seen.thumbnail != g_thumb &&
          memcmp(g_seen.thumbnail, g_thumb, sizeof(g_thumb)) == 0);

    ps4_gcdd_finish(g_seen.serial + 1, GCDD_RESULT_OK); // another dialog's window: ignored
    CHECK(sceGameCustomDataDialogUpdateStatus() == GCDD_STATUS_RUNNING);
    ps4_gcdd_finish(g_seen.serial, GCDD_RESULT_USER_CANCELED);
    CHECK(sceGameCustomDataDialogUpdateStatus() == GCDD_STATUS_FINISHED);

    GcddNpOnlineId list[17];
    memset(list, 0xFF, sizeof(list));
    GcddResult res;
    memset(&res, 0, sizeof(res));
    res.sent = A(list);
    CHECK(sceGameCustomDataDialogGetResult(NULL, &res) == GCDD_RESULT_USER_CANCELED);
    CHECK(res.result == GCDD_RESULT_USER_CANCELED && res.callbackArg == 0xC0FFEE && res.errorCode == 0);
    const uint8_t *bytes = (const uint8_t *)list;
    for (size_t i = 0; i < 4 + 16 * sizeof(GcddNpOnlineId); i++) CHECK(bytes[i] == 0);
    CHECK(bytes[4 + 16 * sizeof(GcddNpOnlineId)] == 0xFF);

    // A finished dialog can be opened again; URL mode with player-chosen recipients.
    make_send(GCDD_MODE_SEND_URL, false);
    g_data.send.addressParam.addressType = GCDD_ADDRESS_TYPE_USERENABLE;
    g_data.send.addressParam.info.editable.userMaxCount = 3;
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == 0 && g_shown == 2);
    CHECK(strcmp(g_seen.data_url, "https://example.com/ghost") == 0 && g_seen.with_item_id);
    CHECK(g_seen.user_editable && g_seen.user_max_count == 3 && g_seen.recipient_count == 0);
    CHECK(sceGameCustomDataDialogTerminate() == 0 && g_dismissed == 1);
    CHECK(!sceCommonDialogIsUsed());
    printf("PASS send-cancel\n");
    return 0;
}

static int test_close_ok(void) {
    CHECK(sceCommonDialogInitialize() == 0 && sceGameCustomDataDialogInitialize() == 0);
    make_send(GCDD_MODE_SEND, true);
    CHECK(sceGameCustomDataDialogOpenA(NULL, &g_param) == 0);
    CHECK(strcmp(g_seen.recipients[0], "account 1001") == 0);
    CHECK(sceGameCustomDataDialogClose() == 0 && g_dismissed == 1);
    CHECK(sceGameCustomDataDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);

    static uint8_t users[8 + 16 * 32 + 1];
    memset(users, 0xFF, sizeof(users));
    GcddResult res;
    memset(&res, 0, sizeof(res));
    res.sent = A(users);
    CHECK(sceGameCustomDataDialogGetResultA(NULL, &res) == GCDD_RESULT_OK && res.result == GCDD_RESULT_OK);
    for (size_t i = 0; i < sizeof(users) - 1; i++) CHECK(users[i] == 0);
    CHECK(users[sizeof(users) - 1] == 0xFF);
    CHECK(sceGameCustomDataDialogTerminate() == 0 && g_dismissed == 1); // not running: nothing to close
    printf("PASS close-ok\n");
    return 0;
}

static int test_headless(void) {
    g_ui_available = false;
    CHECK(sceCommonDialogInitialize() == 0 && sceGameCustomDataDialogInitialize() == 0);
    make_send(GCDD_MODE_RECV, false);
    CHECK(sceGameCustomDataDialogOpen(NULL, &g_param) == 0);
    CHECK(sceGameCustomDataDialogUpdateStatus() == GCDD_STATUS_FINISHED);
    GcddResult res;
    memset(&res, 0, sizeof(res));
    CHECK(sceGameCustomDataDialogGetResult(NULL, &res) == GCDD_RESULT_USER_CANCELED);
    CHECK(sceGameCustomDataDialogTerminate() == 0);
    printf("PASS headless\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "lifecycle") == 0) return test_lifecycle();
    if (strcmp(argv[1], "validation") == 0) return test_validation();
    if (strcmp(argv[1], "send-cancel") == 0) return test_send_cancel();
    if (strcmp(argv[1], "close-ok") == 0) return test_close_ok();
    if (strcmp(argv[1], "headless") == 0) return test_headless();
    return 2;
}
