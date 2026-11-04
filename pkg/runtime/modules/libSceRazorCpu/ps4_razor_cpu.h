// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_RAZOR_CPU_H
#define PS4_RAZOR_CPU_H

#include "recomp_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void shim_sceRazorCpuInit(GuestContext *ctx);
void shim_sceRazorCpuShutdown(GuestContext *ctx);
void shim_sceRazorCpuPushMarker(GuestContext *ctx);
void shim_sceRazorCpuPushMarkerStatic(GuestContext *ctx);
void shim_sceRazorCpuPopMarker(GuestContext *ctx);
void shim_sceRazorCpuNamedSync(GuestContext *ctx);
void shim_sceRazorCpuPlotValue(GuestContext *ctx);
void shim_sceRazorCpuSync(GuestContext *ctx);
void shim_sceRazorCpuIsCapturing(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_RAZOR_CPU_H
