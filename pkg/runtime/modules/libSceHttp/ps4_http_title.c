// SPDX-License-Identifier: GPL-2.0-or-later
// The running title's id for the libSceHttp host overrides (ps4_http_overrides.c). Kept apart
// because it needs the VFS and the param.sfo reader.
#include "ps4_http.h"
#include "ps4_sfo.h"
#include <stdio.h>

void ps4_http_title_id(char *out, size_t out_size) {
    if (out_size == 0) return;
    out[0] = '\0';
    Ps4SfoContext sfo = {0};
    if (ps4_sfo_find_and_load(&sfo)) {
        ps4_sfo_get_string(&sfo, "TITLE_ID", out, out_size);
        ps4_sfo_free(&sfo);
    }
}
