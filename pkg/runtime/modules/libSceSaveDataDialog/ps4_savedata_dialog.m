// libSceSaveDataDialog: the system save data dialog (save slot list, system /
// user messages, error codes, progress bar).
//
// Struct layouts, button/result semantics and system message texts follow
// shadPS4 (core/libraries/save_data/dialog, GPL-2.0-or-later). The list and the
// messages are shown as native macOS alerts on the host main thread while the
// dialog stays RUNNING, so the game keeps rendering and polling
// sceSaveDataDialogUpdateStatus as on the console. Non-interactive runs
// (PS4_RECOMP_HEADLESS, or no terminal and not launched as an app bundle)
// answer automatically; PS4_SAVEDATA_DIALOG=ui|auto forces either behavior and
// PS4_SAVEDATA_DIALOG_PICK=first|new|latest|<dirName> selects the automatic
// list answer (default: see sdd_auto_pick); a comma-separated sequence answers the
// lists in the order they open (the last choice repeats), e.g. "new,gameUser0" for a
// game that offers a save list before its load list.
#import <Cocoa/Cocoa.h>
#include "ps4_savedata_dialog.h"
#include "ps4_savedata.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int ps4_host_choose_language(void); // libSceSystemService/ps4_settings.m

#define SDD_DIRNAME_SIZE 32
#define SDD_MAX_ITEMS 256

// Guest structures (PS4 SysV layout).
typedef struct SddItems {
    int32_t userId;
    int32_t _pad0;
    uint64_t titleId;         // const OrbisSaveDataTitleId*
    uint64_t dirName;         // const OrbisSaveDataDirName* (array)
    uint32_t dirNameNum;
    int32_t _pad1;
    uint64_t newItem;         // const SaveDialogNewItem*
    uint32_t focusPos;
    int32_t _pad2;
    uint64_t focusPosDirName; // const OrbisSaveDataDirName*
    uint32_t itemStyle;
    uint8_t _reserved[32];
} SddItems;

typedef struct SddNewItem {
    uint64_t title;           // const char*
    uint64_t iconBuf;
    uint64_t iconSize;
    uint8_t _reserved[32];
} SddNewItem;

typedef struct SddUserMsg {
    uint32_t buttonType;
    uint32_t msgType;
    uint64_t msg;             // const char*
    uint8_t _reserved[32];
} SddUserMsg;

typedef struct SddSystemMsg {
    uint32_t msgType;
    int32_t _pad0;
    uint64_t value;
    uint8_t _reserved[32];
} SddSystemMsg;

typedef struct SddProgressBar {
    uint32_t barType;
    int32_t _pad0;
    uint64_t msg;             // const char*
    uint32_t sysMsgType;
    uint8_t _reserved[28];
} SddProgressBar;

enum { SDD_FOCUS_LISTHEAD = 0, SDD_FOCUS_LISTTAIL = 1, SDD_FOCUS_DATAHEAD = 2,
       SDD_FOCUS_DATATAIL = 3, SDD_FOCUS_DATALATEST = 4, SDD_FOCUS_DATAOLDEST = 5,
       SDD_FOCUS_DIRNAME = 6 };
enum { SDD_BUTTON_INVALID = 0, SDD_BUTTON_OK = 1, SDD_BUTTON_YES = 1, SDD_BUTTON_NO = 2 };
enum { SDD_BTN_OK = 0, SDD_BTN_YESNO = 1, SDD_BTN_NONE = 2, SDD_BTN_OKCANCEL = 3 };
enum { SDD_SYS_NODATA = 1, SDD_SYS_CONFIRM = 2, SDD_SYS_OVERWRITE = 3, SDD_SYS_NOSPACE = 4,
       SDD_SYS_PROGRESS = 5, SDD_SYS_FILE_CORRUPTED = 6, SDD_SYS_FINISHED = 7,
       SDD_SYS_NOSPACE_CONTINUABLE = 8, SDD_SYS_CORRUPTED_AND_DELETED = 10,
       SDD_SYS_CORRUPTED_AND_CREATED = 11, SDD_SYS_CORRUPTED_AND_RESTORE = 13,
       SDD_SYS_TOTAL_SIZE_EXCEEDED = 14 };

#define SDD_SDK_450 0x4500000u // SDK 4.50: NO / plain OK stop mapping to USER_CANCELED

typedef struct SddEntry {
    char dir[SDD_DIRNAME_SIZE]; // "" = the "new saved data" item
    bool is_new;
    Ps4SaveDataParam param;
    uint64_t bytes;
} SddEntry;

// Host copy of everything one open dialog needs (guest memory is not touched from the UI).
typedef struct SddSpec {
    OrbisSaveDataDialogMode mode;
    OrbisSaveDataDialogType type;
    bool back_enabled;
    SddEntry *entries; // new item first, then the listed saves in the game's order
    int entry_count;
    int focus;         // initially selected entry
    uint32_t focus_pos; // the game's OrbisSaveDataDialogFocusPos
    int first_save;    // index of the first existing save, -1 if none
    uint32_t sys_type;
    uint64_t sys_value;
    uint32_t user_btn;
    char *msg;         // user message / progress text (UTF-8), may be NULL
    uint32_t error_code;
} SddSpec;

static pthread_mutex_t g_sdd_mutex = PTHREAD_MUTEX_INITIALIZER;
static OrbisCommonDialogStatus g_sdd_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
static OrbisSaveDataDialogParam g_sdd_param;
static OrbisSaveDataDialogResult g_sdd_result;
static SddSpec g_sdd_spec;
static uint64_t g_sdd_generation; // bumped on every Open/Close/Terminate
static char g_sdd_sel_dir[SDD_DIRNAME_SIZE];
static bool g_sdd_sel_has_param;
static Ps4SaveDataParam g_sdd_sel_param;
static uint32_t g_sdd_sdk_version;
static NSAlert *g_sdd_alert; // main thread only

static bool sdd_russian(void) { return ps4_host_choose_language() == 8; }
#define TR(en, ru) (sdd_russian() ? @"" ru : @"" en)

static bool sdd_interactive(void) {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    bool ui;
    const char *e = getenv("PS4_SAVEDATA_DIALOG");
    if (e && strcmp(e, "ui") == 0) ui = true;
    else if (e && strcmp(e, "auto") == 0) ui = false;
    else if (getenv("PS4_RECOMP_HEADLESS")) ui = false;
    else if (isatty(fileno(stdin)) || isatty(fileno(stdout))) ui = true;
    // Started from Finder/Dock/open(1): no terminal, but a player in front of the window.
    else ui = [[[NSBundle mainBundle] bundlePath] hasSuffix:@".app"];
    cached = ui ? 1 : 0;
    return ui;
}

static void sdd_spec_free(SddSpec *s) {
    free(s->entries);
    free(s->msg);
    memset(s, 0, sizeof(*s));
}

static void *sdd_guest(const GuestContext *ctx, uint64_t gaddr, size_t bytes) {
    if (!ctx || !ctx->mem_base || gaddr == 0) return NULL;
    if (gaddr >= (uintptr_t)ctx->mem_base && gaddr - (uintptr_t)ctx->mem_base + bytes <= ctx->mem_size)
        return (void *)gaddr;
    if (gaddr < ctx->mem_size && bytes <= ctx->mem_size - gaddr) return ctx->mem_base + gaddr;
    return NULL;
}

static char *sdd_guest_strdup(const GuestContext *ctx, uint64_t gaddr, size_t max) {
    const char *s = (const char *)sdd_guest(ctx, gaddr, 1);
    if (!s) return NULL;
    size_t n = strnlen(s, max);
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static uint32_t sdd_read_sdk_version(const GuestContext *ctx) {
    if (!ctx) return 0;
    uint64_t pp = ctx->proc_param_addr;
    if (!pp && ctx->process_ctx) pp = ctx->process_ctx->proc_param_addr;
    // SceProcParam: u64 size; u32 magic; u32 entryCount; u64 sdkVersion; ...
    const uint8_t *p = (const uint8_t *)sdd_guest(ctx, pp, 0x18);
    if (!p || *(const uint64_t *)p < 0x18) return 0;
    return *(const uint32_t *)(p + 0x10);
}

// Builds the host-side description of the dialog from the guest parameters.
static void sdd_build_spec(const GuestContext *ctx, const OrbisSaveDataDialogParam *param, SddSpec *s) {
    memset(s, 0, sizeof(*s));
    s->mode = param->mode;
    s->type = param->dispType;
    s->first_save = -1;
    s->back_enabled = true;
    const uint32_t *opt = (const uint32_t *)sdd_guest(ctx, (uint64_t)param->optionParam, 4);
    if (opt) s->back_enabled = (*opt == 0); // OptionBack: ENABLE = 0, DISABLE = 1

    const SddItems *items = (const SddItems *)sdd_guest(ctx, (uint64_t)param->items, sizeof(SddItems));
    if (items) {
        uint32_t n = items->dirNameNum < SDD_MAX_ITEMS ? items->dirNameNum : SDD_MAX_ITEMS;
        const char *names = (const char *)sdd_guest(ctx, items->dirName, (size_t)n * SDD_DIRNAME_SIZE);
        if (!names) n = 0;
        const SddNewItem *nitem = (const SddNewItem *)sdd_guest(ctx, items->newItem, sizeof(SddNewItem));
        bool with_new = nitem && s->type == ORBIS_SAVE_DATA_DIALOG_TYPE_SAVE;
        s->entries = (SddEntry *)calloc((size_t)n + 1, sizeof(SddEntry));
        if (s->entries) {
            if (with_new) {
                SddEntry *e = &s->entries[s->entry_count++];
                e->is_new = true;
                char *title = sdd_guest_strdup(ctx, nitem->title, sizeof(e->param.title) - 1);
                if (title) {
                    snprintf(e->param.title, sizeof(e->param.title), "%s", title);
                    free(title);
                }
            }
            for (uint32_t i = 0; i < n; i++) {
                char dir[SDD_DIRNAME_SIZE] = {0};
                memcpy(dir, names + (size_t)i * SDD_DIRNAME_SIZE, SDD_DIRNAME_SIZE - 1);
                if (!dir[0]) continue;
                SddEntry *e = &s->entries[s->entry_count];
                // Like the console, list only directories that exist.
                if (!ps4_savedata_read_dir_info(items->userId, dir, &e->param, &e->bytes)) continue;
                snprintf(e->dir, sizeof(e->dir), "%s", dir);
                if (s->first_save < 0) s->first_save = s->entry_count;
                s->entry_count++;
            }
        }
        // Initial focus.
        s->focus_pos = items->focusPos;
        int last = s->entry_count - 1;
        switch (items->focusPos) {
        case SDD_FOCUS_LISTTAIL: s->focus = last; break;
        case SDD_FOCUS_DATAHEAD: s->focus = s->first_save; break;
        case SDD_FOCUS_DATATAIL: s->focus = s->first_save >= 0 ? last : -1; break;
        case SDD_FOCUS_DATALATEST:
        case SDD_FOCUS_DATAOLDEST: {
            int best = -1;
            for (int i = 0; i < s->entry_count; i++) {
                if (s->entries[i].is_new) continue;
                int64_t t = s->entries[i].param.mtime;
                if (best < 0 || (items->focusPos == SDD_FOCUS_DATALATEST ? t > s->entries[best].param.mtime
                                                                         : t < s->entries[best].param.mtime))
                    best = i;
            }
            s->focus = best;
            break;
        }
        case SDD_FOCUS_DIRNAME: {
            s->focus = -1;
            const char *fd = (const char *)sdd_guest(ctx, items->focusPosDirName, SDD_DIRNAME_SIZE);
            for (int i = 0; fd && i < s->entry_count; i++) {
                if (strncmp(s->entries[i].dir, fd, SDD_DIRNAME_SIZE - 1) == 0) s->focus = i;
            }
            break;
        }
        default: s->focus = 0; break;
        }
        if (s->focus < 0 || s->focus >= s->entry_count) s->focus = 0;
    }

    const SddSystemMsg *sys = (const SddSystemMsg *)sdd_guest(ctx, (uint64_t)param->sysMsgParam, sizeof(SddSystemMsg));
    if (sys) {
        s->sys_type = sys->msgType;
        s->sys_value = sys->value;
    }
    const SddUserMsg *user = (const SddUserMsg *)sdd_guest(ctx, (uint64_t)param->userMsgParam, sizeof(SddUserMsg));
    if (user) {
        s->user_btn = user->buttonType;
        s->msg = sdd_guest_strdup(ctx, user->msg, 4096);
    }
    const uint32_t *err = (const uint32_t *)sdd_guest(ctx, (uint64_t)param->errorCodeParam, 4);
    if (err) s->error_code = *err;
    const SddProgressBar *bar = (const SddProgressBar *)sdd_guest(ctx, (uint64_t)param->progressBarParam, sizeof(SddProgressBar));
    if (bar && !s->msg) s->msg = sdd_guest_strdup(ctx, bar->msg, 4096);
}

// True when the dialog has no confirming button and stays up until the game closes it
// (progress bar, "Saving..." system message, user message without buttons).
static bool sdd_waits_for_close(const SddSpec *s) {
    switch (s->mode) {
    case ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR: return true;
    case ORBIS_SAVE_DATA_DIALOG_MODE_SYSTEM_MSG: return s->sys_type == SDD_SYS_PROGRESS;
    case ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG: return s->user_btn == SDD_BTN_NONE;
    default: return false;
    }
}

// System messages whose OK button reports USER_CANCELED before SDK 4.50.
static bool sdd_sys_returns_cancel(uint32_t t) {
    return t == SDD_SYS_NODATA || t == SDD_SYS_NOSPACE || t == SDD_SYS_FILE_CORRUPTED ||
           t == SDD_SYS_FINISHED || t == SDD_SYS_NOSPACE_CONTINUABLE;
}

static bool sdd_old_sdk(void) { return g_sdd_sdk_version != 0 && g_sdd_sdk_version < SDD_SDK_450; }

// Completes the running dialog (caller holds g_sdd_mutex).
static void sdd_finish_locked(OrbisCommonDialogResult r, int32_t button, int sel) {
    g_sdd_result.mode = g_sdd_spec.mode;
    g_sdd_result.result = r;
    g_sdd_result.buttonId = button;
    memset(g_sdd_sel_dir, 0, sizeof(g_sdd_sel_dir));
    g_sdd_sel_has_param = false;
    if (sel >= 0 && sel < g_sdd_spec.entry_count) {
        const SddEntry *e = &g_sdd_spec.entries[sel];
        snprintf(g_sdd_sel_dir, sizeof(g_sdd_sel_dir), "%s", e->dir);
        if (!e->is_new) {
            g_sdd_sel_param = e->param;
            g_sdd_sel_has_param = true;
        }
    }
    g_sdd_status = ORBIS_COMMON_DIALOG_STATUS_FINISHED;
    fprintf(stderr, "[ps4-savedata-dialog] finished: mode=%d result=%d button=%d dir='%s'\n",
            (int)g_sdd_result.mode, (int)r, (int)button, g_sdd_sel_dir);
}

// Answer of the affirmative (first) button, the second button or cancel, for every
// mode except LIST.
static void sdd_answer_locked(bool affirmative, bool cancel) {
    const SddSpec *s = &g_sdd_spec;
    int sel = s->first_save >= 0 ? s->first_save : -1; // non-list modes report the listed save
    if (cancel) {
        sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, sel);
        return;
    }
    bool old = sdd_old_sdk();
    switch (s->mode) {
    case ORBIS_SAVE_DATA_DIALOG_MODE_SYSTEM_MSG:
        if (affirmative) {
            if (sdd_sys_returns_cancel(s->sys_type) && old)
                sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, sel);
            else
                sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_YES, sel);
        } else if (old) {
            sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, sel);
        } else {
            sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_NO, sel);
        }
        break;
    case ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG:
        if (s->user_btn == SDD_BTN_YESNO) {
            if (affirmative) sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_YES, sel);
            else if (old) sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, sel);
            else sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_NO, sel);
        } else if (s->user_btn == SDD_BTN_OK && old) {
            sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, sel);
        } else {
            sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_OK, sel);
        }
        break;
    case ORBIS_SAVE_DATA_DIALOG_MODE_ERROR_CODE:
        if (old) sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, sel);
        else sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_OK, sel);
        break;
    default:
        sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_OK, sel);
        break;
    }
}

static uint32_t g_sdd_lists_answered; // automatically answered list dialogs (index into the PICK sequence)

// Copies the PICK choice for the list dialog number `index` (0-based) into out ("" = none).
// Every automatically answered list consumes one entry, an empty list (no saves, answered
// with a cancel) too. Entries are taken verbatim: no whitespace is trimmed, so "new, x" names
// the directory " x"; an empty entry ("new,,x") means the default answer.
static void sdd_pick_choice(uint32_t index, char *out, size_t out_size) {
    out[0] = '\0';
    const char *seq = getenv("PS4_SAVEDATA_DIALOG_PICK");
    if (!seq) return;
    const char *item = seq;
    for (uint32_t i = 0; i < index; i++) {
        const char *comma = strchr(item, ',');
        if (!comma) break; // past the end: the last choice repeats
        item = comma + 1;
    }
    size_t n = strcspn(item, ",");
    if (n >= out_size) n = out_size - 1;
    memcpy(out, item, n);
    out[n] = '\0';
}

// Automatic list answer for non-interactive runs. Without PS4_SAVEDATA_DIALOG_PICK it
// never destroys existing data: a save list takes the "new saved data" item when offered
// (else the first save), a load list the save the game focused (what X confirms on the
// console, e.g. the latest save for FOCUS_DATALATEST), and a delete list is canceled.
static int sdd_auto_pick(const SddSpec *s) {
    char choice[SDD_DIRNAME_SIZE + 8];
    sdd_pick_choice(g_sdd_lists_answered++, choice, sizeof(choice));
    if (s->entry_count == 0) return -1;
    const char *pick = choice;
    if (!*pick) {
        if (s->type == ORBIS_SAVE_DATA_DIALOG_TYPE_DELETE) return -1;
        if (s->type == ORBIS_SAVE_DATA_DIALOG_TYPE_SAVE && s->entries[0].is_new) return 0;
        if (s->type == ORBIS_SAVE_DATA_DIALOG_TYPE_LOAD && !s->entries[s->focus].is_new) return s->focus;
        return s->first_save >= 0 ? s->first_save : 0;
    }
    if (strcmp(pick, "new") == 0) {
        return s->entries[0].is_new ? 0 : s->first_save;
    }
    if (strcmp(pick, "latest") == 0) {
        int best = -1;
        for (int i = 0; i < s->entry_count; i++) {
            if (!s->entries[i].is_new && (best < 0 || s->entries[i].param.mtime > s->entries[best].param.mtime))
                best = i;
        }
        return best >= 0 ? best : 0;
    }
    if (strcmp(pick, "first") != 0) {
        for (int i = 0; i < s->entry_count; i++) {
            if (strcmp(s->entries[i].dir, pick) == 0) return i;
        }
    }
    return s->first_save >= 0 ? s->first_save : 0;
}

//----------------------------------------------------------------------------
// Native UI (host main thread)
//----------------------------------------------------------------------------

static NSString *sdd_ns(const char *s) {
    NSString *r = s ? [NSString stringWithUTF8String:s] : nil;
    return r ? r : @"";
}

static NSString *sdd_size_string(uint64_t bytes) {
    return [NSByteCountFormatter stringFromByteCount:(long long)bytes countStyle:NSByteCountFormatterCountStyleFile];
}

static NSString *sdd_entry_label(const SddEntry *e) {
    if (e->is_new) {
        NSString *t = sdd_ns(e->param.title);
        return [@"+ " stringByAppendingString:t.length ? t : TR("New saved data", "Новое сохранение")];
    }
    NSString *title = sdd_ns(e->param.subTitle);
    if (!title.length) title = sdd_ns(e->param.title);
    if (!title.length) title = sdd_ns(e->dir);
    NSString *detail = [[sdd_ns(e->param.detail) componentsSeparatedByCharactersInSet:
                            [NSCharacterSet newlineCharacterSet]] componentsJoinedByString:@" · "];
    NSDateFormatter *df = [[NSDateFormatter alloc] init];
    df.dateFormat = @"dd.MM.yyyy HH:mm";
    NSString *date = [df stringFromDate:[NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)e->param.mtime]];
    NSMutableString *s = [NSMutableString stringWithString:title];
    if (detail.length) [s appendFormat:@"  —  %@", detail];
    [s appendFormat:@"  —  %@, %@", date, sdd_size_string(e->bytes)];
    return s;
}

static NSString *sdd_system_text(const SddSpec *s, bool *yesno, bool *show_cancel) {
    *yesno = false;
    *show_cancel = false;
    OrbisSaveDataDialogType t = s->type;
#define M(save, load, del) (t == ORBIS_SAVE_DATA_DIALOG_TYPE_LOAD ? (load) : t == ORBIS_SAVE_DATA_DIALOG_TYPE_DELETE ? (del) : (save))
    NSString *need = sdd_size_string(s->sys_value * 32768ULL);
    switch (s->sys_type) {
    case SDD_SYS_NODATA: return TR("There is no saved data.", "Нет сохраненных данных.");
    case SDD_SYS_CONFIRM:
        *yesno = true;
        return M(TR("Do you want to save?", "Сохранить?"),
                 TR("Do you want to load this saved data?", "Загрузить эти сохраненные данные?"),
                 TR("Do you want to delete this saved data?", "Удалить эти сохраненные данные?"));
    case SDD_SYS_OVERWRITE:
        *yesno = true;
        return TR("Do you want to overwrite the existing saved data?", "Перезаписать существующие сохраненные данные?");
    case SDD_SYS_NOSPACE:
    case SDD_SYS_NOSPACE_CONTINUABLE:
        return [NSString stringWithFormat:TR("There is not enough space to save the data. %@ of free space is required.",
                                             "Недостаточно места для сохранения. Требуется свободного места: %@."), need];
    case SDD_SYS_FILE_CORRUPTED: return TR("The saved data is corrupted.", "Сохраненные данные повреждены.");
    case SDD_SYS_FINISHED:
        return M(TR("Saved successfully.", "Сохранение завершено."), TR("Loading complete.", "Загрузка завершена."),
                 TR("Deletion complete.", "Удаление завершено."));
    case SDD_SYS_CORRUPTED_AND_DELETED:
        *show_cancel = s->back_enabled;
        return TR("The saved data is corrupted and will be deleted.", "Сохраненные данные повреждены и будут удалены.");
    case SDD_SYS_CORRUPTED_AND_CREATED:
        *show_cancel = s->back_enabled;
        return TR("The saved data is corrupted. It will be deleted and new saved data will be created.",
                  "Сохраненные данные повреждены. Они будут удалены, и будут созданы новые.");
    case SDD_SYS_CORRUPTED_AND_RESTORE:
        *show_cancel = s->back_enabled;
        return TR("The saved data is corrupted. The data backed up by the system will be restored.",
                  "Сохраненные данные повреждены. Будет восстановлена резервная копия.");
    case SDD_SYS_TOTAL_SIZE_EXCEEDED: return TR("Cannot create more saved data.", "Невозможно создать больше сохраненных данных.");
    default: return [NSString stringWithFormat:@"(system message %u)", s->sys_type];
    }
#undef M
}

// Runs the modal alert for the dialog opened as generation `gen` (main thread).
static void sdd_show_ui_main(uint64_t gen) {
    @autoreleasepool {
        pthread_mutex_lock(&g_sdd_mutex);
        if (gen != g_sdd_generation || g_sdd_status != ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
            pthread_mutex_unlock(&g_sdd_mutex);
            return;
        }
        // Copy what the UI needs; the spec may be replaced by a later Open/Terminate.
        OrbisSaveDataDialogMode mode = g_sdd_spec.mode;
        OrbisSaveDataDialogType type = g_sdd_spec.type;
        bool back_enabled = g_sdd_spec.back_enabled;
        int entry_count = g_sdd_spec.entry_count, focus = g_sdd_spec.focus, first_save = g_sdd_spec.first_save;
        uint32_t user_btn = g_sdd_spec.user_btn, error_code = g_sdd_spec.error_code;
        bool yesno = false, sys_cancel = false;
        NSString *sys_text = mode == ORBIS_SAVE_DATA_DIALOG_MODE_SYSTEM_MSG
                                 ? sdd_system_text(&g_sdd_spec, &yesno, &sys_cancel) : nil;
        NSMutableArray<NSString *> *labels = [NSMutableArray array];
        for (int i = 0; i < entry_count; i++) [labels addObject:sdd_entry_label(&g_sdd_spec.entries[i])];
        NSString *msg = sdd_ns(g_sdd_spec.msg);
        pthread_mutex_unlock(&g_sdd_mutex);

        [NSApplication sharedApplication];
        [NSApp activateIgnoringOtherApps:YES];
        NSAlert *alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleInformational;
        NSPopUpButton *popup = nil;
        bool has_second = false, second_is_cancel = false;
        NSString *info = first_save >= 0 ? labels[first_save] : (entry_count > 0 ? labels[0] : nil);

        switch (mode) {
        case ORBIS_SAVE_DATA_DIALOG_MODE_LIST: {
            alert.messageText = type == ORBIS_SAVE_DATA_DIALOG_TYPE_LOAD ? TR("Load Game", "Загрузка игры")
                              : type == ORBIS_SAVE_DATA_DIALOG_TYPE_DELETE ? TR("Delete Saved Data", "Удаление сохранений")
                              : TR("Save Game", "Сохранение игры");
            if (entry_count == 0) {
                alert.informativeText = TR("There is no saved data.", "Нет сохраненных данных.");
                [alert addButtonWithTitle:TR("Back", "Назад")];
                break;
            }
            alert.informativeText = TR("Choose saved data:", "Выберите сохранение:");
            popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 680, 28) pullsDown:NO];
            for (int i = 0; i < entry_count; i++) {
                // Through the menu, so identical labels are kept as separate items.
                [popup.menu addItemWithTitle:labels[i] action:nil keyEquivalent:@""];
            }
            [popup selectItemAtIndex:focus];
            alert.accessoryView = popup;
            [alert addButtonWithTitle:type == ORBIS_SAVE_DATA_DIALOG_TYPE_LOAD ? TR("Load", "Загрузить")
                                     : type == ORBIS_SAVE_DATA_DIALOG_TYPE_DELETE ? TR("Delete", "Удалить")
                                     : TR("Save", "Сохранить")];
            if (back_enabled) {
                [alert addButtonWithTitle:TR("Back", "Назад")];
                has_second = second_is_cancel = true;
            }
            break;
        }
        case ORBIS_SAVE_DATA_DIALOG_MODE_SYSTEM_MSG:
            alert.messageText = sys_text;
            if (info) alert.informativeText = info;
            [alert addButtonWithTitle:yesno ? TR("Yes", "Да") : TR("OK", "OK")];
            if (yesno) {
                [alert addButtonWithTitle:TR("No", "Нет")];
                has_second = true;
            } else if (sys_cancel) {
                [alert addButtonWithTitle:TR("Cancel", "Отмена")];
                has_second = second_is_cancel = true;
            }
            break;
        case ORBIS_SAVE_DATA_DIALOG_MODE_USER_MSG:
            alert.messageText = msg.length ? msg : @" ";
            if (info) alert.informativeText = info;
            if (user_btn == SDD_BTN_YESNO) {
                [alert addButtonWithTitle:TR("Yes", "Да")];
                [alert addButtonWithTitle:TR("No", "Нет")];
                has_second = true;
            } else {
                [alert addButtonWithTitle:TR("OK", "OK")];
                if (user_btn == SDD_BTN_OKCANCEL) {
                    [alert addButtonWithTitle:TR("Cancel", "Отмена")];
                    has_second = second_is_cancel = true;
                }
            }
            break;
        case ORBIS_SAVE_DATA_DIALOG_MODE_ERROR_CODE:
            alert.messageText = error_code == 0x809F0008u ? TR("There is no saved data.", "Нет сохраненных данных.")
                              : error_code == 0x809F000Fu ? TR("The saved data is corrupted.", "Сохраненные данные повреждены.")
                              : [NSString stringWithFormat:TR("An error has occurred. (%08X)", "Произошла ошибка. (%08X)"), error_code];
            if (info) alert.informativeText = info;
            [alert addButtonWithTitle:TR("OK", "OK")];
            break;
        default:
            alert.messageText = TR("Saved data", "Сохраненные данные");
            [alert addButtonWithTitle:TR("OK", "OK")];
            break;
        }

        g_sdd_alert = alert;
        NSModalResponse resp = [alert runModal];
        g_sdd_alert = nil;
        NSInteger sel = popup ? popup.indexOfSelectedItem : -1;

        pthread_mutex_lock(&g_sdd_mutex);
        if (gen == g_sdd_generation && g_sdd_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
            bool first = resp == NSAlertFirstButtonReturn;
            bool cancel = !first && (resp == NSModalResponseAbort || second_is_cancel || !has_second);
            if (mode == ORBIS_SAVE_DATA_DIALOG_MODE_LIST) {
                if (first && sel >= 0)
                    sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_INVALID, (int)sel);
                else
                    sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, -1);
            } else {
                sdd_answer_locked(first, cancel);
            }
        }
        pthread_mutex_unlock(&g_sdd_mutex);
    }
}

// Dismisses an alert left open by a dialog that the game closed (main thread).
static void sdd_dismiss_ui_main(void) {
    if (g_sdd_alert) [NSApp abortModal];
}

//----------------------------------------------------------------------------
// API
//----------------------------------------------------------------------------

int32_t sceSaveDataDialogInitialize(void) {
    pthread_mutex_lock(&g_sdd_mutex);
    g_sdd_status = ORBIS_COMMON_DIALOG_STATUS_INITIALIZED;
    memset(&g_sdd_result, 0, sizeof(g_sdd_result));
    memset(&g_sdd_param, 0, sizeof(g_sdd_param));
    pthread_mutex_unlock(&g_sdd_mutex);
    return 0;
}

static int32_t sdd_open(const GuestContext *ctx, const OrbisSaveDataDialogParam *param) {
    if (!param) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }
    if (param->mode < ORBIS_SAVE_DATA_DIALOG_MODE_LIST || param->mode > ORBIS_SAVE_DATA_DIALOG_MODE_PROGRESS_BAR) {
        return (int32_t)0x80b80009; // PARAM_INVALID
    }
    SddSpec spec;
    sdd_build_spec(ctx, param, &spec);

    pthread_mutex_lock(&g_sdd_mutex);
    if (g_sdd_status != ORBIS_COMMON_DIALOG_STATUS_INITIALIZED &&
        g_sdd_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_sdd_mutex);
        sdd_spec_free(&spec);
        return (int32_t)0x80b80006; // INVALID_STATE
    }
    if (!g_sdd_sdk_version) g_sdd_sdk_version = sdd_read_sdk_version(ctx);
    sdd_spec_free(&g_sdd_spec);
    g_sdd_spec = spec;
    g_sdd_param = *param;
    memset(&g_sdd_result, 0, sizeof(g_sdd_result));
    g_sdd_result.mode = param->mode;
    memset(g_sdd_sel_dir, 0, sizeof(g_sdd_sel_dir));
    g_sdd_sel_has_param = false;
    g_sdd_status = ORBIS_COMMON_DIALOG_STATUS_RUNNING;
    uint64_t gen = ++g_sdd_generation;
    bool until_close = sdd_waits_for_close(&spec);
    bool ui = !until_close && sdd_interactive();

    fprintf(stderr, "[ps4-savedata-dialog] Open: mode=%d dispType=%d entries=%d focus=%d(pos %u) sys=%u user_btn=%u err=0x%x sdk=0x%x %s\n",
            (int)param->mode, (int)param->dispType, spec.entry_count, spec.focus, spec.focus_pos, spec.sys_type, spec.user_btn,
            spec.error_code, g_sdd_sdk_version, ui ? "(ui)" : until_close ? "(until close)" : "(auto)");
    if (spec.msg) {
        // The question an automatic answer replies to, on one line.
        fprintf(stderr, "[ps4-savedata-dialog]   msg: \"");
        for (const char *c = spec.msg; *c; c++) fputc(*c == '\n' || *c == '\r' ? ' ' : *c, stderr);
        fprintf(stderr, "\"\n");
    }
    for (int i = 0; i < spec.entry_count; i++) {
        fprintf(stderr, "[ps4-savedata-dialog]   [%d] %s '%s' | %s | %s\n", i,
                spec.entries[i].is_new ? "new" : "dir", spec.entries[i].dir,
                spec.entries[i].is_new ? spec.entries[i].param.title : spec.entries[i].param.subTitle,
                spec.entries[i].param.detail);
    }
    pthread_mutex_unlock(&g_sdd_mutex);

    if (ui) {
        dispatch_async(dispatch_get_main_queue(), ^{ sdd_show_ui_main(gen); });
    }
    return 0;
}

int32_t sceSaveDataDialogOpen(const OrbisSaveDataDialogParam *param) {
    return sdd_open(NULL, param);
}

OrbisCommonDialogStatus sceSaveDataDialogGetStatus(void) {
    pthread_mutex_lock(&g_sdd_mutex);
    OrbisCommonDialogStatus st = g_sdd_status;
    pthread_mutex_unlock(&g_sdd_mutex);
    return st;
}

OrbisCommonDialogStatus sceSaveDataDialogUpdateStatus(void) {
    pthread_mutex_lock(&g_sdd_mutex);
    if (g_sdd_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING && !sdd_waits_for_close(&g_sdd_spec) &&
        !sdd_interactive()) {
        // Automatic answer: the picked list entry, or the affirmative button.
        if (g_sdd_spec.mode == ORBIS_SAVE_DATA_DIALOG_MODE_LIST) {
            int sel = sdd_auto_pick(&g_sdd_spec);
            if (sel >= 0)
                sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_INVALID, sel);
            else
                sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_USER_CANCELED, SDD_BUTTON_INVALID, -1);
        } else {
            sdd_answer_locked(true, false);
        }
    }
    OrbisCommonDialogStatus st = g_sdd_status;
    pthread_mutex_unlock(&g_sdd_mutex);
    return st;
}

static int32_t sdd_get_result(const GuestContext *ctx, OrbisSaveDataDialogResult *result) {
    if (!result) {
        return (int32_t)0x80b8000d; // ARG_NULL
    }
    pthread_mutex_lock(&g_sdd_mutex);
    if (g_sdd_status != ORBIS_COMMON_DIALOG_STATUS_FINISHED) {
        pthread_mutex_unlock(&g_sdd_mutex);
        return (int32_t)0x80b80005; // NOT_FINISHED
    }
    // dirName/param are guest pointers to caller-owned buffers: write through them,
    // never replace them.
    result->mode = g_sdd_result.mode;
    result->result = g_sdd_result.result;
    result->buttonId = g_sdd_result.buttonId;
    result->userData = g_sdd_param.userData;
    if (ctx) {
        char *dir = (char *)sdd_guest(ctx, (uint64_t)result->dirName, SDD_DIRNAME_SIZE);
        // The list reports the chosen entry ("" = new saved data / canceled); other modes
        // report the save they were opened for, when there was one.
        if (dir && (g_sdd_result.mode == ORBIS_SAVE_DATA_DIALOG_MODE_LIST || g_sdd_sel_dir[0])) {
            memcpy(dir, g_sdd_sel_dir, SDD_DIRNAME_SIZE);
        }
        Ps4SaveDataParam *p = (Ps4SaveDataParam *)sdd_guest(ctx, (uint64_t)result->param, sizeof(Ps4SaveDataParam));
        if (p && g_sdd_sel_has_param) *p = g_sdd_sel_param;
    }
    pthread_mutex_unlock(&g_sdd_mutex);
    return 0;
}

int32_t sceSaveDataDialogGetResult(OrbisSaveDataDialogResult *result) {
    return sdd_get_result(NULL, result);
}

int32_t sceSaveDataDialogTerminate(void) {
    pthread_mutex_lock(&g_sdd_mutex);
    g_sdd_status = ORBIS_COMMON_DIALOG_STATUS_NONE;
    g_sdd_generation++;
    memset(&g_sdd_result, 0, sizeof(g_sdd_result));
    memset(&g_sdd_param, 0, sizeof(g_sdd_param));
    sdd_spec_free(&g_sdd_spec);
    pthread_mutex_unlock(&g_sdd_mutex);
    dispatch_async(dispatch_get_main_queue(), ^{ sdd_dismiss_ui_main(); });
    return 0;
}

// sceSaveDataDialogClose(const OrbisSaveDataDialogCloseParam *param): force-finishes a
// running dialog (games use it to dismiss progress bars once the operation is done).
int32_t sceSaveDataDialogClose(const void *param) {
    (void)param;
    bool had_ui = false;
    pthread_mutex_lock(&g_sdd_mutex);
    if (g_sdd_status == ORBIS_COMMON_DIALOG_STATUS_RUNNING) {
        g_sdd_generation++;
        had_ui = !sdd_waits_for_close(&g_sdd_spec);
        int sel = g_sdd_spec.mode != ORBIS_SAVE_DATA_DIALOG_MODE_LIST && g_sdd_spec.first_save >= 0
                      ? g_sdd_spec.first_save : -1;
        sdd_finish_locked(ORBIS_COMMON_DIALOG_RESULT_OK, SDD_BUTTON_INVALID, sel); // closed by the application
    }
    pthread_mutex_unlock(&g_sdd_mutex);
    if (had_ui) dispatch_async(dispatch_get_main_queue(), ^{ sdd_dismiss_ui_main(); });
    return 0;
}

// Returns 1 (true): the host can always display the dialog immediately.
int32_t sceSaveDataDialogIsReadyToDisplay(void) {
    return 1;
}

// Progress values are not rendered on the host; accept and ignore.
int32_t sceSaveDataDialogProgressBarSetValue(int32_t target, uint32_t rate) {
    (void)target;
    (void)rate;
    return 0;
}

//----------------------------------------------------------------------------
// Shims
//----------------------------------------------------------------------------

void shim_sceSaveDataDialogInitialize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogInitialize();
    SHIM_RETURN();
}

void shim_sceSaveDataDialogOpen(GuestContext *ctx) {
    const OrbisSaveDataDialogParam *param =
        (const OrbisSaveDataDialogParam *)sdd_guest(ctx, ctx->rdi, sizeof(OrbisSaveDataDialogParam));
    ctx->rax = (uint64_t)(int64_t)sdd_open(ctx, param);
    SHIM_RETURN();
}

void shim_sceSaveDataDialogUpdateStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogUpdateStatus();
    SHIM_RETURN();
}

void shim_sceSaveDataDialogGetStatus(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogGetStatus();
    SHIM_RETURN();
}

void shim_sceSaveDataDialogGetResult(GuestContext *ctx) {
    OrbisSaveDataDialogResult *res =
        (OrbisSaveDataDialogResult *)sdd_guest(ctx, ctx->rdi, sizeof(OrbisSaveDataDialogResult));
    ctx->rax = (uint64_t)(int64_t)sdd_get_result(ctx, res);
    SHIM_RETURN();
}

void shim_sceSaveDataDialogTerminate(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceSaveDataDialogTerminate();
    SHIM_RETURN();
}

void shim_sceSaveDataDialogClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataDialogClose(sdd_guest(ctx, ctx->rdi, 1));
    SHIM_RETURN();
}

void shim_sceSaveDataDialogIsReadyToDisplay(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataDialogIsReadyToDisplay();
    SHIM_RETURN();
}

void shim_sceSaveDataDialogProgressBarSetValue(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceSaveDataDialogProgressBarSetValue((int32_t)ctx->rdi, (uint32_t)ctx->rsi);
    SHIM_RETURN();
}
