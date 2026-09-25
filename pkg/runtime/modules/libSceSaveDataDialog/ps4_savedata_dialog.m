#import <Cocoa/Cocoa.h>
#include "ps4_savedata_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t g_savedata_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisSaveDataDialogParam g_savedata_param = {0};
static OrbisSaveDataDialogResult g_savedata_result = {0};

int32_t sceSaveDataDialogInitialize(void) {
    pthread_mutex_lock(&g_savedata_mutex);
    g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_savedata_result, 0, sizeof(g_savedata_result));
    memset(&g_savedata_param, 0, sizeof(g_savedata_param));
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataDialogOpen(const OrbisSaveDataDialogParam *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_savedata_mutex);
    if (g_savedata_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_savedata_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return (int32_t)0x80b80006; // INVALID_STATE
    }

    g_savedata_param = *param;
    memset(&g_savedata_result, 0, sizeof(g_savedata_result));
    g_savedata_result.mode = param->mode;
    g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_savedata_mutex);

    return 0;
}

OrbisCommonDialogStatus sceSaveDataDialogGetStatus(void) {
    pthread_mutex_lock(&g_savedata_mutex);
    OrbisCommonDialogStatus st = g_savedata_status;
    pthread_mutex_unlock(&g_savedata_mutex);
    return st;
}

OrbisCommonDialogStatus sceSaveDataDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_savedata_mutex);
    if (g_savedata_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        OrbisCommonDialogStatus st = g_savedata_status;
        pthread_mutex_unlock(&g_savedata_mutex);
        return st;
    }

    // Headless or non-interactive fallback
    if (getenv("PS4_RECOMP_HEADLESS") != NULL || (!isatty(fileno(stdin)) && !isatty(fileno(stdout)))) {
        g_savedata_result.mode = g_savedata_param.mode;
        g_savedata_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
        g_savedata_result.buttonId = 1; // OK / YES
        g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
        OrbisCommonDialogStatus st = g_savedata_status;
        pthread_mutex_unlock(&g_savedata_mutex);
        return st;
    }

    // Progress bar mode does not need modal blocking
    if (g_savedata_param.mode == ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR) {
        g_savedata_result.mode = g_savedata_param.mode;
        g_savedata_result.result = ORBIS_COMMON_DIALOG_RESULT_OK;
        g_savedata_result.buttonId = 1;
        g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
        OrbisCommonDialogStatus st = g_savedata_status;
        pthread_mutex_unlock(&g_savedata_mutex);
        return st;
    }

    OrbisSaveDataDialogMode mode = g_savedata_param.mode;
    pthread_mutex_unlock(&g_savedata_mutex);

    __block NSModalResponse resp = NSAlertFirstButtonReturn;
    void (^showSaveAlert)(void) = ^{
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];

            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"PlayStation®4 Save Data"];
            if (mode == ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG || mode == ORBIS_SAVE_DATA_DIALOG_MODE_SYSTEM_MSG) {
                [alert setInformativeText:@"Save data operation processed successfully."];
            } else {
                [alert setInformativeText:@"Saved data loaded successfully."];
            }
            [alert setAlertStyle:NSAlertStyleInformational];
            [alert addButtonWithTitle:@"OK"];

            resp = [alert runModal];
        }
    };

    if ([NSThread isMainThread]) {
        showSaveAlert();
    } else {
        dispatch_sync(dispatch_get_main_queue(), showSaveAlert);
    }

    pthread_mutex_lock(&g_savedata_mutex);
    g_savedata_result.mode = mode;
    g_savedata_result.result = (resp == NSAlertFirstButtonReturn) ? ORBIS_COMMON_DIALOG_RESULT_OK : ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED;
    g_savedata_result.buttonId = (resp == NSAlertFirstButtonReturn) ? 1 : 2;
    g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    OrbisCommonDialogStatus final_st = g_savedata_status;
    pthread_mutex_unlock(&g_savedata_mutex);

    return final_st;
}

int32_t sceSaveDataDialogGetResult(OrbisSaveDataDialogResult *result) {
    if (!result) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_savedata_mutex);
    if (g_savedata_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_savedata_mutex);
        return (int32_t)0x80b80005; // NOT_FINISHED
    }

    *result = g_savedata_result;
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

int32_t sceSaveDataDialogTerminate(void) {
    pthread_mutex_lock(&g_savedata_mutex);
    g_savedata_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    memset(&g_savedata_result, 0, sizeof(g_savedata_result));
    memset(&g_savedata_param, 0, sizeof(g_savedata_param));
    pthread_mutex_unlock(&g_savedata_mutex);
    return 0;
}

// Shims
void shim_sceSaveDataDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogInitialize();
}

void shim_sceSaveDataDialogOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogOpen((const OrbisSaveDataDialogParam *)ctx->rdi);
}

void shim_sceSaveDataDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogUpdateStatus();
}

void shim_sceSaveDataDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogGetStatus();
}

void shim_sceSaveDataDialogGetResult(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogGetResult((OrbisSaveDataDialogResult *)ctx->rdi);
}

void shim_sceSaveDataDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogTerminate();
}
