// Startup settings of the host application, resolved once before the guest starts
// (ps4_host_settings_init, called from main()):
//   - system language (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG)),
//   - PS4 Pro (Neo) mode reported to the game (sceKernelIsNeoMode; on by default),
//   - MetalFX upscaling of the presented frame (PS4_UPSCALE),
//   - the Metal performance HUD (FPS counter).
// Values come from ~/Library/Application Support/PS4Recomp/settings/<app dir name>.cfg; a Cocoa
// dialog asks for them on the first launch and whenever Option is held at startup. Environment
// variables override the stored values: PS4_LANG, PS4_PRO=0|1, PS4_UPSCALE=off|metalfx,
// MTL_HUD_ENABLED=0|1; PS4_NO_DIALOGS=1 never shows the dialog.
//
// The language is the console system language. Games typically use it only as the default
// for a new profile: e.g. The Last of Us Remastered stores its text/subtitle/speech language
// in its profile save, so once a save exists, the in-game Options menu changes the language.

#import <Cocoa/Cocoa.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ps4_vfs.h"

typedef struct {
    const char *pack;  // speech pack / folder name used by the game data
    const char *code;  // short name accepted by PS4_LANG and the macOS locale prefix
    const char *title; // shown in the dialog
    int orbis;         // ORBIS_SYSTEM_PARAM_LANG_*
} LangInfo;

static const LangInfo kLangs[] = {
    {"japanese", "ja", "日本語", 0},       {"english", "en", "English", 1},
    {"french", "fr", "Français", 2},       {"spanish", "es", "Español", 3},
    {"german", "de", "Deutsch", 4},        {"italian", "it", "Italiano", 5},
    {"dutch", "nl", "Nederlands", 6},      {"portuguese", "pt", "Português", 7},
    {"russian", "ru", "Русский", 8},       {"korean", "ko", "한국어", 9},
    {"chinese", "zh", "中文", 11},          {"finnish", "fi", "Suomi", 12},
    {"swedish", "sv", "Svenska", 13},      {"danish", "da", "Dansk", 14},
    {"norwegian", "no", "Norsk", 15},      {"polish", "pl", "Polski", 16},
    {"brazilian", "pt-br", "Português (Brasil)", 17},
    {"turkish", "tr", "Türkçe", 19},       {"latam", "es-la", "Español (Latinoamérica)", 20},
    {"arabic", "ar", "العربية", 21},
};
#define NLANGS ((int)(sizeof(kLangs) / sizeof(kLangs[0])))

static int lang_from_name(const char *s) {
    if (!s || !*s) return -1;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end == '\0') return (int)v;
    for (int i = 0; i < NLANGS; i++) {
        if (!strcasecmp(s, kLangs[i].code) || !strcasecmp(s, kLangs[i].pack)) return kLangs[i].orbis;
    }
    return -1;
}




// Languages with a speech pack in the game data; falls back to English only.
static int available_langs(int *idx, int max) {
    int n = 0;
    const char *root = ps4_vfs_get_app_root();
    char dir[2048];
    snprintf(dir, sizeof(dir), "%s/build/ps4/main/sound1/streams/speech", root ? root : ".");
    for (int i = 0; i < NLANGS && n < max; i++) {
        char p[2300];
        struct stat st;
        snprintf(p, sizeof(p), "%s/%s-sp.psarc", dir, kLangs[i].pack);
        int ok = stat(p, &st) == 0;
        if (!ok) {
            snprintf(p, sizeof(p), "%s/%s", dir, kLangs[i].pack);
            ok = stat(p, &st) == 0;
        }
        if (ok) idx[n++] = i;
    }
    if (n == 0) idx[n++] = 1; // English
    return n;
}

static int system_default(const int *idx, int n) {
    NSString *pref = [[NSLocale preferredLanguages] firstObject];
    const char *p = pref ? pref.UTF8String : "en";
    for (int k = 0; k < n; k++) {
        const char *c = kLangs[idx[k]].code;
        if (!strncasecmp(p, c, strlen(c))) return k;
    }
    for (int k = 0; k < n; k++) {
        if (kLangs[idx[k]].orbis == 1) return k;
    }
    return 0;
}


typedef struct {
    int lang;    // Orbis language code
    int pro;     // PS4 Pro mode
    int upscale; // MetalFX upscaling
    int hud;     // Metal performance HUD
} HostSettings;

static HostSettings g_settings = {-1, 1, 0, 1};
static bool g_settings_ready = false;

static void settings_dir(char *out, size_t n) {
    // Follows the save data location (PS4RECOMP_DATA_DIR), so a separate data dir
    // also has its own settings.
    const char *data_dir = getenv("PS4RECOMP_DATA_DIR");
    if (data_dir && *data_dir) {
        snprintf(out, n, "%s/settings", data_dir);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/Library/Application Support/PS4Recomp/settings", home ? home : ".");
}

static void settings_path(char *out, size_t n, const char *ext) {
    char dir[1500];
    settings_dir(dir, sizeof(dir));
    const char *root = ps4_vfs_get_app_root();
    const char *base = root ? strrchr(root, '/') : NULL;
    base = base && base[1] ? base + 1 : (root && *root ? root : "default");
    snprintf(out, n, "%s/%s.%s", dir, base, ext);
}

static bool load_settings(HostSettings *s) {
    char p[2048];
    settings_path(p, sizeof(p), "cfg");
    FILE *f = fopen(p, "r");
    if (!f) {
        // Older builds stored only the language in <name>.lang.
        settings_path(p, sizeof(p), "lang");
        f = fopen(p, "r");
        if (!f) return false;
        int v = -1;
        if (fscanf(f, "%d", &v) == 1) s->lang = v;
        fclose(f);
        return false; // ask once for the new settings
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        int v;
        if (sscanf(line, "%63[^=]=%d", key, &v) != 2) continue;
        if (!strcmp(key, "lang")) s->lang = v;
        else if (!strcmp(key, "pro")) s->pro = v != 0;
        else if (!strcmp(key, "upscale")) s->upscale = v != 0;
        else if (!strcmp(key, "hud")) s->hud = v != 0;
    }
    fclose(f);
    return true;
}

static void save_settings(const HostSettings *s) {
    char dir[1500], p[2048];
    settings_dir(dir, sizeof(dir));
    [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:dir]
                              withIntermediateDirectories:YES attributes:nil error:nil];
    settings_path(p, sizeof(p), "cfg");
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "lang=%d\npro=%d\nupscale=%d\nhud=%d\n", s->lang, s->pro, s->upscale, s->hud);
    fclose(f);
}

static void show_settings_dialog(HostSettings *s) {
    int idx_buf[NLANGS];
    int n = available_langs(idx_buf, NLANGS);
    int *idx = idx_buf;
    int def = system_default(idx, n);
    for (int k = 0; k < n; k++)
        if (kLangs[idx[k]].orbis == s->lang) def = k;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Настройки / Settings";
        alert.informativeText = @"Hold Option at launch to open this window again.\n"
                                @"Удерживайте Option при запуске, чтобы открыть это окно снова.";
        NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 260, 26) pullsDown:NO];
        for (int k = 0; k < n; k++) [popup addItemWithTitle:[NSString stringWithUTF8String:kLangs[idx[k]].title]];
        [popup selectItemAtIndex:def];
        NSTextField *langLabel = [NSTextField labelWithString:@"Язык / Language:"];
        NSButton *pro = [NSButton checkboxWithTitle:@"Режим PS4 Pro / PS4 Pro mode" target:nil action:nil];
        pro.state = s->pro ? NSControlStateValueOn : NSControlStateValueOff;
        NSButton *up = [NSButton checkboxWithTitle:@"Апскейл MetalFX до разрешения окна / MetalFX upscaling"
                                            target:nil action:nil];
        up.state = s->upscale ? NSControlStateValueOn : NSControlStateValueOff;
        NSButton *hud = [NSButton checkboxWithTitle:@"Счётчик FPS (Metal HUD) / FPS counter" target:nil action:nil];
        hud.state = s->hud ? NSControlStateValueOn : NSControlStateValueOff;
        NSButton *remember = [NSButton checkboxWithTitle:@"Запомнить / Remember" target:nil action:nil];
        remember.state = NSControlStateValueOn;
        NSStackView *stack = [NSStackView stackViewWithViews:@[ langLabel, popup, pro, up, hud, remember ]];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading;
        stack.spacing = 8;
        stack.frame = NSMakeRect(0, 0, 400, 170);
        alert.accessoryView = stack;
        [alert addButtonWithTitle:@"OK"];
        [NSApp activateIgnoringOtherApps:YES];
        [alert runModal];
        int pick = (int)popup.indexOfSelectedItem;
        if (pick < 0 || pick >= n) pick = def;
        s->lang = kLangs[idx[pick]].orbis;
        s->pro = pro.state == NSControlStateValueOn;
        s->upscale = up.state == NSControlStateValueOn;
        s->hud = hud.state == NSControlStateValueOn;
        if (remember.state == NSControlStateValueOn) save_settings(s);
    }
}

void ps4_host_settings_init(void) {
    if (g_settings_ready) return;
    HostSettings s = g_settings;
    const bool stored = load_settings(&s);
    const bool option_held = ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
    if ((!stored || option_held) && !getenv("PS4_NO_DIALOGS") && !getenv("PS4_LANG")) {
        if ([NSThread isMainThread]) {
            show_settings_dialog(&s);
        } else {
            __block HostSettings bs = s;
            dispatch_sync(dispatch_get_main_queue(), ^{ show_settings_dialog(&bs); });
            s = bs;
        }
    }
    if (s.lang < 0) {
        int idx_buf[NLANGS];
        int n = available_langs(idx_buf, NLANGS);
        s.lang = kLangs[idx_buf[system_default(idx_buf, n)]].orbis;
    }
    // Environment overrides.
    const int env_lang = lang_from_name(getenv("PS4_LANG"));
    if (env_lang >= 0) s.lang = env_lang;
    const char *e;
    if ((e = getenv("PS4_PRO")) && *e) s.pro = strcmp(e, "0") != 0;
    if ((e = getenv("PS4_UPSCALE")) && *e) s.upscale = strcmp(e, "off") != 0 && strcmp(e, "0") != 0;
    if ((e = getenv("MTL_HUD_ENABLED")) && *e) s.hud = strcmp(e, "0") != 0;
    // Consumers that read the environment (presentation) see the resolved values.
    if (s.upscale && !getenv("PS4_UPSCALE")) setenv("PS4_UPSCALE", "metalfx", 0);
    if (!s.hud) setenv("MTL_HUD_ENABLED", "0", 1);
    g_settings = s;
    g_settings_ready = true;
    fprintf(stderr, "[ps4-settings] language %d, PS4 Pro mode %s, MetalFX upscaling %s, FPS HUD %s\n", s.lang,
            s.pro ? "on" : "off", s.upscale ? "on" : "off", s.hud ? "on" : "off");
}

int ps4_host_choose_language(void) {
    ps4_host_settings_init();
    return g_settings.lang;
}

int ps4_host_setting_pro_mode(void) {
    ps4_host_settings_init();
    return g_settings.pro;
}
