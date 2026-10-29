// SPDX-License-Identifier: GPL-2.0-or-later
// The per-game settings file (<settings dir>/<key>.cfg): one key=value per line. The runtime owns
// lang, pro, upscale and hud; metal (3 or 4) is the renderer the manager app stores. Lines with
// other keys belong to other writers and are kept.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int lang;    // Orbis language code
    int pro;     // PS4 Pro mode, 0 or 1
    int upscale; // MetalFX upscaling, 0 or 1
    int hud;     // Metal performance HUD, 0 or 1
    int metal;   // renderer backend 3 or 4; 0 when the file has none (automatic). Read only.
} Ps4SettingsFile;

// Reads path into s. lang, pro, upscale and hud keep their value in s when the file lacks them;
// metal is 3 or 4 when the file says so, else 0. Returns false when the file cannot be opened.
bool ps4_settings_file_read(const char *path, Ps4SettingsFile *s);

// Writes lang, pro, upscale and hud of s to path, keeping every other line of the existing file
// (metal= included) in its place: each owned key replaces its first line, later duplicates are
// dropped, and owned keys the file lacks are appended. The file is replaced atomically (a temp
// file in the same directory, then rename). Returns false on an I/O error or a file over 64 KiB.
bool ps4_settings_file_write(const char *path, const Ps4SettingsFile *s);

#ifdef __cplusplus
}
#endif
