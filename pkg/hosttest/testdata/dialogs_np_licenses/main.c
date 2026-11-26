// SPDX-License-Identifier: GPL-2.0-or-later

// libSceErrorDialog, libSceHmdSetupDialog, libSceVrServiceDialog, libSceNpPartner001 and
// libSceNpEntitlementAccess against shadPS4's behaviour. The ErrorDialog window is replaced by
// recording hooks; the entitlement cases write license files and a param.sfo to a temporary
// directory and read them through the real runtime code.

#include "ps4_common_dialog.h"
#include "ps4_error_dialog.h"
#include "ps4_np_entitlement_access.h"
#include "ps4_np_partner.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                                             \
        }                                                                         \
    } while (0)

#include "../host_guest_mem.h"

void shim_sceHmdSetupDialogInitialize(GuestContext *ctx);
void shim_sceHmdSetupDialogOpen(GuestContext *ctx);
void shim_sceHmdSetupDialogClose(GuestContext *ctx);
void shim_sceHmdSetupDialogTerminate(GuestContext *ctx);
void shim_sceHmdSetupDialogGetResult(GuestContext *ctx);
void shim_sceHmdSetupDialogGetStatus(GuestContext *ctx);
void shim_sceHmdSetupDialogUpdateStatus(GuestContext *ctx);
void shim_sceVrServiceDialogClose(GuestContext *ctx);
void shim_sceVrServiceDialogGetResult(GuestContext *ctx);
void shim_sceVrServiceDialogGetStatus(GuestContext *ctx);
void shim_sceVrServiceDialogInitialize(GuestContext *ctx);
void shim_sceVrServiceDialogOpen(GuestContext *ctx);
void shim_sceVrServiceDialogTerminate(GuestContext *ctx);
void shim_sceVrServiceDialogUpdateStatus(GuestContext *ctx);

// ---------------------------------------------------------------------------------------------
// ErrorDialog window hooks

static bool g_ui_available = true;
static int g_shown, g_dismissed;
static uint32_t g_serial;
static char g_message[ERROR_DIALOG_MESSAGE_SIZE];

bool ps4_error_dialog_ui_show(uint32_t serial, const char *message) {
    if (!g_ui_available) return false;
    g_shown++;
    g_serial = serial;
    snprintf(g_message, sizeof(g_message), "%s", message);
    return true;
}

void ps4_error_dialog_ui_dismiss(void) {
    g_dismissed++;
}

// ---------------------------------------------------------------------------------------------
// The VFS functions ps4_sfo.c uses: the app root is the case's temporary directory.

static char g_root[512];

const char *ps4_vfs_get_app_root(void) {
    return g_root;
}

int ps4_vfs_resolve(const char *guest_path, char *host_path, size_t host_path_sz) {
    (void)guest_path;
    (void)host_path;
    (void)host_path_sz;
    return -1;
}

static int64_t rc_of(const GuestContext *ctx) {
    return (int64_t)ctx->rax;
}

// ---------------------------------------------------------------------------------------------
// ErrorDialog

static int open_code(int32_t code) {
    OrbisErrorDialogParam p = {sizeof(p), code, 1, 0};
    return sceErrorDialogOpen(&p);
}

static int test_error_dialog(void) {
    CHECK(sceErrorDialogGetStatus() == ERROR_DIALOG_STATUS_NONE);
    CHECK(open_code(1) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceErrorDialogOpen(NULL) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE); // state first
    CHECK(sceErrorDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceErrorDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);

    CHECK(sceErrorDialogInitialize() == 0);
    CHECK(sceErrorDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceErrorDialogUpdateStatus() == ERROR_DIALOG_STATUS_INITIALIZED);
    CHECK(sceErrorDialogOpen(NULL) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);

    // The player presses OK.
    CHECK(open_code((int32_t)0x80020003) == 0);
    CHECK(g_shown == 1);
    CHECK(strcmp(g_message, "An error has occurred. \nCode: 0X80020003") == 0);
    CHECK(sceErrorDialogUpdateStatus() == ERROR_DIALOG_STATUS_RUNNING);
    CHECK(open_code(2) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    ps4_error_dialog_finish(g_serial + 1); // an earlier dialog's window: ignored
    CHECK(sceErrorDialogGetStatus() == ERROR_DIALOG_STATUS_RUNNING);
    ps4_error_dialog_finish(g_serial);
    CHECK(sceErrorDialogGetStatus() == ERROR_DIALOG_STATUS_FINISHED);
    CHECK(sceErrorDialogClose() == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(g_dismissed == 0);

    // The game closes it.
    CHECK(open_code(0x1F) == 0);
    CHECK(strcmp(g_message, "An error has occurred. \nCode: 0X1F") == 0);
    CHECK(sceErrorDialogClose() == 0);
    CHECK(g_dismissed == 1);
    CHECK(sceErrorDialogGetStatus() == ERROR_DIALOG_STATUS_FINISHED);

    // Terminate ends a dialog still up.
    CHECK(open_code(3) == 0);
    CHECK(sceErrorDialogTerminate() == 0);
    CHECK(g_dismissed == 2);
    CHECK(sceErrorDialogGetStatus() == ERROR_DIALOG_STATUS_NONE);
    CHECK(sceErrorDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);

    // No window (headless run): the dialog finishes at once.
    g_ui_available = false;
    CHECK(sceErrorDialogInitialize() == 0);
    CHECK(open_code(4) == 0);
    CHECK(sceErrorDialogUpdateStatus() == ERROR_DIALOG_STATUS_FINISHED);
    CHECK(sceErrorDialogTerminate() == 0);

    GuestContext ctx = {0};
    ctx.mem_base = GUEST_MEM;
    ctx.rax = 99;
    shim_sceErrorDialogOpenDetail(&ctx);
    CHECK(rc_of(&ctx) == 0);
    ctx.rax = 99;
    shim_sceErrorDialogOpenWithReport(&ctx);
    CHECK(rc_of(&ctx) == 0);
    return 0;
}

// A parameter of the wrong size stops the process (shadPS4 asserts).
static int test_error_dialog_bad_size(void) {
    CHECK(sceErrorDialogInitialize() == 0);
    OrbisErrorDialogParam p = {8, 1, 1, 0};
    const int rc = sceErrorDialogOpen(&p);
    printf("sceErrorDialogOpen returned %d\n", rc);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// HmdSetupDialog and VrServiceDialog: no headset, the dialog is finished and cancelled.

static int test_hmd_vr_dialogs(void) {
    GuestContext ctx = {0};
    ctx.mem_base = GUEST_MEM;
    void (*const ok[])(GuestContext *) = {
        shim_sceHmdSetupDialogInitialize, shim_sceHmdSetupDialogOpen,  shim_sceHmdSetupDialogClose,
        shim_sceHmdSetupDialogTerminate,  shim_sceVrServiceDialogClose, shim_sceVrServiceDialogGetResult,
        shim_sceVrServiceDialogGetStatus, shim_sceVrServiceDialogInitialize, shim_sceVrServiceDialogOpen,
        shim_sceVrServiceDialogTerminate,
    };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        ctx.rax = 99;
        ok[i](&ctx);
        CHECK(rc_of(&ctx) == 0);
    }
    shim_sceHmdSetupDialogUpdateStatus(&ctx);
    CHECK(rc_of(&ctx) == 3);
    shim_sceHmdSetupDialogGetStatus(&ctx);
    CHECK(rc_of(&ctx) == 3);
    shim_sceVrServiceDialogUpdateStatus(&ctx);
    CHECK(rc_of(&ctx) == 3);

    struct {
        int32_t result;
        uint8_t reserved[32];
    } res;
    memset(&res, 0xAB, sizeof(res));
    ctx.rdi = A(&res);
    shim_sceHmdSetupDialogGetResult(&ctx);
    CHECK(rc_of(&ctx) == 0);
    CHECK(res.result == 1); // USER_CANCELED
    CHECK(res.reserved[0] == 0xAB && res.reserved[31] == 0xAB);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// NpPartner

static int test_np_partner(void) {
    bool sub = true;
    CHECK(sceNpHasEAAccessSubscription(1, &sub) == ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED);
    CHECK(sceNpHasEAAccessSubscriptionAbortRequest() == ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED);
    CHECK(sceNpEAAccessTerminate() == ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED);
    CHECK(sceNpEAAccessInitialize() == 0);
    CHECK(sceNpEAAccessInitialize() == 0);
    CHECK(sceNpHasEAAccessSubscription(1, NULL) == ORBIS_NP_PARTNER_ERROR_INVALID_ARGUMENT);
    CHECK(sub == true);

    GuestContext ctx = {0};
    ctx.mem_base = GUEST_MEM;
    ctx.rdi = 0x10000001;
    ctx.rsi = A(&sub);
    shim_sceNpHasEAAccessSubscription(&ctx);
    CHECK(rc_of(&ctx) == 0);
    CHECK(sub == false);
    CHECK(sceNpHasEAAccessSubscriptionAbortRequest() == 0);
    CHECK(sceNpEAAccessTerminate() == 0);
    CHECK(sceNpEAAccessTerminate() == ORBIS_NP_PARTNER_ERROR_NOT_INITIALIZED);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// NpEntitlementAccess requests

static int test_entitlement_requests(void) {
    GuestContext ctx = {0};
    ctx.mem_base = GUEST_MEM;
    uint32_t id = 0;
    char tx[NP_ENTITLEMENT_TRANSACTION_ID_SIZE + 4];
    ctx.rdx = A("LABEL");
    ctx.rcx = A("TX");
    ctx.r9 = A(&id);
    shim_sceNpEntitlementAccessRequestConsumeEntitlement(&ctx);
    CHECK(rc_of(&ctx) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
    uint32_t flag = 77;
    CHECK(sceNpEntitlementAccessGetSkuFlag(&flag) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
    CHECK(sceNpEntitlementAccessGenerateTransactionId(tx) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);
    shim_sceNpEntitlementAccessDeleteRequest(&ctx);
    CHECK(rc_of(&ctx) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_INITIALIZED);

    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == 0);
    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_ALREADY_INITIALIZED);
    CHECK(sceNpEntitlementAccessGetSkuFlag(NULL) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);

    // Request ids are handed out in order.
    shim_sceNpEntitlementAccessRequestConsumeEntitlement(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 1);
    shim_sceNpEntitlementAccessRequestConsumeUnifiedEntitlement(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 2);
    shim_sceNpEntitlementAccessRequestConsumeServiceEntitlement(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 3);
    ctx.rcx = 0; // no transaction id
    shim_sceNpEntitlementAccessRequestConsumeEntitlement(&ctx);
    CHECK(rc_of(&ctx) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT && id == 3);

    ctx.rcx = A(&id); // (user, type, label, u32 *request_id)
    shim_sceNpEntitlementAccessRequestConsumableEntitlementInfo(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 4);
    shim_sceNpEntitlementAccessRequestUnifiedEntitlementInfo(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 5);
    shim_sceNpEntitlementAccessRequestServiceEntitlementInfo(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 6);
    ctx.rdx = 0;
    shim_sceNpEntitlementAccessRequestServiceEntitlementInfo(&ctx);
    CHECK(rc_of(&ctx) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);
    ctx.r9 = A(&id); // the list requests need only the request id
    shim_sceNpEntitlementAccessRequestUnifiedEntitlementInfoList(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 7);
    shim_sceNpEntitlementAccessRequestServiceEntitlementInfoList(&ctx);
    CHECK(rc_of(&ctx) == 0 && id == 8);
    ctx.r9 = 0;
    shim_sceNpEntitlementAccessRequestServiceEntitlementInfoList(&ctx);
    CHECK(rc_of(&ctx) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);

    // Polls: status 0, the info cleared (0x40 bytes), no list entries.
    int32_t status = 5;
    uint8_t info[NP_ENTITLEMENT_POLL_INFO_SIZE + 1];
    memset(info, 0xCC, sizeof(info));
    ctx.rdi = 1;
    ctx.rsi = A(&status);
    ctx.rdx = A(info);
    shim_sceNpEntitlementAccessPollUnifiedEntitlementInfo(&ctx);
    CHECK(rc_of(&ctx) == 0 && status == 0);
    CHECK(info[0] == 0 && info[NP_ENTITLEMENT_POLL_INFO_SIZE - 1] == 0 && info[NP_ENTITLEMENT_POLL_INFO_SIZE] == 0xCC);
    uint32_t remaining = 9, count = 9, total = 9;
    status = 5;
    ctx.rdx = A(&remaining);
    shim_sceNpEntitlementAccessPollConsumeEntitlement(&ctx);
    CHECK(rc_of(&ctx) == 0 && status == 0 && remaining == 0);
    status = 5;
    ctx.rdx = 0;
    ctx.r8 = A(&count);
    ctx.r9 = A(&total);
    shim_sceNpEntitlementAccessPollServiceEntitlementInfoList(&ctx);
    CHECK(rc_of(&ctx) == 0 && status == 0 && count == 0 && total == 0);
    ctx.rsi = 0; // all outputs are optional
    ctx.r8 = 0;
    ctx.r9 = 0;
    shim_sceNpEntitlementAccessPollUnifiedEntitlementInfoList(&ctx);
    CHECK(rc_of(&ctx) == 0);
    shim_sceNpEntitlementAccessAbortRequest(&ctx);
    CHECK(rc_of(&ctx) == 0);

    // Transaction ids are random version 4 UUIDs.
    char tx2[NP_ENTITLEMENT_TRANSACTION_ID_SIZE];
    memset(tx, 0x7F, sizeof(tx));
    CHECK(sceNpEntitlementAccessGenerateTransactionId(NULL) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);
    CHECK(sceNpEntitlementAccessGenerateTransactionId(tx) == 0);
    CHECK(sceNpEntitlementAccessGenerateTransactionId(tx2) == 0);
    CHECK(strlen(tx) == 36 && tx[37] == 0x7F);
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            CHECK(tx[i] == '-');
        } else {
            CHECK(isxdigit((unsigned char)tx[i]) && !isupper((unsigned char)tx[i]));
        }
    }
    CHECK(tx[14] == '4' && strchr("89ab", tx[19]) != NULL);
    CHECK(strcmp(tx, tx2) != 0);

    // Add-on content lists are not available offline: nothing is written.
    uint8_t list[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    CHECK(sceNpEntitlementAccessGetAddcontEntitlementInfoList(1, "svc", 8, NULL) ==
          ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);
    CHECK(sceNpEntitlementAccessGetAddcontEntitlementInfoList(1, "svc", 8, list) == 0);
    CHECK(list[0] == 1);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// License files

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
    put_le16(p, (uint16_t)v);
    put_le16(p + 2, (uint16_t)(v >> 16));
}

static void put_le64(uint8_t *p, uint64_t v) {
    put_le32(p, (uint32_t)v);
    put_le32(p + 4, (uint32_t)(v >> 32));
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_file(const char *dir, const char *name, const void *data, size_t size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const size_t n = fwrite(data, 1, size, f);
    return fclose(f) == 0 && n == size ? 0 : -1;
}

// A RIF: content type and the two SKU flag bytes as stored, the entitlement key from seed.
static void make_rif(uint8_t *rif, uint32_t magic, uint16_t content_type, uint8_t sku0, uint8_t sku1, uint8_t seed) {
    memset(rif, 0, RIF_SIZE);
    put_be32(rif, magic);
    put_le16(rif + RIF_CONTENT_TYPE_OFFSET, content_type);
    rif[RIF_SKU_FLAG_OFFSET] = sku0;
    rif[RIF_SKU_FLAG_OFFSET + 1] = sku1;
    for (int i = 0; i < NP_ENTITLEMENT_KEY_SIZE; i++) rif[RIF_ENTITLEMENT_KEY_OFFSET + i] = (uint8_t)(seed + i);
}

typedef struct TestEntry {
    const char *label;
    uint64_t rif_size;
    uint32_t magic;
    uint16_t content_type;
    uint8_t sku0, sku1, seed;
} TestEntry;

// <service>.idx and <service>.rif holding the entries; the RIFs follow the RIFA header.
static int write_container(const char *dir, const char *service, const TestEntry *entries, uint32_t n) {
    static uint8_t ridx[RIDX_HEADER_SIZE + 8 * RIDX_ENTRY_SIZE];
    static uint8_t rifa[RIFA_HEADER_SIZE + 8 * RIF_SIZE];
    memset(ridx, 0, sizeof(ridx));
    memset(rifa, 0, sizeof(rifa));
    put_be32(ridx, RIDX_MAGIC);
    put_le32(ridx + 4, n);
    ridx[8] = RIDX_VERSION;
    put_be32(rifa, RIFA_MAGIC);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = ridx + RIDX_HEADER_SIZE + i * RIDX_ENTRY_SIZE;
        strncpy((char *)e, entries[i].label, 16);
        const uint64_t off = RIFA_HEADER_SIZE + (uint64_t)i * RIF_SIZE;
        put_le64(e + 0x10, off);
        put_le64(e + 0x18, entries[i].rif_size);
        make_rif(rifa + off, entries[i].magic, entries[i].content_type, entries[i].sku0, entries[i].sku1,
                 entries[i].seed);
    }
    char name[256];
    snprintf(name, sizeof(name), "%s.idx", service);
    if (write_file(dir, name, ridx, RIDX_HEADER_SIZE + n * RIDX_ENTRY_SIZE) != 0) return -1;
    snprintf(name, sizeof(name), "%s.rif", service);
    return write_file(dir, name, rifa, RIFA_HEADER_SIZE + n * RIF_SIZE);
}

// A param.sfo with the four values the library reads.
static int write_sfo(const char *content_id, const char *title_id, const char *category, int32_t app_type) {
    static uint8_t sfo[512];
    memset(sfo, 0, sizeof(sfo));
    const char *keys[4] = {"APP_TYPE", "CATEGORY", "CONTENT_ID", "TITLE_ID"};
    const char *values[4] = {NULL, category, content_id, title_id};
    const uint32_t key_table = 20 + 4 * 16, data_table = key_table + 48;
    sfo[1] = 'P';
    sfo[2] = 'S';
    sfo[3] = 'F';
    put_le32(sfo + 4, 0x101);
    put_le32(sfo + 8, key_table);
    put_le32(sfo + 12, data_table);
    put_le32(sfo + 16, 4);
    uint32_t key_off = 0, data_off = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t *e = sfo + 20 + i * 16;
        const uint32_t len = values[i] ? (uint32_t)strlen(values[i]) + 1 : 4;
        put_le16(e, (uint16_t)key_off);
        put_le16(e + 2, values[i] ? 0x0204 : 0x0404);
        put_le32(e + 4, len);
        put_le32(e + 8, 64);
        put_le32(e + 12, data_off);
        memcpy(sfo + key_table + key_off, keys[i], strlen(keys[i]) + 1);
        if (values[i]) {
            memcpy(sfo + data_table + data_off, values[i], len);
        } else {
            put_le32(sfo + data_table + data_off, (uint32_t)app_type);
        }
        key_off += (uint32_t)strlen(keys[i]) + 1;
        data_off += 64;
    }
    return write_file(g_root, "param.sfo", sfo, data_table + data_off);
}

// The case's directory: the app root (param.sfo) and the data directory ($PS4RECOMP_DATA_DIR,
// licenses in its "licenses" subdirectory).
static char g_licenses[600];

static int setup_dirs(bool with_licenses) {
    const char *tmp = getenv("TMPDIR");
    snprintf(g_root, sizeof(g_root), "%s/ps4-entitlement-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(g_root)) return -1;
    setenv("PS4RECOMP_DATA_DIR", g_root, 1);
    snprintf(g_licenses, sizeof(g_licenses), "%s/licenses", g_root);
    return with_licenses ? mkdir(g_licenses, 0700) : 0;
}

static void remove_dirs(void) {
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", g_root);
}

#define CONTENT_ID "UP0001-CUSA01234_00-GAMEDATA00000001"
#define SERVICE_ID "UP0001-CUSA01234_00"

static int check_key(const char *label, uint8_t seed) {
    uint8_t key[NP_ENTITLEMENT_KEY_SIZE];
    memset(key, 0, sizeof(key));
    if (sceNpEntitlementAccessGetEntitlementKey(1, label, key) != 0) return -1;
    for (int i = 0; i < NP_ENTITLEMENT_KEY_SIZE; i++) {
        if (key[i] != (uint8_t)(seed + i)) return -1;
    }
    return 0;
}

static int licenses_case(void) {
    const TestEntry entries[] = {
        {"DLCKEY01", RIF_SIZE, RIF_MAGIC, 0x1C00, 0, 0, 0x10},
        {"BADSIZE", 0x200, RIF_MAGIC, 0x1A00, 0, 9, 0x20},
        {"BADMAGIC", RIF_SIZE, 0x12345678, 0x1A00, 0, 9, 0x30},
        {"DLCKEY01", RIF_SIZE, RIF_MAGIC, 0x1A00, 0, 7, 0x40}, // duplicate label: ignored
        {"GAME", RIF_SIZE, RIF_MAGIC, 0x1A00, 0x00, 0x03, 0x50}, // stored 00 03: flag 3
    };
    CHECK(write_container(g_licenses, SERVICE_ID, entries, 5) == 0);
    const TestEntry other[] = {{"FALLBACK", RIF_SIZE, RIF_MAGIC, 0x1C00, 0, 0, 0x60}};
    CHECK(write_container(g_licenses, "IV9999-NPXX00000_00", other, 1) == 0);
    CHECK(write_sfo(CONTENT_ID, "CUSA01234", "ac", 0) == 0);

    uint32_t flag = 0;
    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == 0);
    CHECK(sceNpEntitlementAccessGetSkuFlag(&flag) == 0);
    CHECK(flag == 3);

    CHECK(check_key("DLCKEY01", 0x10) == 0); // the first entry with the label
    CHECK(check_key("GAME", 0x50) == 0);
    CHECK(check_key("FALLBACK", 0x60) == 0); // found in another container
    uint8_t key[NP_ENTITLEMENT_KEY_SIZE];
    CHECK(sceNpEntitlementAccessGetEntitlementKey(1, "BADSIZE", key) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    CHECK(sceNpEntitlementAccessGetEntitlementKey(1, "BADMAGIC", key) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    CHECK(sceNpEntitlementAccessGetEntitlementKey(1, "MISSING", key) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    CHECK(sceNpEntitlementAccessGetEntitlementKey(1, NULL, key) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_INVALID_ARGUMENT);

    uint8_t info[NP_ENTITLEMENT_ADDCONT_INFO_SIZE + 1];
    memset(info, 0xEE, sizeof(info));
    CHECK(sceNpEntitlementAccessGetAddcontEntitlementInfo(1, "DLCKEY01", info) == 0);
    CHECK(info[0] == 0 && info[NP_ENTITLEMENT_ADDCONT_INFO_SIZE - 1] == 0 && info[NP_ENTITLEMENT_ADDCONT_INFO_SIZE] == 0xEE);
    memset(info, 0xEE, sizeof(info));
    CHECK(sceNpEntitlementAccessGetAddcontEntitlementInfo(1, "MISSING", info) ==
          ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    CHECK(info[0] == 0xEE);
    return 0;
}

// A lone <service>.rif (no index) holding a single RIF; the flag is stored little endian.
static int single_rif_case(void) {
    uint8_t rif[RIF_SIZE];
    make_rif(rif, RIF_MAGIC, 0, 0x00, 0x05, 0); // le 0x0500, be 0x0005: flag 5
    CHECK(write_file(g_licenses, SERVICE_ID ".rif", rif, sizeof(rif)) == 0);
    CHECK(write_sfo(CONTENT_ID, "CUSA01234", "ac", 0) == 0);
    uint32_t flag = 0;
    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == 0);
    CHECK(sceNpEntitlementAccessGetSkuFlag(&flag) == 0);
    CHECK(flag == 5);
    uint8_t key[NP_ENTITLEMENT_KEY_SIZE];
    CHECK(sceNpEntitlementAccessGetEntitlementKey(1, "ANY", key) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    return 0;
}

// The container's name holds TITLE_ID but not the CONTENT_ID service id.
static int title_id_case(void) {
    const TestEntry entries[] = {
        {"OTHER", RIF_SIZE, RIF_MAGIC, 0x1C00, 0x00, 0x04, 1}, // not a game entitlement type
        {"GAME", RIF_SIZE, RIF_MAGIC, 0x001A, 0x02, 0x01, 2},  // le 0x0102, be 0x0201: flag 0x102
    };
    CHECK(write_container(g_licenses, "EP0002-CUSA01234_00", entries, 2) == 0);
    CHECK(write_sfo("UP9999-CUSA01234_00-NOLICENSE0000000", "CUSA01234", "ac", 0) == 0);
    uint32_t flag = 0;
    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == 0);
    CHECK(sceNpEntitlementAccessGetSkuFlag(&flag) == 0);
    CHECK(flag == 0x102);
    return 0;
}

// A game application ("gd") has SKU flag 0 without any license; anything else needs one.
static int game_app_case(void) {
    CHECK(write_sfo(CONTENT_ID, "CUSA01234", "gd", 0) == 0);
    uint32_t flag = 9;
    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == 0);
    CHECK(sceNpEntitlementAccessGetSkuFlag(&flag) == 0);
    CHECK(flag == 0);
    return 0;
}

static int no_licenses_case(void) {
    CHECK(write_sfo(CONTENT_ID, "CUSA01234", "ac", 0) == 0);
    uint32_t flag = 9;
    CHECK(sceNpEntitlementAccessInitialize(NULL, NULL) == 0);
    CHECK(sceNpEntitlementAccessGetSkuFlag(&flag) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    CHECK(flag == 9);
    uint8_t key[NP_ENTITLEMENT_KEY_SIZE];
    CHECK(sceNpEntitlementAccessGetEntitlementKey(1, "GAME", key) == ORBIS_NP_ENTITLEMENT_ACCESS_ERROR_NOT_FOUND);
    return 0;
}

static int run_license_case(int (*fn)(void), bool with_licenses) {
    if (setup_dirs(with_licenses) != 0) {
        fprintf(stderr, "could not create the case directory\n");
        return 1;
    }
    const int rc = fn();
    remove_dirs();
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *c = argv[1];
    int rc;
    if (strcmp(c, "error-dialog") == 0) {
        rc = test_error_dialog();
    } else if (strcmp(c, "error-dialog-bad-size") == 0) {
        rc = test_error_dialog_bad_size();
    } else if (strcmp(c, "hmd-vr-dialogs") == 0) {
        rc = test_hmd_vr_dialogs();
    } else if (strcmp(c, "np-partner") == 0) {
        rc = test_np_partner();
    } else if (strcmp(c, "entitlement-requests") == 0) {
        rc = test_entitlement_requests();
    } else if (strcmp(c, "entitlement-licenses") == 0) {
        rc = run_license_case(licenses_case, true);
    } else if (strcmp(c, "entitlement-single-rif") == 0) {
        rc = run_license_case(single_rif_case, true);
    } else if (strcmp(c, "entitlement-title-id") == 0) {
        rc = run_license_case(title_id_case, true);
    } else if (strcmp(c, "entitlement-game-app") == 0) {
        rc = run_license_case(game_app_case, false);
    } else if (strcmp(c, "entitlement-no-licenses") == 0) {
        rc = run_license_case(no_licenses_case, false);
    } else {
        return 2;
    }
    if (rc == 0) printf("PASS %s\n", c);
    return rc;
}
