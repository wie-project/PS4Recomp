// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_ULOBJMGR_H
#define PS4_ULOBJMGR_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim__sceUlobjmgrRegisterObject(GuestContext *ctx);
void shim__sceUlobjmgrUnregisterObject(GuestContext *ctx);
void shim_Func_1D9F50D9CFB8054E(GuestContext *ctx);
void shim_Func_4B07893BBB77A649(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_ULOBJMGR_H
