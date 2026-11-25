// SPDX-License-Identifier: GPL-2.0-or-later

// libSceGameCustomDataDialog, ported from shadPS4
// (src/core/libraries/game_custom_data_dialog/game_custom_data_dialog.cpp): the same parameter
// validation, state machine, request log and result. The window is native AppKit
// (ps4_game_custom_data_dialog_ui.m) instead of shadPS4's ImGui layer.
//
// shadPS4 sends and lists game custom data through its own online service (shadNet). There is
// no such service here, so the dialog behaves like shadPS4 without a shadNet sign-in: it shows
// the request, sending reports that no online service is available, and the player can only
// cancel. A game that closes the dialog itself (sceGameCustomDataDialogClose) gets OK with no
// recipients, as in shadPS4.

#include "ps4_game_custom_data_dialog.h"
#include "ps4_common_dialog.h"
#include "ps4_user_service.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GCDD_PARAM_SIZE ((uint32_t)sizeof(GcddParam))
// Sizes of OrbisGameCustomDataDialogUserList (OpenA) and OnlineIdList (Open).
#define GCDD_USER_LIST_SIZE (8 + GCDD_ADDRESS_USER_LIST_MAX_SIZE * 32)
#define GCDD_ONLINE_ID_LIST_SIZE (4 + GCDD_ADDRESS_USER_LIST_MAX_SIZE * (uint32_t)sizeof(GcddNpOnlineId))

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_status = GCDD_STATUS_NONE;
static int32_t g_result = GCDD_RESULT_USER_CANCELED;
static uint64_t g_callback_arg;
static uint32_t g_serial;
static GcddView g_view;
static uint8_t *g_thumbnail; // owned copy, see g_view.thumbnail

static void *gaddr(uint8_t *mem, uint64_t addr) {
    return addr ? (void *)((uintptr_t)mem + addr) : NULL;
}

static const char *gstr(uint8_t *mem, uint64_t addr) {
    return (const char *)gaddr(mem, addr);
}

static bool all_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i]) return false;
    }
    return true;
}

static size_t utf8_length(const char *s, size_t n) {
    size_t chars = 0;
    for (size_t i = 0; i < n; i++) chars += ((unsigned char)s[i] & 0xC0) != 0x80;
    return chars;
}

static int32_t reject(int32_t err, const char *why) {
    fprintf(stderr, "[libSceGameCustomDataDialog] Open rejected: %s (0x%08x)\n", why, (uint32_t)err);
    return err;
}

// Message, name, detail and thumbnail, common to SEND and SEND_URL.
static int32_t validate_common_send(uint8_t *mem, const GcddSendInfo *s) {
    const char *msg = gstr(mem, s->userMessage);
    if (!msg) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "userMessage is NULL");
    const size_t msg_bytes = strnlen(msg, GCDD_MAX_MESSAGE_BYTES + 1);
    if (msg_bytes > GCDD_MAX_MESSAGE_BYTES) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "userMessage longer than 2048 bytes");
    }
    if (utf8_length(msg, msg_bytes) > GCDD_MAX_USER_MSG_SIZE) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "userMessage has more than 512 characters");
    }
    const char *name = gstr(mem, s->dataName);
    if (!name) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "dataName is NULL");
    const size_t name_bytes = strnlen(name, GCDD_MAX_NAME_BYTES + 1);
    if (name_bytes == 0 || name_bytes > GCDD_MAX_NAME_BYTES) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "dataName must be 1..256 bytes");
    }
    const char *detail = gstr(mem, s->dataDetail);
    if (!detail) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "dataDetail is NULL");
    const size_t detail_bytes = strnlen(detail, GCDD_MAX_DETAIL_BYTES + 1);
    if (detail_bytes == 0 || detail_bytes > GCDD_MAX_DETAIL_BYTES) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "dataDetail must be 1..2048 bytes");
    }
    if (!s->thumbnail) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "thumbnail is NULL");
    if (s->thumbnailSize == 0 || s->thumbnailSize > GCDD_MAX_THUMBNAIL_SIZE) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "thumbnailSize must be 1..160 KiB");
    }
    return 0;
}

// `account_ids`: the recipients are OrbisNpAccountId (OpenA) rather than OrbisNpOnlineId (Open).
static int32_t validate_platforms_and_recipients(uint8_t *mem, const GcddSendInfo *s, bool account_ids) {
    const GcddAddressParam *a = &s->addressParam;
    if (a->addressType != GCDD_ADDRESS_TYPE_USERDISABLE && a->addressType != GCDD_ADDRESS_TYPE_USERENABLE) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "unknown addressType");
    }
    if (s->platformCount == 0) return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "availablePlatform.count is 0");
    const uint64_t *names = gaddr(mem, s->platformName);
    if (!names) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "availablePlatform.platformName is NULL");
    for (uint32_t i = 0; i < s->platformCount; i++) {
        const char *name = gstr(mem, names[i]);
        if (!name) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "a platform name is NULL");
        if (strnlen(name, GCDD_MAX_PLATFORM_NAME_BYTES) == 0) {
            return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "a platform name is empty");
        }
    }
    if (a->addressType == GCDD_ADDRESS_TYPE_USERENABLE) {
        const uint32_t max = a->info.editable.userMaxCount;
        if (max == 0 || max > GCDD_ADDRESS_USER_LIST_MAX_SIZE) {
            return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "userMaxCount must be 1..16");
        }
    } else {
        const uint32_t count = a->info.fixed.count;
        if (count == 0 || count > GCDD_ADDRESS_USER_LIST_MAX_SIZE) {
            return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "recipient count must be 1..16");
        }
        if (!a->info.fixed.ids) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "recipient list is NULL");
        for (uint32_t i = 0; i < count; i++) {
            const bool empty = account_ids ? ((const uint64_t *)gaddr(mem, a->info.fixed.ids))[i] == 0
                                           : ((const GcddNpOnlineId *)gaddr(mem, a->info.fixed.ids))[i].data[0] == 0;
            if (empty) return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "a recipient is empty");
        }
    }
    if (s->dataNameMultiLangNum > GCDD_MAX_MULTI_LANG_STRINGS ||
        s->dataDetailMultiLangNum > GCDD_MAX_MULTI_LANG_STRINGS) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "more than 30 translations");
    }
    return 0;
}

static int32_t validate_param(uint8_t *mem, const GcddParam *p, bool account_ids) {
    if (!p) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "param is NULL");
    if (p->size != GCDD_PARAM_SIZE) return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "param->size is not 0x90");
    if (!all_zero(p->reserved, sizeof(p->reserved))) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "param->reserved is not zeroed");
    }
    const GcddDataParam *data = gaddr(mem, p->dataParam);
    if (!data) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "param->dataParam is NULL");
    // One user is signed in: the initial user.
    if (p->userId != ORBIS_USER_SERVICE_INITIAL_USER_ID) {
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "the user is not signed in");
    }
    int32_t rc;
    switch (p->mode) {
    case GCDD_MODE_RECV:
        return all_zero(data->recvReserved, sizeof(data->recvReserved))
                   ? 0
                   : reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "RecvInfo.reserved is not zeroed");
    case GCDD_MODE_SEND:
        if ((rc = validate_common_send(mem, &data->send)) != 0) return rc;
        if (!data->send.data) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "data is NULL");
        if (data->send.dataSize == 0 || data->send.dataSize > GCDD_MAX_DATA_SIZE) {
            return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "dataSize must be 1..1 MiB");
        }
        return validate_platforms_and_recipients(mem, &data->send, account_ids);
    case GCDD_MODE_SEND_URL: {
        if ((rc = validate_common_send(mem, &data->send)) != 0) return rc;
        const char *url = gstr(mem, data->send.data);
        if (!url) return reject(ORBIS_COMMON_DIALOG_ERROR_ARG_NULL, "dataUrl is NULL");
        const size_t url_bytes = strnlen(url, GCDD_MAX_URL_LENGTH + 1);
        if (url_bytes == 0 || url_bytes > GCDD_MAX_URL_LENGTH) {
            return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "dataUrl must be 1..1024 bytes");
        }
        return validate_platforms_and_recipients(mem, &data->send, account_ids);
    }
    default:
        return reject(ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID, "unknown mode");
    }
}

static void copy_bounded(char *dst, const char *src, size_t max) {
    const size_t n = strnlen(src, max);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// Copies what the window shows; the guest may reuse its buffers once Open returns.
static void capture_send_locked(uint8_t *mem, const GcddSendInfo *s, int32_t mode, bool account_ids) {
    GcddView *v = &g_view;
    copy_bounded(v->data_name, gstr(mem, s->dataName), GCDD_MAX_NAME_BYTES);
    copy_bounded(v->data_detail, gstr(mem, s->dataDetail), GCDD_MAX_DETAIL_BYTES);
    copy_bounded(v->message, gstr(mem, s->userMessage), GCDD_MAX_MESSAGE_BYTES);
    v->expire_minutes = s->expireMinutes;
    if (mode == GCDD_MODE_SEND_URL) {
        copy_bounded(v->data_url, gstr(mem, s->data), GCDD_MAX_URL_LENGTH);
        v->with_item_id = (s->dataSize & 0xFF) != 0;
    } else {
        v->data_size = s->dataSize;
    }
    const uint64_t *names = gaddr(mem, s->platformName);
    for (uint32_t i = 0; i < s->platformCount && v->platform_count < GCDD_MAX_PLATFORMS; i++) {
        copy_bounded(v->platforms[v->platform_count++], gstr(mem, names[i]), GCDD_MAX_PLATFORM_NAME_BYTES);
    }
    g_thumbnail = malloc(s->thumbnailSize);
    if (g_thumbnail) {
        memcpy(g_thumbnail, gaddr(mem, s->thumbnail), s->thumbnailSize);
        v->thumbnail = g_thumbnail;
        v->thumbnail_size = s->thumbnailSize;
    }
    const GcddAddressParam *a = &s->addressParam;
    if (a->addressType == GCDD_ADDRESS_TYPE_USERENABLE) {
        v->user_editable = true;
        v->user_max_count = a->info.editable.userMaxCount;
        return;
    }
    v->recipient_count = a->info.fixed.count;
    for (uint32_t i = 0; i < v->recipient_count; i++) {
        if (account_ids) {
            snprintf(v->recipients[i], sizeof(v->recipients[i]), "account %llu",
                     (unsigned long long)((const uint64_t *)gaddr(mem, a->info.fixed.ids))[i]);
        } else {
            copy_bounded(v->recipients[i], ((const GcddNpOnlineId *)gaddr(mem, a->info.fixed.ids))[i].data,
                         GCDD_NP_ONLINE_ID_MAX_LENGTH);
        }
    }
}

static void log_request(const GcddParam *p, const char *entry) {
    static const char *const modes[] = {"INVALID", "SEND (game data)", "RECV", "SEND_URL"};
    const GcddView *v = &g_view;
    fprintf(stderr, "[libSceGameCustomDataDialog] %s: mode %s, user 0x%x, callbackArg 0x%llx\n", entry,
            modes[p->mode], (unsigned)p->userId, (unsigned long long)p->callbackArg);
    if (p->mode == GCDD_MODE_RECV) return;
    fprintf(stderr, "  dataName \"%s\", thumbnail %u bytes, %s %s, expireMinutes %d, recipients %s\n", v->data_name,
            v->thumbnail_size, p->mode == GCDD_MODE_SEND_URL ? "url" : "data",
            p->mode == GCDD_MODE_SEND_URL ? v->data_url : "attached", v->expire_minutes,
            v->user_editable ? "chosen by the player" : "fixed by the game");
}

static void reset_view_locked(void) {
    free(g_thumbnail);
    g_thumbnail = NULL;
    memset(&g_view, 0, sizeof(g_view));
}

// Moves a running dialog to FINISHED with `result`; returns whether it was running.
static bool finish_locked(int32_t result) {
    if (g_status != GCDD_STATUS_RUNNING) return false;
    g_result = result;
    g_status = GCDD_STATUS_FINISHED;
    return true;
}

void ps4_gcdd_finish(uint32_t serial, int32_t result) {
    pthread_mutex_lock(&g_mu);
    if (serial == g_view.serial) finish_locked(result);
    pthread_mutex_unlock(&g_mu);
}

int32_t sceGameCustomDataDialogInitialize(void) {
    if (!ps4_common_dialog_system_initialized()) return ORBIS_COMMON_DIALOG_ERROR_NOT_SYSTEM_INITIALIZED;
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status != GCDD_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_ALREADY_INITIALIZED;
    } else if (!ps4_common_dialog_acquire()) {
        rc = ORBIS_COMMON_DIALOG_ERROR_BUSY;
    } else {
        g_status = GCDD_STATUS_INITIALIZED;
    }
    pthread_mutex_unlock(&g_mu);
    return rc;
}

int32_t sceGameCustomDataDialogTerminate(void) {
    pthread_mutex_lock(&g_mu);
    if (g_status == GCDD_STATUS_NONE) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    }
    const bool was_running = finish_locked(GCDD_RESULT_USER_CANCELED); // aborts a dialog still up
    g_status = GCDD_STATUS_NONE;
    pthread_mutex_unlock(&g_mu);
    if (was_running) ps4_gcdd_ui_dismiss();
    ps4_common_dialog_release();
    return 0;
}

static int32_t open_impl(uint8_t *mem, const GcddParam *p, bool account_ids, const char *entry) {
    pthread_mutex_lock(&g_mu);
    const int32_t status = g_status;
    pthread_mutex_unlock(&g_mu);
    if (status == GCDD_STATUS_NONE) return ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    const int32_t rc = validate_param(mem, p, account_ids);
    if (rc != 0) return rc;

    pthread_mutex_lock(&g_mu);
    if (g_status != GCDD_STATUS_INITIALIZED && g_status != GCDD_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_mu);
        return ORBIS_COMMON_DIALOG_ERROR_INVALID_STATE;
    }
    reset_view_locked();
    g_view.serial = ++g_serial;
    g_view.mode = p->mode;
    g_callback_arg = p->callbackArg;
    g_result = GCDD_RESULT_USER_CANCELED;
    if (p->mode != GCDD_MODE_RECV) {
        capture_send_locked(mem, &((const GcddDataParam *)gaddr(mem, p->dataParam))->send, p->mode, account_ids);
    }
    log_request(p, entry);
    g_status = GCDD_STATUS_RUNNING;
    pthread_mutex_unlock(&g_mu);

    // The view stays unchanged while the dialog runs: Open is refused until it finishes.
    if (!ps4_gcdd_ui_show(&g_view)) ps4_gcdd_finish(g_view.serial, GCDD_RESULT_USER_CANCELED);
    return 0;
}

int32_t sceGameCustomDataDialogOpen(uint8_t *mem, const GcddParam *param) {
    return open_impl(mem, param, false, "Open");
}

int32_t sceGameCustomDataDialogOpenA(uint8_t *mem, const GcddParam *param) {
    return open_impl(mem, param, true, "OpenA");
}

int32_t sceGameCustomDataDialogClose(void) {
    pthread_mutex_lock(&g_mu);
    int32_t rc = 0;
    if (g_status == GCDD_STATUS_NONE) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    } else if (!finish_locked(GCDD_RESULT_OK)) {
        rc = ORBIS_COMMON_DIALOG_ERROR_NOT_RUNNING;
    }
    pthread_mutex_unlock(&g_mu);
    if (rc == 0) ps4_gcdd_ui_dismiss();
    return rc;
}

int32_t sceGameCustomDataDialogGetStatus(void) {
    pthread_mutex_lock(&g_mu);
    const int32_t st = g_status;
    pthread_mutex_unlock(&g_mu);
    return st;
}

int32_t sceGameCustomDataDialogUpdateStatus(void) {
    return sceGameCustomDataDialogGetStatus();
}

// No game custom data is ever sent (no online service), so the recipient list is empty.
static int32_t get_result_impl(uint8_t *mem, GcddResult *result, bool user_list) {
    if (!result) return ORBIS_COMMON_DIALOG_ERROR_ARG_NULL;
    if (!all_zero(result->reserved, sizeof(result->reserved))) return ORBIS_COMMON_DIALOG_ERROR_PARAM_INVALID;
    pthread_mutex_lock(&g_mu);
    const int32_t status = g_status;
    const int32_t res = g_result;
    const uint64_t arg = g_callback_arg;
    pthread_mutex_unlock(&g_mu);
    if (status == GCDD_STATUS_NONE) return ORBIS_COMMON_DIALOG_ERROR_NOT_INITIALIZED;
    if (status != GCDD_STATUS_FINISHED) return ORBIS_COMMON_DIALOG_ERROR_NOT_FINISHED;
    result->callbackArg = arg;
    result->errorCode = 0;
    result->result = res;
    void *list = gaddr(mem, result->sent);
    if (list) memset(list, 0, user_list ? GCDD_USER_LIST_SIZE : GCDD_ONLINE_ID_LIST_SIZE);
    return res;
}

int32_t sceGameCustomDataDialogGetResult(uint8_t *mem, GcddResult *result) {
    return get_result_impl(mem, result, false);
}

int32_t sceGameCustomDataDialogGetResultA(uint8_t *mem, GcddResult *result) {
    return get_result_impl(mem, result, true);
}

void shim_sceGameCustomDataDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogInitialize();
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogTerminate();
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogOpen(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogOpen(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogOpenA(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogOpenA(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogClose();
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogUpdateStatus();
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogGetStatus();
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogGetResult(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogGetResult(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi));
    SHIM_RETURN();
}

void shim_sceGameCustomDataDialogGetResultA(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceGameCustomDataDialogGetResultA(ctx->mem_base, gaddr(ctx->mem_base, ctx->rdi));
    SHIM_RETURN();
}
