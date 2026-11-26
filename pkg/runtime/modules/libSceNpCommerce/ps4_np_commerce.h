// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_NP_COMMERCE_H
#define PS4_NP_COMMERCE_H

#include <stdbool.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// CommonDialog::Status values the dialog reports.
#define NP_COMMERCE_STATUS_NONE 0
#define NP_COMMERCE_STATUS_INITIALIZED 1
#define NP_COMMERCE_STATUS_RUNNING 2
#define NP_COMMERCE_STATUS_FINISHED 3

// OrbisNpCommerceDialogResult::result.
#define NP_COMMERCE_RESULT_OK 0
#define NP_COMMERCE_RESULT_USER_CANCELED 1
#define NP_COMMERCE_RESULT_PURCHASED 2

// OrbisNpCommerceDialogMode.
#define NP_COMMERCE_MODE_CATEGORY 0
#define NP_COMMERCE_MODE_PRODUCT 1
#define NP_COMMERCE_MODE_PRODUCT_CODE 2
#define NP_COMMERCE_MODE_CHECKOUT 3
#define NP_COMMERCE_MODE_DOWNLOADLIST 4
#define NP_COMMERCE_MODE_PLUS 5

// OrbisNpCommercePsStoreIconPos and OrbisNpCommercePsStoreIconLayout.
#define NP_COMMERCE_ICON_POS_CENTER 0
#define NP_COMMERCE_ICON_POS_LEFT 1
#define NP_COMMERCE_ICON_POS_RIGHT 2
#define NP_COMMERCE_ICON_LAYOUT_DEFAULT 0
#define NP_COMMERCE_ICON_LAYOUT_FOLLOW_SAFE_AREA 1
#define NP_COMMERCE_ICON_LAYOUT_FIXED_90 2

#define NP_COMMERCE_NUM_TARGETS_MAX 10
#define NP_COMMERCE_MAX_LABEL_LEN 128
#define NP_COMMERCE_USER_NAME_SIZE 17 // ORBIS_USER_SERVICE_MAX_USER_NAME_LENGTH + 1

// sceNpCommerceDialogOpen parameter (shadPS4 np_commerce.h). Guest pointers are guest addresses.
typedef struct NpCommerceDialogParam {
    uint64_t baseSize; // CommonDialog::BaseParam
    uint8_t baseReserved[36];
    uint32_t baseMagic;
    int32_t size;
    int32_t userId;
    int32_t mode;
    uint32_t serviceLabel;
    uint64_t targets; // const char *const *
    uint32_t numTargets;
    int32_t pad;
    uint64_t features; // ORBIS_NP_PLUS_FEATURE_* (PLUS mode only)
    uint64_t userData;
    uint8_t reserved[32];
} NpCommerceDialogParam;
_Static_assert(sizeof(NpCommerceDialogParam) == 0x80, "NpCommerceDialogParam layout");

typedef struct NpCommerceDialogResult {
    int32_t result;
    uint8_t authorized;
    uint8_t pad8;
    int16_t pad16;
    uint64_t userData;
    uint8_t reserved[32];
} NpCommerceDialogResult;
_Static_assert(sizeof(NpCommerceDialogResult) == 0x30, "NpCommerceDialogResult layout");

// What the window shows for an opened dialog (shadPS4 CommerceDialogState).
typedef struct NpCommerceView {
    uint32_t serial; // identifies the opened dialog, passed back to ps4_np_commerce_finish
    int32_t mode;
    int32_t userId;
    uint32_t serviceLabel;
    uint64_t features;
    char userName[NP_COMMERCE_USER_NAME_SIZE]; // empty when the name is unknown
    uint32_t targetCount;
    char targets[NP_COMMERCE_NUM_TARGETS_MAX][NP_COMMERCE_MAX_LABEL_LEN + 1];
} NpCommerceView;

int32_t sceNpCommerceDialogInitialize(void);
int32_t sceNpCommerceDialogInitializeInternal(void);
int32_t sceNpCommerceDialogTerminate(void);
int32_t sceNpCommerceDialogClose(void);
int8_t sceNpCommerceDialogGetStatus(void);
int32_t sceNpCommerceDialogUpdateStatus(void);
int32_t sceNpCommerceDialogGetResult(NpCommerceDialogResult *result);
// `mem` is the guest memory base the parameter's pointers are relative to.
int32_t sceNpCommerceDialogOpen(uint8_t *mem, const NpCommerceDialogParam *param);
int32_t sceNpCommerceShowPsStoreIcon(int32_t pos);
int32_t sceNpCommerceHidePsStoreIcon(void);
int32_t sceNpCommerceSetPsStoreIconLayout(int32_t layout);

// The window (ps4_np_commerce_ui.m). show returns false when no window can be shown (headless
// run, no AppKit application); the dialog then finishes at once as if Close had been pressed.
bool ps4_np_commerce_ui_show(const NpCommerceView *view);
void ps4_np_commerce_ui_dismiss(void);
// Called by the window with the button the player pressed: PURCHASED for the mode's action,
// USER_CANCELED for Close. A window of an earlier dialog (other serial) is ignored.
void ps4_np_commerce_finish(uint32_t serial, int32_t result);
// The store icon overlay over the game window.
void ps4_np_commerce_ui_icon(bool shown, int32_t pos, int32_t layout);

void shim_sceNpCommerceDialogInitialize(GuestContext *ctx);
void shim_sceNpCommerceDialogInitializeInternal(GuestContext *ctx);
void shim_sceNpCommerceDialogTerminate(GuestContext *ctx);
void shim_sceNpCommerceDialogClose(GuestContext *ctx);
void shim_sceNpCommerceDialogGetStatus(GuestContext *ctx);
void shim_sceNpCommerceDialogUpdateStatus(GuestContext *ctx);
void shim_sceNpCommerceDialogGetResult(GuestContext *ctx);
void shim_sceNpCommerceDialogOpen(GuestContext *ctx);
void shim_sceNpCommerceShowPsStoreIcon(GuestContext *ctx);
void shim_sceNpCommerceHidePsStoreIcon(GuestContext *ctx);
void shim_sceNpCommerceSetPsStoreIconLayout(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_NP_COMMERCE_H
