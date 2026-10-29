// SPDX-License-Identifier: GPL-2.0-or-later
// The key of the running game (ps4_game_key.h): its TITLE_ID, or the app root's name.
#include "ps4_game_key.h"
#include "ps4_sfo.h"
#include "ps4_vfs.h"

bool ps4_current_game_key(char *out, size_t out_size) {
    char title_id[64] = {0};
    Ps4SfoContext sfo = {0};
    if (ps4_sfo_find_and_load(&sfo)) {
        ps4_sfo_get_string(&sfo, "TITLE_ID", title_id, sizeof(title_id));
        ps4_sfo_free(&sfo);
    }
    return ps4_game_key_for_root(title_id, ps4_vfs_get_app_root(), out, out_size);
}
