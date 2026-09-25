#import <Cocoa/Cocoa.h>
#include "ps4_error_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t g_error_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_error_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisErrorDialogParam g_error_param = {0};

int32_t sceErrorDialogInitialize(void) {
    pthread_mutex_lock(&g_error_mutex);
    if (g_error_status != ORBIS_COMMON_DIALOG_STATUS_NONE) {
        pthread_mutex_unlock(&g_error_mutex);
        return (int32_t)0x80b80004; // ALREADY_INITIALIZED
    }
    g_error_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_error_param, 0, sizeof(g_error_param));
    pthread_mutex_unlock(&g_error_mutex);
    return 0;
}

int32_t sceErrorDialogOpen(const OrbisErrorDialogParam *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }

    pthread_mutex_lock(&g_error_mutex);
    if (g_error_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_error_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_error_mutex);
        return (int32_t)0x80b80006; // INVALID_STATE
    }

    g_error_param = *param;
    g_error_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_error_mutex);

    return 0;
}

OrbisCommonDialogStatus sceErrorDialogGetStatus(void) {
    pthread_mutex_lock(&g_error_mutex);
    OrbisCommonDialogStatus st = g_error_status;
    pthread_mutex_unlock(&g_error_mutex);
    return st;
}

OrbisCommonDialogStatus sceErrorDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_error_mutex);
    if (g_error_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        OrbisCommonDialogStatus st = g_error_status;
        pthread_mutex_unlock(&g_error_mutex);
        return st;
    }

    int32_t err = g_error_param.errorCode;

    // Headless or non-interactive fallback
    if (getenv("PS4_RECOMP_HEADLESS") != NULL || (!isatty(fileno(stdin)) && !isatty(fileno(stdout)))) {
        printf("[ps4-error-dialog] [HEADLESS] PlayStation®4 Error Code: 0x%08X\n", (uint32_t)err);
        g_error_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
        OrbisCommonDialogStatus st = g_error_status;
        pthread_mutex_unlock(&g_error_mutex);
        return st;
    }

    pthread_mutex_unlock(&g_error_mutex);

    void (^showErrorAlert)(void) = ^{
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];

            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"PlayStation®4 Error"];
            [alert setInformativeText:[NSString stringWithFormat:@"An error occurred in the application.\nError Code: 0x%08X", (uint32_t)err]];
            [alert setAlertStyle:NSAlertStyleWarning];
            [alert addButtonWithTitle:@"OK"];

            [alert runModal];
        }
    };

    if ([NSThread isMainThread]) {
        showErrorAlert();
    } else {
        dispatch_sync(dispatch_get_main_queue(), showErrorAlert);
    }

    pthread_mutex_lock(&g_error_mutex);
    g_error_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    OrbisCommonDialogStatus final_st = g_error_status;
    pthread_mutex_unlock(&g_error_mutex);

    return final_st;
}

int32_t sceErrorDialogTerminate(void) {
    pthread_mutex_lock(&g_error_mutex);
    g_error_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    memset(&g_error_param, 0, sizeof(g_error_param));
    pthread_mutex_unlock(&g_error_mutex);
    return 0;
}

// Shims
void shim_sceErrorDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceErrorDialogInitialize();
}

void shim_sceErrorDialogOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceErrorDialogOpen((const OrbisErrorDialogParam *)ctx->rdi);
}

void shim_sceErrorDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceErrorDialogUpdateStatus();
}

void shim_sceErrorDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceErrorDialogGetStatus();
}

void shim_sceErrorDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceErrorDialogTerminate();
}
