// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_GAME_CUSTOM_DATA_DIALOG_H
#define PS4_GAME_CUSTOM_DATA_DIALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GCDD_STATUS_NONE 0
#define GCDD_STATUS_INITIALIZED 1
#define GCDD_STATUS_RUNNING 2
#define GCDD_STATUS_FINISHED 3

#define GCDD_RESULT_OK 0
#define GCDD_RESULT_USER_CANCELED 1

#define GCDD_MODE_INVALID 0
#define GCDD_MODE_SEND 1
#define GCDD_MODE_RECV 2
#define GCDD_MODE_SEND_URL 3

#define GCDD_ADDRESS_TYPE_USERDISABLE 1 // the game fixes the recipients
#define GCDD_ADDRESS_TYPE_USERENABLE 2  // the player picks them

#define GCDD_ADDRESS_USER_LIST_MAX_SIZE 16
#define GCDD_MAX_USER_MSG_SIZE 512
#define GCDD_MAX_URL_LENGTH 1024
#define GCDD_MAX_MESSAGE_BYTES 2048
#define GCDD_MAX_NAME_BYTES 256
#define GCDD_MAX_DETAIL_BYTES 2048
#define GCDD_MAX_THUMBNAIL_SIZE (160 * 1024)
#define GCDD_MAX_DATA_SIZE (1024 * 1024)
#define GCDD_MAX_MULTI_LANG_STRINGS 30
#define GCDD_MAX_PLATFORM_NAME_BYTES 16
#define GCDD_MAX_PLATFORMS 16
#define GCDD_NP_ONLINE_ID_MAX_LENGTH 16

// ---- Guest structures (pointers are guest addresses) ----

typedef struct GcddNpOnlineId {
    char data[GCDD_NP_ONLINE_ID_MAX_LENGTH];
    int8_t term;
    int8_t dummy[3];
} GcddNpOnlineId;
_Static_assert(sizeof(GcddNpOnlineId) == 20, "OrbisNpOnlineId layout");

typedef struct GcddAddressParam {
    int32_t addressType;
    int32_t pad;
    union {
        struct {
            uint64_t ids; // OrbisNpAccountId[] (OpenA) or OrbisNpOnlineId[] (Open)
            uint32_t count;
            int32_t pad;
        } fixed;
        struct {
            uint32_t userMaxCount;
        } editable;
    } info;
} GcddAddressParam;
_Static_assert(sizeof(GcddAddressParam) == 24, "AddressParam layout");

// SendInfo and SendUrlInfo share this layout; `data` is the data pointer or the URL.
typedef struct GcddSendInfo {
    uint64_t userMessage;
    uint64_t dataName;
    uint64_t dataNameMultiLang;
    uint32_t dataNameMultiLangNum;
    int32_t pad0;
    uint64_t dataDetail;
    uint64_t dataDetailMultiLang;
    uint32_t dataDetailMultiLangNum;
    int32_t pad1;
    uint64_t thumbnail;
    uint32_t thumbnailSize;
    int32_t pad2;
    uint64_t data;     // SEND: data, SEND_URL: dataUrl
    uint32_t dataSize; // SEND_URL: withItemId in the low byte
    uint8_t padding[4];
    int32_t expireMinutes;
    int32_t pad3;
    uint32_t platformCount;
    int32_t pad4;
    uint64_t platformName; // const char *[]
    GcddAddressParam addressParam;
} GcddSendInfo;
_Static_assert(sizeof(GcddSendInfo) == 136, "SendInfo layout");

typedef union GcddDataParam {
    GcddSendInfo send;
    uint8_t recvReserved[160];
} GcddDataParam;
_Static_assert(sizeof(GcddDataParam) == 160, "DataParam layout");

typedef struct GcddParam {
    uint64_t baseSize;
    uint8_t baseReserved[36];
    uint32_t baseMagic;
    uint32_t size;
    int32_t mode;
    int32_t userId;
    int32_t pad;
    uint64_t callbackArg;
    uint64_t dataParam;
    uint8_t reserved[64];
} GcddParam;
_Static_assert(sizeof(GcddParam) == 0x90, "GameCustomDataDialogParam layout");

// OrbisGameCustomDataDialogResult(A): sentOnlineIds / sentUsers point at the lists below.
typedef struct GcddResult {
    uint64_t callbackArg;
    int32_t errorCode;
    int32_t result;
    uint64_t sent;
    uint8_t reserved[32];
} GcddResult;
_Static_assert(sizeof(GcddResult) == 0x38, "GameCustomDataDialogResult layout");

// ---- Dialog state shown by the window ----

typedef struct GcddView {
    uint32_t serial; // which Open this is; a window finishes only its own dialog
    int32_t mode;
    char data_name[GCDD_MAX_NAME_BYTES + 1];
    char data_detail[GCDD_MAX_DETAIL_BYTES + 1];
    char message[GCDD_MAX_MESSAGE_BYTES + 1];
    char data_url[GCDD_MAX_URL_LENGTH + 1];
    bool with_item_id;
    uint32_t data_size;
    int32_t expire_minutes;
    uint32_t platform_count;
    char platforms[GCDD_MAX_PLATFORMS][GCDD_MAX_PLATFORM_NAME_BYTES + 1];
    bool user_editable;
    uint32_t user_max_count;
    uint32_t recipient_count;
    char recipients[GCDD_ADDRESS_USER_LIST_MAX_SIZE][32]; // online IDs, or "account <id>"
    const uint8_t *thumbnail; // a copy of the game's thumbnail (PNG or JPEG)
    uint32_t thumbnail_size;
} GcddView;

// C API. `mem` is the guest memory base; guest addresses are offsets from it.
int32_t sceGameCustomDataDialogInitialize(void);
int32_t sceGameCustomDataDialogTerminate(void);
int32_t sceGameCustomDataDialogOpen(uint8_t *mem, const GcddParam *param);
int32_t sceGameCustomDataDialogOpenA(uint8_t *mem, const GcddParam *param);
int32_t sceGameCustomDataDialogClose(void);
int32_t sceGameCustomDataDialogUpdateStatus(void);
int32_t sceGameCustomDataDialogGetStatus(void);
int32_t sceGameCustomDataDialogGetResult(uint8_t *mem, GcddResult *result);
int32_t sceGameCustomDataDialogGetResultA(uint8_t *mem, GcddResult *result);

// Called by the window when the player closes it (OK or USER_CANCELED). Ignored unless the
// dialog `serial` (GcddView.serial) is the one running.
void ps4_gcdd_finish(uint32_t serial, int32_t result);

// Implemented by the window (ps4_game_custom_data_dialog_ui.m). show() returns false when no
// window can be shown (headless runs): the dialog then finishes as canceled. dismiss() closes
// the window, if any.
bool ps4_gcdd_ui_show(const GcddView *view);
void ps4_gcdd_ui_dismiss(void);

void shim_sceGameCustomDataDialogInitialize(GuestContext *ctx);
void shim_sceGameCustomDataDialogTerminate(GuestContext *ctx);
void shim_sceGameCustomDataDialogOpen(GuestContext *ctx);
void shim_sceGameCustomDataDialogOpenA(GuestContext *ctx);
void shim_sceGameCustomDataDialogClose(GuestContext *ctx);
void shim_sceGameCustomDataDialogUpdateStatus(GuestContext *ctx);
void shim_sceGameCustomDataDialogGetStatus(GuestContext *ctx);
void shim_sceGameCustomDataDialogGetResult(GuestContext *ctx);
void shim_sceGameCustomDataDialogGetResultA(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_GAME_CUSTOM_DATA_DIALOG_H
