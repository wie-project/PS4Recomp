#import <Cocoa/Cocoa.h>
#include "ps4_ime_dialog.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t g_ime_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisImeDialogStatus g_ime_status = ORBIS_IME_DIALOG_STATUS_NONE;
static OrbisImeDialogResult g_ime_result = {0};
static OrbisImeDialogParam g_ime_param = {0};

static size_t utf16_strlen(const uint16_t *s, size_t max_len) {
    if (!s) return 0;
    size_t len = 0;
    while (len < max_len && s[len] != 0) {
        len++;
    }
    return len;
}

int32_t sceImeDialogInit(OrbisImeDialogParam *param, OrbisImeParamExtended *extended) {
    (void)extended;
    if (!param || !param->input_text_buffer) {
        return (int32_t)0x80bc0031; // INVALID_ADDRESS
    }

    pthread_mutex_lock(&g_ime_mutex);
    if (g_ime_status != ORBIS_IME_DIALOG_STATUS_NONE) {
        pthread_mutex_unlock(&g_ime_mutex);
        return (int32_t)0x80bc0001; // BUSY
    }

    g_ime_param = *param;
    memset(&g_ime_result, 0, sizeof(g_ime_result));
    g_ime_status = ORBIS_IME_DIALOG_STATUS_RUNNING;
    pthread_mutex_unlock(&g_ime_mutex);

    return 0;
}

OrbisImeDialogStatus sceImeDialogGetStatus(void) {
    pthread_mutex_lock(&g_ime_mutex);
    if (g_ime_status != ORBIS_IME_DIALOG_STATUS_RUNNING) {
        OrbisImeDialogStatus st = g_ime_status;
        pthread_mutex_unlock(&g_ime_mutex);
        return st;
    }

    // Headless or non-interactive environment fallback
    if (getenv("PS4_RECOMP_HEADLESS") != NULL || (!isatty(fileno(stdin)) && !isatty(fileno(stdout)))) {
        size_t cur_len = utf16_strlen(g_ime_param.input_text_buffer, g_ime_param.max_text_length);
        if (cur_len == 0 && g_ime_param.max_text_length >= 6) {
            // Provide sensible default character name for offline run
            const char *def_name = "Player";
            for (size_t i = 0; i < 6; i++) {
                g_ime_param.input_text_buffer[i] = (uint16_t)def_name[i];
            }
            g_ime_param.input_text_buffer[6] = 0;
        }
        g_ime_result.endstatus = ORBIS_IME_DIALOG_END_STATUS_OK;
        g_ime_status = ORBIS_IME_DIALOG_STATUS_FINISHED;
        OrbisImeDialogStatus st = g_ime_status;
        pthread_mutex_unlock(&g_ime_mutex);
        return st;
    }

    // Prepare Cocoa strings from guest UTF-16
    size_t init_len = utf16_strlen(g_ime_param.input_text_buffer, g_ime_param.max_text_length);
    NSString *initialText = [[NSString alloc] initWithCharacters:(const unichar *)g_ime_param.input_text_buffer length:init_len];
    
    NSString *titleStr = @"PlayStation®4 Character Name";
    if (g_ime_param.title) {
        size_t t_len = utf16_strlen(g_ime_param.title, 512);
        if (t_len > 0) {
            titleStr = [[NSString alloc] initWithCharacters:(const unichar *)g_ime_param.title length:t_len];
        }
    }

    NSString *placeholderStr = @"";
    if (g_ime_param.placeholder) {
        size_t p_len = utf16_strlen(g_ime_param.placeholder, 512);
        if (p_len > 0) {
            placeholderStr = [[NSString alloc] initWithCharacters:(const unichar *)g_ime_param.placeholder length:p_len];
        }
    }

    bool isPassword = (g_ime_param.option & ORBIS_IME_OPTION_PASSWORD) != 0;
    uint32_t max_len = g_ime_param.max_text_length;
    uint16_t *out_buf = g_ime_param.input_text_buffer;
    pthread_mutex_unlock(&g_ime_mutex);

    __block NSModalResponse resp = NSAlertFirstButtonReturn;
    __block NSString *enteredText = nil;

    void (^showImeAlert)(void) = ^{
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp activateIgnoringOtherApps:YES];

            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:titleStr ? titleStr : @"PlayStation®4 Text Input"];
            [alert setInformativeText:placeholderStr ? placeholderStr : @""];
            [alert setAlertStyle:NSAlertStyleInformational];
            [alert addButtonWithTitle:@"OK"];
            [alert addButtonWithTitle:@"Cancel"];

            NSTextField *inputField;
            if (isPassword) {
                inputField = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)];
            } else {
                inputField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)];
            }
            if (initialText) {
                [inputField setStringValue:initialText];
            }
            if (placeholderStr && placeholderStr.length > 0) {
                [[inputField cell] setPlaceholderString:placeholderStr];
            }
            [alert setAccessoryView:inputField];
            [[alert window] setInitialFirstResponder:inputField];

            resp = [alert runModal];
            if (resp == NSAlertFirstButtonReturn) {
                enteredText = [inputField stringValue];
            }
        }
    };

    if ([NSThread isMainThread]) {
        showImeAlert();
    } else {
        dispatch_sync(dispatch_get_main_queue(), showImeAlert);
    }

    pthread_mutex_lock(&g_ime_mutex);
    if (resp == NSAlertFirstButtonReturn) {
        g_ime_result.endstatus = ORBIS_IME_DIALOG_END_STATUS_OK;
        if (enteredText && out_buf && max_len > 0) {
            NSUInteger len = [enteredText length];
            if (len > max_len) {
                len = max_len;
            }
            [enteredText getCharacters:(unichar *)out_buf range:NSMakeRange(0, len)];
            out_buf[len] = 0;
        }
    } else {
        g_ime_result.endstatus = ORBIS_IME_DIALOG_END_STATUS_USER_CANCELED;
    }
    g_ime_status = ORBIS_IME_DIALOG_STATUS_FINISHED;
    OrbisImeDialogStatus final_status = g_ime_status;
    pthread_mutex_unlock(&g_ime_mutex);

    return final_status;
}

int32_t sceImeDialogGetResult(OrbisImeDialogResult *result) {
    if (!result) {
        return (int32_t)0x80bc0031; // INVALID_ADDRESS
    }

    pthread_mutex_lock(&g_ime_mutex);
    if (g_ime_status != ORBIS_IME_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_ime_mutex);
        return (int32_t)0x80bc0106; // DIALOG_NOT_FINISHED
    }

    *result = g_ime_result;
    pthread_mutex_unlock(&g_ime_mutex);
    return 0;
}

int32_t sceImeDialogAbort(void) {
    pthread_mutex_lock(&g_ime_mutex);
    if (g_ime_status == ORBIS_IME_DIALOG_STATUS_RUNNING) {
        g_ime_result.endstatus = ORBIS_IME_DIALOG_END_STATUS_ABORTED;
        g_ime_status = ORBIS_IME_DIALOG_STATUS_FINISHED;
    }
    pthread_mutex_unlock(&g_ime_mutex);
    return 0;
}

int32_t sceImeDialogTerm(void) {
    pthread_mutex_lock(&g_ime_mutex);
    g_ime_status = ORBIS_IME_DIALOG_STATUS_NONE;
    memset(&g_ime_result, 0, sizeof(g_ime_result));
    memset(&g_ime_param, 0, sizeof(g_ime_param));
    pthread_mutex_unlock(&g_ime_mutex);
    return 0;
}

int32_t sceImeDialogGetPanelSizeExtended(const OrbisImeDialogParam *param,
                                        const OrbisImeParamExtended *extended,
                                        uint32_t *width, uint32_t *height) {
    (void)extended;
    if (!param || !width || !height) {
        return (int32_t)0x80bc0031; // INVALID_ADDRESS
    }

    if (param->type == ORBIS_IME_TYPE_NUMBER) {
        *width = 370;
        *height = 522;
    } else {
        *width = 793;
        *height = (param->option & ORBIS_IME_OPTION_MULTILINE) ? 628 : 528;
    }

    if (param->option & ORBIS_IME_OPTION_USE_OVER_2K_COORDINATES) {
        *width <<= 1;
        *height <<= 1;
    }

    return 0;
}

// Shims
void shim_sceImeDialogInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceImeDialogInit((OrbisImeDialogParam *)ctx->rdi, (OrbisImeParamExtended *)ctx->rsi);
}

void shim_sceImeDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceImeDialogGetStatus();
}

void shim_sceImeDialogGetResult(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceImeDialogGetResult((OrbisImeDialogResult *)ctx->rdi);
}

void shim_sceImeDialogAbort(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceImeDialogAbort();
}

void shim_sceImeDialogTerm(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceImeDialogTerm();
}

void shim_sceImeDialogGetPanelSizeExtended(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceImeDialogGetPanelSizeExtended(
        (const OrbisImeDialogParam *)ctx->rdi,
        (const OrbisImeParamExtended *)ctx->rsi,
        (uint32_t *)ctx->rdx,
        (uint32_t *)ctx->rcx
    );
}
