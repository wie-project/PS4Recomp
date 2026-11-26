// SPDX-License-Identifier: GPL-2.0-or-later

// libSceSaveDataDialog against shadPS4's rules (savedatadialog.cpp): the common dialog system
// checks of Initialize / Terminate, the check order of Open and GetResult, Close on a dialog
// that is not running, and the progress bar calls. Runs with PS4_SAVEDATA_DIALOG=auto, so no
// window is shown; the save directories come from the stub below.
#include "ps4_common_dialog.h"
#include "ps4_savedata.h"
#include "ps4_savedata_dialog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime symbols the dialog links against: no save directory exists, English text.
bool ps4_savedata_read_dir_info(int32_t user_id, const char *dir_name, Ps4SaveDataParam *out_param,
                                uint64_t *out_bytes) {
    (void)user_id, (void)dir_name, (void)out_param, (void)out_bytes;
    return false;
}
int ps4_host_choose_language(void) { return 1; }

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define MEM_SIZE 0x100000ull
#define STACK_TOP 0x1000ull
#define PARAM 0x2000ull
#define BAR 0x3000ull
#define BAR_MSG 0x3100ull
#define USER_MSG 0x3200ull
#define RESULT 0x4000ull

static uint8_t *mem;
static GuestContext ctx;

static int32_t call(void (*shim)(GuestContext *), uint64_t a0, uint64_t a1) {
    ctx.rsp = STACK_TOP;
    ctx.rdi = a0, ctx.rsi = a1;
    shim(&ctx);
    CHECK(ctx.rsp == STACK_TOP + 8);
    return (int32_t)ctx.rax;
}

// Writes an OrbisSaveDataDialogParam in `mode` at PARAM.
static void dialog_param(OrbisSaveDataDialogMode mode) {
    OrbisSaveDataDialogParam *p = (OrbisSaveDataDialogParam *)(mem + PARAM);
    memset(p, 0, sizeof(*p));
    p->baseParam.size = sizeof(p->baseParam);
    p->size = sizeof(*p);
    p->mode = mode;
    p->dispType = ORBIS_SAVE_DATA_DIALOG_TYPE_SAVE;
    if (mode == ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR) {
        memset(mem + BAR, 0, 48);
        *(uint64_t *)(mem + BAR + 8) = BAR_MSG; // msg
        strcpy((char *)(mem + BAR_MSG), "Copying");
        p->progressBarParam = (void *)(uintptr_t)BAR;
    } else if (mode == ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG) {
        memset(mem + USER_MSG, 0, 48);
        *(uint32_t *)(mem + USER_MSG) = 0; // OK button
        p->userMsgParam = (void *)(uintptr_t)USER_MSG;
    }
}

static int test_lifecycle(void) {
    CHECK(sceSaveDataDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED);
    CHECK(sceCommonDialogInitialize() == 0);
    // Another common dialog holds the slot.
    CHECK(ps4_common_dialog_acquire());
    CHECK(sceSaveDataDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_BUSY);
    ps4_common_dialog_release();
    CHECK(sceSaveDataDialogTerminate() == ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED);
    CHECK(call(shim_sceSaveDataDialogInitialize, 0, 0) == 0);
    CHECK(sceCommonDialogIsUsed());
    CHECK(sceSaveDataDialogInitialize() == ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED);
    CHECK(sceSaveDataDialogClose(NULL) == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceSaveDataDialogProgressBarInc(0, 1) == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(sceSaveDataDialogProgressBarSetValue(0, 1) == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    CHECK(call(shim_sceSaveDataDialogTerminate, 0, 0) == 0);
    CHECK(!sceCommonDialogIsUsed());
    CHECK(sceSaveDataDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_NONE);
    printf("PASS lifecycle\n");
    return 0;
}

static int test_open_result_order(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    dialog_param(ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR);
    // Open checks the state before the parameter.
    CHECK(call(shim_sceSaveDataDialogOpen, 0, 0) == ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE);
    CHECK(sceSaveDataDialogInitialize() == 0);
    CHECK(call(shim_sceSaveDataDialogOpen, 0, 0) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    // GetResult checks the state before the pointer.
    CHECK(call(shim_sceSaveDataDialogGetResult, 0, 0) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(call(shim_sceSaveDataDialogOpen, PARAM, 0) == 0);
    CHECK(call(shim_sceSaveDataDialogGetResult, 0, 0) == ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED);
    CHECK(sceSaveDataDialogClose(NULL) == 0);
    CHECK(call(shim_sceSaveDataDialogGetResult, 0, 0) == ORBIS_COMMON_DIALOG_ERROR_ARG_NULL);
    CHECK(sceSaveDataDialogTerminate() == 0);
    printf("PASS open-result-order\n");
    return 0;
}

static int test_progress_bar(void) {
    CHECK(sceCommonDialogInitialize() == 0);
    CHECK(sceSaveDataDialogInitialize() == 0);
    dialog_param(ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR);
    CHECK(call(shim_sceSaveDataDialogOpen, PARAM, 0) == 0);
    CHECK(sceSaveDataDialogUpdateStatus() == ORBIS_COMMON_DIALOG_STATUS_RUNNING);
    CHECK(call(shim_sceSaveDataDialogProgressBarSetValue, 1, 10) == ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID);
    CHECK(call(shim_sceSaveDataDialogProgressBarSetValue, 0, 40) == 0);
    CHECK(call(shim_sceSaveDataDialogProgressBarInc, 0, 15) == 0);
    CHECK(ps4_savedata_dialog_progress() == 55);
    CHECK(call(shim_sceSaveDataDialogProgressBarInc, 0, 0) == 0);
    CHECK(ps4_savedata_dialog_progress() == 55);
    // The bar stays up until the game closes it.
    CHECK(sceSaveDataDialogUpdateStatus() == ORBIS_COMMON_DIALOG_STATUS_RUNNING);
    CHECK(call(shim_sceSaveDataDialogClose, 0, 0) == 0);
    CHECK(sceSaveDataDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_FINISHED);
    CHECK(call(shim_sceSaveDataDialogClose, 0, 0) == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);
    OrbisSaveDataDialogResult *r = (OrbisSaveDataDialogResult *)(mem + RESULT);
    memset(r, 0xFF, sizeof(*r));
    r->dirName = NULL;
    r->param = NULL;
    CHECK(call(shim_sceSaveDataDialogGetResult, RESULT, 0) == 0);
    CHECK(r->mode == ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR && r->result == ORBIS_COMMON_DIALOG_RESULT_OK &&
          r->buttonId == 0);
    CHECK(call(shim_sceSaveDataDialogProgressBarInc, 0, 1) == ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING);

    // Only the progress bar mode has a bar.
    dialog_param(ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG);
    CHECK(call(shim_sceSaveDataDialogOpen, PARAM, 0) == 0);
    CHECK(call(shim_sceSaveDataDialogProgressBarSetValue, 0, 10) == ORBIS_COMMON_DIALOG_ERROR_NOT_SUPPORTED);
    // Terminate closes the running dialog and frees the common dialog slot.
    CHECK(sceSaveDataDialogTerminate() == 0);
    CHECK(!sceCommonDialogIsUsed());
    CHECK(sceSaveDataDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_NONE);
    printf("PASS progress-bar\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    setenv("PS4_SAVEDATA_DIALOG", "auto", 1);
    mem = calloc(1, MEM_SIZE);
    if (!mem) return 1;
    ctx.mem_base = mem;
    ctx.mem_size = MEM_SIZE;
    if (strcmp(argv[1], "lifecycle") == 0) return test_lifecycle();
    if (strcmp(argv[1], "open-result-order") == 0) return test_open_result_order();
    if (strcmp(argv[1], "progress-bar") == 0) return test_progress_bar();
    return 2;
}
