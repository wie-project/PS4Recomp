// SPDX-License-Identifier: GPL-2.0-or-later
// Names the per-game files of the host: the settings file, the shader caches and the save directory.
#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writes the settings key for a game: title_id when it is a valid id (letters, digits, '-', '_',
// 1..31 chars), otherwise fallback (the app root's base name). Returns false when both are empty.
bool ps4_settings_key(const char *title_id, const char *fallback, char *out, size_t out_size);

// Writes the per-game key: ps4_settings_key of title_id with the base name of app_root (trailing
// slashes ignored) as the fallback, and "default" when app_root has none.
bool ps4_game_key_for_root(const char *title_id, const char *app_root, char *out, size_t out_size);

// The key of the running game: ps4_game_key_for_root of param.sfo's TITLE_ID and the VFS app
// root. Names <key>.cfg and the shader cache directory. Defined in ps4_game_key_current.c, which
// needs the VFS and the param.sfo reader.
bool ps4_current_game_key(char *out, size_t out_size);

// Finds an existing directory in base_dir whose name ends with " [<title_id>]" (the newest by
// mtime when several do) and writes its full path to out. Returns false when there is none or
// title_id is empty.
bool ps4_find_save_dir_by_title_id(const char *base_dir, const char *title_id, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
