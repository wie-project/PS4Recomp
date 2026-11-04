// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_FONT_FT_H
#define PS4_FONT_FT_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceFontSelectLibraryFt(GuestContext *ctx);
void shim_sceFontSelectRendererFt(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_FONT_FT_H
