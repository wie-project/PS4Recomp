// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_ERROR_DIALOG_H
#define PS4_ERROR_DIALOG_H

#include <stdbool.h>
#include <stdint.h>
#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// CommonDialog::Status values the dialog reports.
#define ERROR_DIALOG_STATUS_NONE 0
#define ERROR_DIALOG_STATUS_INITIALIZED 1
#define ERROR_DIALOG_STATUS_RUNNING 2
#define ERROR_DIALOG_STATUS_FINISHED 3

// sceErrorDialogOpen parameter (shadPS4 ime/error_dialog.cpp Param).
typedef struct OrbisErrorDialogParam {
    int32_t size;
    int32_t errorCode;
    int32_t userId;
    int32_t reserved;
} OrbisErrorDialogParam;
_Static_assert(sizeof(OrbisErrorDialogParam) == 16, "OrbisErrorDialogParam layout");

// Longest text the dialog shows ("An error has occurred. \nCode: 0X" + 8 digits).
#define ERROR_DIALOG_MESSAGE_SIZE 64

int32_t sceErrorDialogInitialize(void);
int32_t sceErrorDialogOpen(const OrbisErrorDialogParam *param);
int32_t sceErrorDialogGetStatus(void);
int32_t sceErrorDialogUpdateStatus(void);
int32_t sceErrorDialogClose(void);
int32_t sceErrorDialogTerminate(void);

// The window (ps4_error_dialog_ui.m). show returns false when no window can be shown (headless
// run, no AppKit application); the dialog then finishes at once, as if OK had been pressed.
// serial identifies the opened dialog: a window that belongs to an earlier one is ignored.
bool ps4_error_dialog_ui_show(uint32_t serial, const char *message);
void ps4_error_dialog_ui_dismiss(void);
// Called by the window when the player presses OK or closes it.
void ps4_error_dialog_finish(uint32_t serial);

void shim_sceErrorDialogInitialize(GuestContext *ctx);
void shim_sceErrorDialogOpen(GuestContext *ctx);
void shim_sceErrorDialogUpdateStatus(GuestContext *ctx);
void shim_sceErrorDialogGetStatus(GuestContext *ctx);
void shim_sceErrorDialogClose(GuestContext *ctx);
void shim_sceErrorDialogTerminate(GuestContext *ctx);
void shim_sceErrorDialogOpenDetail(GuestContext *ctx);
void shim_sceErrorDialogOpenWithReport(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_ERROR_DIALOG_H
