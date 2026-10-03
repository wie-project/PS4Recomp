// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_ULOBJMGR_H
#define PS4_ULOBJMGR_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim__sceUlobjmgrRegisterObject(GuestContext *ctx);
void shim__sceUlobjmgrUnregisterObject(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_ULOBJMGR_H
